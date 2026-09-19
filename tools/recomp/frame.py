"""Routine graphs, and what D and DB are at each instruction (tools/recomp/show.py).

A draft names a direct-page access only when it knows D, and an absolute one
only when it knows DB. Neither is in the analysis (tools/disasm/analyze.py
tracks widths), so this module works them out:

- **Within a routine**, a forward pass over its instructions follows D, DB, the
  accumulator's bytes (for TCD) and the bytes it pushes, so `LDA #$1D00 : TCD`,
  `PEA #$0300 : PLD`, `PHK : PLB`, `PEA #$7E7E : PLB : PLB` and a PHD/PLD pair
  around a stretch all give known values, and everything else unknown. A call
  keeps them unless the callee's own returns show it sets them (a summary per
  callee, computed the same way, from the returns that come back to the
  caller: one made after the callee pulled its caller's frame goes further up).
- **At the entry**, D and DB are what every caller has at its call site: the
  static call sites the analysis found (and, with call logs, the ones ct_dev
  saw), each worked out in its caller the same way, recursively. An entry the
  analysis reaches as an interrupt vector, a coverage seed or a hint has callers
  nobody can see, so its D and DB are unknown; so is any caller whose own entry
  is, or a call site in no traced routine.

The result is sound under the analysis: a value is "known" only where every
path the analysis traced agrees. Nothing here reads RAM or knows the RAM map.
"""

from dataclasses import dataclass, field, replace

from tools.disasm.analyze import Analyzer, State
from tools.disasm.cpu import CONDITIONAL_BRANCHES, RETURNS, SUSPICIOUS, Mode
from tools.disasm.liveness import stack_effect

D0, DB0 = "D0", "DB0"      # the entry's D and DB, symbolically


# ---- graphs -----------------------------------------------------------------------------------


@dataclass
class Graph:
    """One analysis context: its instructions keyed by (pc, m, x) and the
    transfers between them. `calls` maps a call's node to its targets (24-bit;
    empty for a table or pointer call) and resume node."""
    key: tuple
    entry: tuple
    insns: dict = field(default_factory=dict)       # node -> Insn
    succ: dict = field(default_factory=dict)        # node -> [node]
    calls: dict = field(default_factory=dict)       # node -> ([targets], resume node or None)
    exits: dict = field(default_factory=dict)       # node -> [pc it leaves to] (targets not traced here)


def _widths_after_call(an: Analyzer, target: int, st: State):
    override = an.hints.returns.get(target)
    rets = set()
    if override:
        rets.add(override)
    else:
        for key, c in an.contexts.items():
            if key[0] == target and key[1] == st.widths or (key[0] == target and not rets):
                rets |= c.returns
    out = set()
    for w in rets:
        out.add((st.m if w[0] is None else w[0], st.x if w[1] is None else w[1]))
    return out


def build_graph(an: Analyzer, key: tuple) -> Graph:
    c = an.contexts[key]
    g = Graph(key, (c.entry,) + tuple(key[1]))
    states: dict = {}
    for pc, st in c.visited:
        if pc in an.code and st.widths in an.code[pc]:
            states.setdefault((pc,) + tuple(st.widths), []).append(st)
    at = {}
    for node in states:
        at.setdefault(node[0], []).append(node)
        g.insns[node] = an.code[node[0]][node[1:]]

    def find(pc: int, widths_set) -> list:
        nodes = at.get(pc, [])
        exact = [n for n in nodes if n[1:] in widths_set]
        return exact or (nodes if len(nodes) == 1 else [])

    for node, sts in states.items():
        insn = g.insns[node]
        pc, name, mode = node[0], insn.name, insn.mode
        bank = pc & 0xFF0000
        out, leave = [], []

        def go(t, widths_set):
            found = find(t, widths_set)
            if found:
                out.extend(found)
            else:
                leave.append(t)

        nxt = {Analyzer.step_state(insn, st).widths for st in sts}
        if name in RETURNS or name in SUSPICIOUS or name == "STP":
            pass
        elif name in CONDITIONAL_BRANCHES:
            go(insn.branch_target(), nxt)
            go(insn.next_pc, nxt)
        elif name in ("BRA", "BRL"):
            go(insn.branch_target(), nxt)
        elif name == "JMP" and mode is Mode.ABS:
            go(bank | insn.operand, nxt)
        elif name == "JML" and mode is Mode.LONG:
            leave.append(insn.operand)
        elif name in ("JSR", "JSL") and mode in (Mode.ABS, Mode.LONG):
            target = insn.operand if name == "JSL" else bank | insn.operand
            targets = list(an.hints.indirect.get(pc, [target])) if pc not in an.hints.derive else \
                list(an.hints.indirect.get(pc, [])) + sorted(an.derived[pc])
            resume = None
            if not all(t in an.hints.noreturn for t in targets):
                skip = an.hints.inline_args.get(targets[0], 0) if targets else 0
                rpc = bank | ((insn.next_pc + skip) & 0xFFFF)
                widths = set()
                for t in targets:
                    for st in sts:
                        widths |= _widths_after_call(an, t, Analyzer.step_state(insn, st))
                found = find(rpc, widths)
                resume = found[0] if len(found) == 1 else None
                if resume is not None:
                    out.append(resume)
            g.calls[node] = (targets, resume)
        elif mode is Mode.ABSXIND:
            jt = an.tables.get(pc)
            if insn.name == "JSR":
                rpc = insn.next_pc
                widths = set()
                table = an._table_targets(jt, jt.count) + list(jt.extra) if jt is not None and jt.count else []
                for t in table:
                    for st in sts:
                        widths |= _widths_after_call(an, t, Analyzer.step_state(insn, st))
                found = find(rpc, widths)
                resume = found[0] if len(found) == 1 else None
                if resume is not None:
                    out.append(resume)
                g.calls[node] = (table, resume)
            elif jt is not None and jt.count:
                for t in an._table_targets(jt, jt.count) + list(jt.extra):
                    go(t, nxt)
        elif mode in (Mode.ABSIND, Mode.ABSLIND):
            pass
        else:
            go(insn.next_pc, nxt)
        g.succ[node] = list(dict.fromkeys(out))
        if leave:
            g.exits[node] = leave
    return g


# ---- the state -------------------------------------------------------------------------------
#
# D and DB are sets of the values they can have -- ints, or D0/DB0 for the
# entry's own -- or None when nothing is known (more than MAX_VALUES values, or
# a computed one). A set survives a callee that returns with D reset on one path
# and kept on another: in a caller that already had that D, it is one value.
# Accumulator and stack bytes are an int, ("D", set, 0 or 1) or ("DB", set)
# for a byte of a D or DB, or None. The stack is a tuple of bytes (top last),
# or None when not followed.

MAX_VALUES = 4


def vset(*vals):
    return frozenset(vals)


def union(a, b):
    if a is None or b is None:
        return None
    u = a | b
    return u if len(u) <= MAX_VALUES else None


def resolve(vals, entry):
    """vals with D0/DB0 replaced by the entry's set."""
    if vals is None:
        return None
    out = frozenset()
    for v in vals:
        if v in (D0, DB0):
            if entry is None:
                return None
            out = out | entry
        else:
            out = out | {v}
    return out if len(out) <= MAX_VALUES else None


def single(vals):
    """The one int in vals, else None."""
    if vals is not None and len(vals) == 1:
        v = next(iter(vals))
        return v if isinstance(v, int) else None
    return None


@dataclass(frozen=True)
class Frame:
    d: object = frozenset({D0})
    db: object = frozenset({DB0})
    al: object = None
    ah: object = None
    stack: tuple | None = ()
    x: int | None = None          # the index registers, when an immediate loaded them
    y: int | None = None


def _word(lo, hi):
    if isinstance(lo, int) and isinstance(hi, int):
        return vset(hi << 8 | lo)
    if isinstance(lo, tuple) and isinstance(hi, tuple) and lo[0] == hi[0] == "D" and lo[1] == hi[1] \
            and (lo[2], hi[2]) == (0, 1):
        return lo[1]
    return None


def _bytes(d):
    v = single(d)
    if v is not None:
        return v & 0xFF, v >> 8 & 0xFF
    if d is None:
        return None, None
    return ("D", d, 0), ("D", d, 1)


def _db_byte(db):
    v = single(db)
    if v is not None:
        return v
    return None if db is None else ("DB", db)


def _db_of(b):
    if isinstance(b, int):
        return vset(b)
    if isinstance(b, tuple) and b[0] == "DB":
        return b[1]
    return None


def meet(a: Frame, b: Frame) -> Frame:
    def m(x, y):
        return x if x == y else None
    if a.stack is None or b.stack is None or len(a.stack) != len(b.stack):
        stack = None
    else:
        stack = tuple(m(x, y) for x, y in zip(a.stack, b.stack))
    return Frame(union(a.d, b.d), union(a.db, b.db), m(a.al, b.al), m(a.ah, b.ah), stack, m(a.x, b.x), m(a.y, b.y))


def _push(f: Frame, *bs) -> Frame:
    if f.stack is None:
        return f
    return replace(f, stack=f.stack + tuple(bs))


def _pull(f: Frame, n: int):
    """(bytes pulled, lowest address first, the frame after)."""
    if f.stack is None or len(f.stack) < n:
        return [None] * n, replace(f, stack=None if f.stack is None else ())
    return list(reversed(f.stack[-n:])), replace(f, stack=f.stack[:-n])


def _index_bytes(v, wide):
    if v is None:
        return [None] * wide
    return [v >> 8 & 0xFF, v & 0xFF] if wide == 2 else [v & 0xFF]


INDEX_WRITES = {"LDX": "x", "LDY": "y", "INX": "x", "DEX": "x", "INY": "y", "DEY": "y", "TAX": "x",
                "TAY": "y", "TSX": "x", "TXY": "y", "TYX": "x", "PLX": "x", "PLY": "y"}


def step(f: Frame, insn, m8, x8, call_effect=None) -> Frame:
    """The frame after one instruction (m8/x8 its widths, None if unknown).
    call_effect: a call's (D, DB) after it, already resolved against f."""
    n, mode, v = insn.name, insn.mode, insn.operand
    if n in INDEX_WRITES or n in ("JSR", "JSL", "MVN", "MVP") or (n == "SEP" and v & 0x10):
        g = _index_step(f, insn, x8)
        if n not in ("JSR", "JSL", "MVN", "MVP", "PLX", "PLY", "TAX", "TAY"):
            return g
        f = g
    d, db, al, ah = f.d, f.db, f.al, f.ah
    wide_a = 1 if m8 else (2 if m8 is False else None)
    wide_x = 1 if x8 else (2 if x8 is False else None)
    if n == "LDA" and mode is Mode.IMM_M:
        al = v & 0xFF
        if m8 is False:
            ah = v >> 8
        return replace(f, al=al, ah=ah)
    if n == "TDC":
        al, ah = _bytes(d)
        return replace(f, al=al, ah=ah)
    if n == "TCD":
        return replace(f, d=_word(al, ah))
    if n == "XBA":
        return replace(f, al=ah, ah=al)
    if n in ("TXA", "TYA"):
        src = f.x if n == "TXA" else f.y
        if src is None or m8 is None:
            return replace(f, al=None, ah=None if m8 is not True else ah)
        return replace(f, al=src & 0xFF, ah=ah if m8 else src >> 8)
    if n == "PHA":
        if wide_a is None:
            return replace(f, stack=None)
        return _push(f, *((ah, al) if wide_a == 2 else (al,)))
    if n == "PHD":
        lo, hi = _bytes(d)
        return _push(f, hi, lo)
    if n == "PHB":
        return _push(f, _db_byte(db))
    if n == "PHK":
        return _push(f, insn.pc >> 16)
    if n == "PEA":
        return _push(f, v >> 8, v & 0xFF)
    if n in ("PEI", "PER"):
        return _push(f, None, None)
    if n in ("PHX", "PHY"):
        if wide_x is None:
            return replace(f, stack=None)
        return _push(f, *_index_bytes(f.x if n == "PHX" else f.y, wide_x))
    if n == "PHP":
        return _push(f, None)
    if n == "PLA":
        if wide_a is None:
            return replace(f, al=None, ah=None, stack=None)
        got, f2 = _pull(f, wide_a)
        return replace(f2, al=got[0], ah=got[1] if wide_a == 2 else ah)
    if n == "PLD":
        got, f2 = _pull(f, 2)
        return replace(f2, d=_word(got[0], got[1]))
    if n == "PLB":
        got, f2 = _pull(f, 1)
        return replace(f2, db=_db_of(got[0]))
    if n in ("PLX", "PLY"):
        if wide_x is None:
            return replace(f, stack=None)
        got, f2 = _pull(f, wide_x)
        val = None
        if all(isinstance(b, int) for b in got):
            val = got[0] | (got[1] << 8 if wide_x == 2 else 0)
        return replace(f2, **{n[2].lower(): val})
    if n == "PLP":
        _, f2 = _pull(f, 1)
        return f2
    if n in ("TCS", "TXS"):
        return replace(f, stack=None)
    if n in ("MVN", "MVP"):
        return replace(f, db=vset(v & 0xFF), al=None, ah=None)
    if n in ("JSR", "JSL"):
        d2, db2 = call_effect if call_effect is not None else (None, None)
        return replace(f, d=d2, db=db2, al=None, ah=None)
    if n in ("TAX", "TAY"):
        return f
    # everything else that writes A leaves it unknown
    if n in ("LDA", "ADC", "SBC", "AND", "ORA", "EOR", "TSC") or \
            (mode is Mode.ACC and n in ("ASL", "LSR", "ROL", "ROR", "INC", "DEC")):
        return replace(f, al=None, ah=None if m8 is not True else ah)
    return f


def _index_step(f: Frame, insn, x8) -> Frame:
    """What an instruction does to the index registers' known values."""
    n, mode, v = insn.name, insn.mode, insn.operand
    if n in ("JSR", "JSL", "MVN", "MVP"):
        return replace(f, x=None, y=None)
    if n == "SEP":
        return replace(f, x=None if f.x is None else f.x & 0xFF, y=None if f.y is None else f.y & 0xFF)
    reg = INDEX_WRITES[n]
    val = None
    if n in ("LDX", "LDY") and mode is Mode.IMM_X:
        val = v
    elif n in ("INX", "INY", "DEX", "DEY"):
        old = f.x if reg == "x" else f.y
        if old is not None and x8 is not None:
            val = (old + (1 if n[0] == "I" else -1)) & (0xFF if x8 else 0xFFFF)
    elif n in ("TAX", "TAY"):
        if isinstance(f.al, int) and (x8 or isinstance(f.ah, int)):
            val = f.al if x8 else f.ah << 8 | f.al
    elif n == "TXY":
        val = f.x
    elif n == "TYX":
        val = f.y
    return replace(f, **{reg: val})


def return_depths(g: Graph) -> dict | None:
    """node -> bytes pushed since the routine's entry, for every instruction
    (calls taken to return where they were made); None where S can't be
    followed that way. A return at a depth other than 0 isn't to the caller."""
    depth = {g.entry: 0}
    work = [g.entry]
    while work:
        node = work.pop()
        insn = g.insns[node]
        if insn.name in ("TCS", "TXS", "XCE"):
            return None
        eff = stack_effect(insn, node[1:])
        if eff is None:
            return None
        for succ in g.succ.get(node, []):
            d = depth[node] + eff
            if succ not in depth:
                depth[succ] = d
                work.append(succ)
            elif depth[succ] != d:
                return None
    return depth


def resolve_in(vals, here, sym):
    """A callee's outcome set with `sym` (what the caller had) replaced by here."""
    if vals is None:
        return None
    out = frozenset()
    for v in vals:
        if v == sym:
            if here is None:
                return None
            out = out | here
        else:
            out = out | {v}
    return out if len(out) <= MAX_VALUES else None


class Frames:
    """D and DB at every instruction of the analysis's routines, and at their
    entries from their callers. `dynamic_calls` is target -> call sites (ct_dev
    --calls); `overrides` maps an entry to (d, db) given by hand (None: work it
    out)."""

    MAX_DEPTH = 24

    def __init__(self, an: Analyzer, dynamic_calls: dict | None = None):
        self.an = an
        self.dynamic = dynamic_calls or {}
        self._graphs: dict = {}
        self._frames: dict = {}
        self._summary: dict = {}
        self._entry: dict = {}
        self._busy: set = set()           # entries being worked out
        self._summing: set = set()        # callee summaries being worked out
        self.containing: dict = {}
        self.dead: set = set()            # instructions only code nothing runs has
        for key, c in an.contexts.items():
            if c.kinds <= {"show", "unreached"}:
                # made up to draft from, or code nothing runs: no call sites
                self.dead |= c.insns if c.kinds <= {"unreached"} else set()
                continue
            for pc in c.insns:
                self.containing.setdefault(pc, []).append(key)
        self.dead -= set(self.containing)

    def graph(self, key) -> Graph:
        g = self._graphs.get(key)
        if g is None:
            g = self._graphs[key] = build_graph(self.an, key)
        return g

    def _callee_key(self, target: int, m8, x8):
        forced = self.an.hints.state_at.get(target)
        w = forced or (m8, x8)
        if (target, w) in self.an.contexts:
            return (target, w)
        keys = [k for k in self.an.contexts if k[0] == target]
        return keys[0] if len(keys) == 1 else None

    def summary(self, target: int, m8, x8):
        """(Ds, DBs) a call can leave, D0/DB0 standing for the caller's own."""
        key = self._callee_key(target, m8, x8)
        if key is None:
            return (None, None)
        if key in self._summary:
            return self._summary[key]
        if key in self._summing:
            # recursion: assume the call keeps them, as the non-recursive paths decide
            return (vset(D0), vset(DB0))
        self._summing.add(key)
        frames = self.frames(key)
        g = self.graph(key)
        depth = return_depths(g)
        ds, dbs = frozenset(), frozenset()
        for node, insn in g.insns.items():
            if insn.name in ("RTS", "RTL", "RTI") and node in frames:
                if depth is not None and depth.get(node, 0) != 0:
                    continue      # returns past its caller (it unwound the caller's frame), not to it
                ds = union(ds, frames[node].d)
                dbs = union(dbs, frames[node].db)
        self._summing.discard(key)
        self._summary[key] = (vset(D0) if ds == frozenset() else ds, vset(DB0) if dbs == frozenset() else dbs)
        return self._summary[key]

    def frames(self, key) -> dict:
        """node -> Frame before the instruction, with the entry's D and DB symbolic."""
        if key in self._frames:
            return self._frames[key]
        g = self.graph(key)
        into = {g.entry: Frame()}
        work = [g.entry]
        out = {}
        while work:
            node = work.pop()
            f = into[node]
            insn = g.insns[node]
            effect = None
            if node in g.calls:
                targets, _ = g.calls[node]
                ds, dbs = frozenset(), frozenset()
                for t in targets:
                    e = self.summary(t, node[1], node[2])
                    ds = union(ds, resolve_in(e[0], f.d, D0))
                    dbs = union(dbs, resolve_in(e[1], f.db, DB0))
                if not targets:
                    ds = dbs = None
                effect = (ds, dbs)
            after = step(f, insn, node[1], node[2], effect)
            out[node] = f
            for s in g.succ.get(node, []):
                old = into.get(s)
                new = after if old is None else meet(old, after)
                if new != old:
                    into[s] = new
                    work.append(s)
        self._frames[key] = out
        return out

    # ---- entries ------------------------------------------------------------------------------
    #
    # Every routine's entry D and DB, solved together: a routine's are the union
    # of what its callers have at their call sites, and those depend on the
    # callers' own entries. It starts from nothing known about any routine and
    # only adds values, so it converges on the least solution: a value reaches a
    # routine only along a chain of calls from somewhere D and DB are set. A
    # routine no chain reaches (only other unknown routines call it) ends up
    # unknown, as does anything an interrupt, a coverage seed or a hint enters.

    def _site_facts(self, key):
        """[(caller key, D at the site, DB at the site)] symbolic in the caller's
        entry, or None when a call site is in no traced routine."""
        c = self.an.contexts[key]
        facts = []
        for site in sorted(set(c.callers) | set(self.dynamic.get(c.entry, ()))):
            callers = self.containing.get(site, [])
            if not callers:
                if site in self.dead:
                    continue            # a call nothing makes
                return None
            for ck in callers:
                fr = self.frames(ck)
                here = [f for n, f in fr.items() if n[0] == site]
                if not here:
                    return None
                f = here[0]
                for h in here[1:]:
                    f = meet(f, h)
                facts.append((ck, f.d, f.db))
        return facts

    def solve_entries(self):
        keys = [k for k, c in self.an.contexts.items() if not c.kinds <= {"show", "unreached"}]
        facts, why = {}, {}
        val = {}
        for k in keys:
            c = self.an.contexts[k]
            unseen = c.kinds & {"vector", "coverage", "hint"}
            f = None if unseen else self._site_facts(k)
            if unseen:
                why[k] = f"entered as {', '.join(sorted(unseen))}"
            elif f is None:
                why[k] = "called from code the analysis didn't trace"
            elif not f:
                why[k] = "no callers"
            else:
                sites = len(set(c.callers) | set(self.dynamic.get(c.entry, ())))
                why[k] = f"{sites} call site{'s' if sites != 1 else ''}"
            facts[k] = f
            val[k] = (None, None) if not f else (frozenset(), frozenset())
        changed = True
        while changed:
            changed = False
            for k in keys:
                f = facts[k]
                if not f:
                    continue
                ds, dbs = frozenset(), frozenset()
                for ck, cd, cdb in f:
                    ed, edb = val.get(ck, (None, None))
                    ds = union(ds, _resolve_partial(cd, ed))
                    dbs = union(dbs, _resolve_partial(cdb, edb))
                if (ds, dbs) != val[k]:
                    val[k] = (ds, dbs)
                    changed = True
        for k in keys:
            ds, dbs = val[k]
            # nothing reached it from a routine whose D is known: unknown
            ds = None if ds == frozenset() else ds
            dbs = None if dbs == frozenset() else dbs
            w = why[k]
            if ds is not None and single(ds) is None:
                w += ", D " + "/".join(f"${v:04X}" for v in sorted(v for v in ds if isinstance(v, int)))
            self._entry[k] = (ds, dbs, w)
        self._solved = True

    def entry(self, key) -> tuple:
        """(Ds, DBs, why): the sets of D and DB the entry is called with (None:
        unknown), and a few words on where they came from."""
        if not getattr(self, "_solved", False):
            self.solve_entries()
        if key in self._entry:
            return self._entry[key]
        return self._inside(key)

    def _inside(self, key) -> tuple:
        """A draft that starts inside routines (a loop head, a shared tail): D and
        DB there, in every routine the analysis traced through it."""
        pc = key[0]
        ds, dbs = frozenset(), frozenset()
        n = 0
        for ck in self.containing.get(pc, []):
            fr = self.frames(ck)
            for node, f in fr.items():
                if node[0] != pc:
                    continue
                n += 1
                ed, edb, _ = self.entry(ck)
                ds, dbs = union(ds, resolve(f.d, ed)), union(dbs, resolve(f.db, edb))
        if not n:
            return (None, None, "not inside any routine the analysis traced")
        res = (ds, dbs, f"inside {len(self.containing.get(pc, []))} routine(s)")
        self._entry[key] = res
        return res


def _resolve_partial(vals, entry):
    """resolve() where the entry may still be the empty set (nothing known yet):
    D0 then contributes nothing rather than making the result unknown."""
    if vals is None or entry is None:
        return None if (vals is None or D0 in vals or DB0 in vals) else vals
    return resolve(vals, entry)
