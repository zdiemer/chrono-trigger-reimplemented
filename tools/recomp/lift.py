"""Lift a routine's instructions into draft statements (tools/recomp/ir.py).

One instruction becomes the statements that say what it does to the locals,
memory and the machine, with every flag it sets as its own assignment: the
cleanup (tools/recomp/draft.py) drops the ones liveness shows nobody reads.
Addresses come out as constants wherever D and DB are known
(tools/recomp/frame.py), so the renderer can name them; where they aren't,
`m_dp`/`m_abs` read the machine's, which the draft keeps current.

Decimal mode isn't modelled (ADC/SBC are binary): a draft that has them says so
and is registered with the binary-mode precondition, like the hand ports.
"""

from dataclasses import dataclass, field

from tools.disasm.cpu import Mode

from .frame import resolve, single
from .ir import (ALL_KEYS, Assign, Bin, Branch, CallOut, Cast, Const, Exit, Expr, Fn, Goto, Load, Mach, MachSet,
                 Part, Push, Store, Switch, Un, Var, simplify)

GROUP_OPS = {"ORA": "|", "AND": "&", "EOR": "^"}
BRANCH_FLAGS = {"BPL": ("n", False), "BMI": ("n", True), "BVC": ("v", False), "BVS": ("v", True),
                "BCC": ("c", False), "BCS": ("c", True), "BNE": ("z", False), "BEQ": ("z", True)}
HAND_BACK = {"BRK", "COP", "WAI", "STP", "XCE", "WDM"}


def region(a: int) -> str:
    """What a 24-bit bus address reaches: ram, rom, io, sram."""
    bank, off = a >> 16 & 0xFF, a & 0xFFFF
    if bank in (0x7E, 0x7F):
        return "ram"
    if bank < 0x40 or 0x80 <= bank < 0xC0:
        if off < 0x2000:
            return "ram"
        if off >= 0x8000:
            return "rom"
        if off >= 0x6000:
            return "sram"
        return "io"
    return "rom"


def canonical(a: int) -> int:
    """Low RAM mirrors as $7E:0000-$1FFF, the form the RAM map names."""
    if region(a) == "ram" and a >> 16 not in (0x7E, 0x7F):
        return 0x7E0000 | (a & 0xFFFF)
    return a


def long_const(a: int) -> Const:
    return Const(canonical(a & 0xFFFFFF), 24, "long")


def pure_at(e: Expr) -> bool:
    """A load from e is plain memory: the address is a constant (plus an index)
    outside the I/O area."""
    base = e
    while isinstance(base, Bin) and base.op == "+":
        base = base.l
    return isinstance(base, Const) and region(base.v) in ("ram", "rom", "sram") and \
        (region(base.v) != "ram" or (base.v >> 16) in (0x7E, 0x7F))


@dataclass
class Lifted:
    node: tuple
    insn: object
    stmts: list
    fall: tuple | None          # continues here after its statements (or after a Branch)


@dataclass
class Routine:
    """What the lifter needs to know about the routine and its callees."""
    entry: int
    m8: bool
    x8: bool
    d: frozenset | None         # the D values it is entered with (None: unknown)
    db: frozenset | None
    ret_live: frozenset         # keys a caller may read after it returns
    callee_live_in: object      # target -> keys (ALL_KEYS if unknown)
    callee_may_def: object      # target -> keys
    noreturn: set = field(default_factory=set)
    temps: dict = field(default_factory=dict)     # name -> width
    dp_name: object = None                        # offset -> name in a page with no fixed D
    decimal_sensitive: bool = False

    def temp(self, w: int) -> str:
        name = f"t{len(self.temps) + 1}"
        self.temps[name] = w
        return name


class Lifter:
    def __init__(self, graph, frames: dict, routine: Routine, tables: dict, rom, stops=None):
        self.g = graph
        self.frames = frames
        self.r = routine
        self.tables = tables      # dispatcher pc -> [targets] for resolved JMP (abs,X)
        self.rom = rom
        self.stops = stops or {}  # pc -> name: other entries (hand ports) the draft stops at

    # ---- machine state at a node --------------------------------------------------------

    def d_at(self, node):
        """D here as an int, or None."""
        f = self.frames.get(node)
        return None if f is None else single(resolve(f.d, self.r.d))

    def db_at(self, node):
        f = self.frames.get(node)
        return None if f is None else single(resolve(f.db, self.r.db))

    # ---- operands ----------------------------------------------------------------------

    def dp(self, node, o: int, index: Expr | None = None) -> Expr:
        d = self.d_at(node)
        if isinstance(d, int):
            a = (d + o) & 0xFFFF
            base = long_const(a) if a < 0x2000 else Const(a, 24, "long")
            return base if index is None else Bin("+", base, index, 24)
        named = self.r.dp_name(o) if self.r.dp_name else None
        m = Mach(f"m_dp(s, {named or f'0x{o:02X}'})", 16)
        return m if index is None else Bin("+", m, index, 16)

    def absolute(self, node, o: int, index: Expr | None = None) -> Expr:
        db = self.db_at(node)
        if isinstance(db, int):
            base = long_const(db << 16 | o)
        else:
            base = Mach(f"m_abs(s, 0x{o:04X})", 24)
        return base if index is None else Bin("+", base, index, 24)

    def bank_of_data(self, node) -> Expr:
        db = self.db_at(node)
        return Const(db << 16, 24) if isinstance(db, int) else Mach("m_abs(s, 0)", 24)

    def ptr(self, node, lo16: Expr, index: Expr | None = None) -> Expr:
        """DB:ptr (+ index), 24-bit."""
        db = self.db_at(node)
        base = Bin("|", Const(db << 16, 24), lo16, 24) if isinstance(db, int) else \
            Bin("+", Mach("m_abs(s, 0)", 24), lo16, 24)
        return base if index is None else Bin("+", base, index, 24)

    def address(self, node, insn, xw, yw) -> Expr:
        m, o = insn.mode, insn.operand
        X, Y = Var("x", xw), Var("y", yw)
        if m is Mode.DP:
            return self.dp(node, o)
        if m is Mode.DPX:
            return self.dp(node, o, X)
        if m is Mode.DPY:
            return self.dp(node, o, Y)
        if m is Mode.DPIND:
            return self.ptr(node, self.load(16, self.dp(node, o)))
        if m is Mode.DPXIND:
            return self.ptr(node, self.load(16, self.dp(node, o, X)))
        if m is Mode.DPINDY:
            return self.ptr(node, self.load(16, self.dp(node, o)), Y)
        if m is Mode.DPLIND:
            return self.load(24, self.dp(node, o))
        if m is Mode.DPLINDY:
            return Bin("+", self.load(24, self.dp(node, o)), Y, 24)
        if m is Mode.ABS:
            return self.absolute(node, o)
        if m is Mode.ABSX:
            return self.absolute(node, o, X)
        if m is Mode.ABSY:
            return self.absolute(node, o, Y)
        if m is Mode.LONG:
            return long_const(o)
        if m is Mode.LONGX:
            return Bin("+", long_const(o), X, 24)
        if m is Mode.SR:
            return Mach(f"(uint16_t)(s->cpu.s + 0x{o:02X})", 16)
        if m is Mode.SRINDY:
            return self.ptr(node, Load(16, Mach(f"(uint16_t)(s->cpu.s + 0x{o:02X})", 16), False), Y)
        raise AssertionError(f"no address for {m}")

    @staticmethod
    def load(w: int, addr: Expr) -> Load:
        return Load(w, addr, pure_at(addr))


def lift_routine(lifter: Lifter) -> dict:
    """node -> Lifted for every node of the graph."""
    return {node: lift_node(lifter, node) for node in lifter.g.insns}


def lift_node(L: Lifter, node) -> Lifted:
    insn = L.g.insns[node]
    pc, m8, x8 = node
    r = L.r
    n, mode, o = insn.name, insn.mode, insn.operand
    bank = pc & 0xFF0000
    out: list = []
    succ = L.g.succ.get(node, [])

    def to(pc_target: int, prefer=None):
        """The node a transfer to pc_target continues at, or None (leaves the routine)."""
        cands = [s for s in succ if s[0] == pc_target]
        if prefer is not None:
            best = [s for s in cands if s[1:] == prefer]
            if best:
                return best[0]
        return cands[0] if len(cands) == 1 else None

    def stop(pc_target: int, note: str = "", widths=True) -> Exit:
        return Exit("stop", Const(pc_target & 0xFFFF, 16), pc_target >> 16 if pc_target >> 16 != bank >> 16 else None,
                    m8 if widths else None, x8 if widths else None, note=note)

    def goto(pc_target: int) -> list:
        t = to(pc_target)
        return [Goto(t)] if t is not None else [stop(pc_target)]

    def hand_back(why: str) -> Lifted:
        return Lifted(node, insn, [Exit("stop", Const(pc & 0xFFFF, 16), None, m8, x8, note=why)], None)

    # widths this instruction needs
    needs_m = n in ("LDA", "STA", "STZ", "ORA", "AND", "EOR", "ADC", "SBC", "CMP", "BIT", "PHA", "PLA", "TXA",
                    "TYA", "ASL", "LSR", "ROL", "ROR", "INC", "DEC", "TSB", "TRB")
    needs_x = n in ("LDX", "LDY", "STX", "STY", "CPX", "CPY", "INX", "INY", "DEX", "DEY", "PHX", "PHY", "PLX",
                    "PLY", "TAX", "TAY", "TXY", "TYX", "TSX") or mode in (Mode.DPX, Mode.DPY, Mode.DPXIND,
                                                                          Mode.DPINDY, Mode.DPLINDY, Mode.ABSX,
                                                                          Mode.ABSY, Mode.LONGX, Mode.SRINDY)
    if n in ("MVN", "MVP"):
        needs_x = True
    if pc in L.stops and node != L.g.entry:
        return hand_back(f"{L.stops[pc]} is its own entry")
    if (needs_m and m8 is None) or (needs_x and x8 is None):
        return hand_back(f"{insn.text()} with widths the analysis doesn't know")
    if n in HAND_BACK:
        return hand_back(f"{insn.text()} isn't drafted")

    wm = 8 if m8 else 16
    wx = 8 if x8 else 16
    top_m = 0x80 if m8 else 0x8000

    def A() -> Expr:
        return Part(False) if m8 else Var("a", 16)

    def X() -> Expr:
        return Var("x", wx)

    def Y() -> Expr:
        return Var("y", wx)

    def set_a(e: Expr):
        if m8:
            out.append(Assign("a", simplify(Cast(e, 8)), lo=True))
        else:
            out.append(Assign("a", simplify(e)))

    def set_reg(name: str, e: Expr):
        out.append(Assign(name, simplify(Cast(e, wx) if x8 else e)))

    def nz(v: Expr, w: int):
        top = 0x80 if w == 8 else 0x8000
        out.append(Assign("z", Bin("==", v, Const(0, w), 1)))
        out.append(Assign("n", Bin("!=", Bin("&", v, Const(top, w), w), Const(0, w), 1)))

    def value(w: int, e: Expr) -> Expr:
        """e, or a temporary holding it when it would be read more than once."""
        if isinstance(e, (Const, Var, Part)):
            return e
        t = r.temp(w)
        out.append(Assign(t, e))
        return Var(t, w)

    def operand(w: int) -> Expr:
        if mode in (Mode.IMM_M, Mode.IMM_X):
            return Const(o, w)
        return L.load(w, L.address(node, insn, wx, wx))

    def fallthrough():
        return to(insn.next_pc)

    # ---- the instruction set ----------------------------------------------------------

    if n == "LDA":
        set_a(operand(wm))
        nz(A(), wm)
    elif n in GROUP_OPS:
        set_a(Bin(GROUP_OPS[n], A(), operand(wm), wm))
        nz(A(), wm)
    elif n in ("ADC", "SBC"):
        r.decimal_sensitive = True
        v = value(wm, operand(wm))
        mask = 0xFF if m8 else 0xFFFF
        a = A()
        if n == "ADC":
            # the sum in a wider temporary: carry out is its bit 8 (16)
            tw = 16 if m8 else 32
            t = r.temp(tw)
            out.append(Assign(t, Bin("+", Bin("+", a, v, tw), Var("c", 1), tw)))
            out.append(Assign("v", Bin("!=", Bin("&", Bin("&", Cast(Un("~", Bin("^", a, v, wm), wm), wm),
                                                               Bin("^", a, Var(t, tw), tw), tw),
                                                      Const(top_m, wm), wm), Const(0, wm), 1)))
            out.append(Assign("c", Bin(">", Var(t, tw), Const(mask, tw), 1)))
        else:
            # A - M - borrow; carry out is "no borrow": A >= M + borrow
            tw = wm
            t = r.temp(tw)
            borrow = Un("!", Var("c", 1), 1)
            out.append(Assign(t, Bin("-", Bin("-", a, v, wm), borrow, wm)))
            out.append(Assign("v", Bin("!=", Bin("&", Bin("&", Bin("^", a, v, wm), Bin("^", a, Var(t, tw), wm), wm),
                                                      Const(top_m, wm), wm), Const(0, wm), 1)))
            out.append(Assign("c", Bin(">=", a, Bin("+", v, borrow, 32), 1)))
        set_a(Cast(Var(t, tw), wm))
        nz(A(), wm)
    elif n in ("CMP", "CPX", "CPY"):
        w = wm if n == "CMP" else wx
        reg = A() if n == "CMP" else (X() if n == "CPX" else Y())
        v = value(w, operand(w))
        top = 0x80 if w == 8 else 0x8000
        out.append(Assign("c", Bin(">=", reg, v, 1)))
        out.append(Assign("z", Bin("==", reg, v, 1)))
        out.append(Assign("n", Bin("!=", Bin("&", Bin("-", reg, v, w), Const(top, w), w), Const(0, w), 1)))
    elif n in ("STA", "STZ", "STX", "STY"):
        w = wm if n in ("STA", "STZ") else wx
        val = {"STA": A(), "STZ": Const(0, w), "STX": X(), "STY": Y()}[n]
        out.append(Store(w, L.address(node, insn, wx, wx), val))
    elif n in ("LDX", "LDY"):
        reg = n[2].lower()
        set_reg(reg, operand(wx))
        nz(Var(reg, wx), wx)
    elif n == "BIT":
        v = value(wm, operand(wm))
        out.append(Assign("z", Bin("==", Bin("&", A(), v, wm), Const(0, wm), 1)))
        if mode is not Mode.IMM_M:
            out.append(Assign("n", Bin("!=", Bin("&", v, Const(top_m, wm), wm), Const(0, wm), 1)))
            out.append(Assign("v", Bin("!=", Bin("&", v, Const(top_m >> 1, wm), wm), Const(0, wm), 1)))
    elif n in ("ASL", "LSR", "ROL", "ROR", "INC", "DEC") and mode is Mode.ACC:
        old = A()
        if n in ("INC", "DEC"):
            set_a(Bin("+" if n == "INC" else "-", old, Const(1, wm), wm))
        else:
            res = shift(n, old, wm, top_m)
            t = r.temp(wm)
            out.append(Assign(t, res))
            out.append(Assign("c", carry_out(n, old, wm, top_m)))
            set_a(Var(t, wm))
        nz(A(), wm)
    elif n in ("ASL", "LSR", "ROL", "ROR", "INC", "DEC", "TSB", "TRB"):
        addr = L.address(node, insn, wx, wx)
        if any(isinstance(e, (Load, Mach)) for e in addr.walk()):
            ta = r.temp(24)
            out.append(Assign(ta, addr))
            addr = Var(ta, 24)
        old = value(wm, L.load(wm, addr))
        if n in ("TSB", "TRB"):
            out.append(Assign("z", Bin("==", Bin("&", old, A(), wm), Const(0, wm), 1)))
            new = Bin("|", old, A(), wm) if n == "TSB" else Bin("&", old, Cast(Un("~", A(), wm), wm), wm)
            out.append(Store(wm, addr, simplify(new)))
        else:
            t = r.temp(wm)
            if n in ("INC", "DEC"):
                out.append(Assign(t, Cast(Bin("+" if n == "INC" else "-", old, Const(1, wm), wm), wm)))
            else:
                out.append(Assign(t, shift(n, old, wm, top_m)))
                out.append(Assign("c", carry_out(n, old, wm, top_m)))
            out.append(Store(wm, addr, Var(t, wm)))
            nz(Var(t, wm), wm)
    elif n in ("INX", "INY", "DEX", "DEY"):
        reg = n[2].lower()
        set_reg(reg, Bin("+" if n[0] == "I" else "-", Var(reg, wx), Const(1, wx), wx))
        nz(Var(reg, wx), wx)
    elif n in ("TAX", "TAY"):
        reg = n[2].lower()
        set_reg(reg, Var("a", 16) if not x8 else Part(False))
        nz(Var(reg, wx), wx)
    elif n in ("TXY", "TYX"):
        dst, src = n[2].lower(), n[1].lower()
        set_reg(dst, Var(src, wx))
        nz(Var(dst, wx), wx)
    elif n in ("TXA", "TYA"):
        set_a(Var(n[1].lower(), wx))
        nz(A(), wm)
    elif n == "TSX":
        set_reg("x", Mach("s->cpu.s", 16))
        nz(X(), wx)
    elif n in ("TCS", "TXS"):
        src = Var("a", 16) if n == "TCS" else X()
        out.append(MachSet("s->cpu.s = {};", src, "the stack moves"))
    elif n == "TCD":
        out.append(MachSet("s->cpu.d = {};", Var("a", 16), "direct page"))
        nz(Var("a", 16), 16)
    elif n == "TDC":
        d = L.d_at(node)
        out.append(Assign("a", Const(d, 16) if isinstance(d, int) else Mach("s->cpu.d", 16)))
        nz(Var("a", 16), 16)
    elif n == "TSC":
        out.append(Assign("a", Mach("s->cpu.s", 16)))
        nz(Var("a", 16), 16)
    elif n == "XBA":
        out.append(Assign("a", Bin("|", Bin(">>", Var("a", 16), Const(8, 8), 16),
                                   Bin("<<", Var("a", 16), Const(8, 8), 16), 16)))
        nz(Part(False), 8)
    elif n in ("CLC", "SEC"):
        out.append(Assign("c", Const(int(n == "SEC"), 1)))
    elif n == "CLV":
        out.append(Assign("v", Const(0, 1)))
    elif n in ("CLI", "SEI", "CLD", "SED"):
        flag = "CPU_FLAG_I" if n[2] == "I" else "CPU_FLAG_D"
        text = f"s->cpu.p |= {flag};" if n[0] == "S" else f"s->cpu.p &= (uint8_t)~{flag};"
        out.append(MachSet(text, None, "interrupts" if n[2] == "I" else "decimal mode"))
    elif n in ("REP", "SEP"):
        on = n == "SEP"
        for bit, flag in ((0x01, "c"), (0x02, "z"), (0x40, "v"), (0x80, "n")):
            if o & bit:
                out.append(Assign(flag, Const(int(on), 1)))
        for bit, flag in ((0x04, "CPU_FLAG_I"), (0x08, "CPU_FLAG_D")):
            if o & bit:
                out.append(MachSet(f"s->cpu.p |= {flag};" if on else f"s->cpu.p &= (uint8_t)~{flag};", None,
                                   "interrupts" if bit == 0x04 else "decimal mode"))
        if on and o & 0x10 and x8 is False:
            out.append(Assign("x", Cast(Var("x", 16), 8)))
            out.append(Assign("y", Cast(Var("y", 16), 8)))
    elif n in ("PHA", "PHX", "PHY"):
        w = wm if n == "PHA" else wx
        out.append(Push(w, A() if n == "PHA" else Var(n[2].lower(), wx)))
    elif n == "PHB":
        db = L.db_at(node)
        out.append(Push(8, Const(db, 8) if isinstance(db, int) else Mach("s->cpu.db", 8)))
    elif n == "PHD":
        d = L.d_at(node)
        out.append(Push(16, Const(d, 16) if isinstance(d, int) else Mach("s->cpu.d", 16)))
    elif n == "PHK":
        out.append(Push(8, Const(pc >> 16, 8)))
    elif n == "PHP":
        mx = (0x20 if m8 else 0) | (0x10 if x8 else 0)
        out.append(Push(8, Fn("draft_p", (Const(mx, 8), Var("n", 1), Var("v", 1), Var("z", 1), Var("c", 1)), 8,
                              pure=False)))
    elif n == "PEA":
        out.append(Push(16, Const(o, 16)))
    elif n == "PEI":
        out.append(Push(16, L.load(16, L.dp(node, o))))
    elif n == "PER":
        out.append(Push(16, Const(insn.branch_target() & 0xFFFF, 16)))
    elif n == "PLA":
        out.append(Assign("a", pull(wm), lo=bool(m8)))
        nz(A(), wm)
    elif n in ("PLX", "PLY"):
        reg = n[2].lower()
        out.append(Assign(reg, pull(wx)))
        nz(Var(reg, wx), wx)
    elif n in ("PLB", "PLD"):
        w = 8 if n == "PLB" else 16
        t = r.temp(w)
        out.append(Assign(t, pull(w)))
        out.append(MachSet("s->cpu.db = {};" if n == "PLB" else "s->cpu.d = {};", Var(t, w),
                           "data bank" if n == "PLB" else "direct page"))
        nz(Var(t, w), w)
    elif n == "PLP":
        t = r.temp(8)
        out.append(Assign(t, pull(8)))
        out.append(MachSet("s->cpu.p = {};", Var(t, 8), "widths and interrupt state"))
        for bit, flag in ((0x01, "c"), (0x02, "z"), (0x40, "v"), (0x80, "n")):
            out.append(Assign(flag, Bin("!=", Bin("&", Var(t, 8), Const(bit, 8), 8), Const(0, 8), 1)))
        after = [s for s in succ]
        if x8 is False and after and after[0][2] is True:
            out.append(Assign("x", Cast(Var("x", 16), 8)))
            out.append(Assign("y", Cast(Var("y", 16), 8)))
    elif n in ("MVN", "MVP"):
        dst, src = o & 0xFF, o >> 8
        step = "+" if n == "MVN" else "-"
        out.append(MachSet("s->cpu.db = {};", Const(dst, 8), "data bank"))
        out.append(Store(8, Bin("+", Const(dst << 16, 24, "long"), Var("y", wx), 24),
                         Load(8, Bin("+", Const(src << 16, 24, "long"), Var("x", wx), 24),
                              region(src << 16) != "io")))
        set_reg("x", Bin(step, Var("x", wx), Const(1, wx), wx))
        set_reg("y", Bin(step, Var("y", wx), Const(1, wx), wx))
        out.append(Assign("a", Bin("-", Var("a", 16), Const(1, 16), 16)))
        out.append(Branch(Bin("!=", Var("a", 16), Const(0xFFFF, 16), 1), node))
        return Lifted(node, insn, out, fallthrough())
    elif n == "NOP":
        pass
    # ---- control --------------------------------------------------------------------
    elif n in BRANCH_FLAGS:
        flag, on = BRANCH_FLAGS[n]
        cond = Var(flag, 1) if on else Un("!", Var(flag, 1), 1)
        t = to(insn.branch_target())
        if t is None:
            # leaves the routine when taken: a branch to an exit
            out.append(Branch(cond, ("exit", insn.branch_target(), m8, x8)))
        else:
            out.append(Branch(cond, t))
        return Lifted(node, insn, out, fallthrough())
    elif n in ("BRA", "BRL") or (n == "JMP" and mode is Mode.ABS):
        target = insn.branch_target() if n != "JMP" else bank | o
        out += goto(target)
        return Lifted(node, insn, out, None)
    elif n in ("JML", "JMP") and mode is Mode.LONG:
        t = to(o)
        out += [Goto(t)] if t is not None else [Exit("stop", Const(o & 0xFFFF, 16), o >> 16, m8, x8,
                                                     note="continues in another routine")]
        return Lifted(node, insn, out, None)
    elif n == "JMP" and mode is Mode.ABSIND:
        out.append(Exit("jump", Bin("|", Const(bank, 24), L.load(16, long_const(o)), 24), m8=m8, x8=x8,
                        note=f"jumps through the pointer at ${o:04X}"))
        return Lifted(node, insn, out, None)
    elif n == "JML" and mode is Mode.ABSLIND:
        out.append(Exit("jump", L.load(24, long_const(o)), m8=m8, x8=x8,
                        note=f"jumps through the pointer at ${o:04X}"))
        return Lifted(node, insn, out, None)
    elif n == "JMP" and mode is Mode.ABSXIND:
        targets = L.tables.get(pc)
        if not targets:
            return hand_back("a jump table the analysis didn't size")
        cases = []
        for i, t in enumerate(targets):
            tn = to(t)
            cases.append((i * 2, tn if tn is not None else ("exit", t, m8, x8)))
        out.append(Switch(X(), cases, ("exit", pc, m8, x8)))
        return Lifted(node, insn, out, None)
    elif n in ("JSR", "JSL") and mode in (Mode.ABS, Mode.LONG):
        target = o if n == "JSL" else bank | o
        live_in = r.callee_live_in(target)
        out.append(CallOut(pc, target, n == "JSL", bool(m8), bool(x8), live_in, frozenset(),
                           may_def=r.callee_may_def(target)))
        if target in r.noreturn:
            out.append(Exit("noreturn", live=frozenset()))
            return Lifted(node, insn, out, None)
        _, resume = L.g.calls.get(node, ([], None))
        if resume is None:
            skip = 0
            out.append(stop(bank | ((insn.next_pc + skip) & 0xFFFF), "widths after the call aren't known", False))
            return Lifted(node, insn, out, None)
        return Lifted(node, insn, out, resume)
    elif n == "JSR" and mode is Mode.ABSXIND:
        targets = L.tables.get(pc) or []
        live = frozenset().union(*[r.callee_live_in(bank | (t & 0xFFFF)) for t in targets]) if targets else ALL_KEYS
        md = frozenset().union(*[r.callee_may_def(bank | (t & 0xFFFF)) for t in targets]) if targets else ALL_KEYS
        tex = Fn("m_jump_table", (Const(bank >> 16, 8), Const(o, 16), X()), 16)
        out.append(CallOut(pc, None, False, bool(m8), bool(x8), live, frozenset(), target_expr=tex, may_def=md,
                           note=f"through the table at ${bank >> 16:02X}:{o:04X}"))
        _, resume = L.g.calls.get(node, ([], None))
        if resume is None:
            out.append(stop(insn.next_pc, "widths after the call aren't known", False))
            return Lifted(node, insn, out, None)
        return Lifted(node, insn, out, resume)
    elif n in ("RTS", "RTL", "RTI"):
        live = r.ret_live if n != "RTI" else frozenset({"al", "ah", "x", "y"})
        out.append(Exit(n.lower(), m8=m8, x8=x8, live=live))
        return Lifted(node, insn, out, None)
    else:
        return hand_back(f"{insn.text()} isn't drafted")
    return Lifted(node, insn, out, fallthrough())


def pull(w: int) -> Expr:
    """PLA/PLX/PLY/PLB/PLD/PLP's read: the stack moves, so it is an effect."""
    return Fn(f"m_pull{w}", (), w, pure=False)


def shift(n: str, v: Expr, w: int, top: int) -> Expr:
    if n == "ASL":
        return Cast(Bin("<<", v, Const(1, 8), w), w)
    if n == "LSR":
        return Bin(">>", v, Const(1, 8), w)
    if n == "ROL":
        return Cast(Bin("|", Bin("<<", v, Const(1, 8), w), Var("c", 1), w), w)
    return Bin("|", Bin(">>", v, Const(1, 8), w),
               Bin("<<", Var("c", 1), Const(w - 1, 8), w), w)


def carry_out(n: str, v: Expr, w: int, top: int) -> Expr:
    if n in ("ASL", "ROL"):
        return Bin("!=", Bin("&", v, Const(top, w), w), Const(0, w), 1)
    return Bin("!=", Bin("&", v, Const(1, w), w), Const(0, w), 1)
