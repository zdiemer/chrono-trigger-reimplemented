"""Translation of one 65816 instruction into C statements.

The statements operate on a `Cpu *c` through the helpers in runtime/port/generated.h
and match the interpreter (runtime/cpu/cpu65816.c) exactly: registers, flags,
bus accesses in order, internal cycles, and the bytes it fetches.

Operands, the instruction's address and its program bank are *values*: an int
when they are known at generation time (code generated from the ROM, where the
C compiler sees constants), or a C expression when they are only known at run
time (the SingleStepTests harness, which runs every test of an opcode through
one function). Both go through the same templates, so the tests exercise the
code the generator emits.

Control transfers go through a `Flow`, which decides what a jump to a static
target becomes (a `goto` inside a block, or setting PC and returning to the
runtime) and how to hand back to the runtime once PC is set.
"""

from dataclasses import dataclass, field
from typing import Protocol

from tools.disasm.cpu import OPCODES, Mode, operand_size

Value = int | str


@dataclass(frozen=True)
class Ctx:
    """Register widths the code is generated for. Emulation mode is 8-bit."""
    m8: bool
    x8: bool
    e: bool = False

    def __post_init__(self):
        if self.e and not (self.m8 and self.x8):
            raise ValueError("emulation mode has 8-bit registers")

    def name(self) -> str:
        if self.e:
            return "emu"
        return f"m{8 if self.m8 else 16}x{8 if self.x8 else 16}"


class Flow(Protocol):
    def jump(self, pc: Value, pb: Value | None = None) -> list[str]:
        """Statements that continue at a static target (pb None: same bank)."""

    def exit(self) -> list[str]:
        """Statements that return to the runtime, with PC and PB already set."""


@dataclass
class Translation:
    lines: list[str]
    size: int                    # instruction bytes
    falls_through: bool = True   # execution may continue with the next instruction
    next_ctx: Ctx | None = None  # widths for the next instruction; None if only known at run time
    targets: list = field(default_factory=list)   # static jump targets, (pc, pb or None)
    calls: list = field(default_factory=list)     # static call targets, 24-bit when known


# ---- values -----------------------------------------------------------------------------


def lit(v: Value, digits: int) -> str:
    return f"0x{v:0{digits}X}" if isinstance(v, int) else v


def add16(a: Value, b: Value) -> Value:
    if isinstance(a, int) and isinstance(b, int):
        return (a + b) & 0xFFFF
    if isinstance(b, int) and b < 0:
        return f"(uint16_t)({lit(a, 4)} - 0x{-b:X})"
    return f"(uint16_t)({lit(a, 4)} + {lit(b, 4)})"


def addr24(bank: Value, off: Value) -> Value:
    if isinstance(bank, int) and isinstance(off, int):
        return bank << 16 | off
    return f"((uint32_t){lit(bank, 2)} << 16 | {lit(off, 4)})"


def c_bool(v: bool) -> str:
    return "true" if v else "false"


@dataclass(frozen=True)
class Operand:
    """The operand bytes after the opcode, as values."""
    bytes: tuple = ()

    @staticmethod
    def of(raw: bytes) -> "Operand":
        return Operand(tuple(raw))

    def _join(self, n: int) -> Value:
        b = self.bytes[:n]
        if all(isinstance(x, int) for x in b):
            return sum(x << (8 * i) for i, x in enumerate(b))
        cast = "uint16_t" if n == 2 else "uint32_t"
        parts = [lit(b[0], 2)] + [f"({cast}){lit(x, 2)} << {8 * i}" for i, x in enumerate(b[1:], 1)]
        return f"({cast})({' | '.join(parts)})"

    @property
    def v8(self) -> Value:
        return self.bytes[0]

    @property
    def v16(self) -> Value:
        return self._join(2)

    @property
    def v24(self) -> Value:
        return self._join(3)

    def rel8(self) -> Value:
        b = self.bytes[0]
        if isinstance(b, int):
            return b - 0x100 if b & 0x80 else b
        return f"(int8_t){b}"


# ---- translation ---------------------------------------------------------------------------

GROUP1 = {"ORA": "ora", "AND": "and", "EOR": "eor", "ADC": "adc", "SBC": "sbc", "LDA": "lda", "CMP": None}
MODIFY = {"ASL": "asl", "LSR": "lsr", "ROL": "rol", "ROR": "ror", "INC": "inc", "DEC": "dec",
          "TSB": "tsb", "TRB": "trb"}
BRANCH_CONDITIONS = {
    "BPL": "!(c->p & CPU_FLAG_N)", "BMI": "c->p & CPU_FLAG_N",
    "BVC": "!(c->p & CPU_FLAG_V)", "BVS": "c->p & CPU_FLAG_V",
    "BCC": "!(c->p & CPU_FLAG_C)", "BCS": "c->p & CPU_FLAG_C",
    "BNE": "!(c->p & CPU_FLAG_Z)", "BEQ": "c->p & CPU_FLAG_Z",
}
FLAG_OPS = {
    "CLC": "c->p &= (uint8_t)~CPU_FLAG_C;", "SEC": "c->p |= CPU_FLAG_C;",
    "CLI": "c->p &= (uint8_t)~CPU_FLAG_I;", "SEI": "c->p |= CPU_FLAG_I;",
    "CLD": "c->p &= (uint8_t)~CPU_FLAG_D;", "SED": "c->p |= CPU_FLAG_D;",
    "CLV": "c->p &= (uint8_t)~CPU_FLAG_V;",
}
# Effective address helpers (runtime/port/generated.h) per addressing mode. `always`
# is whether indexing takes its extra cycle regardless of page crossing.
EFFECTIVE_ADDRESS = {
    Mode.DP: "rc_dp(c, {o8})", Mode.DPX: "rc_dp_index{emu}(c, {o8}, c->x)",
    Mode.DPY: "rc_dp_index{emu}(c, {o8}, c->y)", Mode.DPIND: "rc_dpind(c, {o8})",
    Mode.DPXIND: "rc_dpxind{emu}(c, {o8})", Mode.DPINDY: "rc_dpindy(c, {o8}, {always})",
    Mode.DPLIND: "rc_dplind(c, {o8})", Mode.DPLINDY: "rc_dplindy(c, {o8})",
    Mode.ABS: "rc_abs(c, {o16})", Mode.ABSX: "rc_absx(c, {o16}, {always})", Mode.ABSY: "rc_absy(c, {o16}, {always})",
    Mode.LONG: "{o24}", Mode.LONGX: "rc_longx(c, {o24})",
    Mode.SR: "rc_sr(c, {o8})", Mode.SRINDY: "rc_srindy(c, {o8})",
}
# Addressing modes whose data wraps within bank 0 (the address is 16 bits).
BANK0_MODES = {Mode.DP, Mode.DPX, Mode.DPY, Mode.SR}


class _Emitter:
    def __init__(self, opcode: int, operand: Operand, pc: Value, pb: Value, ctx: Ctx, flow: Flow,
                 live: bool = False):
        self.live = live
        self.opcode = opcode
        self.name, self.mode = OPCODES[opcode]
        self.op = operand
        self.pc = pc
        self.pb = pb
        self.ctx = ctx
        self.flow = flow
        n = operand_size(self.mode, ctx.m8, ctx.x8)
        if len(operand.bytes) != n:
            raise ValueError(f"opcode {opcode:02X} takes {n} operand bytes, got {len(operand.bytes)}")
        self.size = 1 + n
        self.lines: list[str] = []
        self.t = Translation(self.lines, self.size, next_ctx=ctx)

    # -- small pieces --

    def emit(self, *lines: str):
        self.lines.extend(lines)

    def io(self, n: int):
        self.emit(f"RC_IO(c, {n});")

    def fetch(self, start: int, n: int):
        """The interpreter fetching n instruction bytes starting at offset `start`."""
        where = addr24(self.pb, add16(self.pc, start) if start else self.pc)
        if self.live:
            self.emit(f"RC_FETCH_LIVE(c, {lit(where, 6)}, {n}, {start});")
            return
        last = self.opcode if start + n == 1 else self.op.bytes[start + n - 2]
        self.emit(f"RC_FETCH(c, {lit(where, 6)}, {n}, {lit(last, 2)});")

    def next_pc(self, extra: int = 0) -> Value:
        return add16(self.pc, self.size + extra)

    def exit(self):
        self.emit(*self.flow.exit())
        self.t.falls_through = False

    def jump(self, pc: Value, pb: Value | None = None):
        self.emit(*self.flow.jump(pc, pb))
        self.t.targets.append((pc, pb))

    @property
    def emu(self) -> str:
        return "_emu" if self.ctx.e else ""

    def push8(self, v: str):
        self.emit(f"rc_push8{self.emu}(c, {v});")

    def push16(self, v: str):
        self.emit(f"rc_push16{self.emu}(c, {v});")

    def fix_stack(self):
        """After the 65816-only stack instructions, which don't wrap S per byte."""
        if self.ctx.e:
            self.emit("rc_fix_stack_emu(c);")

    def w(self, eight: bool) -> str:
        return "8" if eight else "16"

    # -- addresses and data --

    def ea(self, write: bool = False) -> tuple[str, bool]:
        """C expression for the effective address, and whether its data wraps in bank 0."""
        o = self.op
        text = EFFECTIVE_ADDRESS[self.mode].format(
            o8=lit(o.v8, 2), o16=lit(o.v16, 4), o24=lit(o.v24, 6), emu=self.emu,
            always=c_bool(write or not self.ctx.x8))
        return text, self.mode in BANK0_MODES

    @staticmethod
    def rd(addr: str, bank0: bool, eight: bool) -> str:
        if eight:
            return f"rc_rd8(c, {addr})"
        return f"rc_rd16{'_b0' if bank0 else ''}(c, {addr})"

    @staticmethod
    def wr(addr: str, bank0: bool, eight: bool, value: str) -> str:
        if eight:
            return f"rc_wr8(c, {addr}, {value});"
        return f"rc_wr16{'_b0' if bank0 else ''}(c, {addr}, {value});"

    def operand_data(self, eight: bool, write: bool = False) -> str:
        """The value an instruction reads: immediate operand or memory."""
        if self.mode in (Mode.IMM_M, Mode.IMM_X):
            return lit(self.op.v8, 2) if eight else lit(self.op.v16, 4)
        addr, bank0 = self.ea(write)
        return self.rd(addr, bank0, eight)

    @staticmethod
    def reg(name: str, eight: bool) -> str:
        return f"(uint8_t)c->{name}" if eight else f"c->{name}"

    def load_index(self, reg: str, value: str):
        if self.ctx.x8:
            self.emit(f"c->{reg} = {value};", f"rc_nz8(c, (uint8_t)c->{reg});")
        else:
            self.emit(f"c->{reg} = {value};", f"rc_nz16(c, c->{reg});")

    def transfer_index(self, dst: str, src: str):
        self.io(1)
        self.load_index(dst, f"c->{src} & 0xFF" if self.ctx.x8 else f"c->{src}")

    # -- the instruction --

    def run(self) -> Translation:
        n, m, ctx = self.name, self.mode, self.ctx
        wm, wx = ctx.m8, ctx.x8
        if not (n == "JSL" or (n == "JSR" and m is Mode.ABSXIND)):
            self.fetch(0, self.size)   # these two push between fetches

        if n in GROUP1:
            data = self.operand_data(wm)
            if n == "CMP":
                self.emit(f"rc_cmp{self.w(wm)}(c, {self.reg('a', wm)}, {data});")
            else:
                self.emit(f"rc_{GROUP1[n]}{self.w(wm)}(c, {data});")
        elif n in ("STA", "STZ", "STX", "STY"):
            eight = wx if n in ("STX", "STY") else wm
            value = "0" if n == "STZ" else self.reg(n[2].lower(), eight)
            addr, bank0 = self.ea(write=True)
            self.emit(self.wr(addr, bank0, eight, value))
        elif n in ("LDX", "LDY"):
            self.load_index(n[2].lower(), self.operand_data(wx))
        elif n in ("CPX", "CPY"):
            self.emit(f"rc_cmp{self.w(wx)}(c, {self.reg(n[2].lower(), wx)}, {self.operand_data(wx)});")
        elif n == "BIT":
            imm = "_imm" if m is Mode.IMM_M else ""
            self.emit(f"rc_bit{imm}{self.w(wm)}(c, {self.operand_data(wm)});")
        elif n in MODIFY and m is Mode.ACC:
            self.io(1)
            if wm:
                self.emit(f"rc_set_a8(c, rc_{MODIFY[n]}8(c, (uint8_t)c->a));")
            else:
                self.emit(f"c->a = rc_{MODIFY[n]}16(c, c->a);")
        elif n in MODIFY:
            addr, bank0 = self.ea(write=True)
            b0 = "_b0" if bank0 and not wm else ""
            self.emit("{",
                      f"{'uint16_t' if bank0 else 'uint32_t'} ea = {addr};",
                      f"{'uint8_t' if wm else 'uint16_t'} v = {self.rd('ea', bank0, wm)};")
            self.io(1)
            if wm:
                self.emit(f"rc_wr8(c, ea, rc_{MODIFY[n]}8(c, v));")
            else:
                self.emit(f"rc_wr16_rev{b0}(c, ea, rc_{MODIFY[n]}16(c, v));")
            self.emit("}")
        elif n in ("INX", "INY", "DEX", "DEY"):
            self.io(1)
            reg = n[2].lower()
            op = "+" if n[0] == "I" else "-"
            self.load_index(reg, f"(uint{'8' if wx else '16'}_t)(c->{reg} {op} 1)")
        elif n in BRANCH_CONDITIONS or n == "BRA":
            target = add16(self.next_pc(), self.op.rel8())
            if not ctx.e:
                taken_io: Value = 1
            elif isinstance(target, int) and isinstance(self.next_pc(), int):
                taken_io = 2 if target >> 8 != self.next_pc() >> 8 else 1
            else:
                taken_io = f"({target} >> 8 != {lit(self.next_pc(), 4)} >> 8 ? 2 : 1)"
            if n == "BRA":
                self.emit(f"RC_IO(c, {taken_io});")
                self.jump(target)
                self.t.falls_through = False
            else:
                self.emit(f"if ({BRANCH_CONDITIONS[n]}) {{", f"RC_IO(c, {taken_io});")
                self.jump(target)
                self.emit("}")
        elif n == "BRL":
            self.io(1)
            self.jump(add16(self.next_pc(), self.op.v16))
            self.t.falls_through = False
        elif n == "JMP" and m is Mode.ABS:
            self.jump(self.op.v16)
            self.t.falls_through = False
        elif n == "JML" and m is Mode.LONG:
            v = self.op.v24
            if isinstance(v, int):
                self.jump(v & 0xFFFF, v >> 16)
            else:
                self.jump(self.op.v16, self.op.bytes[2])
            self.t.falls_through = False
        elif n == "JMP" and m is Mode.ABSIND:
            self.emit(f"c->pc = rc_rd16_b0(c, {lit(self.op.v16, 4)});")
            self.exit()
        elif n == "JMP" and m is Mode.ABSXIND:
            self.emit("{", f"uint16_t p = (uint16_t)({lit(self.op.v16, 4)} + c->x);")
            self.io(1)
            self.emit("c->pc = rc_rd16_pb(c, p);", "}")
            self.exit()
        elif n == "JML" and m is Mode.ABSLIND:
            self.emit("{", f"uint32_t t = rc_rd24_b0(c, {lit(self.op.v16, 4)});",
                      "c->pb = (uint8_t)(t >> 16);", "c->pc = (uint16_t)t;", "}")
            self.exit()
        elif n == "JSR" and m is Mode.ABS:
            self.io(1)
            self.push16(lit(self.next_pc(-1), 4))
            self.emit(f"c->pc = {lit(self.op.v16, 4)};")
            self.call(self.op.v16, same_bank=True)
        elif n == "JSL":
            self.fetch(0, 3)
            self.emit("rc_push8(c, c->pb);")
            self.io(1)
            self.fetch(3, 1)
            self.emit(f"rc_push16(c, {lit(self.next_pc(-1), 4)});")
            self.fix_stack()
            self.emit(f"c->pb = {lit(self.op.bytes[2], 2)};", f"c->pc = {lit(self.op.v16, 4)};")
            self.call(self.op.v24, same_bank=False)
        elif n == "JSR" and m is Mode.ABSXIND:
            self.fetch(0, 2)
            self.push16(lit(self.next_pc(-1), 4))
            self.fetch(2, 1)
            self.emit("{", f"uint16_t p = (uint16_t)({lit(self.op.v16, 4)} + c->x);")
            self.io(1)
            self.emit("c->pc = rc_rd16_pb(c, p);", "}")
            self.exit()
        elif n == "RTS":
            self.io(2)
            self.emit(f"c->pc = (uint16_t)(rc_pull16{self.emu}(c) + 1);")
            self.io(1)
            self.exit()
        elif n == "RTL":
            self.io(2)
            self.emit("c->pc = (uint16_t)(rc_pull16(c) + 1);", "c->pb = rc_pull8(c);")
            self.fix_stack()
            self.exit()
        elif n == "RTI":
            self.io(2)
            self.emit(f"c->p = rc_pull8{self.emu}(c);", "rc_normalize_p(c);",
                      f"c->pc = rc_pull16{self.emu}(c);")
            if not ctx.e:
                self.emit("c->pb = rc_pull8(c);")
            self.exit()
        elif n in ("BRK", "COP"):
            vector = {("BRK", False): 0xFFE6, ("BRK", True): 0xFFFE,
                      ("COP", False): 0xFFE4, ("COP", True): 0xFFF4}[(n, ctx.e)]
            self.emit(f"rc_software_interrupt{self.emu}(c, {lit(self.next_pc(), 4)}, 0x{vector:04X});")
            self.exit()
        elif n in ("PHA", "PHX", "PHY"):
            eight = wm if n == "PHA" else wx
            self.io(1)
            reg = n[2].lower()
            if eight:
                self.push8(f"(uint8_t)c->{reg}")
            else:
                self.push16(f"c->{reg}")
        elif n == "PLA":
            self.io(2)
            self.emit(f"rc_lda{self.w(wm)}(c, rc_pull{self.w(wm)}{self.emu}(c));")
        elif n in ("PLX", "PLY"):
            self.io(2)
            self.load_index(n[2].lower(), f"rc_pull{self.w(wx)}{self.emu}(c)")
        elif n in ("PHB", "PHK", "PHP"):
            self.io(1)
            self.push8({"PHB": "c->db", "PHK": "c->pb", "PHP": "c->p"}[n])
        elif n == "PLB":
            self.io(2)
            self.emit("c->db = rc_pull8(c);", "rc_nz8(c, c->db);")
            self.fix_stack()
        elif n == "PHD":
            self.io(1)
            self.emit("rc_push16(c, c->d);")
            self.fix_stack()
        elif n == "PLD":
            self.io(2)
            self.emit("c->d = rc_pull16(c);", "rc_nz16(c, c->d);")
            self.fix_stack()
        elif n == "PLP":
            self.io(2)
            self.emit(f"c->p = rc_pull8{self.emu}(c);", "rc_normalize_p(c);")
            self.t.next_ctx = None
        elif n == "PEA":
            self.emit(f"rc_push16(c, {lit(self.op.v16, 4)});")
            self.fix_stack()
        elif n == "PEI":
            self.emit(f"rc_push16(c, rc_rd16_b0(c, rc_dp(c, {lit(self.op.v8, 2)})));")
            self.fix_stack()
        elif n == "PER":
            self.io(1)
            self.emit(f"rc_push16(c, {lit(add16(self.next_pc(), self.op.v16), 4)});")
            self.fix_stack()
        elif n in ("TAX", "TAY"):
            self.transfer_index(n[2].lower(), "a")
        elif n == "TXY":
            self.transfer_index("y", "x")
        elif n == "TYX":
            self.transfer_index("x", "y")
        elif n == "TSX":
            self.transfer_index("x", "s")
        elif n in ("TXA", "TYA"):
            self.io(1)
            self.emit(f"rc_lda{self.w(wm)}(c, {self.reg(n[1].lower(), wm)});")
        elif n in ("TXS", "TCS"):
            self.io(1)
            src = "x" if n == "TXS" else "a"
            if ctx.e:
                self.emit(f"c->s = (uint16_t)(0x0100 | (c->{src} & 0xFF));")
            else:
                self.emit(f"c->s = c->{src};")
        elif n == "TCD":
            self.io(1)
            self.emit("c->d = c->a;", "rc_nz16(c, c->d);")
        elif n in ("TDC", "TSC"):
            self.io(1)
            self.emit(f"rc_lda16(c, c->{'d' if n == 'TDC' else 's'});")
        elif n == "XBA":
            self.io(2)
            self.emit("c->a = (uint16_t)(c->a >> 8 | c->a << 8);", "rc_nz8(c, (uint8_t)c->a);")
        elif n in FLAG_OPS:
            self.io(1)
            self.emit(FLAG_OPS[n])
        elif n in ("REP", "SEP"):
            bits = lit(self.op.v8, 2)
            self.emit(f"c->p &= (uint8_t)~{bits};" if n == "REP" else f"c->p |= {bits};")
            self.io(1)
            self.emit("rc_normalize_p(c);")
            self.t.next_ctx = self.widths_after(n, self.op.v8)
        elif n == "XCE":
            self.io(1)
            self.emit("rc_xce(c);")
            self.t.next_ctx = None
        elif n in ("MVN", "MVP"):
            dst, src = self.op.bytes
            step = "+" if n == "MVN" else "-"
            cast = "uint8_t" if wx else "uint16_t"
            self.emit(f"c->db = {lit(dst, 2)};",
                      f"rc_wr8(c, {lit(addr24(dst, 'c->y'), 6)}, rc_rd8(c, {lit(addr24(src, 'c->x'), 6)}));")
            self.io(2)
            self.emit(f"c->x = ({cast})(c->x {step} 1);", f"c->y = ({cast})(c->y {step} 1);",
                      "if (c->a-- != 0) {")
            # One byte per pass: while bytes remain the instruction repeats, so
            # interrupts can land between passes as they do in the interpreter.
            self.jump(self.pc)
            self.emit("}")
        elif n == "NOP":
            self.io(1)
        elif n == "WDM":
            pass
        elif n in ("WAI", "STP"):
            self.io(2)
            self.emit(f"c->{'waiting' if n == 'WAI' else 'stopped'} = true;", f"c->pc = {lit(self.next_pc(), 4)};")
            self.exit()
        else:
            raise AssertionError(f"untranslated opcode {self.opcode:02X} {n} {m}")
        return self.t

    def call(self, target: Value, same_bank: bool):
        if isinstance(target, int) and isinstance(self.pb, int):
            self.t.calls.append(self.pb << 16 | target if same_bank else target)
        self.exit()

    def widths_after(self, name: str, bits: Value) -> Ctx | None:
        if self.ctx.e:
            return self.ctx
        if not isinstance(bits, int):
            return None
        m8, x8 = self.ctx.m8, self.ctx.x8
        if name == "REP":
            return Ctx(m8 and not bits & 0x20, x8 and not bits & 0x10)
        return Ctx(m8 or bool(bits & 0x20), x8 or bool(bits & 0x10))


LIVE_OPERAND = tuple(f"rc_timing.op[{k}]" for k in range(1, 4))


def translate(opcode: int, operand: Operand, pc: Value, pb: Value, ctx: Ctx, flow: Flow,
              live: bool = False) -> Translation:
    """C statements for the instruction at pb:pc. Unless the translation says it
    doesn't fall through, the caller continues with the next instruction (or sets
    PC to it); the statements never set PC for fall-through.

    live: the operand bytes are whatever the instruction fetches at run time
    (code in RAM that the program patches): the fetch stores them in
    rc_timing.op[1..3] (RC_FETCH_LIVE), and `operand` must be
    Operand(LIVE_OPERAND[:n])."""
    return _Emitter(opcode, operand, pc, pb, ctx, flow, live).run()


def indent(lines: list[str], depth: int = 1, unit: str = "    ") -> list[str]:
    """Indent statements by brace depth (lines ending in '{' open, lines starting with '}' close)."""
    out = []
    for line in lines:
        if line.startswith("}"):
            depth -= 1
        out.append(line if line.endswith(":") else unit * depth + line)
        if line.endswith("{"):
            depth += 1
    return out
