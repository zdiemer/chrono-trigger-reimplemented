"""Code the game runs from RAM: sections built from [[ramcode]] hints.

A section is the image the game puts at a run address before jumping there:
the output of a compressed stream (format "lz", the decompressor at $C3:0557,
tools/disasm/lz.py), bytes copied from ROM (format "copy"), bytes that a
run of code stores one by one (format "writes": the constants that
LDA/LDX/LDY immediate and STA/STX/STY/STZ put into the section, simulated over
the code from `source` to `end`), or the bytes a routine writes through the
WRAM port when called with given registers (format "run": the routine at
`source` is executed by a small 65816 interpreter, simulate_run). The analysis
traces code in a section like ROM code, reading the image; the matching build
assembles the section at its run address and checks the result against the
image. Images are ROM-derived and only ever built in memory or the build tree.
"""

import bisect
from dataclasses import dataclass, field

from .cpu import Mode, decode
from .lz import decompress
from .memmap import Rom, bus_to_offset, fmt

FORMATS = ("lz", "copy", "writes", "run")


def canonical_ram(addr: int) -> int:
    """$00-$3F/$80-$BF:0000-1FFF mirror work RAM $7E:0000-1FFF."""
    bank, a = addr >> 16, addr & 0xFFFF
    if (bank <= 0x3F or 0x80 <= bank <= 0xBF) and a < 0x2000:
        return 0x7E0000 | a
    return addr


@dataclass
class RamSection:
    name: str
    base: int                 # canonical run address of the first byte
    source: int               # ROM bus address of the stream or copied bytes (writes: of the code)
    format: str
    image: bytes = b""
    consumed: int = 0         # ROM bytes the section comes from
    over: str | None = None   # the section this one overwrites part of at run time
    patch: str | None = None  # the section whose code the program patches with this one for a while

    @property
    def end(self) -> int:     # exclusive
        return self.base + len(self.image)


@dataclass
class RamImages:
    sections: list = field(default_factory=list)
    problems: list = field(default_factory=list)

    def __post_init__(self):
        self._index()

    def _index(self):
        self.sections.sort(key=lambda s: s.base)
        self._starts = [s.base for s in self.sections]

    def section_at(self, addr: int) -> RamSection | None:
        """The section the analysis reads at addr: one written over another
        (`over`) wins where it lies, otherwise the first by base and hint order,
        and a code patch (`patch`) only where nothing else lies. Sections at the
        same address with different contents (the installers' vector stubs, a
        patch and the code it patches) are alternatives; the matching source
        checks each one."""
        addr = canonical_ram(addr)
        holding = [s for s in self.sections[:bisect.bisect_right(self._starts, addr)] if s.base <= addr < s.end]
        overlays = [s for s in holding if s.over]
        plain = [s for s in holding if not s.patch]
        return (overlays or plain or holding or [None])[0]

    def patched(self, sec: RamSection, addr: int) -> bool:
        """Whether a code patch of `sec` lies at addr."""
        addr = canonical_ram(addr)
        return any(s.patch == sec.name and s.base <= addr < s.end for s in self.sections)

    def covered_by_overlay(self, sec: RamSection, addr: int) -> bool:
        addr = canonical_ram(addr)
        return any(s.over == sec.name and s.base <= addr < s.end for s in self.sections)

    def read(self, addr: int, n: int) -> bytes | None:
        s = self.section_at(addr)
        if s is None:
            return None
        i = canonical_ram(addr) - s.base
        if i + n > len(s.image):
            return None
        return s.image[i:i + n]


def build(data: bytes, hints) -> RamImages:
    """Images for every [[ramcode]] hint. A hint whose source doesn't decode
    becomes a problem instead of a section."""
    sections, problems = [], []
    rom = Rom(data)
    for h in hints.ramcode:
        off = bus_to_offset(h.source)
        # the code a "writes" section simulates may itself run from a section built before it
        built = RamImages(list(sections))
        read = lambda a, n, built=built: rom.read(a, n) if bus_to_offset(a) is not None else built.read(a, n)
        if off is None and (h.format != "writes" or built.section_at(h.source) is None):
            problems.append(f"{h.name}: source {fmt(h.source)} isn't ROM or an earlier section")
            continue
        if h.format == "lz":
            r = decompress(data, off)
            if r is None:
                problems.append(f"{h.name}: no stream at {fmt(h.source)}")
                continue
            image, consumed = r
        elif h.format == "copy":
            image, consumed = bytes(data[off:off + h.size]), h.size
        else:
            image, why = simulate_writes(rom, h, read) if h.format == "writes" else simulate_run(rom, h)
            if image is None:
                problems.append(f"{h.name}: {why}")
                continue
            consumed = 0
        if h.size is not None and h.format == "lz":
            if h.size > len(image):
                problems.append(f"{h.name}: decodes to {len(image)} bytes, fewer than the hint's {h.size}")
                continue
            image = image[:h.size]          # the rest of the output isn't code the section covers
        sections.append(RamSection(h.name, canonical_ram(h.base), h.source, h.format, image, consumed, h.over,
                                   h.patch))
    return RamImages(sections, problems)


def simulate_writes(rom: Rom, h, read=None) -> tuple:
    """(image, None) for a "writes" section: run the code from h.source to h.end
    (inclusive) keeping A, X and Y where an immediate load, or a long load from
    ROM or an earlier section's image, makes them known, and collect what the
    stores put into [base, base + size). Stores to absolute addresses go to the
    section's bank. (None, reason) if a byte stays unset or a store into the
    section isn't a known constant. `read` reads code and loads (default: ROM)."""
    read = read or rom.read
    base, size = canonical_ram(h.base), h.size
    image = [None] * size
    reg = {"A": None, "X": None, "Y": None}
    m, x = h.m, h.x
    pc = h.source
    while pc <= h.end:
        insn = decode(read, pc, m, x)
        if insn is None:
            return None, f"can't decode {fmt(pc)}"
        n, mode, v = insn.name, insn.mode, insn.operand
        if n in ("LDA", "LDX", "LDY") and mode in (Mode.IMM_M, Mode.IMM_X):
            reg[n[2]] = v
        elif n in ("LDA", "LDX", "LDY") and mode is Mode.LONG:
            b = read(v, 1 if (m if n == "LDA" else x) else 2)
            reg[n[2]] = None if b is None else int.from_bytes(b, "little")
        elif n in ("STA", "STX", "STY", "STZ") and mode in (Mode.ABS, Mode.LONG):
            width = 1 if (m if n in ("STA", "STZ") else x) else 2
            val = 0 if n == "STZ" else reg[n[2]]
            dest = canonical_ram(v if mode is Mode.LONG else (base & 0xFF0000) | v)
            for i in range(width):
                if base <= dest + i < base + size:
                    if val is None:
                        return None, f"{fmt(pc)} stores a value that isn't a constant"
                    image[dest + i - base] = val >> (8 * i) & 0xFF
        elif n == "REP":
            m = False if v & 0x20 else m
            x = False if v & 0x10 else x
        elif n == "SEP":
            m = True if v & 0x20 else m
            x = True if v & 0x10 else x
        elif n in ("TAX", "TAY", "TXA", "TYA", "TXY", "TYX", "PLA", "PLX", "PLY", "TDC", "INC", "DEC",
                   "INX", "INY", "DEX", "DEY") or (n.startswith("LD") and mode not in (Mode.IMM_M, Mode.IMM_X)):
            for r in {"TAX": "X", "TAY": "Y", "TXA": "A", "TYA": "A", "TXY": "Y", "TYX": "X", "PLA": "A",
                      "PLX": "X", "PLY": "Y", "TDC": "A", "INC": "A", "DEC": "A", "INX": "X", "INY": "Y",
                      "DEX": "X", "DEY": "Y"}.get(n, n[2] if n.startswith("LD") else ""):
                reg[r] = None
        elif mode is Mode.IMM_M and n in ("AND", "ORA", "EOR", "ADC", "SBC"):
            reg["A"] = None
        pc = insn.next_pc
    if None in image:
        return None, f"byte {image.index(None)} isn't written by {fmt(h.source)}-{fmt(h.end)}"
    return bytes(image), None


class RunError(Exception):
    pass


def simulate_run(rom: Rom, h, step_limit: int = 2_000_000) -> tuple:
    """(image, None) for a "run" section: execute the routine at h.source with
    the registers in h.regs (a, x, y, db, d) and widths h.m/h.x until it
    returns, with the WRAM port ($2181-$2183) starting at the section's base,
    and collect every byte written through $2180. RAM the routine reads must
    have been written by it first; ROM reads come from the ROM. Only the
    instructions such generators use are modelled (binary mode ADC, no calls,
    no interrupts); anything else gives (None, reason)."""
    try:
        return _Machine(rom, h).run(step_limit), None
    except RunError as e:
        return None, str(e)


class _Machine:
    def __init__(self, rom: Rom, h):
        regs = {k: (int(v, 16) if isinstance(v, str) else v) for k, v in (h.regs or {}).items()}
        self.rom, self.h = rom, h
        self.a, self.x, self.y = regs.get("a", 0), regs.get("x", 0), regs.get("y", 0)
        self.db, self.d = regs.get("db", 0), regs.get("d", 0)
        self.m8, self.x8 = h.m, h.x
        self.n = self.z = self.c = False
        self.stack: list[int] = []
        self.mem: dict[int, int] = {}
        self.port = canonical_ram(h.base)
        self.out: dict[int, int] = {}

    # -- memory --
    def read(self, addr: int) -> int:
        addr &= 0xFFFFFF
        if bus_to_offset(addr) is not None:
            return self.rom.read(addr, 1)[0]
        ram = canonical_ram(addr)
        if ram in self.mem:
            return self.mem[ram]
        raise RunError(f"reads {fmt(addr)}, which the routine didn't write")

    def write(self, addr: int, v: int):
        addr &= 0xFFFFFF
        bank, a = addr >> 16, addr & 0xFFFF
        if (bank <= 0x3F or 0x80 <= bank <= 0xBF) and 0x2180 <= a <= 0x2183:
            p = self.port & 0x1FFFF
            if a == 0x2180:
                self.out[self.port] = v
                p = (p + 1) & 0x1FFFF
            elif a == 0x2183:
                p = (p & 0xFFFF) | (v & 1) << 16
            else:
                shift = (a - 0x2181) * 8
                p = (p & ~(0xFF << shift)) | v << shift
            self.port = 0x7E0000 | p
            return
        ram = canonical_ram(addr)
        if ram >> 17 != 0x3F:
            raise RunError(f"writes {fmt(addr)}, which isn't work RAM or the WRAM port")
        self.mem[ram] = v

    def read_n(self, addr: int, wide: bool) -> int:
        return self.read(addr) | (self.read(addr + 1) << 8 if wide else 0)

    def write_n(self, addr: int, v: int, wide: bool):
        self.write(addr, v & 0xFF)
        if wide:
            self.write(addr + 1, v >> 8 & 0xFF)

    def push(self, v: int, n: int):
        for i in reversed(range(n)):
            self.stack.append(v >> (8 * i) & 0xFF)

    def pull(self, n: int) -> int:
        if len(self.stack) < n:
            raise RunError("pulls more than the routine pushed")
        v = 0
        for i in range(n):
            v |= self.stack.pop() << (8 * i)
        return v

    def nz(self, v: int, wide: bool) -> int:
        v &= 0xFFFF if wide else 0xFF
        self.z = v == 0
        self.n = bool(v >> (15 if wide else 7))
        return v

    def ea(self, insn) -> int:
        m, op = insn.mode, insn.operand
        if m is Mode.DP:
            return (self.d + op) & 0xFFFF
        if m is Mode.DPX:
            return (self.d + op + self.x) & 0xFFFF
        if m in (Mode.ABS, Mode.ABSX, Mode.ABSY):
            index = {Mode.ABS: 0, Mode.ABSX: self.x, Mode.ABSY: self.y}[m]
            return (self.db << 16 | op) + index
        if m in (Mode.LONG, Mode.LONGX):
            return op + (self.x if m is Mode.LONGX else 0)
        raise RunError(f"{fmt(insn.pc)}: addressing mode {m.value} isn't modelled")

    def value(self, insn, wide: bool) -> int:
        if insn.mode in (Mode.IMM_M, Mode.IMM_X):
            return insn.operand
        return self.read_n(self.ea(insn), wide)

    def set_a(self, v: int, wide: bool):
        self.a = v if wide else (self.a & 0xFF00) | v

    def run(self, step_limit: int) -> bytes:
        pc = self.h.source
        for _ in range(step_limit):
            insn = decode(self.rom.read, pc, self.m8, self.x8)
            if insn is None:
                raise RunError(f"can't decode {fmt(pc)}")
            n, mode = insn.name, insn.mode
            wm, wx = not self.m8, not self.x8
            nxt = insn.next_pc
            if n in ("LDA", "LDX", "LDY"):
                wide = wm if n == "LDA" else wx
                v = self.nz(self.value(insn, wide), wide)
                if n == "LDA":
                    self.set_a(v, wide)
                else:
                    setattr(self, n[2].lower(), v)
            elif n in ("STA", "STX", "STY", "STZ"):
                wide = wx if n in ("STX", "STY") else wm
                v = 0 if n == "STZ" else getattr(self, n[2].lower())
                self.write_n(self.ea(insn), v, wide)
            elif n in ("CMP", "CPX", "CPY"):
                wide = wm if n == "CMP" else wx
                mask = 0xFFFF if wide else 0xFF
                arg = self.value(insn, wide)
                reg = getattr(self, "a" if n == "CMP" else n[2].lower()) & mask
                self.c = reg >= arg
                self.nz(reg - arg, wide)
            elif n in ("ADC", "AND", "ORA", "EOR"):
                mask = 0xFFFF if wm else 0xFF
                arg, acc = self.value(insn, wm), self.a & mask
                if n == "ADC":
                    r = acc + arg + int(self.c)
                    self.c = r > mask
                else:
                    r = {"AND": acc & arg, "ORA": acc | arg, "EOR": acc ^ arg}[n]
                self.set_a(self.nz(r, wm), wm)
            elif n in ("INC", "DEC"):
                step = 1 if n == "INC" else -1
                if mode is Mode.ACC:
                    self.set_a(self.nz(self.a + step, wm), wm)
                else:
                    addr = self.ea(insn)
                    self.write_n(addr, self.nz(self.read_n(addr, wm) + step, wm), wm)
            elif n in ("INX", "INY", "DEX", "DEY"):
                r = n[2].lower()
                setattr(self, r, self.nz(getattr(self, r) + (1 if n[0] == "I" else -1), wx))
            elif n in ("TAX", "TAY", "TXY", "TYX"):
                src = {"TAX": "a", "TAY": "a", "TXY": "x", "TYX": "y"}[n]
                setattr(self, n[2].lower(), self.nz(getattr(self, src), wx))
            elif n in ("TXA", "TYA"):
                self.set_a(self.nz(getattr(self, n[1].lower()), wm), wm)
            elif n == "TCD":
                self.d = self.nz(self.a, True)
            elif n == "TDC":
                self.a = self.nz(self.d, True)
            elif n == "XBA":
                self.a = (self.a >> 8 | self.a << 8) & 0xFFFF
                self.nz(self.a, False)
            elif n in ("PHA", "PHX", "PHY"):
                self.push(getattr(self, n[2].lower()), 2 if (wm if n == "PHA" else wx) else 1)
            elif n in ("PLA", "PLX", "PLY"):
                wide = wm if n == "PLA" else wx
                v = self.nz(self.pull(2 if wide else 1), wide)
                if n == "PLA":
                    self.set_a(v, wide)
                else:
                    setattr(self, n[2].lower(), v)
            elif n in ("PHB", "PHD"):
                self.push(self.db if n == "PHB" else self.d, 1 if n == "PHB" else 2)
            elif n == "PLB":
                self.db = self.nz(self.pull(1), False)
            elif n == "PLD":
                self.d = self.nz(self.pull(2), True)
            elif n in ("REP", "SEP"):
                if insn.operand & 0xCE:
                    raise RunError(f"{fmt(pc)}: only M, X and C are modelled in REP and SEP")
                on = n == "SEP"
                self.m8 = on if insn.operand & 0x20 else self.m8
                self.x8 = on if insn.operand & 0x10 else self.x8
                self.c = on if insn.operand & 0x01 else self.c
            elif n in ("CLC", "SEC"):
                self.c = n == "SEC"
            elif n in ("BNE", "BEQ", "BPL", "BMI", "BCC", "BCS", "BRA"):
                taken = {"BNE": not self.z, "BEQ": self.z, "BPL": not self.n, "BMI": self.n,
                         "BCC": not self.c, "BCS": self.c, "BRA": True}[n]
                if taken:
                    nxt = insn.branch_target()
            elif n == "JMP" and mode is Mode.ABS:
                nxt = (pc & 0xFF0000) | insn.operand
            elif n in ("RTS", "RTL"):
                if self.stack:
                    raise RunError(f"{fmt(pc)}: returns with bytes still pushed")
                break
            elif n != "NOP":
                raise RunError(f"{fmt(pc)}: {n} isn't modelled")
            if self.x8:
                self.x &= 0xFF
                self.y &= 0xFF
            pc = nxt
        else:
            raise RunError(f"doesn't return within {step_limit} instructions")
        if not self.out:
            raise RunError("writes nothing through the WRAM port")
        base, end = canonical_ram(self.h.base), max(self.out) + 1
        if min(self.out) != base or len(self.out) != end - base:
            raise RunError("the port writes don't form one run from the base")
        if self.h.size is not None and end - base != self.h.size:
            raise RunError(f"writes {end - base} bytes, not the hint's {self.h.size}")
        return bytes(self.out[a] for a in range(base, end))
