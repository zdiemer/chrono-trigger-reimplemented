"""Interprocedural register and flag liveness.

For every function, computes which of A, X, Y, the N/V/Z/C flags, DB and D
may be read by some caller after the function returns (`ret_live`), and which
it reads before writing (`live_in`). Ported routines use the complement of
`ret_live` to know which registers and flags they don't have to reproduce.

The analysis is conservative: unknown control flow, unresolved indirect
jumps, interrupt handlers, and functions reachable from call sites static
analysis didn't see (for example code running from RAM) keep everything live.
"""

from collections import defaultdict

from .analyze import Analyzer
from .cpu import CONDITIONAL_BRANCHES, SUSPICIOUS, Insn, Mode

A, X, Y, N, V, Z, C, DB, D = (1 << i for i in range(9))
ALL = (1 << 9) - 1
NAMES = [("A", A), ("X", X), ("Y", Y), ("N", N), ("V", V), ("Z", Z), ("C", C), ("DB", DB), ("D", D)]
FLAGS = N | V | Z | C


def describe(mask: int) -> str:
    return " ".join(name for name, bit in NAMES if mask & bit) or "-"


def _address_uses(mode: Mode) -> int:
    """Registers an addressing mode reads to form its address."""
    uses = 0
    if mode in (Mode.DP, Mode.DPX, Mode.DPY, Mode.DPIND, Mode.DPXIND, Mode.DPINDY, Mode.DPLIND,
                Mode.DPLINDY, Mode.PEI):
        uses |= D
    if mode in (Mode.ABS, Mode.ABSX, Mode.ABSY, Mode.DPIND, Mode.DPXIND, Mode.DPINDY, Mode.SRINDY):
        uses |= DB
    if mode in (Mode.DPX, Mode.DPXIND, Mode.ABSX, Mode.LONGX, Mode.ABSXIND):
        uses |= X
    if mode in (Mode.DPY, Mode.DPINDY, Mode.DPLINDY, Mode.ABSY, Mode.SRINDY):
        uses |= Y
    return uses


def use_def(insn: Insn, m8: bool | None) -> tuple[int, int]:
    """(uses, defs) of one instruction. 8-bit accumulator writes don't define A
    as a whole (the high byte survives), so they aren't counted as defs."""
    n, mode = insn.name, insn.mode
    addr = 0 if mode in (Mode.IMP, Mode.ACC, Mode.IMM_M, Mode.IMM_X, Mode.IMM8, Mode.REL8, Mode.REL16,
                         Mode.MOVE, Mode.PEA) else _address_uses(mode)
    a_def = A if m8 is False else 0
    acc = mode is Mode.ACC

    if n == "LDA":
        return addr, a_def | N | Z
    if n == "LDX":
        return addr, X | N | Z
    if n == "LDY":
        return addr, Y | N | Z
    if n == "STA":
        return addr | A, 0
    if n == "STX":
        return addr | X, 0
    if n == "STY":
        return addr | Y, 0
    if n == "STZ":
        return addr, 0
    if n in ("ORA", "AND", "EOR"):
        return addr | A, a_def | N | Z
    if n in ("ADC", "SBC"):
        return addr | A | C, a_def | N | V | Z | C
    if n == "CMP":
        return addr | A, N | Z | C
    if n == "CPX":
        return addr | X, N | Z | C
    if n == "CPY":
        return addr | Y, N | Z | C
    if n == "BIT":
        return addr | A, Z if mode is Mode.IMM_M else N | V | Z
    if n in ("INC", "DEC"):
        return (A, a_def | N | Z) if acc else (addr, N | Z)
    if n in ("ASL", "LSR"):
        return (A, a_def | N | Z | C) if acc else (addr, N | Z | C)
    if n in ("ROL", "ROR"):
        return (A | C, a_def | N | Z | C) if acc else (addr | C, N | Z | C)
    if n in ("TSB", "TRB"):
        return addr | A, Z
    if n in ("INX", "DEX"):
        return X, X | N | Z
    if n in ("INY", "DEY"):
        return Y, Y | N | Z
    transfers = {
        "TAX": (A, X | N | Z), "TAY": (A, Y | N | Z), "TXA": (X, a_def | N | Z), "TYA": (Y, a_def | N | Z),
        "TXY": (X, Y | N | Z), "TYX": (Y, X | N | Z), "TCD": (A, D | N | Z), "TDC": (D, A | N | Z),
        "TCS": (A, 0), "TSC": (0, A | N | Z), "TXS": (X, 0), "TSX": (0, X | N | Z), "XBA": (A, N | Z),
    }
    if n in transfers:
        return transfers[n]
    stack = {
        "PHA": (A, 0), "PHX": (X, 0), "PHY": (Y, 0), "PHB": (DB, 0), "PHD": (D, 0), "PHK": (0, 0),
        "PHP": (FLAGS, 0), "PLA": (0, a_def | N | Z), "PLX": (0, X | N | Z), "PLY": (0, Y | N | Z),
        "PLB": (0, DB | N | Z), "PLD": (0, D | N | Z), "PLP": (0, FLAGS), "PEA": (0, 0), "PEI": (D, 0),
        "PER": (0, 0),
    }
    if n in stack:
        return stack[n]
    branch_use = {"BCC": C, "BCS": C, "BEQ": Z, "BNE": Z, "BMI": N, "BPL": N, "BVC": V, "BVS": V}
    if n in branch_use:
        return branch_use[n], 0
    if n in ("CLC", "SEC"):
        return 0, C
    if n == "CLV":
        return 0, V
    if n in ("REP", "SEP"):
        bits = insn.operand
        defs = (C if bits & 0x01 else 0) | (Z if bits & 0x02 else 0) | (V if bits & 0x40 else 0) | \
               (N if bits & 0x80 else 0)
        return 0, defs
    if n == "XCE":
        return C, C
    if n in ("MVN", "MVP"):
        return A | X | Y, A | X | Y | DB
    return addr, 0   # NOP, WDM, SEI, CLI, SED, CLD, WAI, control flow (handled by the caller)


class Liveness:
    def __init__(self, an: Analyzer, dynamic_calls: dict[int, set] | None = None):
        """dynamic_calls: target -> call sites observed at runtime (ct_dev --calls)."""
        self.an = an
        self.live_in: dict[tuple, dict[int, int]] = {}     # ctx key -> pc -> live-in mask
        self.func_live_in: dict[int, int] = defaultdict(int)
        self.ret_live: dict[int, int] = defaultdict(int)
        self.all_live_returns: set[int] = set()
        self.reasons: dict[int, str] = {}
        self._seed(dynamic_calls or {})

    # ---- seeding -----------------------------------------------------------------

    def _seed(self, dynamic_calls: dict[int, set]):
        an = self.an
        static_sites = set()
        for key, c in an.contexts.items():
            for pc in c.insns:
                for insn in an.code[pc].values():
                    if insn.name in ("JSR", "JSL"):
                        static_sites.add(pc)
        for key, c in an.contexts.items():
            unsafe = c.kinds & {"vector", "coverage", "indirect", "hint"}
            if unsafe:
                self._all_live(c.entry, f"entered as {', '.join(sorted(unsafe))}")
        for target, sites in dynamic_calls.items():
            unknown = sorted(s for s in sites if s not in static_sites)
            if unknown:
                self._all_live(target, f"called from sites static analysis doesn't know ({len(unknown)})")

    def _all_live(self, entry: int, why: str):
        self.all_live_returns.add(entry)
        self.ret_live[entry] = ALL
        self.reasons.setdefault(entry, why)

    # ---- per-context dataflow ------------------------------------------------------

    def _entries_at(self, addr: int) -> list:
        return [k for k in self.an.contexts if k[0] == addr]

    def _callee_live_in(self, target: int) -> int:
        keys = self._entries_at(target)
        if not keys:
            return ALL
        return self.func_live_in[target]

    def _instruction_live_in(self, key, pc, insn: Insn, widths, live_at) -> int:
        an = self.an
        entry = key[0]
        name, mode = insn.name, insn.mode
        m8 = widths[0]
        uses, defs = use_def(insn, m8)
        bank = pc & 0xFF0000

        if name in SUSPICIOUS or name == "STP":
            return ALL
        if name in ("RTS", "RTL"):
            return self.ret_live[entry]
        if name == "RTI":
            return ALL
        if name in CONDITIONAL_BRANCHES:
            out = live_at(insn.next_pc) | live_at(insn.branch_target())
            return uses | out
        if name in ("BRA", "BRL"):
            return live_at(insn.branch_target())
        if name == "JMP" and mode is Mode.ABS:
            return live_at(bank | insn.operand)
        if name == "JML" and mode is Mode.LONG:
            self._merge_ret(insn.operand, self.ret_live[entry])
            return self._callee_live_in(insn.operand)
        if name in ("JSR", "JSL") and mode in (Mode.ABS, Mode.LONG):
            target = insn.operand if name == "JSL" else bank | insn.operand
            callee = self._callee_live_in(target)
            if target in an.hints.noreturn:
                return callee
            resume = insn.next_pc + an.hints.inline_args.get(target, 0)
            after = live_at(resume)
            self._merge_ret(target, after)
            return callee | after
        if mode is Mode.ABSXIND:
            jt = an.tables.get(pc)
            if jt is None or jt.count is None:
                return ALL
            targets = self._table_targets(jt)
            if jt.call:
                after = live_at(insn.next_pc)
                live = uses | after
                for t in targets:
                    self._merge_ret(t, after)
                    live |= self._callee_live_in(t)
                return live
            live = uses
            for t in targets:
                live |= live_at(t)
            return live
        if mode in (Mode.ABSIND, Mode.ABSLIND):
            targets = an.hints.indirect.get(pc)
            if not targets:
                return ALL
            live = uses
            for t in targets:
                self._merge_ret(t, self.ret_live[entry])
                live |= self._callee_live_in(t)
            return live
        return uses | (live_at(insn.next_pc) & ~defs)

    def _table_targets(self, jt) -> list:
        bank = jt.dispatcher & 0xFF0000
        out = []
        for i in range(jt.count):
            if jt.kind == "long":
                t = self.an.u24(jt.table + i * 3)
            else:
                v = self.an.u16(jt.table + i * 2)
                t = None if v is None else bank | v
            if t is not None:
                out.append(t)
        return out

    def _merge_ret(self, target: int, live: int):
        if target in self.all_live_returns:
            return
        new = self.ret_live[target] | live
        if new != self.ret_live[target]:
            self.ret_live[target] = new
            self._changed = True

    def _solve_context(self, key):
        an = self.an
        c = an.contexts[key]
        live = self.live_in.setdefault(key, {})
        pcs = sorted(c.insns)

        def live_at(addr):
            if addr not in c.insns:
                return ALL   # flows somewhere this context didn't trace
            return live.get(addr, 0)

        changed = True
        while changed:
            changed = False
            for pc in reversed(pcs):
                value = 0
                for widths, insn in an.code[pc].items():
                    value |= self._instruction_live_in(key, pc, insn, widths, live_at)
                if value != live.get(pc):
                    live[pc] = value
                    changed = True
        entry_live = live.get(c.entry, ALL)
        if (self.func_live_in[c.entry] | entry_live) != self.func_live_in[c.entry]:
            self.func_live_in[c.entry] |= entry_live
            self._changed = True

    def solve(self, max_rounds: int = 50):
        for _ in range(max_rounds):
            self._changed = False
            for key in self.an.contexts:
                self._solve_context(key)
            if not self._changed:
                return
        raise RuntimeError("liveness did not converge")

    def dead_at_return(self, entry: int) -> int:
        return ALL & ~self.ret_live[entry]
