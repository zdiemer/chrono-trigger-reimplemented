"""Code the game runs from RAM: sections built from [[ramcode]] hints.

A section is the image the game puts at a run address before jumping there:
the output of a compressed stream (format "lz", the decompressor at $C3:0557,
tools/disasm/lz.py), bytes copied from ROM (format "copy"), or bytes that a
run of code stores one by one (format "writes": the constants that
LDA/LDX/LDY immediate and STA/STX/STY/STZ put into the section, simulated over
the code from `source` to `end`). The analysis
traces code in a section like ROM code, reading the image; the matching build
assembles the section at its run address and checks the result against the
image. Images are ROM-derived and only ever built in memory or the build tree.
"""

import bisect
from dataclasses import dataclass, field

from .cpu import Mode, decode
from .lz import decompress
from .memmap import Rom, bus_to_offset, fmt

FORMATS = ("lz", "copy", "writes")


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
    source: int               # ROM bus address of the stream or copied bytes
    format: str
    image: bytes = b""
    consumed: int = 0         # ROM bytes the section comes from
    over: str | None = None   # the section this one overwrites part of at run time

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
        (`over`) wins where it lies, otherwise the first by base and hint order.
        Sections at the same address with different contents (the installers'
        vector stubs) are alternatives; the matching source checks each one."""
        addr = canonical_ram(addr)
        holding = [s for s in self.sections[:bisect.bisect_right(self._starts, addr)] if s.base <= addr < s.end]
        overlays = [s for s in holding if s.over]
        return (overlays or holding or [None])[0]

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
    for h in hints.ramcode:
        off = bus_to_offset(h.source)
        if off is None:
            problems.append(f"{h.name}: source {fmt(h.source)} isn't ROM")
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
            image, why = simulate_writes(Rom(data), h)
            if image is None:
                problems.append(f"{h.name}: {why}")
                continue
            consumed = 0
        if h.size is not None and h.format == "lz":
            if h.size > len(image):
                problems.append(f"{h.name}: decodes to {len(image)} bytes, fewer than the hint's {h.size}")
                continue
            image = image[:h.size]          # the rest of the output isn't code the section covers
        sections.append(RamSection(h.name, canonical_ram(h.base), h.source, h.format, image, consumed, h.over))
    return RamImages(sections, problems)


def simulate_writes(rom: Rom, h) -> tuple:
    """(image, None) for a "writes" section: run the code from h.source to h.end
    (inclusive) keeping A, X and Y where an immediate load makes them known, and
    collect what the stores put into [base, base + size). Stores to absolute
    addresses go to the section's bank. (None, reason) if a byte stays unset or
    a store into the section isn't a known constant."""
    base, size = canonical_ram(h.base), h.size
    image = [None] * size
    reg = {"A": None, "X": None, "Y": None}
    m, x = h.m, h.x
    pc = h.source
    while pc <= h.end:
        insn = decode(rom.read, pc, m, x)
        if insn is None:
            return None, f"can't decode {fmt(pc)}"
        n, mode, v = insn.name, insn.mode, insn.operand
        if n in ("LDA", "LDX", "LDY") and mode in (Mode.IMM_M, Mode.IMM_X):
            reg[n[2]] = v
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
