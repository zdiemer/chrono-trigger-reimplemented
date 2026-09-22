#!/usr/bin/env python3
"""SPC700 disassembler for the sound driver the boot upload sends (docs/SOUND.md).

    python3 -m tools.spc700dis [--rom roms/ct-usa.sfc] [--block 0] [--from 0x0200] [--to 0x0400]
                               [--follow ADDR ...] [--table ADDR:COUNT ...] [--coverage spc-cov.txt]
                               [--blocks]

Reads boot block k of the upload at `$C7:0000` straight from the ROM (block k's
ROM address is the word at `$C7:0010 + 2k`, its SPC700 destination the word at
`$C7:001C + 2k`, and it starts with a 16-bit length; game/core/sound_data.h) and
prints a linear listing, one instruction a line, in Sony's syntax: `$12` is a
direct-page operand, `!$1234` an absolute one, `#$12` an immediate, `m.b` an
absolute bit. The I/O registers at `$F0-$FF` are named, and a write of a
constant to `$F2` (the S-DSP address) gets the DSP register's name.

`--follow` decodes only what is reachable from the given entry points
(branches, CALL, JMP, the fall-through of everything but RET, RETI, BRA, JMP
and STOP; a `JMP [!abs+X]` is a table it can't see and ends the path), and
prints the rest as data, `.db` lines. `--table ADDR:COUNT` adds the COUNT
words at ADDR as entry points: the driver dispatches through word tables it
pushes and RETs through, which no static walk sees. `--coverage` takes the SPC700 PCs
`ct_dev --spc-coverage` writes and marks each instruction executed (`*`) or
not; with it and no `--follow`, the executed PCs are the entry points.
`--blocks` lists the six blocks and exits.

Listings are ROM-derived: keep them out of the repo (CLAUDE.md). The opcode
table matches runtime/apu/spc700.c's operand fetches; tests/test_spc700dis.py
checks it on synthetic bytes.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

HIROM_BANK = 0xC7
BOOT_BLOCKS = 6
BOOT_SOURCES = 0x0010   # word k: ROM address of block k in bank $C7
BOOT_DESTINATIONS = 0x001C   # word k: its SPC700 address

# ---- the instruction set ------------------------------------------------------
#
# Operand kinds, in the order their bytes follow the opcode:
#   d  a direct-page byte      a  an absolute word     i  an immediate byte
#   r  a relative branch byte  m  a 13-bit address and a 3-bit bit number (m.b)
#   u  the low byte of a PCALL target in page $FF
# A template names them {d}, {a}, ...; an instruction with two direct-page
# operands (dp,dp and dp,#imm forms) names them {d0} (first byte) and {d1}.

_ALU = ["OR", "AND", "EOR", "CMP", "ADC", "SBC"]      # rows 0, 2, 4, 6, 8, A
_SHIFT = ["ASL", "ROL", "LSR", "ROR", "DEC", "INC"]   # rows 0, 2, 4, 6, 8, A
_BRANCH = {0x10: "BPL", 0x30: "BMI", 0x50: "BVC", 0x70: "BVS",
           0x90: "BCC", 0xB0: "BCS", 0xD0: "BNE", 0xF0: "BEQ"}

_SPECIAL = {
    0x00: ("NOP", ""), 0x0D: ("PUSH", "PSW"), 0x0E: ("TSET1", "!{a}"), 0x0F: ("BRK", ""),
    0x1A: ("DECW", "{d}"), 0x1D: ("DEC", "X"), 0x1E: ("CMP", "X,!{a}"), 0x1F: ("JMP", "[!{a}+X]"),
    0x20: ("CLRP", ""), 0x2D: ("PUSH", "A"), 0x2E: ("CBNE", "{d},{r}"), 0x2F: ("BRA", "{r}"),
    0x3A: ("INCW", "{d}"), 0x3D: ("INC", "X"), 0x3E: ("CMP", "X,{d}"), 0x3F: ("CALL", "!{a}"),
    0x40: ("SETP", ""), 0x4D: ("PUSH", "X"), 0x4E: ("TCLR1", "!{a}"), 0x4F: ("PCALL", "{u}"),
    0x5A: ("CMPW", "YA,{d}"), 0x5D: ("MOV", "X,A"), 0x5E: ("CMP", "Y,!{a}"), 0x5F: ("JMP", "!{a}"),
    0x60: ("CLRC", ""), 0x6D: ("PUSH", "Y"), 0x6E: ("DBNZ", "{d},{r}"), 0x6F: ("RET", ""),
    0x7A: ("ADDW", "YA,{d}"), 0x7D: ("MOV", "A,X"), 0x7E: ("CMP", "Y,{d}"), 0x7F: ("RETI", ""),
    0x80: ("SETC", ""), 0x8D: ("MOV", "Y,#{i}"), 0x8E: ("POP", "PSW"), 0x8F: ("MOV", "{d1},#{i}"),
    0x9A: ("SUBW", "YA,{d}"), 0x9D: ("MOV", "X,SP"), 0x9E: ("DIV", "YA,X"), 0x9F: ("XCN", "A"),
    0xA0: ("EI", ""), 0xAD: ("CMP", "Y,#{i}"), 0xAE: ("POP", "A"), 0xAF: ("MOV", "(X)+,A"),
    0xBA: ("MOVW", "YA,{d}"), 0xBD: ("MOV", "SP,X"), 0xBE: ("DAS", "A"), 0xBF: ("MOV", "A,(X)+"),
    0xC0: ("DI", ""), 0xC4: ("MOV", "{d},A"), 0xC5: ("MOV", "!{a},A"), 0xC6: ("MOV", "(X),A"),
    0xC7: ("MOV", "[{d}+X],A"), 0xC8: ("CMP", "X,#{i}"), 0xC9: ("MOV", "!{a},X"), 0xCB: ("MOV", "{d},Y"),
    0xCC: ("MOV", "!{a},Y"), 0xCD: ("MOV", "X,#{i}"), 0xCE: ("POP", "X"), 0xCF: ("MUL", "YA"),
    0xD4: ("MOV", "{d}+X,A"), 0xD5: ("MOV", "!{a}+X,A"), 0xD6: ("MOV", "!{a}+Y,A"),
    0xD7: ("MOV", "[{d}]+Y,A"), 0xD8: ("MOV", "{d},X"), 0xD9: ("MOV", "{d}+Y,X"), 0xDA: ("MOVW", "{d},YA"),
    0xDB: ("MOV", "{d}+X,Y"), 0xDC: ("DEC", "Y"), 0xDD: ("MOV", "A,Y"), 0xDE: ("CBNE", "{d}+X,{r}"),
    0xDF: ("DAA", "A"),
    0xE0: ("CLRV", ""), 0xE4: ("MOV", "A,{d}"), 0xE5: ("MOV", "A,!{a}"), 0xE6: ("MOV", "A,(X)"),
    0xE7: ("MOV", "A,[{d}+X]"), 0xE8: ("MOV", "A,#{i}"), 0xE9: ("MOV", "X,!{a}"), 0xEB: ("MOV", "Y,{d}"),
    0xEC: ("MOV", "Y,!{a}"), 0xED: ("NOTC", ""), 0xEE: ("POP", "Y"), 0xEF: ("SLEEP", ""),
    0xF4: ("MOV", "A,{d}+X"), 0xF5: ("MOV", "A,!{a}+X"), 0xF6: ("MOV", "A,!{a}+Y"),
    0xF7: ("MOV", "A,[{d}]+Y"), 0xF8: ("MOV", "X,{d}"), 0xF9: ("MOV", "X,{d}+Y"), 0xFA: ("MOV", "{d1},{d0}"),
    0xFB: ("MOV", "Y,{d}+X"), 0xFC: ("INC", "Y"), 0xFD: ("MOV", "Y,A"), 0xFE: ("DBNZ", "Y,{r}"),
    0xFF: ("STOP", ""),
    # absolute bit operations
    0x0A: ("OR1", "C,{m}"), 0x2A: ("OR1", "C,/{m}"), 0x4A: ("AND1", "C,{m}"), 0x6A: ("AND1", "C,/{m}"),
    0x8A: ("EOR1", "C,{m}"), 0xAA: ("MOV1", "C,{m}"), 0xCA: ("MOV1", "{m},C"), 0xEA: ("NOT1", "{m}"),
}

_KIND_SIZE = {"d": 1, "d0": 1, "d1": 1, "a": 2, "i": 1, "r": 1, "m": 2, "u": 1}


def _operand_kinds(template: str) -> list[str]:
    """The operand kinds a template names, in byte order."""
    kinds = []
    i = 0
    while (i := template.find("{", i)) >= 0:
        j = template.index("}", i)
        kinds.append(template[i + 1:j])
        i = j
    if "d0" in kinds:   # dp,dp: the source byte comes first
        kinds = ["d0", "d1"]
    elif "d1" in kinds and "i" in kinds:   # dp,#imm: the immediate comes first
        kinds = ["i", "d1"]
    return kinds


@dataclass(frozen=True)
class Opcode:
    mnemonic: str
    template: str
    kinds: tuple[str, ...]

    @property
    def size(self) -> int:
        return 1 + sum(_KIND_SIZE[k] for k in self.kinds)


def _build_table() -> list[Opcode]:
    table: list[tuple[str, str] | None] = [None] * 256
    for op, (mn, t) in _SPECIAL.items():
        table[op] = (mn, t)
    for hi in range(16):
        table[hi << 4 | 0x1] = ("TCALL", str(hi))
        bit = hi >> 1
        if hi & 1:
            table[hi << 4 | 0x2] = ("CLR1", "{d}.%d" % bit)
            table[hi << 4 | 0x3] = ("BBC", "{d}.%d,{r}" % bit)
        else:
            table[hi << 4 | 0x2] = ("SET1", "{d}.%d" % bit)
            table[hi << 4 | 0x3] = ("BBS", "{d}.%d,{r}" % bit)
    for op, mn in _BRANCH.items():
        table[op] = (mn, "{r}")
    for row in range(0, 0xC, 2):
        alu = _ALU[row // 2]
        base = row << 4
        table[base | 0x4] = (alu, "A,{d}")
        table[base | 0x5] = (alu, "A,!{a}")
        table[base | 0x6] = (alu, "A,(X)")
        table[base | 0x7] = (alu, "A,[{d}+X]")
        table[base | 0x8] = (alu, "A,#{i}")
        table[base | 0x9] = (alu, "{d1},{d0}")
        odd = (row + 1) << 4
        table[odd | 0x4] = (alu, "A,{d}+X")
        table[odd | 0x5] = (alu, "A,!{a}+X")
        table[odd | 0x6] = (alu, "A,!{a}+Y")
        table[odd | 0x7] = (alu, "A,[{d}]+Y")
        table[odd | 0x8] = (alu, "{d1},#{i}")
        table[odd | 0x9] = (alu, "(X),(Y)")
        sh = _SHIFT[row // 2]
        table[base | 0xB] = (sh, "{d}")
        table[base | 0xC] = (sh, "!{a}")
        table[odd | 0xB] = (sh, "{d}+X")
        table[odd | 0xC] = (sh, "A")
    out = []
    for op, entry in enumerate(table):
        assert entry is not None, f"opcode {op:02X} missing"
        mn, t = entry
        out.append(Opcode(mn, t, tuple(_operand_kinds(t))))
    return out


OPCODES: list[Opcode] = _build_table()

# ---- names for the reader -----------------------------------------------------

IO_NAMES = {
    0xF0: "TEST", 0xF1: "CONTROL", 0xF2: "DSPADDR", 0xF3: "DSPDATA",
    0xF4: "CPUIO0", 0xF5: "CPUIO1", 0xF6: "CPUIO2", 0xF7: "CPUIO3",
    0xF8: "AUX4", 0xF9: "AUX5", 0xFA: "T0TARGET", 0xFB: "T1TARGET", 0xFC: "T2TARGET",
    0xFD: "T0OUT", 0xFE: "T1OUT", 0xFF: "T2OUT",
}

_VOICE_REGS = ["VOLL", "VOLR", "PITCHL", "PITCHH", "SRCN", "ADSR1", "ADSR2", "GAIN", "ENVX", "OUTX"]
_GLOBAL_REGS = {
    0x0C: "MVOLL", 0x1C: "MVOLR", 0x2C: "EVOLL", 0x3C: "EVOLR", 0x4C: "KON", 0x5C: "KOFF",
    0x6C: "FLG", 0x7C: "ENDX", 0x0D: "EFB", 0x2D: "PMON", 0x3D: "NON", 0x4D: "EON",
    0x5D: "DIR", 0x6D: "ESA", 0x7D: "EDL",
}


def dsp_register_name(reg: int) -> str:
    """The S-DSP register at address `reg` ($00-$7F), as the docs name it."""
    reg &= 0x7F
    lo = reg & 0x0F
    if lo < 10:
        return f"V{reg >> 4}{_VOICE_REGS[lo]}"
    if lo == 0x0F:
        return f"FIR{reg >> 4}"
    return _GLOBAL_REGS.get(reg, f"DSP{reg:02X}")


# ---- decoding -----------------------------------------------------------------


@dataclass(frozen=True)
class Insn:
    addr: int
    raw: bytes
    op: Opcode
    text: str
    target: int | None      # branch, CALL or JMP !abs destination
    comment: str

    @property
    def size(self) -> int:
        return len(self.raw)

    @property
    def ends_path(self) -> bool:
        """Whether execution never falls through to the next instruction."""
        return self.op.mnemonic in ("RET", "RETI", "BRA", "JMP", "STOP", "SLEEP")


def decode(read, addr: int) -> Insn | None:
    """The instruction at `addr`; `read(addr, n)` returns n bytes or None."""
    head = read(addr, 1)
    if not head:
        return None
    op = OPCODES[head[0]]
    raw = read(addr, op.size)
    if raw is None or len(raw) != op.size:
        return None
    vals: dict[str, str] = {}
    target = None
    comments = []
    pos = 1
    for k in op.kinds:
        if k in ("a", "m"):
            w = raw[pos] | raw[pos + 1] << 8
            pos += 2
            if k == "a":
                vals[k] = f"${w:04X}"
                if op.mnemonic in ("CALL", "JMP") and "X" not in op.template:
                    target = w
            else:
                vals[k] = f"${w & 0x1FFF:04X}.{w >> 13}"
        else:
            b = raw[pos]
            pos += 1
            if k == "r":
                target = (addr + op.size + (b - 256 if b & 0x80 else b)) & 0xFFFF
                vals[k] = f"${target:04X}"
            elif k == "i":
                vals[k] = f"${b:02X}"
            elif k == "u":
                target = 0xFF00 | b
                vals[k] = f"${b:02X}"
            else:
                vals[k] = f"${b:02X}"
                if b in IO_NAMES:
                    comments.append(IO_NAMES[b])
    text = op.template.format(**vals)
    # A constant written to the DSP address register names the DSP register.
    if raw[0] == 0x8F and raw[2] == 0xF2:
        comments = [f"DSPADDR = {dsp_register_name(raw[1])}"]
    line = f"{op.mnemonic:<5} {text}".rstrip()
    return Insn(addr, raw, op, line, target, "; ".join(dict.fromkeys(comments)))


def linear(read, start: int, end: int):
    """Every instruction from start up to end, decoded back to back."""
    addr = start
    while addr < end:
        insn = decode(read, addr)
        if insn is None:
            return
        yield insn
        addr += insn.size


def reachable(read, entries, start: int, end: int) -> dict[int, Insn]:
    """The instructions reachable from `entries` without leaving [start, end)."""
    found: dict[int, Insn] = {}
    work = [e for e in entries if start <= e < end]
    while work:
        addr = work.pop()
        while start <= addr < end and addr not in found:
            insn = decode(read, addr)
            if insn is None or addr + insn.size > end:
                break
            found[addr] = insn
            if insn.target is not None and start <= insn.target < end and insn.op.mnemonic != "PCALL":
                work.append(insn.target)
            if insn.ends_path:
                break
            addr += insn.size
    return found


# ---- the ROM ------------------------------------------------------------------


def _rom_offset(bank: int, addr: int) -> int:
    return (bank & 0x3F) << 16 | addr


@dataclass(frozen=True)
class BootBlock:
    index: int
    rom_addr: int     # of its length prefix, in bank $C7
    dest: int         # SPC700 address
    data: bytes

    def reader(self):
        def read(addr: int, n: int) -> bytes | None:
            i = addr - self.dest
            return self.data[i:i + n] if 0 <= i and i + n <= len(self.data) else None
        return read


def boot_blocks(rom: bytes) -> list[BootBlock]:
    """The six blocks of the boot upload, read by the table at $C7:0010."""
    base = _rom_offset(HIROM_BANK, 0)
    word = lambda off: rom[base + off] | rom[base + off + 1] << 8   # noqa: E731
    out = []
    for k in range(BOOT_BLOCKS):
        src = word(BOOT_SOURCES + 2 * k)
        dest = word(BOOT_DESTINATIONS + 2 * k)
        length = word(src)
        start = base + src + 2
        out.append(BootBlock(k, src, dest, bytes(rom[start:start + length])))
    return out


def load_rom(path: str | Path) -> bytes:
    data = Path(path).read_bytes()
    if len(data) % 0x8000 == 0x200:
        data = data[0x200:]
    return data


def read_coverage(path: str | Path) -> set[int]:
    """SPC700 PCs from a `ct_dev --spc-coverage` file (one hex address a line)."""
    out = set()
    for line in Path(path).read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.add(int(line.split()[0], 16))
    return out


def table_entries(block: BootBlock, spec: str) -> list[int]:
    """The words of a `--table ADDR:COUNT` spec, read from the block."""
    addr, count = spec.split(":")
    at = int(addr, 16)
    raw = block.reader()(at, 2 * int(count, 0)) or b""
    return [raw[i] | raw[i + 1] << 8 for i in range(0, len(raw) - 1, 2)]


def listing(block: BootBlock, start: int, end: int, follow=None, executed=None) -> list[str]:
    read = block.reader()
    lines = []
    if follow is None:
        insns = {i.addr: i for i in linear(read, start, end)}
    else:
        insns = reachable(read, follow, start, end)
    addr = start
    while addr < end:
        insn = insns.get(addr)
        if insn is None:
            n = 1
            while addr + n < end and n < 8 and addr + n not in insns:
                n += 1
            raw = read(addr, n) or b""
            lines.append(f"{addr:04X}  {'':<9}  .db {', '.join(f'${b:02X}' for b in raw)}")
            addr += n
            continue
        mark = ""
        if executed is not None:
            mark = "*" if addr in executed else " "
        hexb = " ".join(f"{b:02X}" for b in insn.raw)
        tail = f"  ; {insn.comment}" if insn.comment else ""
        lines.append(f"{addr:04X}{mark} {hexb:<9}  {insn.text}{tail}")
        addr += insn.size
    return lines


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rom", default="roms/ct-usa.sfc")
    ap.add_argument("--block", type=int, default=0)
    ap.add_argument("--from", dest="start", type=lambda s: int(s, 16))
    ap.add_argument("--to", dest="end", type=lambda s: int(s, 16))
    ap.add_argument("--follow", nargs="*", type=lambda s: int(s, 16))
    ap.add_argument("--table", action="append", default=[])
    ap.add_argument("--coverage")
    ap.add_argument("--blocks", action="store_true")
    args = ap.parse_args(argv)

    blocks = boot_blocks(load_rom(args.rom))
    if args.blocks:
        for b in blocks:
            print(f"block {b.index}: $C7:{b.rom_addr:04X}  {len(b.data):5d} bytes to SPC700 "
                  f"${b.dest:04X}-${b.dest + len(b.data) - 1:04X}")
        return 0
    block = blocks[args.block]
    start = args.start if args.start is not None else block.dest
    end = args.end if args.end is not None else block.dest + len(block.data)
    executed = read_coverage(args.coverage) if args.coverage else None
    follow = args.follow
    if follow is not None and not follow:
        follow = [block.dest]
    if follow is None and executed is not None:
        follow = sorted(executed)
    if args.table:
        follow = list(follow or [block.dest])
        for spec in args.table:
            follow += table_entries(block, spec)
    for line in listing(block, start, end, follow, executed):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
