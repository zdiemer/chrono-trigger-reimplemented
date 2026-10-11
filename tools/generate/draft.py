"""From lifted instructions to the statements of a draft (docs/RECOMP.md, M5).

tools/generate/lift.py says what every instruction does, flags and all. This
makes that readable without changing what it computes:

1. **Blocks.** Instructions chain into basic blocks in address order.
2. **Liveness.** Backward over the blocks, per register byte and flag, from
   what the routine's callers read after it returns (tools/disasm/liveness.py's
   `ret_live`) and what each callee reads (`live_in`).
3. **Dead code.** A flag or register assignment nobody reads is dropped; a
   call-out reads back only the registers the code after it uses.
4. **Substitution.** A value computed once and read once, in the same block,
   is written where it is read, when nothing in between changes what it reads
   (a store or a call moves no load past it). This is what turns `LDA $10 :
   STA $20` into one statement and `CMP #$05 : BCC` into `if (a < 5)`.

Every step keeps what the routine does: which memory it reads and writes, in
which order, and what it leaves in the registers its callers read. It is the
readability that changes.
"""

import re
from dataclasses import dataclass

from .ir import (Assign, Branch, CallOut, Comment, Const, Exit, Goto, Load, Mach, Pull, Stmt, Switch, Var, count_var,
                 has_load, impure, map_exprs, simplify, substitute, uses)


@dataclass
class Eval(Stmt):
    """An expression kept for its side effect (a read of an I/O register)."""
    e: object
    effect = True

    def exprs(self):
        return (self.e,)

    def with_exprs(self, es):
        return Eval(es[0])


@dataclass
class Block:
    name: str
    pc: int
    stmts: list
    fall: str | None = None     # the block it continues into when it doesn't transfer
    live_in: frozenset = frozenset()
    live_out: frozenset = frozenset()

    def succs(self) -> list:
        out = []
        for s in self.stmts:
            if isinstance(s, (Branch, Goto)):
                out.append(s.target)
            elif isinstance(s, Switch):
                out += [t for _, t in s.cases] + [s.default]
        if self.fall is not None:
            out.append(self.fall)
        return out


def label(node) -> str:
    if isinstance(node, tuple) and node and node[0] == "exit":
        return f"X_{node[1]:06X}" + ("" if node[2] is None else f"_m{8 if node[2] else 16}x{8 if node[3] else 16}")
    pc, m8, x8 = node
    return f"L_{pc:06X}"


def build_blocks(lifted: dict, entry) -> list:
    """Basic blocks in address order, the entry's first."""
    preds: dict = {n: 0 for n in lifted}
    targets = set()
    exits = {}
    for node, l in lifted.items():
        for s in l.stmts:
            ts = []
            if isinstance(s, (Branch, Goto)):
                ts = [s.target]
            elif isinstance(s, Switch):
                ts = [t for _, t in s.cases] + [s.default]
            for t in ts:
                if isinstance(t, tuple) and t and t[0] == "exit":
                    exits[t] = t
                else:
                    preds[t] = preds.get(t, 0) + 1
                    targets.add(t)
        if l.fall is not None:
            preds[l.fall] = preds.get(l.fall, 0) + 1
    order = sorted(lifted, key=lambda n: (n[0], str(n[1:])))
    # labels must be unique per pc: a pc drafted with two widths gets both in its name
    by_pc: dict = {}
    for n in order:
        by_pc.setdefault(n[0], []).append(n)

    def name(n):
        if isinstance(n, tuple) and n and n[0] == "exit":
            return label(n)
        base = label(n)
        if len(by_pc[n[0]]) > 1:
            base += f"_m{8 if n[1] else 16}x{8 if n[2] else 16}"
        return base

    def ends_block(l) -> bool:
        return bool(l.stmts) and l.stmts[-1].control

    leaders = {entry} | targets
    for node, l in lifted.items():
        if l.fall is not None and (ends_block(l) or preds.get(l.fall, 0) != 1):
            leaders.add(l.fall)
    for n in lifted:
        if preds.get(n, 0) != 1:
            leaders.add(n)
    blocks = []
    seen = set()
    for lead in [entry] + [n for n in order if n in leaders and n != entry]:
        if lead in seen:
            continue
        stmts = []
        n = lead
        fall = None
        while True:
            seen.add(n)
            l = lifted[n]
            stmts += [Comment(f"{l.insn.pc >> 16:02X}:{l.insn.pc & 0xFFFF:04X}  {l.insn.text()}")] + list(l.stmts)
            if l.fall is None:
                break
            if ends_block(l) or l.fall in leaders or l.fall in seen:
                fall = l.fall
                break
            n = l.fall
        for s in stmts:
            _retarget(s, name)
        blocks.append(Block(name(lead), lead[0], stmts, None if fall is None else name(fall)))
    for t in sorted(exits, key=lambda t: (t[1], str(t[2:]))):
        _, pc, m8, x8 = t
        blocks.append(Block(label(t), pc, [Exit("stop", Const(pc & 0xFFFF, 16),
                                                None if pc >> 16 == entry[0] >> 16 else pc >> 16, m8, x8)]))
    return blocks


def _retarget(s, name):
    if isinstance(s, (Branch, Goto)):
        s.target = name(s.target)
    elif isinstance(s, Switch):
        s.cases = [(v, name(t)) for v, t in s.cases]
        s.default = name(s.default)


# ---- liveness --------------------------------------------------------------------------------


def stmt_live_before(s: Stmt, after: frozenset, block_live: dict) -> frozenset:
    if isinstance(s, (Branch, Goto, Switch)):
        extra = frozenset()
        for t in ([s.target] if isinstance(s, (Branch, Goto)) else [t for _, t in s.cases] + [s.default]):
            extra |= block_live.get(t, frozenset())
        base = after if isinstance(s, Branch) else frozenset()
        return base | extra | s.uses()
    if isinstance(s, Exit):
        return s.uses()
    if isinstance(s, CallOut):
        return (after - s.defs()) | s.uses()
    return (after - s.defs()) | s.uses()


def liveness(blocks: list) -> None:
    live_in = {b.name: frozenset() for b in blocks}
    changed = True
    while changed:
        changed = False
        for b in reversed(blocks):
            out = live_in[b.fall] if b.fall is not None and b.fall in live_in else frozenset()
            live = out
            for s in reversed(b.stmts):
                live = stmt_live_before(s, live, live_in)
            if live != live_in[b.name]:
                live_in[b.name] = live
                changed = True
            b.live_out = out
    for b in blocks:
        b.live_in = live_in[b.name]


def live_after(b: Block, block_live: dict) -> list:
    """live[i] = keys live after statement i of block b, on every way on from it
    (a branch's target as well as what follows)."""
    live = b.live_out
    out = [None] * len(b.stmts)
    for i in range(len(b.stmts) - 1, -1, -1):
        s = b.stmts[i]
        after = live
        if isinstance(s, (Branch, Goto, Switch)):
            ts = [s.target] if isinstance(s, (Branch, Goto)) else [t for _, t in s.cases] + [s.default]
            after = (live if isinstance(s, Branch) else frozenset()).union(
                *[block_live.get(t, frozenset()) for t in ts])
        out[i] = after
        live = stmt_live_before(s, live, block_live)
    return out


# ---- cleanup ------------------------------------------------------------------------------


def eliminate_dead(blocks: list) -> bool:
    live_map = {b.name: b.live_in for b in blocks}
    changed = False
    for b in blocks:
        after = live_after(b, live_map)
        new = []
        for s, live in zip(b.stmts, after):
            if isinstance(s, Assign):
                if not (s.defs() & live):
                    changed = True
                    if impure(s.e):
                        new.append(Eval(s.e))
                    continue
                if isinstance(s.e, Var) and s.e.name == s.dst and not s.lo:
                    changed = True
                    continue
            elif isinstance(s, Pull) and s.dst is not None and not (s.defs() & live):
                s = Pull(None, s.w)
                changed = True
            elif isinstance(s, CallOut):
                outs = frozenset(s.outputs) & live
                if outs != s.outputs:
                    s.outputs = outs
                    changed = True
            new.append(s)
        b.stmts = new
    return changed


BIG = 14    # expression nodes: past this a value keeps its local


def _size(e) -> int:
    return sum(1 for _ in e.walk())


def _barrier(s: Stmt, e) -> bool:
    if s.defs() & uses(e):
        return True
    return has_load(e) and (s.effect or any(impure(x) for x in s.exprs()))


def substitute_block(b: Block, block_live: dict) -> bool:
    changed = False
    while True:
        after = live_after(b, block_live)
        done = True
        for i, s in enumerate(b.stmts):
            if not isinstance(s, Assign) or s.dst in ("n", "v", "z", "c") and False:
                continue
            keys = s.defs()
            j = None
            for k in range(i + 1, len(b.stmts)):
                t = b.stmts[k]
                if isinstance(t, Comment):
                    continue
                if t.uses() & keys:
                    j = k
                    break
                if t.defs() & keys or _barrier(t, s.e):
                    break
            if j is None:
                continue
            t = b.stmts[j]
            if isinstance(t, CallOut) or not t.exprs():
                continue
            if isinstance(t, Exit) and not t.exprs():
                continue
            # the one read of the value, and nothing after it
            n = sum(count_var(e, s.dst) for e in t.exprs())
            if n != 1:
                continue
            if s.lo and any(isinstance(x, Var) and x.name == "a" for e in t.exprs() for x in e.walk()):
                continue
            if (keys & after[j]) - t.defs():
                continue
            if impure(s.e) and any(has_load(e) for e in t.exprs()):
                continue
            new_e = simplify(s.e)
            if _size(new_e) + sum(_size(e) for e in t.exprs()) > BIG * 2 and not isinstance(t, (Branch, Exit)):
                if _size(new_e) > BIG:
                    continue
            b.stmts[j] = map_exprs(t, lambda e: simplify(substitute(e, s.dst, new_e, lo_only=s.lo)))
            del b.stmts[i]
            changed = True
            done = False
            break
        if done:
            return changed


def cleanup(blocks: list) -> None:
    while True:
        changed = False
        for b in blocks:
            changed |= propagate_constants(b)
        changed |= drop_unreachable(blocks)
        liveness(blocks)
        changed |= eliminate_dead(blocks)
        liveness(blocks)
        live_map = {b.name: b.live_in for b in blocks}
        for b in blocks:
            changed |= substitute_block(b, live_map)
        for b in blocks:
            b.stmts = [map_exprs(s, simplify) for s in b.stmts]
        if not changed:
            liveness(blocks)
            return


def propagate_constants(b: Block) -> bool:
    """A register or flag known to hold a constant is read as that constant,
    until something writes it (within the block)."""
    env: dict = {}          # "a" (whole), "al", "x", "y", flags -> Const
    changed = False
    new = []
    for s in b.stmts:
        if env and s.exprs():
            s2 = map_exprs(s, lambda e: simplify(_fold_env(e, env)))
            if s2.exprs() != s.exprs():
                changed = True
                s = s2
        if isinstance(s, Branch) and isinstance(s.cond, Const):
            changed = True
            if s.cond.v:
                s = Goto(s.target)
                new.append(s)
                b.fall = None
                break
            continue
        for k in s.defs():
            env.pop(k, None)
            if k in ("al", "ah"):
                env.pop("a", None)
        if isinstance(s, Assign) and isinstance(s.e, Const):
            if s.dst == "a" and s.lo:
                env["al"] = Const(s.e.v & 0xFF, 8)
            elif s.dst == "a":
                env["a"] = Const(s.e.v & 0xFFFF, 16)
                env["al"] = Const(s.e.v & 0xFF, 8)
            else:
                env[s.dst] = s.e
        new.append(s)
    if len(new) != len(b.stmts):
        changed = True
    b.stmts = new
    return changed


def _fold_env(e, env):
    from .ir import Part
    if isinstance(e, Var):
        if e.name == "a":
            return env.get("a", e)
        c = env.get(e.name)
        return Const(c.v, e.w) if c is not None else e
    if isinstance(e, Part) and not e.hi:
        return env.get("al", e)
    kids = e.children()
    if not kids:
        return e
    return e.rebuild(tuple(_fold_env(k, env) for k in kids))


def drop_unreachable(blocks: list) -> bool:
    by = {b.name: b for b in blocks}
    seen, work = set(), [blocks[0].name]
    while work:
        n = work.pop()
        if n in seen or n not in by:
            continue
        seen.add(n)
        work += by[n].succs()
    keep = [b for b in blocks if b.name in seen]
    if len(keep) == len(blocks):
        return False
    blocks[:] = keep
    return True


def assigned_keys(blocks: list) -> frozenset:
    out = set()
    for b in blocks:
        for s in b.stmts:
            if isinstance(s, (Assign, Pull, CallOut)):
                out |= s.defs()
    return frozenset(out)


def restrict_exits(blocks: list) -> None:
    """Exits read back only what the routine assigns: the rest the machine
    already holds. A register written back is written whole."""
    assigned = assigned_keys(blocks)
    for b in blocks:
        for s in b.stmts:
            if isinstance(s, Exit) and s.kind in ("rts", "rtl", "rti", "stop", "jump"):
                live = frozenset(s.live) & assigned
                if live & {"al", "ah"}:
                    live |= frozenset(s.live) & {"al", "ah"}
                s.live = live


# ---- wait loops ------------------------------------------------------------------------------


def _lifted_succs(l) -> list:
    out = []
    for s in l.stmts:
        if isinstance(s, (Branch, Goto)):
            out.append(s.target)
        elif isinstance(s, Switch):
            out += [t for _, t in s.cases] + [s.default]
    if l.fall is not None:
        out.append(l.fall)
    return [t for t in out if not (isinstance(t, tuple) and t and t[0] == "exit")]


def _sccs(nodes: dict) -> list:
    """Strongly connected components (Tarjan, iterative) of the lifted graph."""
    index, low, on, stack, out = {}, {}, set(), [], []
    counter = [0]
    for root in nodes:
        if root in index:
            continue
        work = [(root, iter(_lifted_succs(nodes[root])))]
        index[root] = low[root] = counter[0]
        counter[0] += 1
        stack.append(root)
        on.add(root)
        while work:
            v, it = work[-1]
            advanced = False
            for w in it:
                if w not in nodes:
                    continue
                if w not in index:
                    index[w] = low[w] = counter[0]
                    counter[0] += 1
                    stack.append(w)
                    on.add(w)
                    work.append((w, iter(_lifted_succs(nodes[w]))))
                    advanced = True
                    break
                if w in on:
                    low[v] = min(low[v], index[w])
            if advanced:
                continue
            work.pop()
            if work:
                low[work[-1][0]] = min(low[work[-1][0]], low[v])
            if low[v] == index[v]:
                comp = []
                while True:
                    w = stack.pop()
                    on.discard(w)
                    comp.append(w)
                    if w == v:
                        break
                out.append(comp)
    return out


def wait_loops(lifted: dict, entry, region) -> list:
    """Loops that wait for something outside the CPU. A loop that calls nothing
    and reads an I/O register, or stores nothing and reads only fixed addresses
    (a flag an interrupt sets): native code runs in no time, so such a loop
    would never end in a draft, which hands back to the interpreter where the
    loop is entered instead. A loop that calls out lets time pass there.
    Returns [(loop entry nodes, what it waits on)]."""
    from .ir import CallOut, Push, Store
    out = []
    preds: dict = {}
    for n, l in lifted.items():
        for t in _lifted_succs(l):
            preds.setdefault(t, set()).add(n)
    for comp in _sccs(lifted):
        cs = set(comp)
        if len(comp) == 1 and comp[0] not in _lifted_succs(lifted[comp[0]]):
            continue
        stmts = [s for n in comp for s in lifted[n].stmts]
        defined = set()
        for s in stmts:
            defined |= s.defs()
        loads = [x for s in stmts for e in s.exprs() for x in e.walk() if isinstance(x, Load)]
        io = []
        fixed = True
        for ld in loads:
            if isinstance(ld.addr, Const) and region(ld.addr.v) == "io":
                io.append(ld.addr.v)
            elif isinstance(ld.addr, Mach):
                # through a data bank the draft doesn't know: I/O if the offset is
                m = re.fullmatch(r"m_abs\(s, 0x([0-9A-F]{4})\)", ld.addr.text)
                if m and 0x2000 <= int(m.group(1), 16) < 0x6000:
                    io.append(int(m.group(1), 16))
            if uses(ld.addr) & defined or any(isinstance(x, Load) for x in ld.addr.walk() if x is not ld.addr):
                fixed = False
        if any(isinstance(s, CallOut) for s in stmts):
            continue            # time passes in the call-outs
        effects = any(isinstance(s, (Store, Push)) for s in stmts)
        if io or (loads and fixed and not effects):
            heads = sorted(n for n in comp if n == entry or preds.get(n, set()) - cs)
            what = sorted({f"${a & 0xFFFF:04X}" for a in io} or
                          {f"${ld.addr.v >> 16:02X}:{ld.addr.v & 0xFFFF:04X}" if isinstance(ld.addr, Const)
                           else (ld.addr.text if isinstance(ld.addr, Mach) else "memory") for ld in loads})
            out.append((heads, what))
    return out
