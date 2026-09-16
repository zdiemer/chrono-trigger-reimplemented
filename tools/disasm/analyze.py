"""Recursive-descent code discovery with M/X width tracking.

Code is traced per *context*: a function entry address plus the (M, X) widths it
was entered with. A call continues in the caller with the widths the callee was
observed to return with, which is resolved to a fixpoint. When a callee is
never seen to return, the caller's widths are assumed and the call is reported.
"""

from collections import defaultdict
from dataclasses import dataclass, field, replace

from .cpu import BRANCHES as BRANCH_NAMES, CONDITIONAL_BRANCHES, RETURNS, SUSPICIOUS, Insn, Mode, decode
from .hints import Hints
from .memmap import Rom, bus_to_offset, fmt
from .ram import RamImages
from .ram import build as build_ram
from .resources import expand as expand_resources

PSTACK_DEPTH = 8
# Instructions that push or pull, or move the stack: a PER followed by one of
# these no longer has its address on top.
STACK_OPS = {"PHA", "PHX", "PHY", "PHB", "PHD", "PHK", "PHP", "PLA", "PLX", "PLY", "PLB", "PLD", "PLP", "PEA",
             "PEI", "JSR", "JSL", "RTS", "RTL", "RTI", "TCS", "TXS", "BRK", "COP"}
MAX_TABLE_ENTRIES = 256

Widths = tuple  # (m, x), each True (8-bit), False (16-bit) or None (unknown)


@dataclass(frozen=True)
class State:
    m: bool | None
    x: bool | None
    c: bool | None = None               # carry, known only right after CLC/SEC/REP/SEP
    ps: tuple = ()                      # widths pushed by PHP within this context
    per: int | None = None              # a return address pushed by PER, nothing pushed or pulled since

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
    extra: list = field(default_factory=list)     # targets a hint lists for a table filled at run time
    pending: list = field(default_factory=list)   # (ctx key, state) awaiting a length


class Analyzer:
    def __init__(self, rom: Rom, hints: Hints, assume_returns: bool = True, coverage: dict | None = None,
                 prune_returns: bool = False, ram: RamImages | None = None):
        self.rom = rom
        self.hints = hints
        # [[resource]] hints expand into typed data regions, once per Hints object.
        self.resources = expand_resources(hints, rom.data)
        # Code run from RAM ([[ramcode]] hints) is traced from its images.
        self.ram = ram if ram is not None else build_ram(rom.data, hints)
        self.assume_returns = assume_returns
        self.prune_returns = prune_returns
        self.coverage = coverage or {}                   # executed addr -> {(m, x)}
        self.coverage_seeded: set = set()                # executed addrs static tracing missed
        self.coverage_unobserved: set = set()            # (return addr, widths) the coverage never ran
        self.coverage_pruned: set = set()                # the same, dropped (prune_returns only)
        self.contexts: dict[tuple, Context] = {}
        self.code: dict[int, dict[Widths, Insn]] = defaultdict(dict)
        self.labels: dict[int, set] = defaultdict(set)   # bus addr -> {"sub","loc","jt"}
        self.tables: dict[int, JumpTable] = {}           # keyed by dispatcher address
        self.xrefs: dict[int, set] = defaultdict(set)    # long operand target -> referencing pcs
        self.work: list = []
        self.assumed: set = set()                        # (callee ctx key, call site)
        # Targets derived from data the code interprets (`derive` on indirect and
        # jumptable hints, tools/disasm/derive.py), and the states each such site
        # was traced with, so targets found later are followed from all of them.
        self.derived: dict[int, set] = defaultdict(set)
        self.site_states: dict[int, list] = defaultdict(list)
        self.derivations: dict = {}                      # deriver name -> its result (reports, checks)
        self.last_derived: dict = {}                     # the targets the last derivation gave
        self.resumes: set = set()                        # where indirect calls return (JSR operands, PER)
        self.derive_unstable = False                     # rounds didn't reach a fixpoint
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
                # The return address ran, but never with these widths. That is
                # either a path the movies didn't take or a static
                # over-approximation; `returns` and `state` hints decide which.
                # Pruning on coverage alone is only an exploration aid.
                self.coverage_unobserved.add((resume_pc, (m, x)))
                if self.prune_returns:
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

    def u16(self, addr: int) -> int | None:
        b = self.read(addr, 2)
        return None if b is None else b[0] | b[1] << 8

    def u24(self, addr: int) -> int | None:
        b = self.read(addr, 3)
        return None if b is None else b[0] | b[1] << 8 | b[2] << 16

    def read(self, addr: int, n: int):
        if bus_to_offset(addr) is None:
            return self.ram.read(addr, n)
        return self.rom.read(addr, n)

    MAX_DERIVE_ROUNDS = 8

    def run(self):
        # Derivations read the code found so far, which a partial code set can
        # mislead (a script command whose pointer effect is in code not traced
        # yet). So the analysis runs in rounds: each starts from nothing with the
        # targets the previous round derived last, until a round derives exactly
        # the targets it started from.
        seed = {}
        for rnd in range(self.MAX_DERIVE_ROUNDS):
            if rnd:
                self.__init__(self.rom, self.hints, self.assume_returns, self.coverage, self.prune_returns, self.ram)
            self.derived = defaultdict(set, {k: set(v) for k, v in seed.items()})
            self._run_static()
            self.derive_rounds = rnd + 1
            final = {k: set(v) for k, v in self.last_derived.items() if v}
            if final == {k: v for k, v in seed.items() if v}:
                break
            seed = final
        else:
            self.derive_unstable = True
        # Executed code that static tracing didn't reach seeds new contexts, in
        # address order so each seed can absorb the code that follows it.
        for addr in sorted(self.coverage):
            if addr in self.code:
                continue
            self.coverage_seeded.add(addr)
            for widths in sorted(self.coverage[addr], key=str):
                self.ctx(addr, widths, "coverage")
            self._fixpoint()

    def _run_static(self):
        for e in self.hints.entries:
            self.ctx(e.addr, (e.m, e.x), e.kind)
        self._hardware_vectors()
        self._fixpoint()
        self._derive()
        while self._patch_entries():
            self._fixpoint()
            self._derive()

    # Vectors in the header ($00:FFE4-FFFF): native NMI and IRQ are entered from
    # any code with any widths, reset in emulation mode (8-bit). COP, BRK and
    # ABORT are only entered by the instructions or hardware that raise them;
    # the analysis stops at BRK and COP and lists them (`suspicious`) instead.
    NATIVE_VECTORS = (0x00FFEA, 0x00FFEE)
    RESET_VECTOR = 0x00FFFC

    def _hardware_vectors(self):
        if not self.hints.header_vectors:
            return
        for at, widths in [(a, (None, None)) for a in self.NATIVE_VECTORS] + [(self.RESET_VECTOR, (True, True))]:
            v = self.rom.u16(at) if hasattr(self.rom, "u16") else None
            if v not in (None, 0x0000, 0xFFFF):
                self.ctx(v, widths, "vector")

    def _patch_entries(self) -> bool:
        """Code a `patch` section puts over a program's instructions for a while:
        its instructions run with the widths the program's code has at the
        patch's base, so an instruction start only the patch has, the address
        after the patch, and the targets of its branches and jumps are entries."""
        from .cpu import decode as _decode
        added = False
        for sec in self.ram.sections:
            if not sec.patch:
                continue
            variants = self.code.get(sec.base, {})
            read = (lambda s: lambda a, n: s.image[a - s.base:a - s.base + n]
                    if 0 <= a - s.base and a - s.base + n <= len(s.image) else None)(sec)
            for widths in sorted(variants, key=str):
                st = State(*widths)
                pc = sec.base
                while sec.base <= pc < sec.end:
                    insn = _decode(read, pc, st.m, st.x)
                    if insn is None:
                        break
                    targets = []
                    if pc not in self.code:
                        targets.append(pc)
                    if insn.name in BRANCH_NAMES:
                        targets.append(insn.branch_target())
                    elif insn.name == "JMP" and insn.mode is Mode.ABS:
                        targets.append((pc & 0xFF0000) | insn.operand)
                    st = self.step_state(insn, st)
                    for t in targets:
                        if (t, st.widths if t != pc else widths) not in self.contexts:
                            self.ctx(t, st.widths if t != pc else widths, "patch")
                            added = True
                    if insn.name in ("BRA", "BRL", "JMP", "RTS", "RTL"):
                        break
                    pc = insn.next_pc
        return added

    def _derive(self):
        """Closure over data the code interprets: derivers read the code found so
        far (call arguments) and the data (scripts), and give targets for sites;
        new targets are traced, which can give derivers new arguments, until
        nothing changes."""
        if not self.hints.derive:
            return
        from .derive import derive_all
        while True:
            added = False
            self.last_derived = derive_all(self)
            for site, targets in self.last_derived.items():
                new = set(targets) - self.derived[site]
                if not new:
                    continue
                added = True
                self.derived[site] |= new
                for state in list(self.site_states[site]):
                    self._follow_indirect(site, state, sorted(new))
            if not added:
                return
            self._fixpoint()

    def _follow_indirect(self, site: int, state: tuple, targets):
        """Transfers from an indirect site traced with `state`: ("call", key, resume, st, kind),
        ("tail", key, st) or ("table", key, st, jt)."""
        if state[0] == "table":
            _, key, st, jt = state
            self._follow_targets(jt, key, st, targets)
            return
        if state[0] == "call":
            _, key, resume, st, kind = state
            self.resumes.add(resume)
            for t in targets:
                callee = self.ctx(t, st.widths, kind)
                callee.callers.add(site)
                if t in self.hints.noreturn:
                    continue
                self.wait_on(callee, ("call", key, resume + self.hints.inline_args.get(t, 0), st))
            return
        _, key, st = state
        for t in targets:
            callee = self.ctx(t, st.widths, "indirect")
            callee.callers.add(site)
            self.wait_on(callee, ("tail", key))

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
                st = State(forced[0], forced[1], st.c, st.ps, st.per)
            vkey = (pc, st)
            if vkey in c.visited:
                return
            c.visited.add(vkey)

            if bus_to_offset(pc) is None and self.ram.section_at(pc) is None:
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
                # An `indirect` hint on a call means code writes its operand at
                # run time: the hint's (and its derivation's) targets replace the
                # one in the bytes.
                state = ("call", key, insn.next_pc, nxt, name.lower())
                if pc in self.hints.derive:
                    self.site_states[pc].append(state)
                    targets = list(self.hints.indirect.get(pc, [])) + sorted(self.derived[pc])
                else:
                    targets = self.hints.indirect.get(pc, [target])
                self._follow_indirect(pc, state, targets)
                return

            if mode is Mode.ABSXIND:   # JMP (abs,X) / JSR (abs,X)
                self.dispatch_table(key, insn, nxt)
                return

            if mode in (Mode.ABSIND, Mode.ABSLIND):   # JMP (abs) / JML [abs]
                targets = list(self.hints.indirect.get(pc, [])) + sorted(self.derived[pc])
                state = ("tail", key, nxt)
                if name == "JMP" and nxt.per is not None:
                    # PER then JMP: a call whose targets return after the PER's address
                    state = ("call", key, (pc & 0xFF0000) | (nxt.per + 1) & 0xFFFF, replace(nxt, per=None),
                             "indirect")
                if pc in self.hints.nofollow:
                    return          # the targets are entries (threads on their own stacks)
                if pc in self.hints.derive:
                    self.site_states[pc].append(state)
                elif not targets:
                    self.unresolved[pc] = insn
                    return
                self._follow_indirect(pc, state, targets)
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
        per = st.per
        if n == "PER":
            per = insn.branch_target() & 0xFFFF
        elif n in STACK_OPS:
            per = None
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
        return State(m, x, c, ps, per)

    # ---- jump tables ------------------------------------------------------------

    def dispatch_table(self, key: tuple, insn: Insn, st: State):
        bank = insn.pc & 0xFF0000
        jt = self.tables.get(insn.pc)
        if jt is None:
            hint = self.hints.tables.get(insn.pc)
            jt = JumpTable(insn.pc, bank | insn.operand, "abs", insn.name == "JSR")
            if hint:
                jt.count, jt.kind, jt.extra = hint.count, hint.kind, hint.targets
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
        targets = []
        for i in range(jt.count):
            ent = jt.table + i * step
            if jt.kind == "long":
                t = self.u24(ent)
            else:
                v = self.u16(ent)
                t = None if v is None else bank | v
            if t is not None:
                targets.append(t)
        if jt.dispatcher in self.hints.derive:
            self.site_states[jt.dispatcher].append(("table", key, st, jt))
            targets += sorted(self.derived[jt.dispatcher])
        self._follow_targets(jt, key, st, targets + list(jt.extra))

    def _follow_targets(self, jt: JumpTable, key: tuple, st: State, targets):
        for t in targets:
            if jt.call or st.per is not None:
                # JSR (abs,X), or PER then JMP (abs,X): returns after the PER's address
                resume = jt.dispatcher + 3 if jt.call else st.per + 1
                self.resumes.add((jt.dispatcher & 0xFF0000) | (resume & 0xFFFF))
                callee = self.ctx(t, st.widths, "table")
                callee.callers.add(jt.dispatcher)
                self.wait_on(callee, ("call", key, (jt.dispatcher & 0xFF0000) | (resume & 0xFFFF),
                                      replace(st, per=None)))
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
                t = self.u24(jt.table + i * 3)
            else:
                v = self.u16(jt.table + i * 2)
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
            v = self.u16(ent)
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
            if (bus_to_offset(pc) is None and self.ram.section_at(pc) is None) or self.hints.is_data(pc):
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
            if off is None:           # code in a RAM section
                continue
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

    def width_conflicts(self, declared: bool = False) -> dict:
        """Addresses decoded with more than one instruction length. Those an
        `overlap` hint declares are left out, or listed alone with declared=True."""
        return {pc: v for pc, v in self.code.items()
                if len({i.size for i in v.values()}) > 1 and (pc in self.hints.overlaps) == declared}

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
