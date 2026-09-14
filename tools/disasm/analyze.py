"""Recursive-descent code discovery with M/X width tracking.

Code is traced per *context*: a function entry address plus the (M, X) widths it
was entered with. A call continues in the caller with the widths the callee was
observed to return with, which is resolved to a fixpoint. When a callee is
never seen to return, the caller's widths are assumed and the call is reported.
"""

from collections import defaultdict
from dataclasses import dataclass, field

from .cpu import CONDITIONAL_BRANCHES, RETURNS, SUSPICIOUS, Insn, Mode, decode
from .hints import Hints
from .memmap import Rom, bus_to_offset, fmt

PSTACK_DEPTH = 8
MAX_TABLE_ENTRIES = 256

Widths = tuple  # (m, x), each True (8-bit), False (16-bit) or None (unknown)


@dataclass(frozen=True)
class State:
    m: bool | None
    x: bool | None
    c: bool | None = None               # carry, known only right after CLC/SEC/REP/SEP
    ps: tuple = ()                      # widths pushed by PHP within this context

    @property
    def widths(self) -> Widths:
        return (self.m, self.x)


@dataclass
class Context:
    entry: int
    widths: Widths
    kinds: set = field(default_factory=set)             # "jsr", "jsl", "vector", "hint", ...
    callers: set = field(default_factory=set)           # call-site bus addresses
    returns: set = field(default_factory=set)           # widths observed at RTS/RTL/RTI
    waiters: list = field(default_factory=list)         # continuations blocked on returns
    visited: set = field(default_factory=set)
    insns: set = field(default_factory=set)             # bus addresses traced in this context


@dataclass
class JumpTable:
    dispatcher: int
    table: int
    kind: str                  # "abs" (16-bit, program bank) or "long"
    call: bool                 # JSR (abs,X) vs JMP (abs,X)
    count: int | None = None
    source: str = "hint"       # "hint" or "heuristic"
    pending: list = field(default_factory=list)   # (ctx key, state) awaiting a length


class Analyzer:
    def __init__(self, rom: Rom, hints: Hints, assume_returns: bool = True, coverage: dict | None = None):
        self.rom = rom
        self.hints = hints
        self.assume_returns = assume_returns
        self.coverage = coverage or {}                   # executed addr -> {(m, x)}
        self.coverage_seeded: set = set()                # executed addrs static tracing missed
        self.coverage_pruned: set = set()                # (return addr, widths) contradicted by coverage
        self.contexts: dict[tuple, Context] = {}
        self.code: dict[int, dict[Widths, Insn]] = defaultdict(dict)
        self.labels: dict[int, set] = defaultdict(set)   # bus addr -> {"sub","loc","jt"}
        self.tables: dict[int, JumpTable] = {}           # keyed by dispatcher address
        self.xrefs: dict[int, set] = defaultdict(set)    # long operand target -> referencing pcs
        self.work: list = []
        self.assumed: set = set()                        # (callee ctx key, call site)
        # Diagnostics
        self.unresolved: dict[int, Insn] = {}
        self.width_unknown: set = set()
        self.suspicious: dict[int, Insn] = {}
        self.unmapped: set = set()
        self.forced_data: set = set()

    # ---- context management -------------------------------------------------

    def ctx(self, entry: int, widths: Widths, kind: str) -> Context:
        forced = self.hints.state_at.get(entry)
        if forced:
            widths = forced
        key = (entry, widths)
        c = self.contexts.get(key)
        if c is None:
            c = self.contexts[key] = Context(entry, widths)
            self.work.append((key, entry, State(*widths)))
        c.kinds.add(kind)
        self.labels[entry].add("sub")
        return c

    def add_return(self, key: tuple, widths: Widths):
        c = self.contexts[key]
        if widths in c.returns:
            return
        c.returns.add(widths)
        for w in list(c.waiters):
            self._wake(w, widths)

    def _wake(self, waiter: tuple, widths: Widths):
        if waiter[0] == "call":
            _, caller_key, resume_pc, st = waiter
            m = st.m if widths[0] is None else widths[0]
            x = st.x if widths[1] is None else widths[1]
            observed = self.coverage.get(resume_pc)
            if observed is not None and (m, x) not in observed:
                # The return address ran, but never with these widths: this
                # return path is a static over-approximation for this call site.
                self.coverage_pruned.add((resume_pc, (m, x)))
                return
            self.work.append((caller_key, resume_pc, State(m, x, None, st.ps)))
        else:
            _, caller_key = waiter
            self.add_return(caller_key, widths)

    def wait_on(self, callee: Context, waiter: tuple):
        override = self.hints.returns.get(callee.entry)
        if override:
            self._wake(waiter, override)
            return
        callee.waiters.append(waiter)
        for r in list(callee.returns):
            self._wake(waiter, r)

    # ---- tracing --------------------------------------------------------------

    def read(self, addr: int, n: int):
        return self.rom.read(addr, n)

    def run(self):
        for e in self.hints.entries:
            self.ctx(e.addr, (e.m, e.x), e.kind)
        self._fixpoint()
        # Executed code that static tracing didn't reach seeds new contexts, in
        # address order so each seed can absorb the code that follows it.
        for addr in sorted(self.coverage):
            if addr in self.code:
                continue
            self.coverage_seeded.add(addr)
            for widths in sorted(self.coverage[addr], key=str):
                self.ctx(addr, widths, "coverage")
            self._fixpoint()

    def _fixpoint(self):
        while True:
            while self.work:
                key, pc, st = self.work.pop()
                self.trace(key, pc, st)
            if self._resolve_heuristic_tables():
                continue
            if self.assume_returns and self._assume_returns():
                continue
            break

    def trace(self, key: tuple, pc: int, st: State):
        c = self.contexts[key]
        while True:
            forced = self.hints.state_at.get(pc)
            if forced and pc != c.entry:
                st = State(forced[0], forced[1], st.c, st.ps)
            vkey = (pc, st)
            if vkey in c.visited:
                return
            c.visited.add(vkey)

            if bus_to_offset(pc) is None:
                self.unmapped.add(pc)
                return
            if self.hints.is_data(pc):
                self.forced_data.add(pc)
                return
            insn = decode(self.read, pc, st.m, st.x)
            if insn is None:
                self.width_unknown.add(pc)
                return

            self.code[pc][st.widths] = insn
            c.insns.add(pc)
            if insn.name in SUSPICIOUS:
                self.suspicious[pc] = (insn, key)
                return

            nxt = self.step_state(insn, st)
            name, mode = insn.name, insn.mode

            if name in RETURNS:
                self.add_return(key, st.widths)
                return

            if name in CONDITIONAL_BRANCHES:
                t = insn.branch_target()
                self.labels[t].add("loc")
                self.work.append((key, t, nxt))
                pc, st = insn.next_pc, nxt
                continue

            if name in ("BRA", "BRL") or (name == "JMP" and mode is Mode.ABS):
                t = insn.branch_target() if name != "JMP" else (pc & 0xFF0000) | insn.operand
                self.labels[t].add("loc")
                pc, st = t, nxt
                continue

            if name == "JML" and mode is Mode.LONG:
                callee = self.ctx(insn.operand, nxt.widths, "jml")
                callee.callers.add(pc)
                self.wait_on(callee, ("tail", key))
                return

            if name in ("JSR", "JSL") and mode in (Mode.ABS, Mode.LONG):
                target = insn.operand if name == "JSL" else (pc & 0xFF0000) | insn.operand
                callee = self.ctx(target, nxt.widths, name.lower())
                callee.callers.add(pc)
                if target in self.hints.noreturn:
                    return
                resume = insn.next_pc + self.hints.inline_args.get(target, 0)
                self.wait_on(callee, ("call", key, resume, nxt))
                return

            if mode is Mode.ABSXIND:   # JMP (abs,X) / JSR (abs,X)
                self.dispatch_table(key, insn, nxt)
                return

            if mode in (Mode.ABSIND, Mode.ABSLIND):   # JMP (abs) / JML [abs]
                targets = self.hints.indirect.get(pc)
                if not targets:
                    self.unresolved[pc] = insn
                    return
                for t in targets:
                    callee = self.ctx(t, nxt.widths, "indirect")
                    callee.callers.add(pc)
                    self.wait_on(callee, ("tail", key))
                return

            if name == "STP":
                return

            if mode in (Mode.LONG, Mode.LONGX) and bus_to_offset(insn.operand) is not None:
                self.xrefs[insn.operand].add(pc)

            pc, st = insn.next_pc, nxt

    @staticmethod
    def step_state(insn: Insn, st: State) -> State:
        m, x, ps = st.m, st.x, st.ps
        c = None
        n = insn.name
        if n == "REP":
            if insn.operand & 0x20:
                m = False
            if insn.operand & 0x10:
                x = False
            if insn.operand & 0x01:
                c = False
        elif n == "SEP":
            if insn.operand & 0x20:
                m = True
            if insn.operand & 0x10:
                x = True
            if insn.operand & 0x01:
                c = True
        elif n == "CLC":
            c = False
        elif n == "SEC":
            c = True
        elif n == "PHP":
            ps = (ps + ((m, x),))[-PSTACK_DEPTH:]
        elif n == "PLP":
            if ps:
                (m, x), ps = ps[-1], ps[:-1]
            else:
                m = x = None
        elif n == "XCE":
            if st.c is True:        # entering emulation mode forces 8-bit
                m = x = True
        return State(m, x, c, ps)

    # ---- jump tables ------------------------------------------------------------

    def dispatch_table(self, key: tuple, insn: Insn, st: State):
        bank = insn.pc & 0xFF0000
        jt = self.tables.get(insn.pc)
        if jt is None:
            hint = self.hints.tables.get(insn.pc)
            jt = JumpTable(insn.pc, bank | insn.operand, "abs", insn.name == "JSR")
            if hint:
                jt.count, jt.kind = hint.count, hint.kind
                if hint.table is not None:
                    jt.table = hint.table
            else:
                jt.source = "heuristic"
            self.tables[insn.pc] = jt
            self.labels[jt.table].add("jt")
        if jt.count is None:
            # Resolved once the rest of the code is known; see _resolve_heuristic_tables.
            jt.pending.append((key, st))
            return
        self._follow_table(jt, key, st)

    def _follow_table(self, jt: JumpTable, key: tuple, st: State):
        step = 3 if jt.kind == "long" else 2
        bank = jt.dispatcher & 0xFF0000
        for i in range(jt.count):
            ent = jt.table + i * step
            if jt.kind == "long":
                t = self.rom.u24(ent)
            else:
                v = self.rom.u16(ent)
                t = None if v is None else bank | v
            if t is None:
                continue
            if jt.call:
                callee = self.ctx(t, st.widths, "table")
                callee.callers.add(jt.dispatcher)
                self.wait_on(callee, ("call", key, (jt.dispatcher & 0xFF0000) |
                                      ((jt.dispatcher + 3) & 0xFFFF), st))
            else:
                self.labels[t].add("loc")
                self.work.append((key, t, st))

    def _resolve_heuristic_tables(self) -> bool:
        pending = [jt for jt in self.tables.values() if jt.pending]
        if not pending:
            return False
        # Lengths only shrink as barriers are added, so iterate to a fixpoint:
        # every table's targets and start address bound every other table.
        unsized = [jt for jt in pending if jt.count is None]
        guesses = {id(jt): MAX_TABLE_ENTRIES for jt in unsized}
        while True:
            barriers = set(self.code) | {t.table for t in self.tables.values()}
            for t in self.tables.values():
                n = t.count if t.count is not None else guesses.get(id(t), 0)
                barriers.update(self._table_targets(t, n))
            changed = False
            for jt in unsized:
                n = self._guess_table_length(jt, barriers - {jt.table}, guesses[id(jt)])
                if n != guesses[id(jt)]:
                    guesses[id(jt)] = n
                    changed = True
            if not changed:
                break
        for jt in unsized:
            jt.count = guesses[id(jt)]
        for jt in pending:
            waiting, jt.pending = jt.pending, []
            for key, st in waiting:
                self._follow_table(jt, key, st)
        return True

    def _table_targets(self, jt: JumpTable, n: int) -> list:
        bank = jt.dispatcher & 0xFF0000
        out = []
        for i in range(n):
            if jt.kind == "long":
                t = self.rom.u24(jt.table + i * 3)
            else:
                v = self.rom.u16(jt.table + i * 2)
                t = None if v is None else bank | v
            if t is not None:
                out.append(t)
        return out

    def _guess_table_length(self, jt: JumpTable, barriers: set, limit: int) -> int:
        bank = jt.dispatcher & 0xFF0000
        state = next(iter(jt.pending))[1]
        n = 0
        while n < limit:
            ent = jt.table + n * 2
            if n and (ent in barriers or ent + 1 in barriers):
                break
            v = self.rom.u16(ent)
            if v is None:
                break
            t = bank | v
            if jt.table <= t < jt.table + (n + 1) * 2:
                break
            if not self._plausible_code(t, state):
                break
            n += 1
        return n

    def _plausible_code(self, pc: int, st: State, depth: int = 6) -> bool:
        """Cheap check that a few instructions linearly decode without data-like opcodes."""
        if pc in self.code:
            return True
        for _ in range(depth):
            if bus_to_offset(pc) is None or self.hints.is_data(pc):
                return False
            insn = decode(self.read, pc, st.m, st.x)
            if insn is None or insn.name in SUSPICIOUS:
                return False
            if insn.name in RETURNS or insn.name in ("JMP", "JML", "BRA", "BRL"):
                return True
            st = self.step_state(insn, st)
            pc = insn.next_pc
        return True

    def _assume_returns(self) -> bool:
        """For calls whose callee never returned, continue with the caller's widths."""
        progressed = False
        for ckey, c in list(self.contexts.items()):
            if c.returns or c.entry in self.hints.noreturn or c.entry in self.hints.returns:
                continue
            for w in c.waiters:
                if w[0] != "call":
                    continue
                marker = (ckey, w[2])
                if marker in self.assumed:
                    continue
                self.assumed.add(marker)
                st = w[3]
                self.work.append((w[1], w[2], State(st.m, st.x, None, st.ps)))
                progressed = True
        return progressed

    # ---- queries ----------------------------------------------------------------

    def covered_offsets(self) -> set:
        cov = set()
        for pc, variants in self.code.items():
            off = bus_to_offset(pc)
            size = max(i.size for i in variants.values())
            cov.update(range(off, off + size))
        return cov

    def coverage_conflicts(self) -> dict:
        """Executed addresses where static tracing decoded an instruction length
        that differs from the length under the widths actually observed."""
        out = {}
        for addr, observed in self.coverage.items():
            variants = self.code.get(addr)
            if not variants:
                continue
            sizes = set()
            for m, x in observed:
                insn = decode(self.read, addr, m, x)
                if insn:
                    sizes.add(insn.size)
            wrong = {w: i for w, i in variants.items() if i.size not in sizes}
            if wrong:
                out[addr] = (observed, wrong)
        return out

    def width_conflicts(self) -> dict:
        return {pc: v for pc, v in self.code.items()
                if len({i.size for i in v.values()}) > 1}

    def label(self, addr: int) -> str | None:
        name = self.hints.names.get(addr)
        if name:
            return name
        kinds = self.labels.get(addr)
        if not kinds:
            return None
        prefix = "sub" if "sub" in kinds else "jt" if "jt" in kinds else "loc"
        return f"{prefix}_{addr:06X}"


def describe_widths(w: Widths) -> str:
    def f(v, name):
        return f"{name}?" if v is None else f"{name}8" if v else f"{name}16"
    return f"{f(w[0], 'm')} {f(w[1], 'x')}"


__all__ = ["Analyzer", "describe_widths", "fmt"]
