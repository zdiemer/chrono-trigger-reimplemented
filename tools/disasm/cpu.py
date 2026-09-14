"""WDC 65C816 opcode table and instruction decoding."""

from dataclasses import dataclass
from enum import Enum


class Mode(Enum):
    IMP = "imp"            # implied
    ACC = "acc"            # A
    IMM_M = "imm_m"        # #const, width follows M
    IMM_X = "imm_x"        # #const, width follows X
    IMM8 = "imm8"          # #const, always 8-bit (REP/SEP/BRK/COP/WDM)
    DP = "dp"              # dp
    DPX = "dpx"            # dp,X
    DPY = "dpy"            # dp,Y
    DPIND = "dpind"        # (dp)
    DPXIND = "dpxind"      # (dp,X)
    DPINDY = "dpindy"      # (dp),Y
    DPLIND = "dplind"      # [dp]
    DPLINDY = "dplindy"    # [dp],Y
    ABS = "abs"            # abs
    ABSX = "absx"          # abs,X
    ABSY = "absy"          # abs,Y
    ABSIND = "absind"      # (abs)      JMP
    ABSXIND = "absxind"    # (abs,X)    JMP/JSR
    ABSLIND = "abslind"    # [abs]      JML
    LONG = "long"          # long
    LONGX = "longx"        # long,X
    SR = "sr"              # sr,S
    SRINDY = "srindy"      # (sr,S),Y
    REL8 = "rel8"
    REL16 = "rel16"
    MOVE = "move"          # MVN/MVP dst,src (encoded src? see decode)
    PEA = "pea"            # #abs
    PEI = "pei"            # (dp)


# Operand byte count per mode; IMM_M / IMM_X are resolved at decode time.
OPERAND_SIZE = {
    Mode.IMP: 0, Mode.ACC: 0, Mode.IMM8: 1,
    Mode.DP: 1, Mode.DPX: 1, Mode.DPY: 1, Mode.DPIND: 1, Mode.DPXIND: 1,
    Mode.DPINDY: 1, Mode.DPLIND: 1, Mode.DPLINDY: 1,
    Mode.ABS: 2, Mode.ABSX: 2, Mode.ABSY: 2, Mode.ABSIND: 2, Mode.ABSXIND: 2,
    Mode.ABSLIND: 2, Mode.LONG: 3, Mode.LONGX: 3, Mode.SR: 1, Mode.SRINDY: 1,
    Mode.REL8: 1, Mode.REL16: 2, Mode.MOVE: 2, Mode.PEA: 2, Mode.PEI: 1,
}

_M = Mode
# fmt: off
_TABLE = [
    # 0x00-0x0F
    ("BRK", _M.IMM8), ("ORA", _M.DPXIND), ("COP", _M.IMM8), ("ORA", _M.SR),
    ("TSB", _M.DP), ("ORA", _M.DP), ("ASL", _M.DP), ("ORA", _M.DPLIND),
    ("PHP", _M.IMP), ("ORA", _M.IMM_M), ("ASL", _M.ACC), ("PHD", _M.IMP),
    ("TSB", _M.ABS), ("ORA", _M.ABS), ("ASL", _M.ABS), ("ORA", _M.LONG),
    # 0x10-0x1F
    ("BPL", _M.REL8), ("ORA", _M.DPINDY), ("ORA", _M.DPIND), ("ORA", _M.SRINDY),
    ("TRB", _M.DP), ("ORA", _M.DPX), ("ASL", _M.DPX), ("ORA", _M.DPLINDY),
    ("CLC", _M.IMP), ("ORA", _M.ABSY), ("INC", _M.ACC), ("TCS", _M.IMP),
    ("TRB", _M.ABS), ("ORA", _M.ABSX), ("ASL", _M.ABSX), ("ORA", _M.LONGX),
    # 0x20-0x2F
    ("JSR", _M.ABS), ("AND", _M.DPXIND), ("JSL", _M.LONG), ("AND", _M.SR),
    ("BIT", _M.DP), ("AND", _M.DP), ("ROL", _M.DP), ("AND", _M.DPLIND),
    ("PLP", _M.IMP), ("AND", _M.IMM_M), ("ROL", _M.ACC), ("PLD", _M.IMP),
    ("BIT", _M.ABS), ("AND", _M.ABS), ("ROL", _M.ABS), ("AND", _M.LONG),
    # 0x30-0x3F
    ("BMI", _M.REL8), ("AND", _M.DPINDY), ("AND", _M.DPIND), ("AND", _M.SRINDY),
    ("BIT", _M.DPX), ("AND", _M.DPX), ("ROL", _M.DPX), ("AND", _M.DPLINDY),
    ("SEC", _M.IMP), ("AND", _M.ABSY), ("DEC", _M.ACC), ("TSC", _M.IMP),
    ("BIT", _M.ABSX), ("AND", _M.ABSX), ("ROL", _M.ABSX), ("AND", _M.LONGX),
    # 0x40-0x4F
    ("RTI", _M.IMP), ("EOR", _M.DPXIND), ("WDM", _M.IMM8), ("EOR", _M.SR),
    ("MVP", _M.MOVE), ("EOR", _M.DP), ("LSR", _M.DP), ("EOR", _M.DPLIND),
    ("PHA", _M.IMP), ("EOR", _M.IMM_M), ("LSR", _M.ACC), ("PHK", _M.IMP),
    ("JMP", _M.ABS), ("EOR", _M.ABS), ("LSR", _M.ABS), ("EOR", _M.LONG),
    # 0x50-0x5F
    ("BVC", _M.REL8), ("EOR", _M.DPINDY), ("EOR", _M.DPIND), ("EOR", _M.SRINDY),
    ("MVN", _M.MOVE), ("EOR", _M.DPX), ("LSR", _M.DPX), ("EOR", _M.DPLINDY),
    ("CLI", _M.IMP), ("EOR", _M.ABSY), ("PHY", _M.IMP), ("TCD", _M.IMP),
    ("JML", _M.LONG), ("EOR", _M.ABSX), ("LSR", _M.ABSX), ("EOR", _M.LONGX),
    # 0x60-0x6F
    ("RTS", _M.IMP), ("ADC", _M.DPXIND), ("PER", _M.REL16), ("ADC", _M.SR),
    ("STZ", _M.DP), ("ADC", _M.DP), ("ROR", _M.DP), ("ADC", _M.DPLIND),
    ("PLA", _M.IMP), ("ADC", _M.IMM_M), ("ROR", _M.ACC), ("RTL", _M.IMP),
    ("JMP", _M.ABSIND), ("ADC", _M.ABS), ("ROR", _M.ABS), ("ADC", _M.LONG),
    # 0x70-0x7F
    ("BVS", _M.REL8), ("ADC", _M.DPINDY), ("ADC", _M.DPIND), ("ADC", _M.SRINDY),
    ("STZ", _M.DPX), ("ADC", _M.DPX), ("ROR", _M.DPX), ("ADC", _M.DPLINDY),
    ("SEI", _M.IMP), ("ADC", _M.ABSY), ("PLY", _M.IMP), ("TDC", _M.IMP),
    ("JMP", _M.ABSXIND), ("ADC", _M.ABSX), ("ROR", _M.ABSX), ("ADC", _M.LONGX),
    # 0x80-0x8F
    ("BRA", _M.REL8), ("STA", _M.DPXIND), ("BRL", _M.REL16), ("STA", _M.SR),
    ("STY", _M.DP), ("STA", _M.DP), ("STX", _M.DP), ("STA", _M.DPLIND),
    ("DEY", _M.IMP), ("BIT", _M.IMM_M), ("TXA", _M.IMP), ("PHB", _M.IMP),
    ("STY", _M.ABS), ("STA", _M.ABS), ("STX", _M.ABS), ("STA", _M.LONG),
    # 0x90-0x9F
    ("BCC", _M.REL8), ("STA", _M.DPINDY), ("STA", _M.DPIND), ("STA", _M.SRINDY),
    ("STY", _M.DPX), ("STA", _M.DPX), ("STX", _M.DPY), ("STA", _M.DPLINDY),
    ("TYA", _M.IMP), ("STA", _M.ABSY), ("TXS", _M.IMP), ("TXY", _M.IMP),
    ("STZ", _M.ABS), ("STA", _M.ABSX), ("STZ", _M.ABSX), ("STA", _M.LONGX),
    # 0xA0-0xAF
    ("LDY", _M.IMM_X), ("LDA", _M.DPXIND), ("LDX", _M.IMM_X), ("LDA", _M.SR),
    ("LDY", _M.DP), ("LDA", _M.DP), ("LDX", _M.DP), ("LDA", _M.DPLIND),
    ("TAY", _M.IMP), ("LDA", _M.IMM_M), ("TAX", _M.IMP), ("PLB", _M.IMP),
    ("LDY", _M.ABS), ("LDA", _M.ABS), ("LDX", _M.ABS), ("LDA", _M.LONG),
    # 0xB0-0xBF
    ("BCS", _M.REL8), ("LDA", _M.DPINDY), ("LDA", _M.DPIND), ("LDA", _M.SRINDY),
    ("LDY", _M.DPX), ("LDA", _M.DPX), ("LDX", _M.DPY), ("LDA", _M.DPLINDY),
    ("CLV", _M.IMP), ("LDA", _M.ABSY), ("TSX", _M.IMP), ("TYX", _M.IMP),
    ("LDY", _M.ABSX), ("LDA", _M.ABSX), ("LDX", _M.ABSY), ("LDA", _M.LONGX),
    # 0xC0-0xCF
    ("CPY", _M.IMM_X), ("CMP", _M.DPXIND), ("REP", _M.IMM8), ("CMP", _M.SR),
    ("CPY", _M.DP), ("CMP", _M.DP), ("DEC", _M.DP), ("CMP", _M.DPLIND),
    ("INY", _M.IMP), ("CMP", _M.IMM_M), ("DEX", _M.IMP), ("WAI", _M.IMP),
    ("CPY", _M.ABS), ("CMP", _M.ABS), ("DEC", _M.ABS), ("CMP", _M.LONG),
    # 0xD0-0xDF
    ("BNE", _M.REL8), ("CMP", _M.DPINDY), ("CMP", _M.DPIND), ("CMP", _M.SRINDY),
    ("PEI", _M.PEI), ("CMP", _M.DPX), ("DEC", _M.DPX), ("CMP", _M.DPLINDY),
    ("CLD", _M.IMP), ("CMP", _M.ABSY), ("PHX", _M.IMP), ("STP", _M.IMP),
    ("JML", _M.ABSLIND), ("CMP", _M.ABSX), ("DEC", _M.ABSX), ("CMP", _M.LONGX),
    # 0xE0-0xEF
    ("CPX", _M.IMM_X), ("SBC", _M.DPXIND), ("SEP", _M.IMM8), ("SBC", _M.SR),
    ("CPX", _M.DP), ("SBC", _M.DP), ("INC", _M.DP), ("SBC", _M.DPLIND),
    ("INX", _M.IMP), ("SBC", _M.IMM_M), ("NOP", _M.IMP), ("XBA", _M.IMP),
    ("CPX", _M.ABS), ("SBC", _M.ABS), ("INC", _M.ABS), ("SBC", _M.LONG),
    # 0xF0-0xFF
    ("BEQ", _M.REL8), ("SBC", _M.DPINDY), ("SBC", _M.DPIND), ("SBC", _M.SRINDY),
    ("PEA", _M.PEA), ("SBC", _M.DPX), ("INC", _M.DPX), ("SBC", _M.DPLINDY),
    ("SED", _M.IMP), ("SBC", _M.ABSY), ("PLX", _M.IMP), ("XCE", _M.IMP),
    ("JSR", _M.ABSXIND), ("SBC", _M.ABSX), ("INC", _M.ABSX), ("SBC", _M.LONGX),
]
# fmt: on
assert len(_TABLE) == 256

OPCODES = [(name, mode) for name, mode in _TABLE]

BRANCHES = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ", "BRA", "BRL"}
CONDITIONAL_BRANCHES = BRANCHES - {"BRA", "BRL"}
RETURNS = {"RTS", "RTL", "RTI"}
# Opcodes that are almost never intentional in shipped code; hitting one usually
# means the analysis walked into data.
SUSPICIOUS = {"BRK", "COP", "STP", "WDM"}


@dataclass(frozen=True)
class Insn:
    pc: int              # 24-bit bus address
    opcode: int
    name: str
    mode: Mode
    operand: int         # little-endian operand value (MOVE: low byte dst bank, high byte src bank)
    size: int            # total bytes including opcode
    raw: bytes

    @property
    def next_pc(self) -> int:
        return (self.pc & 0xFF0000) | ((self.pc + self.size) & 0xFFFF)

    def branch_target(self) -> int:
        """Target of a relative branch or PER, in the program bank."""
        if self.mode is Mode.REL8:
            off = self.operand - 0x100 if self.operand & 0x80 else self.operand
        else:
            off = self.operand - 0x10000 if self.operand & 0x8000 else self.operand
        return (self.pc & 0xFF0000) | ((self.pc + self.size + off) & 0xFFFF)

    def text(self, label_for=None) -> str:
        """Format as assembly. label_for(bus_addr) -> str|None renders targets symbolically."""
        n, m, v = self.name, self.mode, self.operand
        bank = self.pc & 0xFF0000

        def lbl(addr: int, fallback: str) -> str:
            if label_for:
                s = label_for(addr)
                if s:
                    return s
            return fallback

        if m in (Mode.IMP,):
            return n
        if m is Mode.ACC:
            return f"{n} A"
        if m in (Mode.IMM_M, Mode.IMM_X, Mode.IMM8):
            w = (self.size - 1) * 2
            return f"{n} #${v:0{w}X}"
        if m is Mode.PEA:
            return f"{n} #${v:04X}"
        if m in (Mode.REL8, Mode.REL16):
            t = self.branch_target()
            return f"{n} {lbl(t, f'${t & 0xFFFF:04X}')}"
        if m is Mode.MOVE:
            # Encoded as opcode, dst bank, src bank; conventional syntax is src,dst.
            dst, src = v & 0xFF, v >> 8
            return f"{n} ${src:02X},${dst:02X}"
        fmt = {
            Mode.DP: "${:02X}", Mode.DPX: "${:02X},X", Mode.DPY: "${:02X},Y",
            Mode.DPIND: "(${:02X})", Mode.DPXIND: "(${:02X},X)", Mode.DPINDY: "(${:02X}),Y",
            Mode.DPLIND: "[${:02X}]", Mode.DPLINDY: "[${:02X}],Y", Mode.PEI: "(${:02X})",
            Mode.SR: "${:02X},S", Mode.SRINDY: "(${:02X},S),Y",
            Mode.ABS: "${:04X}", Mode.ABSX: "${:04X},X", Mode.ABSY: "${:04X},Y",
            Mode.ABSIND: "(${:04X})", Mode.ABSXIND: "(${:04X},X)", Mode.ABSLIND: "[${:04X}]",
            Mode.LONG: "${:06X}", Mode.LONGX: "${:06X},X",
        }[m]
        if n in ("JSR", "JMP") and m is Mode.ABS:
            return f"{n} {lbl(bank | v, fmt.format(v))}"
        if n in ("JSL", "JML") and m is Mode.LONG:
            return f"{n} {lbl(v, fmt.format(v))}"
        return f"{n} {fmt.format(v)}"


def operand_size(mode: Mode, m_flag: bool | None, x_flag: bool | None) -> int | None:
    """Operand byte count, or None if it depends on an unknown flag."""
    if mode is Mode.IMM_M:
        return None if m_flag is None else (1 if m_flag else 2)
    if mode is Mode.IMM_X:
        return None if x_flag is None else (1 if x_flag else 2)
    return OPERAND_SIZE[mode]


def decode(read, pc: int, m_flag: bool | None, x_flag: bool | None) -> Insn | None:
    """Decode one instruction. read(bus_addr, n) -> bytes|None. Returns None if the
    bytes aren't readable or the width depends on an unknown flag."""
    op = read(pc, 1)
    if op is None:
        return None
    opcode = op[0]
    name, mode = OPCODES[opcode]
    n = operand_size(mode, m_flag, x_flag)
    if n is None:
        return None
    raw = read(pc, 1 + n)
    if raw is None:
        return None
    operand = int.from_bytes(raw[1:], "little") if n else 0
    return Insn(pc, opcode, name, mode, operand, 1 + n, raw)
