"""Whether A's high byte (B) is known to be zero at a point of the code.

With an 8-bit accumulator the 65816 still keeps B, and TAX or TAY with 16-bit
index registers copy all of C = B:A into the index. A jump table dispatched by
`ASL A : TAX : JSR (table,X)` reaches no more than 128 words where B is zero
there (tools/v_readers.py's `index_reach` hints), so this follows B through the
code the analysis traced.

A value is 'Z' (known zero) or 'U' (not known). The direct page register's
high byte (Dh) is followed alongside, because TDC, the usual way to clear C,
copies D into it; with `d_high_zero` a TDC is taken to clear B outright (the
battle's direct page is $0000, which its ports' guards check at every entry),
and the result says it rests on that. What sets them:

- TDC: B takes Dh. TCD: Dh takes B. TSC, XBA: B unknown.
- with a 16-bit accumulator: LDA #imm sets B to the immediate's high byte;
  AND #imm with a zero high byte clears it; ORA and EOR #imm with a zero high
  byte, and LSR A, leave a zero B zero; anything else that writes A (a load, an
  add, a transfer, a pull, INC A...) makes it unknown;
- with an 8-bit accumulator nothing but XBA, TDC and TSC touches B;
- PLD takes back the Dh of the PHD the liveness analysis pairs it with, or
  unknown; a call applies what the callee returns with, given what it is
  called with (each routine is solved for each of the four ways it can be
  entered).

Each routine is entered with the meet of what every call site the analysis
traced has there; one the analysis entered some other way (a vector, a hint,
coverage, a jump through RAM) is entered with both unknown. Interrupt handlers
are taken to give A back as they found it.
"""

from tools.disasm.cpu import CONDITIONAL_BRANCHES, Mode

Z, U = "Z", "U"
A_WRITERS = {"LDA", "ADC", "SBC", "AND", "ORA", "EOR", "TXA", "TYA", "PLA"}
A_SHIFTS = {"ASL", "LSR", "ROL", "ROR", "INC", "DEC"}
CALL_KINDS = {"jsr", "jsl", "table", "jml"}
ENTRIES = ((Z, Z), (Z, U), (U, Z), (U, U))


def meet(a, b):
    if a is None:
        return b
    if b is None:
        return a
    return (a[0] if a[0] == b[0] else U, a[1] if a[1] == b[1] else U)


def _hex(a):
    return f"${a >> 16:02X}:{a & 0xFFFF:04X}"


class HighByte:
    def __init__(self, an, lv=None, d_high_zero=False):
        self.an = an
        self.lv = lv
        self.d_high_zero = d_high_zero
        self.returns = {}        # (routine entry, entry value) -> value at its returns, or None
        self.entry = {}          # ctx key -> the value it is entered with
        self.states = {}         # ctx key -> pc -> value before the instruction (with its entry value)
        self.losses = {}         # ctx key -> pc -> why B stops being known zero there
        self.edges = {}          # ctx key -> pc -> successors
        self.afters = {}         # ctx key -> pc -> value after the instruction
        self._pld = self._pair_pld()

    def _pair_pld(self):
        out = {}
        if self.lv is None:
            return out
        for (key, push_pc), pulls in self.lv.push_pairs.items():
            if not any(i.name == "PHD" for i in self.an.code.get(push_pc, {}).values()):
                continue
            for resume, name, _w in pulls:
                if name != "PLD":
                    continue
                for pc in self.an.contexts[key].insns:
                    for insn in self.an.code[pc].values():
                        if insn.name == "PLD" and insn.next_pc == resume:
                            out.setdefault((key, pc), set()).add(push_pc)
        return out

    def _code(self, key, pc):
        if self.lv is not None:
            return self.lv._code(key, pc)
        return list(self.an.code[pc].items())

    def _table_targets(self, jt):
        if self.lv is not None:
            return self.lv._table_targets(jt)
        bank = jt.dispatcher & 0xFF0000
        out = [bank | v for v in (self.an.u16(jt.table + i * 2) for i in range(jt.count)) if v is not None]
        return out + sorted(self.an.derived.get(jt.dispatcher, ()))

    def _call_targets(self, pc, insn):
        if self.lv is not None:
            return [t for t in self.lv._call_targets(pc, insn) if t is not None]
        return [insn.operand if insn.name == "JSL" else (pc & 0xFF0000) | insn.operand]

    def _called(self, targets, before):
        """What the targets return with when called with `before`: None while
        none of them is known to return yet."""
        out = None
        for t in targets:
            if not any(k[0] == t for k in self.an.contexts):
                return (U, U)
            r = self.returns.get((t, before))
            if r is not None:
                out = meet(out, r)
        return out

    def _after(self, key, pc, insn, m8, st, states):
        b, dh = st
        name, mode = insn.name, insn.mode
        if name == "TDC":
            return (Z if self.d_high_zero else dh, dh)
        if name == "TCD":
            return (b, b)
        if name in ("TSC", "XBA"):
            return (U, dh)
        if name == "PLD":
            v = None
            for p in self._pld.get((key, pc), ()):
                s = states.get(p)
                v = meet(v, s) if s else (U, U)
            return (b, v[1] if v else U)
        if m8:
            return st
        if name in A_WRITERS:
            hi_zero = mode is Mode.IMM_M and insn.operand is not None and (insn.operand >> 8) == 0
            if name == "LDA" and mode is Mode.IMM_M:
                return (Z if hi_zero else U, dh)
            if name == "AND" and hi_zero:
                return (Z, dh)
            if name in ("ORA", "EOR") and hi_zero:
                return st
            return (U, dh)
        if name in A_SHIFTS and mode is Mode.ACC:
            return st if name == "LSR" else (U, dh)
        return st

    def _solve(self, key, entry, record=False):
        """Solve one context entered with `entry`: its states, and what it returns with."""
        an = self.an
        c = an.contexts[key]
        states = {c.entry: entry}
        losses, edges, afters = {}, {}, {}
        returns = None
        work = [c.entry]
        while work:
            pc = work.pop()
            st = states[pc]
            if pc not in c.insns:
                continue
            for widths, insn in self._code(key, pc):
                m8 = widths[0] is not False
                name, mode = insn.name, insn.mode
                after = self._after(key, pc, insn, m8, st, states)
                if record and st[0] == Z and after[0] == U:
                    losses[pc] = name
                succs = []
                if name in ("RTS", "RTL"):
                    returns = meet(returns, after)
                    continue
                if name in ("RTI", "STP", "BRK", "COP", "WDM"):
                    continue
                if name in CONDITIONAL_BRANCHES:
                    succs = [insn.next_pc, insn.branch_target()]
                elif name in ("BRA", "BRL"):
                    succs = [insn.branch_target()]
                elif name == "JMP" and mode is Mode.ABS:
                    succs = [(pc & 0xFF0000) | insn.operand]
                elif (name in ("JSR", "JSL") and mode in (Mode.ABS, Mode.LONG)) or \
                        (mode is Mode.ABSXIND and an.tables.get(pc) is not None and an.tables[pc].call):
                    if mode is Mode.ABSXIND:
                        jt = an.tables[pc]
                        if jt.count is None:
                            continue
                        targets, resume = self._table_targets(jt), insn.next_pc
                    else:
                        targets = self._call_targets(pc, insn)
                        if any(t in an.hints.noreturn for t in targets):
                            continue
                        resume = insn.next_pc + max((an.hints.inline_args.get(t, 0) for t in targets), default=0)
                    r = self._called(targets, after)
                    if r is None:
                        continue
                    if record and after[0] == Z and r[0] == U:
                        losses[pc] = sorted(t for t in targets
                                            if (self.returns.get((t, after)) or (U, U))[0] == U)
                    after = r
                    succs = [resume]
                elif mode is Mode.ABSXIND:
                    jt = an.tables.get(pc)
                    if jt is None or jt.count is None:
                        continue
                    succs = self._table_targets(jt)
                elif name == "JML" or mode in (Mode.ABSIND, Mode.ABSLIND):
                    targets = [insn.operand] if (name == "JML" and mode is Mode.LONG) else \
                        (self.lv.indirect_targets(pc) if self.lv else [])
                    r = self._called(targets, after) if targets else (U, U)
                    if r is not None:
                        returns = meet(returns, r)
                    continue
                else:
                    succs = [insn.next_pc]
                afters[pc] = meet(afters.get(pc), after)
                edges.setdefault(pc, set()).update(succs)
                for t in succs:
                    old = states.get(t)
                    new = meet(old, after)
                    if new != old:
                        states[t] = new
                        work.append(t)
        if record:
            self.states[key], self.losses[key], self.edges[key], self.afters[key] = states, losses, edges, afters
        return returns, states

    def solve(self, rounds=60):
        an = self.an
        for _ in range(rounds):
            changed = False
            for key in an.contexts:
                for entry in ENTRIES:
                    r, _ = self._solve(key, entry)
                    k = (key[0], entry)
                    new = meet(self.returns.get(k), r)
                    if new != self.returns.get(k):
                        self.returns[k] = new
                        changed = True
            if not changed:
                break
        sites = {}
        for key, c in an.contexts.items():
            for pc in c.insns:
                sites.setdefault(pc, []).append(key)
        at_sites = {}
        for _ in range(rounds):
            changed = False
            for key, c in an.contexts.items():
                if c.kinds - CALL_KINDS or not c.callers:
                    v = (U, U)
                else:
                    v = None
                    for site in c.callers:
                        for ck in sites.get(site, []):
                            st = at_sites.get((ck, site))
                            if st is not None:
                                v = meet(v, st)
                if v is None or v == self.entry.get(key):
                    continue
                self.entry[key] = v
                changed = True
                _, states = self._solve(key, v)
                for pc, st in states.items():
                    at_sites[(key, pc)] = st
            if not changed:
                break
        for key in an.contexts:
            if key in self.entry:
                self._solve(key, self.entry[key], record=True)

    def at(self, pc):
        """The value before the instruction at pc, over every context that runs it."""
        v = None
        for key, states in self.states.items():
            if pc in states:
                v = meet(v, states[pc])
        return v

    def why_not(self, pc) -> list:
        """What keeps B from being known zero at pc, context by context: the call
        sites that enter the routine with it unknown, or the instructions on the
        way that make it so (for a call, the routines whose returns do)."""
        out = []
        for key, states in self.states.items():
            st = states.get(pc)
            if st is None or st[0] == Z:
                continue
            where = _hex(key[0])
            preds = self._reaching(key, pc)
            losses = [(p, why) for p, why in sorted(self.losses[key].items()) if p in preds]
            for p, why in losses:
                if isinstance(why, list):
                    out.append(f"in {where}, the call at {_hex(p)}: " + (", ".join(_hex(t) for t in why)
                                                                          or "a callee") + " return with B unknown")
                else:
                    out.append(f"in {where}, the {why} at {_hex(p)}")
            if self.entry.get(key, (U, U))[0] == U:
                c = self.an.contexts[key]
                if c.kinds - CALL_KINDS:
                    out.append(f"{where} is entered as {', '.join(sorted(c.kinds - CALL_KINDS))}")
                bad = sorted(s for s in c.callers if (self.at(s) or (U, U))[0] != Z)
                if bad:
                    out.append(f"{where} is called with B not known zero from " + ", ".join(_hex(s) for s in bad))
        return out

    def _reaching(self, key, pc):
        """The instructions of the context that pass pc an unknown B: back from
        pc through every predecessor that leaves it unknown, stopping at the
        ones that leave it known zero."""
        back = {}
        for p, succs in self.edges.get(key, {}).items():
            for s in succs:
                back.setdefault(s, set()).add(p)
        afters = self.afters.get(key, {})
        seen, work = set(), [pc]
        while work:
            for p in back.get(work.pop(), ()):
                if p not in seen and (afters.get(p) or (U, U))[0] == U:
                    seen.add(p)
                    work.append(p)
        return seen


__all__ = ["HighByte", "Z", "U"]
