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

**Callees that leave through their caller's frame.** A routine can pull its
own return address and then its caller's saved registers and return for the
caller (the sound driver's `$C7:0A39`, refused, runs its caller `$C7:0004`'s
exit). A push whose byte such a callee reaches is followed into it
(`_follow_into`): where every path either returns normally with the byte
untouched, or pulls it with a pull of its size and then only pulls the rest of
the caller's frame and returns as the caller does (`_escapes`), the push reads
what is live after that pull (`push_foreign`), which is what the caller's
callers read. A return below the depth a routine was entered at is a return
through its caller's frame: the packed values carry what is read after those
apart from what is read after an ordinary return (segments from ESC on,
`esc_live`), a call maps them to what is read after its caller returns (and,
where the callee may pull more than the caller's frame, further up), and
`after_return` counts both.

**Indirect jumps.** A `JMP (abs)` or `JML [abs]` goes to the targets the
analysis follows (a hint's and the ones derived from data); preceded by a
`PER` it is a call returning to the `PER`'s address, otherwise a tail jump
whose targets return for the routine making it. A routine an indirect jump
reaches keeps everything live after it only where some site that reaches it
has targets nobody argued complete (neither a `derive` nor a `bound` hint):
such a site could reach it from anywhere (`indirect_entries` says which).
Tables filled at run time are called at every target the analysis follows
(their entries, a hint's, the derived ones).

Each context runs its instructions with the widths the analysis traced it
with there (`_code`), not every context's; and `RTI` takes P from the stack,
so the flags before it are dead.
"""

from collections import defaultdict

from .analyze import Analyzer
from .cpu import CONDITIONAL_BRANCHES, RETURNS, SUSPICIOUS, Insn, Mode
from .memmap import bus_to_offset

# Bit order matches runtime/port/native.h's DC_* bits, except that DC_A means
# the whole of A, so A and AH dead together is DC_A and AH alone DC_AH.
A, X, Y, N, V, Z, C, DB, D, AH = (1 << i for i in range(10))
ALL = (1 << 10) - 1
NAMES = [("A", A), ("AH", AH), ("X", X), ("Y", Y), ("N", N), ("V", V), ("Z", Z), ("C", C), ("DB", DB), ("D", D)]
FLAGS = N | V | Z | C
AW = A | AH       # the whole accumulator
# Register names a `keeps` hint uses: P is the four flags the analysis tracks.
KEEP_BITS = {"A": A, "AH": AH, "X": X, "Y": Y, "P": FLAGS, "DB": DB, "D": D}

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
        # A 16-bit AND #$00xx (or ORA #$FFxx) sets the high byte whatever it
        # held -- the `AND #$00FF` that widens a byte -- so it reads the low
        # byte alone.
        if m8 is False and mode is Mode.IMM_M and insn.operand is not None and \
                ((n == "AND" and insn.operand >> 8 == 0x00) or (n == "ORA" and insn.operand >> 8 == 0xFF)):
            return A, AW | N | Z
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


# Packed values (see Liveness's dataflow): NSEG segments of SEG bits. Segment
# 0 is live whatever the callers read; segment b + 1 is live if what runs
# after the routine returns reads register bit b; segment ESC + b if what runs
# after it leaves through its caller's frame (a return that pulls more than
# the routine pushed, into its caller's caller) does.
SEG = 10
ESC = SEG + 1
NSEG = 2 * SEG + 1
_REPS: dict = {}


def _rep(mask: int) -> int:
    """mask in every segment."""
    r = _REPS.get(mask)
    if r is None:
        r = _REPS[mask] = sum(mask << (SEG * i) for i in range(NSEG))
    return r


ONES = _rep(1)
IDENT = sum((1 << b) << (SEG * (b + 1)) for b in range(SEG))    # a return: live if the caller reads it
ESC_IDENT = sum((1 << b) << (SEG * (ESC + b)) for b in range(SEG))   # a return through the caller's frame
_BIT = {bit: i for i in range(SEG) for bit in [1 << i]}


def _eval(value: int, ret: int, esc: int = 0) -> int:
    """A packed value as a mask, given what is read after the routine returns
    and after it leaves through its caller's frame."""
    out = value & ALL
    for b in range(SEG):
        if ret >> b & 1:
            out |= (value >> (SEG * (b + 1))) & ALL
        if esc >> b & 1:
            out |= (value >> (SEG * (ESC + b))) & ALL
    return out


def _apply(summary: int, after: int, deep: bool = True) -> int:
    """The packed value before a call to a routine with this summary, given the
    packed value after it. Where the callee leaves through the caller's frame
    it returns for the caller, and where it may pull more than the caller's
    frame (`deep`), further up: what is read after either counts."""
    out = summary & ALL
    for c in range(SEG):
        m = (summary >> (SEG * (c + 1))) & ALL
        if m:
            sel = (after >> c) & ONES
            if sel:
                out |= sel * m
        m = (summary >> (SEG * (ESC + c))) & ALL
        if m:
            out |= (m << (SEG * (c + 1))) | ((m << (SEG * (ESC + c))) if deep else 0)
    return out


def _as_caller(value: int) -> int:
    """A packed value from a callee's path that ends leaving through its
    caller's frame, as the caller's: that return is the caller's own."""
    out = value & ALL
    for c in range(SEG):
        m = ((value >> (SEG * (c + 1))) | (value >> (SEG * (ESC + c)))) & ALL
        if m:
            out |= m << (SEG * (c + 1))
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
    def __init__(self, an: Analyzer, dynamic_calls: dict[int, set] | None = None, strict: bool = False,
                 bounded: dict[int, list] | None = None):
        """dynamic_calls: target -> call sites observed at runtime (ct_dev --calls).
        strict: a transfer whose targets are read from memory at run time or
        derived from data (dynamic_transfer) keeps everything live, instead
        of taking the target set the analysis follows as complete.
        bounded: dispatcher -> targets, for a jump table whose targets strict
        mode would otherwise give up on: every target its index can reach,
        argued from the dispatch rather than from the data it runs on
        (tools/v_readers.py). The table is followed to exactly those."""
        self.an = an
        self.strict = strict
        self.bounded = dict(bounded or {})
        self.dynamic_calls = dynamic_calls or {}
        self.live_in: dict[tuple, dict[int, int]] = {}     # ctx key -> pc -> live-in mask
        self.func_live_in: dict[int, int] = defaultdict(int)
        self.ret_live: dict[int, int] = defaultdict(int)
        # routine -> what may be read after it leaves through its caller's frame
        # (only a routine with such a return has it read: after_return())
        self.esc_live: dict[int, int] = defaultdict(int)
        self.all_live_returns: set[int] = set()
        self.reasons: dict[int, str] = {}
        self.reach: dict[int, int] = {}                  # routine -> bytes above its entry S it may touch
        self.push_pairs: dict[tuple, frozenset] = {}     # (ctx key, push pc) -> the pulls that restore it
        self.push_cases: dict[tuple, str] = {}           # (ctx key, push pc) -> a PAIRING_CASES entry
        # (ctx key, push pc) -> pulls in a callee that leaves through the pusher's frame,
        # {(callee's ctx key, resume pc, pull, widths)} (_follow_into)
        self.push_foreign: dict[tuple, frozenset] = {}
        self._depths: dict = {}
        self._deep_cache: dict = {}
        self._widths: dict = {}                          # ctx key -> pc -> widths it runs with there
        self._foreign_deps: set = set()                  # (ctx key, pc) whose value a push elsewhere reads
        self.summary: dict[int, int] = {}                # routine -> packed value at its entry
        self.sym: dict[tuple, dict[int, int]] = {}       # ctx key -> pc -> packed value
        # routines an indirect JMP/JML reaches -> why everything stays live after them, or None
        self.indirect_entries: dict[int, str | None] = {}
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
            unsafe = c.kinds & {"vector", "coverage", "hint"}
            if unsafe:
                self._all_live(c.entry, f"entered as {', '.join(sorted(unsafe))}")
            elif "indirect" in c.kinds:
                why = self._indirect_entry(c)
                self.indirect_entries[c.entry] = why
                if why is not None:
                    self._all_live(c.entry, why)
        for target, sites in dynamic_calls.items():
            unknown = sorted(s for s in sites if s not in static_sites)
            if unknown:
                self._all_live(target, f"called from sites static analysis doesn't know ({len(unknown)})")

    def _indirect_entry(self, c) -> str | None:
        """Why a routine an indirect JMP or JML reaches must keep everything
        live after it returns, or None when its callers are known: every
        indirect site the analysis follows to it has a target set argued
        complete (a `derive` hint, or a `bound`), so it is entered from those
        sites (and the calls and jumps the analysis sees) and nowhere else,
        and returns where they do. A site whose targets are only the ones the
        movies ran could reach it from elsewhere too."""
        an = self.an
        sites = [pc for pc in sorted(c.callers)
                 if any(i.mode in (Mode.ABSIND, Mode.ABSLIND) for i in an.code.get(pc, {}).values())]
        if not sites:
            return "entered as indirect"
        loose = [pc for pc in sites if pc not in an.hints.derive and pc not in an.hints.bounds]
        if loose:
            return ("entered by an indirect jump whose targets aren't argued complete "
                    f"(${loose[0] >> 16:02X}:{loose[0] & 0xFFFF:04X})")
        return None

    def _code(self, key, pc: int) -> list:
        """The instruction at pc as the context runs it: (widths, insn) for
        each set of widths the analysis traced it with there. (an.code has
        every context's; a routine entered with 8- and 16-bit index
        registers pushes one or two bytes at a PHX, and only one of them in
        each context.)"""
        ws = self._widths.get(key)
        if ws is None:
            ws = self._widths[key] = defaultdict(set)
            for p, st in self.an.contexts[key].visited:
                ws[p].add(st.widths)
        variants = self.an.code[pc]
        mine = [(w, variants[w]) for w in sorted(ws.get(pc, ()), key=str) if w in variants]
        return mine or list(variants.items())

    def dynamic_transfer(self, pc: int, insn: Insn) -> str | None:
        """Why the targets of the transfer at pc aren't fixed by code and ROM
        alone, or None: they are read from work RAM at run time (a table or
        pointer in RAM, a call whose operand the program writes), or the set
        the analysis follows comes from data (a `derive` hint) or from what the
        movies ran. The analysis still follows such a site where its set is
        argued complete for the game's own data; strict mode doesn't, since a
        caller that puts the target there itself (a port that stores a
        script's operand and calls the dispatcher) can put anything there."""
        an = self.an
        name, mode = insn.name, insn.mode
        if name in ("JSR", "JSL", "JML") and mode in (Mode.ABS, Mode.LONG):
            if pc in an.hints.indirect or pc in an.hints.derive:
                return "its operand is written at run time"
            return None
        if mode is Mode.ABSXIND:
            table = (pc & 0xFF0000) | insn.operand
            if bus_to_offset(table) is None:
                return f"its table (${table >> 16:02X}:{table & 0xFFFF:04X}) is in RAM"
            jt = an.tables.get(pc)
            if jt is not None and (jt.extra or an.derived.get(pc) or pc in an.hints.derive):
                return "its table is filled out from data or a hint"
            return None
        if mode in (Mode.ABSIND, Mode.ABSLIND):
            if bus_to_offset(insn.operand) is None:
                return f"its pointer (${insn.operand:04X}) is in RAM"
            if an.derived.get(pc) or pc in an.hints.derive or pc in an.hints.indirect:
                return "its targets come from data or a hint"
            return None
        return None

    def dynamic_sites(self, target: int) -> dict:
        """The transfers with dynamic targets (dynamic_transfer) the routine at
        target runs or reaches through the calls and jumps the analysis
        follows: pc -> why."""
        an = self.an
        out, seen, work = {}, set(), [target]
        while work:
            entry = work.pop()
            if entry in seen:
                continue
            seen.add(entry)
            for key in self._entries_at(entry):
                for pc in an.contexts[key].insns:
                    for _, insn in self._code(key, pc):
                        why = None if pc in self.bounded else self.dynamic_transfer(pc, insn)
                        if why is not None:
                            out.setdefault(pc, why)
                        work += [t for t in self._callees(pc, insn) if t is not None]
                        if insn.name == "JML" and insn.mode is Mode.LONG:
                            work.append(insn.operand)
                        elif insn.mode in (Mode.ABSIND, Mode.ABSLIND):
                            work += self.indirect_targets(pc)
                        elif insn.mode is Mode.ABSXIND:
                            jt = an.tables.get(pc)
                            if jt is not None and jt.count is not None:
                                work += self._table_targets(jt)
        return out

    def _all_live(self, entry: int, why: str):
        self.all_live_returns.add(entry)
        self.ret_live[entry] = ALL
        self.esc_live[entry] = ALL
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
            for widths, insn in self._code(key, pc):
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
                    targets = [insn.operand] if mode is Mode.LONG else self.indirect_targets(pc)
                    if not targets:
                        return UNBOUNDED
                    r = max([r] + [reach.get(t, UNBOUNDED) - d for t in targets])
                    if r >= UNBOUNDED // 2:
                        return UNBOUNDED
                    # PER then JMP (abs): the targets return to the PER's address, its two bytes pulled
                    work += [(n, d - 2, None) for n in self.indirect_resumes(pc)] if mode is Mode.ABSIND else []
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
        # a routine a `keeps` hint describes pulls only its own return address
        # off its caller's stack: the stacks it switches to aren't the caller's
        kept = set(self.an.hints.keeps)
        for entry in kept:
            reach[entry] = 2
        changed = True
        while changed:
            changed = False
            for entry, keys in entries.items():
                if reach[entry] == UNBOUNDED or entry in kept:
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
        foreign = set()
        work = [(insn.next_pc, 0, None) for insn in (i for _, i in self._code(key, push_pc))]
        while work:
            pc, k, prev = work.pop()            # k: bytes pushed above the pushed ones
            if pc not in c.insns:
                return LEAVES, frozenset()
            if above.setdefault(pc, k) != k:
                return S_UNKNOWN, frozenset()
            if (pc, prev) in seen:
                continue
            seen.add((pc, prev))
            for widths, insn in self._code(key, pc):
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
                reaching = [t for t in self._callees(pc, insn) if self.reach.get(t, UNBOUNDED) > k + call]
                if reaching:
                    got = self._follow_into(key, push_pc, size, reaching, k + call, call)
                    if got is None:
                        return CALLEE_REACHES, frozenset()
                    foreign |= got
                nexts, why = self._successors(pc, insn)
                if why is not None and not (name in ("JSR", "JSL") and nexts == []):
                    return why, frozenset()       # (a call that doesn't return ends the path)
                loads = self._loads(insn, widths)
                work += [(n, k + eff, loads) for n in nexts]
        if foreign:
            self.push_foreign[(key, push_pc)] = frozenset(foreign)
        return PAIRED, frozenset(pulls)

    def _follow_into(self, key, push_pc: int, size: int, callees, k0: int, call: int):
        """A callee that reaches the pushed bytes (they lie `k0` bytes above
        its entry S, the last `call` of them its return address): the pulls in
        it that take them off, as {(callee's ctx key, resume pc, pull, pull's
        widths)}, or None where it isn't clear the callee does only that.

        This is a routine that leaves through its caller's frame: the sound
        driver's `$C7:0A39`, refused, pulls its own return address and runs
        its caller's exit, which restores the caller's registers and returns
        to the caller's caller. Every path through the callee must either
        return to the caller normally, the bytes untouched, or pull them with
        a pull of the same size and then only pull the rest of the caller's
        frame and return as the caller does (`_escapes`). What is live after
        such a pull is then what the caller's callers read, the same packed
        value as at the caller's own pull, so the push reads it."""
        an = self.an
        owner = self._returns_as(key)
        dp0 = self._depth_before(key, push_pc)
        out = set()
        for t in callees:
            fkeys = self._entries_at(t)
            if not fkeys:
                return None
            for fkey in fkeys:
                c = an.contexts[fkey]
                above, seen = {}, set()
                work = [(c.entry, k0, None)]
                while work:
                    pc, k, prev = work.pop()      # k: bytes above the pushed ones
                    if pc not in c.insns or above.setdefault(pc, k) != k:
                        return None
                    if (pc, prev) in seen:
                        continue
                    seen.add((pc, prev))
                    for widths, insn in self._code(fkey, pc):
                        name, mode = insn.name, insn.mode
                        sets = self._sets_s(insn, prev)
                        if sets == "reset":
                            continue
                        if sets == "unknown" or name in ("TSC", "TSX") or name in SUSPICIOUS:
                            return None
                        if mode in (Mode.SR, Mode.SRINDY):
                            w = _sr_bytes(insn, widths)
                            if w is None or insn.operand + w - 1 > k:
                                return None
                        if name in RETURNS:
                            if k == k0 and {"RTS": 2, "RTL": 3}.get(name) == call:
                                continue          # back to the caller, the bytes untouched
                            return None
                        eff = stack_effect(insn, widths)
                        if eff is None:
                            return None
                        if k == 0 and eff == -size and name in PULL_DEST:
                            if owner is None or dp0 is None or not self._escapes(fkey, insn.next_pc, dp0, owner):
                                return None
                            out.add((fkey, insn.next_pc, name, widths))
                            continue
                        if k + eff < 0:
                            return None
                        c2 = 3 if name == "JSL" else 2
                        if any(self.reach.get(t2, UNBOUNDED) > k + c2 for t2 in self._callees(pc, insn)):
                            return None
                        nexts, why = self._successors(pc, insn)
                        if why is not None:
                            if name in ("JSR", "JSL") and nexts == []:
                                continue          # a call that doesn't return ends the path
                            return None
                        work += [(n, k + eff, self._loads(insn, widths)) for n in nexts]
        return out

    def _escapes(self, fkey, pc: int, depth: int, ret: str) -> bool:
        """Whether every path from pc (in context fkey) pulls `depth` more
        bytes and then returns with `ret`, reading nothing further up: the
        return the routine whose frame those bytes are makes."""
        an = self.an
        c = an.contexts[fkey]
        above, seen = {}, set()
        work = [(pc, depth, None)]
        while work:
            pc, j, prev = work.pop()
            if pc not in c.insns or above.setdefault(pc, j) != j:
                return False
            if (pc, prev) in seen:
                continue
            seen.add((pc, prev))
            for widths, insn in self._code(fkey, pc):
                name, mode = insn.name, insn.mode
                if self._sets_s(insn, prev) is not None or name in ("TSC", "TSX") or name in SUSPICIOUS:
                    return False
                if name in RETURNS:
                    if j != 0 or name != ret:
                        return False
                    continue
                if mode in (Mode.SR, Mode.SRINDY):
                    w = _sr_bytes(insn, widths)
                    if w is None or insn.operand + w - 1 > j:
                        return False
                eff = stack_effect(insn, widths)
                if eff is None or j + eff < 0:
                    return False
                c2 = 3 if name == "JSL" else 2
                if any(self.reach.get(t, UNBOUNDED) > j + c2 for t in self._callees(pc, insn)):
                    return False
                nexts, why = self._successors(pc, insn)
                if why is not None:
                    if name in ("JSR", "JSL") and nexts == []:
                        continue
                    return False
                work += [(n, j + eff, self._loads(insn, widths)) for n in nexts]
        return True

    def _returns_as(self, key) -> str | None:
        """The return a routine entered only by JSL ("RTL"), or only by JSR
        and call tables ("RTS"), makes to its caller; None for others."""
        kinds = self.an.contexts[key].kinds
        if kinds and kinds <= {"jsl"}:
            return "RTL"
        if kinds and kinds <= {"jsr", "table"}:
            return "RTS"
        return None

    def _depth_before(self, key, pc: int) -> int | None:
        """Bytes a context has pushed since its entry when it reaches pc, if
        that is one number on every path."""
        if key not in self._depths:
            an = self.an
            c = an.contexts[key]
            depth, seen, ok = {}, set(), True
            work = [(c.entry, 0, None)]
            while work and ok:
                p, d, prev = work.pop()
                if p not in c.insns:
                    continue
                if depth.setdefault(p, d) != d:
                    ok = False
                    break
                if (p, prev) in seen:
                    continue
                seen.add((p, prev))
                for widths, insn in self._code(key, p):
                    if self._sets_s(insn, prev) is not None:
                        continue
                    eff = stack_effect(insn, widths)
                    if eff is None:
                        ok = False
                        break
                    nexts, _ = self._successors(p, insn)
                    work += [(n, d + eff, self._loads(insn, widths)) for n in nexts]
            self._depths[key] = depth if ok else None
        depth = self._depths[key]
        return None if depth is None else depth.get(pc)

    def _pair_pushes(self):
        self._compute_reach()
        for key, c in self.an.contexts.items():
            for pc in c.insns:
                variants = {(insn.name, stack_effect(insn, w)) for w, insn in self._code(key, pc)}
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

    def _foreign_value(self, fkey, pc: int) -> int:
        """The packed value at pc in another context, read by a push this
        context makes (solve() iterates until these stop changing too)."""
        self._foreign_deps.add((fkey, pc))
        if pc not in self.an.contexts[fkey].insns:
            return ALL
        return self.sym.get(fkey, {}).get(pc, 0)

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
        # a pull in a callee that leaves through this routine's frame: what is
        # live after it is read by this routine's callers (its return is this
        # routine's), so its packed value counts as this routine's
        pulled = [(live_at(r), p, w) for r, p, w in pulls] + \
                 [(_as_caller(self._foreign_value(fkey, r)), p, w)
                  for fkey, r, p, w in self.push_foreign.get((key, pc), ())]
        for after, pull, pull_widths in pulled:
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
        kept = self._kept_summary(target)
        if kept is not None:
            return kept
        if not self._entries_at(target):
            return ALL
        return self.summary.get(target, 0)

    def _kept_summary(self, target: int) -> int | None:
        """The summary a `keeps` hint gives a routine, or None: it returns to
        its caller with the registers it keeps as the caller had them (each
        live before the call where it is live after), and reads only what it
        names. A task switch is the case: it saves the task's registers on the
        task's own stack, runs the other tasks and takes them back off, which
        the pairing of pushes can't follow across the stacks it switches."""
        hint = self.an.hints.keeps.get(target)
        if hint is None:
            return None
        kept, read = hint
        value = 0
        for name in read:
            value |= KEEP_BITS[name]
        for name in kept:
            for b in range(SEG):
                if KEEP_BITS[name] >> b & 1:
                    value |= (1 << b) << (SEG * (b + 1))
        return value

    def _call(self, targets, after: int, key=None, pc: int | None = None, size: int | None = None) -> int:
        """The value before a call to any of targets, given the value after
        (the call's context, address and bytes pushed say how far up a
        callee that leaves through the caller's frame can go)."""
        live = 0
        for t in targets:
            live |= _apply(self._summary(t), after, self._deep(key, pc, t, size))
        return live

    def _deep(self, key, pc, target: int, size: int | None) -> bool:
        """Whether the routine a call at pc makes may pull more than the
        caller's frame (what the caller pushed and its return address), and
        so return past the caller's caller; True where that isn't known."""
        if key is None or size is None:
            return True
        got = self._deep_cache.get((key, pc, target))
        if got is None:
            r = self.reach.get(target, UNBOUNDED)
            depth = self._depth_before(key, pc)
            ret = {"RTS": 2, "RTL": 3}.get(self._returns_as(key))
            got = r >= UNBOUNDED or depth is None or ret is None or r - size > depth + ret
            self._deep_cache[(key, pc, target)] = got
        return got

    def _instruction_live_in(self, key, pc, insn: Insn, widths, live_at) -> int:
        an = self.an
        name, mode = insn.name, insn.mode
        uses, defs = use_def(insn, widths[0], widths[1])
        bank = pc & 0xFF0000

        if name in SUSPICIOUS or name == "STP":
            return ALL
        if self.strict and pc not in self.bounded and self.dynamic_transfer(pc, insn) is not None:
            return ALL
        if name == "RTI":
            return ALL & ~FLAGS     # the interrupted code's registers; its P comes off the stack
        if name in ("RTS", "RTL"):
            depth = self._depth_before(key, pc)
            # below the depth it was entered at, it returns through its caller's
            # frame; where S isn't followed it is taken to return to the caller
            return ESC_IDENT if depth is not None and depth < 0 else IDENT
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
            size = 3 if name == "JSL" else 2
            if any(t in an.hints.noreturn for t in targets):
                return self._call(targets, 0, key, pc, size)
            skip = max(an.hints.inline_args.get(t, 0) for t in targets)
            return self._call(targets, live_at(insn.next_pc + skip), key, pc, size)
        if mode is Mode.ABSXIND:
            jt = an.tables.get(pc)
            if jt is None or jt.count is None:
                return ALL
            targets = self._table_targets(jt)
            if jt.call:
                return uses | self._call(targets, live_at(insn.next_pc), key, pc, 2)
            live = uses
            resumes = self.indirect_resumes(pc)
            if resumes:         # PER then JMP (abs,X): the targets return to the PER's address
                after = 0
                for r in resumes:
                    after |= live_at(r)
                live |= self._call(targets, after, key, pc, 2)
            for t in targets:
                live |= live_at(t)
            return live
        if mode in (Mode.ABSIND, Mode.ABSLIND):
            targets = self.indirect_targets(pc)
            if not targets:
                return ALL
            resumes = self.indirect_resumes(pc)
            if resumes:         # PER then JMP (abs): a call
                after = 0
                for r in resumes:
                    after |= live_at(r)
                return uses | self._call(targets, after, key, pc, 2)
            return uses | self._call(targets, IDENT)     # a tail jump: they return for this routine
        return uses | (live_at(insn.next_pc) & ~_rep(defs))

    def _table_targets(self, jt) -> list:
        """Every target the analysis follows from a sized table: its entries,
        a hint's extra targets and the ones derived from data (or the bound
        given for it)."""
        if jt.dispatcher in self.bounded:
            return list(self.bounded[jt.dispatcher])
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
        out += list(jt.extra) + sorted(self.an.derived.get(jt.dispatcher, ()))
        return list(dict.fromkeys(out))

    def indirect_targets(self, pc: int) -> list:
        """Where a JMP (abs) or JML [abs] goes, as the analysis follows it (a
        hint's targets and the ones derived from data); empty if it doesn't
        (no targets, or targets that are threads' entries)."""
        an = self.an
        if pc in an.hints.nofollow:
            return []
        return list(dict.fromkeys(list(an.hints.indirect.get(pc, [])) + sorted(an.derived.get(pc, ()))))

    def indirect_resumes(self, pc: int) -> list:
        """Where the targets of an indirect JMP at pc return to when it is a
        call (a PER before it pushed the address); empty for a tail jump."""
        return sorted(self.an.call_resumes.get(pc, ()))

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
                for widths, insn in self._code(key, pc):
                    value |= self._instruction_live_in(key, pc, insn, widths, live_at)
                if value != live.get(pc):
                    live[pc] = value
                    changed = True
        return live.get(c.entry, ALL)

    def _call_edges(self):
        """(caller key, the call's pc, pc after the call or None for a tail,
        targets, bytes the call pushes or None for a tail) of every call and
        tail jump, for carrying what callers read into ret_live."""
        an = self.an
        for key, c in an.contexts.items():
            for pc in c.insns:
                for insn in an.code[pc].values():
                    name, mode = insn.name, insn.mode
                    if name == "JML" and mode is Mode.LONG:
                        yield key, pc, None, [insn.operand], None
                    elif name in ("JSR", "JSL") and mode in (Mode.ABS, Mode.LONG):
                        targets = self._call_targets(pc, insn)
                        if not targets or any(t in an.hints.noreturn for t in targets):
                            continue
                        skip = max(an.hints.inline_args.get(t, 0) for t in targets)
                        yield key, pc, insn.next_pc + skip, targets, 3 if name == "JSL" else 2
                    elif mode is Mode.ABSXIND:
                        jt = an.tables.get(pc)
                        if jt is not None and jt.count is not None:
                            if jt.call:
                                yield key, pc, insn.next_pc, self._table_targets(jt), 2
                            else:
                                for r in self.indirect_resumes(pc):
                                    yield key, pc, r, self._table_targets(jt), 2
                    elif mode in (Mode.ABSIND, Mode.ABSLIND) and self.indirect_targets(pc):
                        resumes = self.indirect_resumes(pc)
                        for r in resumes:
                            yield key, pc, r, self.indirect_targets(pc), 2
                        if not resumes:
                            yield key, pc, None, self.indirect_targets(pc), None

    def solve(self, max_rounds: int = 60):
        an = self.an
        # 1. every routine's summary, from nothing up to the least fixpoint
        for _ in range(max_rounds):
            changed = False
            foreign = {(k, pc): self.sym.get(k, {}).get(pc) for k, pc in self._foreign_deps}
            for key in an.contexts:
                entry_value = self._solve_context(key)
                new = self.summary.get(key[0], 0) | entry_value
                if new != self.summary.get(key[0], 0):
                    self.summary[key[0]] = new
                    changed = True
            if len(foreign) != len(self._foreign_deps) or \
                    any(self.sym.get(k, {}).get(pc) != v for (k, pc), v in foreign.items()):
                changed = True        # a push reads a value in another context that has moved
            if not changed:
                break
        else:
            raise RuntimeError("liveness did not converge")
        # 2. what callers read after each routine returns
        edges = list(self._call_edges())
        for _ in range(max_rounds * 4):
            changed = False
            for key, pc, resume, targets, size in edges:
                c = an.contexts[key]
                ret, esc = self.ret_live[key[0]], self.esc_live[key[0]]
                if resume is None:
                    after = ret
                elif resume in c.insns:
                    after = _eval(self.sym[key].get(resume, 0), ret, esc)
                else:
                    after = ALL
                for t in targets:
                    if t in self.all_live_returns:
                        continue
                    # leaving through the caller's frame, it returns for the caller or further up
                    new = self.ret_live[t] | after
                    new_esc = self.esc_live[t] | ret | (esc if self._deep(key, pc, t, size) else 0)
                    if new != self.ret_live[t] or new_esc != self.esc_live[t]:
                        self.ret_live[t], self.esc_live[t] = new, new_esc
                        changed = True
            if not changed:
                break
        else:
            raise RuntimeError("liveness did not converge")
        # 3. the values, given what the callers read
        for key, values in self.sym.items():
            ret, esc = self.ret_live[key[0]], self.esc_live[key[0]]
            self.live_in[key] = {pc: _eval(v, ret, esc) for pc, v in values.items()}
            c = an.contexts[key]
            self.func_live_in[key[0]] |= self.live_in[key].get(c.entry, ALL)

    def escaped_returns(self, key) -> set:
        """The return instructions of a context that return through its
        caller's frame: below the depth it was entered at."""
        out = set()
        for pc in self.an.contexts[key].insns:
            for _, insn in self._code(key, pc):
                if insn.name in ("RTS", "RTL"):
                    depth = self._depth_before(key, pc)
                    if depth is not None and depth < 0:
                        out.add(pc)
        return out

    def after_return(self, entry: int) -> int:
        """What may be read after the routine at entry returns, whichever way:
        to its caller, or (a routine that does) through its caller's frame."""
        if entry in self.all_live_returns:
            return ALL
        live = self.ret_live[entry]
        if any(self.escaped_returns(key) for key in self._entries_at(entry)):
            live |= self.esc_live[entry]
        return live

    def dead_at_return(self, entry: int) -> int:
        return ALL & ~self.after_return(entry)

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
        pc; None if none does. At a return instruction, what is read after
        that return: to the caller, or through the caller's frame."""
        keys = self.contexts_at(pc)
        if not keys:
            return None
        live = 0
        for key in keys:
            if key[0] in self.all_live_returns:
                return ALL
            rets = [i for _, i in self._code(key, pc) if i.name in ("RTS", "RTL")]
            if rets:
                depth = self._depth_before(key, pc)     # as the solve takes it
                live |= self.esc_live[key[0]] if depth is not None and depth < 0 else self.ret_live[key[0]]
            else:
                live |= self.after_return(key[0])
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
