"""Interprocedural register and flag liveness.

For every function, computes which of A's low byte (`A`), A's high byte
(`AH`), X, Y, the N/V/Z/C flags, DB and D may be read by some caller after the
function returns (`ret_live`), and which it reads before writing (`live_in`).
The two bytes of A are apart because code working on an 8-bit accumulator
reads and writes only the low one, and leaves the high one to whatever last
set it. Ported routines use the complement of
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

# Bit order matches runtime/port/native.h's DC_* bits, except that DC_A means
# the whole of A, so A and AH dead together is DC_A and AH alone DC_AH.
A, X, Y, N, V, Z, C, DB, D, AH = (1 << i for i in range(10))
ALL = (1 << 10) - 1
NAMES = [("A", A), ("AH", AH), ("X", X), ("Y", Y), ("N", N), ("V", V), ("Z", Z), ("C", C), ("DB", DB), ("D", D)]
FLAGS = N | V | Z | C
AW = A | AH       # the whole accumulator

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


DC_NAMES = [("DC_A", A), ("DC_X", X), ("DC_Y", Y), ("DC_N", N), ("DC_V", V), ("DC_Z", Z), ("DC_C", C),
            ("DC_DB", DB), ("DC_D", D), ("DC_AH", AH)]


def dc_mask(dead: int) -> int:
    """A set of dead registers as the DC_* bits that declare it: DC_A only
    for the whole of A, DC_AH for its high byte alone."""
    out = dead & ~AW
    if dead & AW == AW:
        out |= A
    elif dead & AH:
        out |= AH
    return out


def dead_of_dc(dc: int) -> int:
    """What a DC_* set declares dead, as liveness bits."""
    out = dc & ~AW
    if dc & A:
        out |= AW
    if dc & AH:
        out |= AH
    return out


def dc_text(dc: int) -> str:
    return " | ".join(name for name, bit in DC_NAMES if dc & bit) or "0"


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


def use_def(insn: Insn, m8: bool | None, x8: bool | None = None) -> tuple[int, int]:
    """(uses, defs) of one instruction. An 8-bit accumulator reads and writes
    A's low byte alone; with its width unknown, a read is of both bytes and a
    write defines neither."""
    n, mode = insn.name, insn.mode
    addr = 0 if mode in (Mode.IMP, Mode.ACC, Mode.IMM_M, Mode.IMM_X, Mode.IMM8, Mode.REL8, Mode.REL16,
                         Mode.MOVE, Mode.PEA) else _address_uses(mode)
    a_def = A if m8 is True else (AW if m8 is False else 0)
    a_use = A if m8 is True else AW
    acc = mode is Mode.ACC

    if n == "LDA":
        return addr, a_def | N | Z
    if n == "LDX":
        return addr, X | N | Z
    if n == "LDY":
        return addr, Y | N | Z
    if n == "STA":
        return addr | a_use, 0
    if n == "STX":
        return addr | X, 0
    if n == "STY":
        return addr | Y, 0
    if n == "STZ":
        return addr, 0
    if n in ("ORA", "AND", "EOR"):
        return addr | a_use, a_def | N | Z
    if n in ("ADC", "SBC"):
        return addr | a_use | C, a_def | N | V | Z | C
    if n == "CMP":
        return addr | a_use, N | Z | C
    if n == "CPX":
        return addr | X, N | Z | C
    if n == "CPY":
        return addr | Y, N | Z | C
    if n == "BIT":
        return addr | a_use, Z if mode is Mode.IMM_M else N | V | Z
    if n in ("INC", "DEC"):
        return (a_use, a_def | N | Z) if acc else (addr, N | Z)
    if n in ("ASL", "LSR"):
        return (a_use, a_def | N | Z | C) if acc else (addr, N | Z | C)
    if n in ("ROL", "ROR"):
        return (a_use | C, a_def | N | Z | C) if acc else (addr | C, N | Z | C)
    if n in ("TSB", "TRB"):
        return addr | a_use, Z
    if n in ("INX", "DEX"):
        return X, X | N | Z
    if n in ("INY", "DEY"):
        return Y, Y | N | Z
    index_a = A if x8 is True else AW     # TAX/TAY copy as much of A as the index registers hold
    transfers = {
        "TAX": (index_a, X | N | Z), "TAY": (index_a, Y | N | Z), "TXA": (X, a_def | N | Z),
        "TYA": (Y, a_def | N | Z), "TXY": (X, Y | N | Z), "TYX": (Y, X | N | Z), "TCD": (AW, D | N | Z),
        "TDC": (D, AW | N | Z), "TCS": (AW, 0), "TSC": (0, AW | N | Z), "TXS": (X, 0), "TSX": (0, X | N | Z),
        "XBA": (AW, AW | N | Z),
    }
    if n in transfers:
        return transfers[n]
    stack = {
        "PHA": (a_use, 0), "PHX": (X, 0), "PHY": (Y, 0), "PHB": (DB, 0), "PHD": (D, 0), "PHK": (0, 0),
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
        return AW | X | Y, AW | X | Y | DB
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


# Packed values (see Liveness's dataflow): NSEG segments of SEG bits.
SEG = 10
NSEG = 11
_REPS: dict = {}


def _rep(mask: int) -> int:
    """mask in every segment."""
    r = _REPS.get(mask)
    if r is None:
        r = _REPS[mask] = sum(mask << (SEG * i) for i in range(NSEG))
    return r


ONES = _rep(1)
IDENT = sum((1 << b) << (SEG * (b + 1)) for b in range(SEG))    # a return: live if the caller reads it
_BIT = {bit: i for i in range(SEG) for bit in [1 << i]}


def _eval(value: int, ret: int) -> int:
    """A packed value as a mask, given what the callers read after the return."""
    out = value & ALL
    for b in range(SEG):
        if ret >> b & 1:
            out |= (value >> (SEG * (b + 1))) & ALL
    return out


def _apply(summary: int, after: int) -> int:
    """The packed value before a call to a routine with this summary, given the
    packed value after it."""
    out = summary & ALL
    for c in range(SEG):
        m = (summary >> (SEG * (c + 1))) & ALL
        if m:
            sel = (after >> c) & ONES
            if sel:
                out |= sel * m
    return out


# The registers a push saves, and where a pull puts what it takes (with
# whether it sets N and Z from it).
PUSH_BITS = {"PHP": FLAGS, "PHA": None, "PHX": X, "PHY": Y, "PHB": DB, "PHD": D}
PULL_DEST = {"PLP": "PHP", "PLA": "PHA", "PLX": "PHX", "PLY": "PHY", "PLB": "PHB", "PLD": "PHD"}


def _push_bits(name: str, widths) -> int:
    if name == "PHA":
        return A if widths[0] is True else AW
    return PUSH_BITS[name]


def _pull_dest(name: str, widths) -> tuple:
    if name == "PLP":
        return FLAGS, False
    if name == "PLA":
        return (A if widths[0] is True else AW), True
    return {"PLX": X, "PLY": Y, "PLB": DB, "PLD": D}[name], True


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
        self.push_pairs: dict[tuple, frozenset] = {}     # (ctx key, push pc) -> the pulls that restore it
        self.push_cases: dict[tuple, str] = {}           # (ctx key, push pc) -> a PAIRING_CASES entry
        self.summary: dict[int, int] = {}                # routine -> packed value at its entry
        self.sym: dict[tuple, dict[int, int]] = {}       # ctx key -> pc -> packed value
        self._seed(dynamic_calls or {})
        self._pair_pushes()

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
        """Where a JSR/JSL goes, as the analysis follows it (an `indirect` hint
        replaces the operand); empty if that isn't known."""
        an = self.an
        target = insn.operand if insn.name == "JSL" else (pc & 0xFF0000) | insn.operand
        if pc in an.hints.derive:
            return list(an.hints.indirect.get(pc, [])) + sorted(an.derived.get(pc, ()))
        return list(an.hints.indirect.get(pc, [target]))

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
            return [insn.next_pc + skip], (None if targets else LEAVES)
        if mode is Mode.ABSXIND:
            jt = an.tables.get(pc)
            if jt is None or jt.count is None:
                return [], LEAVES
            return ([insn.next_pc] if jt.call else self._table_targets(jt)), None
        return [insn.next_pc], None

    def _callees(self, pc: int, insn: Insn) -> list:
        """What an instruction calls and returns from."""
        if insn.name in ("JSR", "JSL") and insn.mode in (Mode.ABS, Mode.LONG):
            return self._call_targets(pc, insn) or [None]
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

    def _follow_push(self, key, push_pc: int, size: int) -> tuple:
        """Walk forward from a push of `size` bytes counting the bytes above
        them, to the pulls that take them off: (PAIRED, {(resume pc, pull
        name, pull's widths)}) or (why not, empty). A path that sets a new
        stack, or never ends, leaves the bytes unread."""
        an = self.an
        c = an.contexts[key]
        above = {}
        seen = set()
        pulls = set()
        work = [(insn.next_pc, 0, None) for insn in an.code[push_pc].values()]
        while work:
            pc, k, prev = work.pop()            # k: bytes pushed above the pushed ones
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
                if mode in (Mode.SR, Mode.SRINDY):
                    w = _sr_bytes(insn, widths)
                    if w is None:
                        return S_UNKNOWN, frozenset()
                    if insn.operand <= k + size and k + 1 <= insn.operand + w - 1:
                        return READ_OTHERWISE, frozenset()
                if name in RETURNS:
                    return (READ_OTHERWISE if k < {"RTS": 2, "RTL": 3, "RTI": 4}[name] else LEAVES), frozenset()
                eff = stack_effect(insn, widths)
                if eff is None:
                    return S_UNKNOWN, frozenset()
                if k == 0 and eff == -size and name in PULL_DEST:
                    pulls.add((insn.next_pc, name, widths))
                    continue
                if k + eff < 0:
                    return READ_OTHERWISE, frozenset()
                call = 3 if name == "JSL" else 2
                if any(self.reach.get(t, UNBOUNDED) > k + call for t in self._callees(pc, insn)):
                    return CALLEE_REACHES, frozenset()
                nexts, why = self._successors(pc, insn)
                if why is not None and not (name in ("JSR", "JSL") and nexts == []):
                    return why, frozenset()       # (a call that doesn't return ends the path)
                loads = self._loads(insn, widths)
                work += [(n, k + eff, loads) for n in nexts]
        return PAIRED, frozenset(pulls)

    def _pair_pushes(self):
        self._compute_reach()
        for key, c in self.an.contexts.items():
            for pc in c.insns:
                variants = {(insn.name, stack_effect(insn, w)) for w, insn in self.an.code[pc].items()}
                if len(variants) != 1:
                    continue
                name, size = variants.pop()
                if name not in PUSH_BITS:
                    continue
                if size is None:
                    case, pulls = S_UNKNOWN, frozenset()
                else:
                    case, pulls = self._follow_push(key, pc, size)
                self.push_cases[(key, pc)] = case
                if case == PAIRED:
                    self.push_pairs[(key, pc)] = pulls

    def pairing_stats(self, names=("PHP",)) -> dict:
        """PAIRING_CASES entry -> (pushes counted per context, distinct push
        addresses), for the pushes named."""
        out = {}
        for case in PAIRING_CASES:
            sites = [k for k, v in self.push_cases.items() if v == case
                     and self.an.code[k[1]] and next(iter(self.an.code[k[1]].values())).name in names]
            out[case] = (len(sites), len({pc for _key, pc in sites}))
        return out

    def _pushed_read(self, key, pc, insn: Insn, widths, live_at) -> int | None:
        """What a paired push reads: of the register it saves, what is live
        after the pulls that restore it (all of it where a pull puts it
        somewhere else that is live, or sets N and Z from it and they are);
        None if the push isn't paired."""
        pulls = self.push_pairs.get((key, pc))
        if pulls is None:
            return None
        saved = _push_bits(insn.name, widths)
        read = 0
        for resume, pull, pull_widths in pulls:
            after = live_at(resume)
            dest, nz = _pull_dest(pull, pull_widths)
            if PULL_DEST[pull] == insn.name:
                read |= after & _rep(saved)
                any_of = (N | Z) if nz else 0
            else:
                any_of = dest | ((N | Z) if nz else 0)
            sel = 0
            for bit in range(SEG):
                if any_of >> bit & 1:
                    sel |= (after >> bit) & ONES
            read |= sel * saved
        return read

    # ---- per-context dataflow ------------------------------------------------------
    #
    # Liveness is solved as a function of what the routine's callers read after
    # it returns, so that a call transfers only what *its* caller reads after it
    # through the callee, not what every caller of the callee does. At each
    # instruction the value is packed into one integer of NSEG segments of SEG
    # bits: segment 0 is what is live whatever the callers read, segment b + 1
    # what is live if they read register bit b. A routine's entry value is its
    # summary; a call applies the callee's summary to the value after the call.

    def _entries_at(self, addr: int) -> list:
        return [k for k in self.an.contexts if k[0] == addr]

    def _callee_live_in(self, target: int) -> int:
        keys = self._entries_at(target)
        if not keys:
            return ALL
        return self.func_live_in[target]

    def _summary(self, target: int) -> int:
        if not self._entries_at(target):
            return ALL
        return self.summary.get(target, 0)

    def _call(self, targets, after: int) -> int:
        """The value before a call to any of targets, given the value after."""
        live = 0
        for t in targets:
            live |= _apply(self._summary(t), after)
        return live

    def _instruction_live_in(self, key, pc, insn: Insn, widths, live_at) -> int:
        an = self.an
        name, mode = insn.name, insn.mode
        uses, defs = use_def(insn, widths[0], widths[1])
        bank = pc & 0xFF0000

        if name in SUSPICIOUS or name == "STP" or name == "RTI":
            return ALL
        if name in ("RTS", "RTL"):
            return IDENT
        if name == "XBA":
            # the bytes change places, so their liveness does; N and Z come
            # from the byte that ends up low
            out = live_at(insn.next_pc)
            swapped = ((out & _rep(A)) << 9) | ((out & _rep(AH)) >> 9)
            nz = (((out >> _BIT[N]) | (out >> _BIT[Z])) & ONES) * AH
            return swapped | nz | (out & ~_rep(AW | N | Z))
        if name in PUSH_BITS:
            read = self._pushed_read(key, pc, insn, widths, live_at)
            if read is not None:
                return read | live_at(insn.next_pc)
        if name in CONDITIONAL_BRANCHES:
            return uses | live_at(insn.next_pc) | live_at(insn.branch_target())
        if name in ("BRA", "BRL"):
            return live_at(insn.branch_target())
        if name == "JMP" and mode is Mode.ABS:
            return live_at(bank | insn.operand)
        if name == "JML" and mode is Mode.LONG:
            return self._call([insn.operand], IDENT)
        if name in ("JSR", "JSL") and mode in (Mode.ABS, Mode.LONG):
            targets = self._call_targets(pc, insn)
            if not targets:
                return ALL
            if any(t in an.hints.noreturn for t in targets):
                return self._call(targets, 0)
            skip = max(an.hints.inline_args.get(t, 0) for t in targets)
            return self._call(targets, live_at(insn.next_pc + skip))
        if mode is Mode.ABSXIND:
            jt = an.tables.get(pc)
            if jt is None or jt.count is None:
                return ALL
            targets = self._table_targets(jt)
            if jt.call:
                return uses | self._call(targets, live_at(insn.next_pc))
            live = uses
            for t in targets:
                live |= live_at(t)
            return live
        if mode in (Mode.ABSIND, Mode.ABSLIND):
            targets = an.hints.indirect.get(pc)
            if not targets:
                return ALL
            return uses | self._call(targets, IDENT)
        return uses | (live_at(insn.next_pc) & ~_rep(defs))

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

    def _solve_context(self, key) -> int:
        """Solve a context with the summaries as they are; its entry's value."""
        an = self.an
        c = an.contexts[key]
        live = self.sym.setdefault(key, {})
        pcs = sorted(c.insns, reverse=True)

        def live_at(addr):
            if addr not in c.insns:
                return ALL   # flows somewhere this context didn't trace
            return live.get(addr, 0)

        changed = True
        while changed:
            changed = False
            for pc in pcs:
                value = 0
                for widths, insn in an.code[pc].items():
                    value |= self._instruction_live_in(key, pc, insn, widths, live_at)
                if value != live.get(pc):
                    live[pc] = value
                    changed = True
        return live.get(c.entry, ALL)

    def _call_edges(self):
        """(caller key, pc after the call or None for a tail, targets) of every
        call and tail jump, for carrying what callers read into ret_live."""
        an = self.an
        for key, c in an.contexts.items():
            for pc in c.insns:
                for insn in an.code[pc].values():
                    name, mode = insn.name, insn.mode
                    if name == "JML" and mode is Mode.LONG:
                        yield key, None, [insn.operand]
                    elif name in ("JSR", "JSL") and mode in (Mode.ABS, Mode.LONG):
                        targets = self._call_targets(pc, insn)
                        if not targets or any(t in an.hints.noreturn for t in targets):
                            continue
                        skip = max(an.hints.inline_args.get(t, 0) for t in targets)
                        yield key, insn.next_pc + skip, targets
                    elif mode is Mode.ABSXIND:
                        jt = an.tables.get(pc)
                        if jt is not None and jt.count is not None and jt.call:
                            yield key, insn.next_pc, self._table_targets(jt)
                    elif mode in (Mode.ABSIND, Mode.ABSLIND) and an.hints.indirect.get(pc):
                        yield key, None, an.hints.indirect[pc]

    def solve(self, max_rounds: int = 60):
        an = self.an
        # 1. every routine's summary, from nothing up to the least fixpoint
        for _ in range(max_rounds):
            changed = False
            for key in an.contexts:
                entry_value = self._solve_context(key)
                new = self.summary.get(key[0], 0) | entry_value
                if new != self.summary.get(key[0], 0):
                    self.summary[key[0]] = new
                    changed = True
            if not changed:
                break
        else:
            raise RuntimeError("liveness did not converge")
        # 2. what callers read after each routine returns
        edges = list(self._call_edges())
        for _ in range(max_rounds * 4):
            changed = False
            for key, resume, targets in edges:
                c = an.contexts[key]
                if resume is None:
                    after = self.ret_live[key[0]]
                elif resume in c.insns:
                    after = _eval(self.sym[key].get(resume, 0), self.ret_live[key[0]])
                else:
                    after = ALL
                for t in targets:
                    if t in self.all_live_returns:
                        continue
                    new = self.ret_live[t] | after
                    if new != self.ret_live[t]:
                        self.ret_live[t] = new
                        changed = True
            if not changed:
                break
        else:
            raise RuntimeError("liveness did not converge")
        # 3. the values, given what the callers read
        for key, values in self.sym.items():
            ret = self.ret_live[key[0]]
            self.live_in[key] = {pc: _eval(v, ret) for pc, v in values.items()}
            c = an.contexts[key]
            self.func_live_in[key[0]] |= self.live_in[key].get(c.entry, ALL)

    def dead_at_return(self, entry: int) -> int:
        return ALL & ~self.ret_live[entry]

    # ---- queries at program points (after solve) --------------------------------------

    def contexts_at(self, pc: int) -> list:
        """The contexts that run the instruction at pc."""
        return [key for key, c in self.an.contexts.items() if pc in c.insns]

    def live_at(self, pc: int) -> int | None:
        """What may be read from pc on before it is set, over every context
        that runs pc; None if none does."""
        keys = self.contexts_at(pc)
        if not keys:
            return None
        live = 0
        for key in keys:
            live |= self.live_in.get(key, {}).get(pc, ALL)
        return live

    def live_after_return(self, pc: int) -> int | None:
        """What may be read after the routine running pc returns (pc a return
        instruction, or any instruction of it), over every context that runs
        pc; None if none does."""
        keys = self.contexts_at(pc)
        if not keys:
            return None
        live = 0
        for key in keys:
            live |= ALL if key[0] in self.all_live_returns else self.ret_live[key[0]]
        return live

    def callee_reads(self, target: int) -> int:
        """What a routine reads before it returns, whatever its caller reads
        after: what a call to it needs set (a register it only carries to its
        return is left out)."""
        if not self._entries_at(target):
            return ALL
        return _eval(self._summary(target), 0)

    def live_into_call(self, site: int, target: int) -> int:
        """What may be read from target's entry on when the JSR/JSL at site
        calls it: what the callee reads, and what the caller reads after the
        call returns that the callee leaves alone. Other call sites' readers
        don't count, unlike func_live_in; a site that isn't such a call gets
        func_live_in."""
        an = self.an
        after = None
        for key in self.contexts_at(site):
            c = an.contexts[key]
            for insn in an.code[site].values():
                if insn.name in ("JSR", "JSL") and insn.mode in (Mode.ABS, Mode.LONG):
                    targets = self._call_targets(site, insn)
                elif insn.mode is Mode.ABSXIND and insn.name == "JSR":
                    targets = self._callees(site, insn)
                else:
                    continue
                if target not in targets:
                    continue
                resume = insn.next_pc + an.hints.inline_args.get(target, 0)
                live = self.live_in.get(key, {}).get(resume, ALL) if resume in c.insns else ALL
                after = live if after is None else after | live
        if not self._entries_at(target):
            return ALL
        if after is None:
            return self.func_live_in[target]
        return _eval(_apply(self._summary(target), after), 0)
