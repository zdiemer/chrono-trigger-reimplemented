"""Interprocedural register and flag liveness.

For every function, computes which of A, X, Y, the N/V/Z/C flags, DB and D
may be read by some caller after the function returns (`ret_live`), and which
it reads before writing (`live_in`). Ported routines use the complement of
`ret_live` to know which registers and flags they don't have to reproduce.

The analysis is conservative: unknown control flow, unresolved indirect
jumps, interrupt handlers, and functions reachable from call sites static
analysis didn't see (for example code running from RAM) keep everything live.

**PHP and PLP.** Saving P reads nothing by itself: the flags it saves are read
only if the PLP that restores them is followed by a read. So a flag is live at
a `PHP` exactly when it is live after the `PLP`s that pull its byte, which a
walk forward from each `PHP` finds by counting the bytes pushed and pulled
above it (`php_pairs`). Where that can't be established the `PHP` reads every
flag, as before: the stack pointer is set or can't be followed (`TCS`, `TXS`,
`XCE`, a push whose width isn't known, paths that meet with different depths),
something other than a `PLP` reads the byte (a pull, a return, a
stack-relative access, `TSC`/`TSX` while it is on the stack), a callee may
reach it (`reach`: how far above its own frame a routine, or what it calls,
pulls or reads the stack), or control leaves with it on the stack (a return,
a tail jump, an indirect jump, a flow out of the traced code, a call that
doesn't return). `pairing_stats` counts each case.
"""

from collections import defaultdict

from .analyze import Analyzer
from .cpu import CONDITIONAL_BRANCHES, RETURNS, SUSPICIOUS, Insn, Mode

A, X, Y, N, V, Z, C, DB, D = (1 << i for i in range(9))
ALL = (1 << 9) - 1
NAMES = [("A", A), ("X", X), ("Y", Y), ("N", N), ("V", V), ("Z", Z), ("C", C), ("DB", DB), ("D", D)]
FLAGS = N | V | Z | C

# Whether a PHP's byte could be followed to the PLPs that restore it, and why not.
PAIRED = "paired"
S_UNKNOWN = "S changes"
READ_OTHERWISE = "read otherwise"
CALLEE_REACHES = "a callee reaches it"
LEAVES = "control leaves"
PAIRING_CASES = (PAIRED, S_UNKNOWN, READ_OTHERWISE, CALLEE_REACHES, LEAVES)

UNBOUNDED = 1 << 20   # a routine's reach when S can't be followed
MAX_REACH = 255


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


_FIXED_STACK = {"PHB": 1, "PHK": 1, "PHP": 1, "PHD": 2, "PEA": 2, "PEI": 2, "PER": 2,
                "PLB": -1, "PLP": -1, "PLD": -2}


def stack_effect(insn: Insn, widths) -> int | None:
    """Bytes an instruction pushes (positive) or pulls (negative), calls and
    returns aside; None when it depends on a width that isn't known."""
    n = insn.name
    if n in _FIXED_STACK:
        return _FIXED_STACK[n]
    if n in ("PHA", "PLA"):
        w = widths[0]
    elif n in ("PHX", "PHY", "PLX", "PLY"):
        w = widths[1]
    else:
        return 0
    if w is None:
        return None
    size = 1 if w else 2
    return size if n.startswith("PH") else -size


def _sr_bytes(insn: Insn, widths) -> int | None:
    """Bytes a stack-relative instruction touches from S + operand."""
    if insn.mode is Mode.SRINDY:
        return 2                              # the pointer
    return None if widths[0] is None else (1 if widths[0] else 2)


class Liveness:
    def __init__(self, an: Analyzer, dynamic_calls: dict[int, set] | None = None):
        """dynamic_calls: target -> call sites observed at runtime (ct_dev --calls)."""
        self.an = an
        self.live_in: dict[tuple, dict[int, int]] = {}     # ctx key -> pc -> live-in mask
        self.func_live_in: dict[int, int] = defaultdict(int)
        self.ret_live: dict[int, int] = defaultdict(int)
        self.all_live_returns: set[int] = set()
        self.reasons: dict[int, str] = {}
        self.reach: dict[int, int] = {}                  # routine -> bytes above its entry S it may touch
        self.php_pairs: dict[tuple, frozenset] = {}      # (ctx key, PHP pc) -> resume pcs of its PLPs
        self.php_cases: dict[tuple, str] = {}            # (ctx key, PHP pc) -> a PAIRING_CASES entry
        self._seed(dynamic_calls or {})
        self._pair_php()

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

    # ---- PHP/PLP pairing -----------------------------------------------------------

    def _call_targets(self, pc: int, insn: Insn) -> list:
        target = insn.operand if insn.name == "JSL" else (pc & 0xFF0000) | insn.operand
        return list(self.an.hints.indirect.get(pc, [target]))

    def _successors(self, pc: int, insn: Insn):
        """(next pcs, why control leaves or None), for the stack walks. A call
        continues after its arguments; its callees are checked apart."""
        an = self.an
        name, mode = insn.name, insn.mode
        if name in SUSPICIOUS or name == "STP" or name in RETURNS:
            return [], LEAVES
        if name in CONDITIONAL_BRANCHES:
            return [insn.next_pc, insn.branch_target()], None
        if name in ("BRA", "BRL"):
            return [insn.branch_target()], None
        if name == "JMP" and mode is Mode.ABS:
            return [(pc & 0xFF0000) | insn.operand], None
        if name == "JML" or mode in (Mode.ABSIND, Mode.ABSLIND):
            return [], LEAVES
        if name in ("JSR", "JSL") and mode in (Mode.ABS, Mode.LONG):
            targets = self._call_targets(pc, insn)
            if any(t in an.hints.noreturn for t in targets):
                return [], LEAVES
            skip = max((an.hints.inline_args.get(t, 0) for t in targets), default=0)
            return [insn.next_pc + skip], None
        if mode is Mode.ABSXIND:
            jt = an.tables.get(pc)
            if jt is None or jt.count is None:
                return [], LEAVES
            return ([insn.next_pc] if jt.call else self._table_targets(jt)), None
        return [insn.next_pc], None

    def _callees(self, pc: int, insn: Insn) -> list:
        """What an instruction calls and returns from."""
        if insn.name in ("JSR", "JSL") and insn.mode in (Mode.ABS, Mode.LONG):
            return self._call_targets(pc, insn)
        if insn.mode is Mode.ABSXIND:
            jt = self.an.tables.get(pc)
            if jt is not None and jt.call and jt.count is not None:
                return self._table_targets(jt)
        return []

    @staticmethod
    def _sets_s(insn: Insn, prev: str | None) -> str | None:
        """What an instruction does to S beyond pushes and pulls: None,
        "reset" (TXS/TCS of a constant the previous instruction loaded: a new
        stack, so nothing above the old S is reached through it again), or
        "unknown" (TCS/TXS of anything else, XCE that may enter emulation)."""
        n = insn.name
        if n == "TXS":
            return "reset" if prev == "X" else "unknown"
        if n == "TCS":
            return "reset" if prev == "A" else "unknown"
        if n == "XCE":
            return None if prev == "CLC" else "unknown"   # CLC : XCE stays in native mode
        return None

    @staticmethod
    def _loads(insn: Insn, widths) -> str | None:
        """What the next instruction may rely on: "X" or "A" if this loads the
        whole register with a constant, "CLC" if it clears the carry."""
        n, mode = insn.name, insn.mode
        if n == "LDX" and mode is Mode.IMM_X:
            return "X"
        if n == "LDA" and mode is Mode.IMM_M and widths[0] is False:
            return "A"
        if n == "CLC":
            return "CLC"
        return None

    def _context_reach(self, key, reach: dict) -> int:
        """How far above its entry S a context may touch the stack, in bytes,
        given `reach` for what it calls: 2 for a routine that only pulls its
        JSR's return address, UNBOUNDED where S can't be followed."""
        an = self.an
        c = an.contexts[key]
        depth = {}
        seen = set()
        work = [(c.entry, 0, None)]
        r = 0
        while work:
            pc, d, prev = work.pop()            # d: bytes pushed since the entry
            if pc not in c.insns:
                return UNBOUNDED
            if depth.setdefault(pc, d) != d:
                return UNBOUNDED
            if (pc, prev) in seen:
                continue
            seen.add((pc, prev))
            for widths, insn in an.code[pc].items():
                name, mode = insn.name, insn.mode
                sets = self._sets_s(insn, prev)
                if sets == "reset":
                    continue
                if sets == "unknown" or name in ("TSC", "TSX") or name in SUSPICIOUS:
                    return UNBOUNDED
                if name in RETURNS:
                    r = max(r, {"RTS": 2, "RTL": 3, "RTI": 4}[name] - d)
                    continue
                if mode in (Mode.SR, Mode.SRINDY):
                    w = _sr_bytes(insn, widths)
                    if w is None:
                        return UNBOUNDED
                    r = max(r, insn.operand + w - 1 - d)
                eff = stack_effect(insn, widths)
                if eff is None:
                    return UNBOUNDED
                if eff < 0:
                    r = max(r, -eff - d)
                if name == "JML" or mode in (Mode.ABSIND, Mode.ABSLIND):
                    targets = [insn.operand] if mode is Mode.LONG else an.hints.indirect.get(pc, [])
                    if not targets:
                        return UNBOUNDED
                    r = max([r] + [reach.get(t, UNBOUNDED) - d for t in targets])
                    continue
                size = 3 if name == "JSL" else 2
                for t in self._callees(pc, insn):
                    r = max(r, reach.get(t, UNBOUNDED) - d - size)
                if r >= UNBOUNDED // 2:
                    return UNBOUNDED
                nexts, why = self._successors(pc, insn)
                if why is not None and name != "STP":
                    if not (name in ("JSR", "JSL") and nexts == []):   # a call that doesn't return
                        return UNBOUNDED
                loads = self._loads(insn, widths)
                work += [(n, d + eff, loads) for n in nexts]
        return r

    def _compute_reach(self):
        """`reach` for every routine: the most bytes above its entry S it, or
        anything it calls, may pull or read (UNBOUNDED where S isn't
        followed). A routine that stays in its frame reaches exactly its
        return address; one that reaches further can read what its caller
        pushed before calling it."""
        entries = defaultdict(list)
        for key in self.an.contexts:
            entries[key[0]].append(key)
        reach = {entry: 0 for entry in entries}
        changed = True
        while changed:
            changed = False
            for entry, keys in entries.items():
                if reach[entry] == UNBOUNDED:
                    continue
                r = max(self._context_reach(key, reach) for key in keys)
                if r > MAX_REACH:
                    r = UNBOUNDED
                if r > reach[entry]:
                    reach[entry] = r
                    changed = True
        self.reach = reach

    def _follow_php(self, key, php_pc: int) -> tuple:
        """Walk forward from a PHP counting the bytes above its byte, to the
        PLPs that pull it: (PAIRED, their resume pcs) or (why not, empty). A
        path that sets a new stack, or never ends, leaves the byte unread."""
        an = self.an
        c = an.contexts[key]
        above = {}
        seen = set()
        plps = set()
        work = [(insn.next_pc, 0, None) for insn in an.code[php_pc].values()]
        while work:
            pc, k, prev = work.pop()            # k: bytes pushed above the PHP's byte
            if pc not in c.insns:
                return LEAVES, frozenset()
            if above.setdefault(pc, k) != k:
                return S_UNKNOWN, frozenset()
            if (pc, prev) in seen:
                continue
            seen.add((pc, prev))
            for widths, insn in an.code[pc].items():
                name, mode = insn.name, insn.mode
                sets = self._sets_s(insn, prev)
                if sets == "reset":
                    continue
                if sets == "unknown":
                    return S_UNKNOWN, frozenset()
                if name in ("TSC", "TSX"):
                    return READ_OTHERWISE, frozenset()
                if name == "PLP" and k == 0:
                    plps.add(insn.next_pc)
                    continue
                if mode in (Mode.SR, Mode.SRINDY):
                    w = _sr_bytes(insn, widths)
                    if w is None:
                        return S_UNKNOWN, frozenset()
                    if insn.operand <= k + 1 <= insn.operand + w - 1:
                        return READ_OTHERWISE, frozenset()
                if name in RETURNS:
                    return (READ_OTHERWISE if k < (2 if name == "RTS" else 3) else LEAVES), frozenset()
                eff = stack_effect(insn, widths)
                if eff is None:
                    return S_UNKNOWN, frozenset()
                if k + eff < 0:
                    return READ_OTHERWISE, frozenset()
                size = 3 if name == "JSL" else 2
                if any(self.reach.get(t, UNBOUNDED) > k + size for t in self._callees(pc, insn)):
                    return CALLEE_REACHES, frozenset()
                nexts, why = self._successors(pc, insn)
                if why is not None and not (name in ("JSR", "JSL") and nexts == []):
                    return why, frozenset()       # (a call that doesn't return ends the path)
                loads = self._loads(insn, widths)
                work += [(n, k + eff, loads) for n in nexts]
        return PAIRED, frozenset(plps)

    def _pair_php(self):
        self._compute_reach()
        for key, c in self.an.contexts.items():
            for pc in c.insns:
                if not any(insn.name == "PHP" for insn in self.an.code[pc].values()):
                    continue
                case, plps = self._follow_php(key, pc)
                self.php_cases[(key, pc)] = case
                if case == PAIRED:
                    self.php_pairs[(key, pc)] = plps

    def pairing_stats(self) -> dict:
        """PAIRING_CASES entry -> (PHPs counted per context, distinct PHP addresses)."""
        out = {}
        for case in PAIRING_CASES:
            sites = [k for k, v in self.php_cases.items() if v == case]
            out[case] = (len(sites), len({pc for _key, pc in sites}))
        return out

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
        if name == "PHP":
            plps = self.php_pairs.get((key, pc))
            if plps is not None:
                saved = 0
                for resume in plps:
                    saved |= live_at(resume)
                return (saved & FLAGS) | live_at(insn.next_pc)
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
