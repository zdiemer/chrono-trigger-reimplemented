"""Gotos into if, else, loops, break and continue (tools/generate/show.py).

The draft's statements come in address order with labels and gotos, the way
the original is laid out. This rewrites patterns of them into structured C
when, and only when, the rewrite can't change where control goes:

- **if**: `if (c) goto L; S; L:` becomes `if (!c) { S }` when nothing outside
  S jumps into it.
- **else**: `if (c) { S1; goto M; } S2; M:` becomes `if (c) { S1 } else { S2 }`
  under the same condition for S2.
- **do-while**: `L: S; if (c) goto L;` becomes `do { S } while (c);` when that
  branch is the only way to L.
- **loop**: `L: S; goto L;` becomes `for (;;) { S }`, the other jumps to L
  inside it `continue`, and jumps to the label right after it `break`; a loop
  that starts by leaving is a `while`.

Loops are taken first, then ifs and elses, each smallest first, so inner structure
forms before outer and no if hides a back branch from its loop. What
matches none keeps its goto: a correct goto beats a wrong loop.
"""

from collections import Counter
from dataclasses import dataclass, field

from .ir import Branch, Comment, Goto, Stmt, Switch, Un, impure, simplify


@dataclass
class Label(Stmt):
    name: str


@dataclass
class If(Stmt):
    cond: object
    then: list
    els: list = field(default_factory=list)


@dataclass
class Loop(Stmt):
    kind: str                    # "do", "while", "forever"
    cond: object
    body: list


@dataclass
class Break(Stmt):
    pass


@dataclass
class Continue(Stmt):
    pass


def children(s) -> list:
    if isinstance(s, If):
        return [s.then, s.els]
    if isinstance(s, Loop):
        return [s.body]
    return []


def walk(items):
    for s in items:
        yield s
        for kid in children(s):
            yield from walk(kid)


def targets(s) -> list:
    if isinstance(s, (Goto, Branch)):
        return [s.target]
    if isinstance(s, Switch):
        return [t for _, t in s.cases] + [s.default]
    return []


def refs(items) -> Counter:
    c = Counter()
    for s in walk(items):
        c.update(targets(s))
    return c


def labels_in(items) -> set:
    return {s.name for s in walk(items) if isinstance(s, Label)}


def entered_from_outside(segment, total: Counter) -> bool:
    inside = refs(segment)
    return any(total[name] > inside[name] for name in labels_in(segment))


def negate(c):
    return simplify(Un("!", c, 1))


def _drop_dead_labels(items, total):
    out = []
    for s in items:
        if isinstance(s, Label) and total[s.name] == 0:
            continue
        for kid in children(s):
            kid[:] = _drop_dead_labels(kid, total)
        out.append(s)
    return out


def _lists(items):
    """Every statement list in the tree, outermost first."""
    yield items
    for s in items:
        for kid in children(s):
            yield from _lists(kid)


def _next_real(items, i):
    """Index of the next statement after i that isn't a comment."""
    j = i + 1
    while j < len(items) and isinstance(items[j], Comment):
        j += 1
    return j


def _candidates(root, total):
    out = []
    for lst in _lists(root):
        pos = {s.name: i for i, s in enumerate(lst) if isinstance(s, Label)}
        for i, s in enumerate(lst):
            if isinstance(s, Branch) and s.target in pos and pos[s.target] > i:
                j = pos[s.target]
                out.append((j - i, "if", lst, i, j))
            if isinstance(s, (Branch, Goto)) and s.target in pos and pos[s.target] < i:
                j = pos[s.target]
                out.append((i - j, "do" if isinstance(s, Branch) else "loop", lst, j, i))
            if isinstance(s, If) and not s.els and s.then and isinstance(s.then[-1], Goto) \
                    and s.then[-1].target in pos and pos[s.then[-1].target] > i:
                j = pos[s.then[-1].target]
                out.append((j - i, "else", lst, i, j))
    # loops first: an if or else formed around a back branch would hide it
    out.sort(key=lambda c: (c[1] not in ("do", "loop"), c[1] == "else", c[0]))
    return out


def _apply(kind, lst, i, j, total) -> bool:
    if kind == "if":
        seg = lst[i + 1:j]
        if entered_from_outside(seg, total):
            return False
        br = lst[i]
        if not any(not isinstance(s, (Comment, Label)) for s in seg) and not impure(br.cond):
            # a branch over nothing, on a condition without side effects
            lst[i:j] = seg
            return True
        lst[i:j] = [If(negate(br.cond), seg)]
        return True
    if kind == "else":
        seg = lst[i + 1:j]
        if entered_from_outside(seg, total):
            return False
        node = lst[i]
        node.then.pop()
        node.els = seg
        lst[i:j] = [node]
        return True
    if kind in ("do", "loop"):
        head = lst[i].name
        seg = lst[i + 1:j]
        back = lst[j]
        if entered_from_outside(seg, total) or _loose_jumps(seg):
            return False
        # every other jump to the head must be one `continue` can say
        if total[head] != _shallow_refs(seg, head) + 1:
            return False
        if kind == "do" and total[head] == 1:
            lst[i:j + 1] = [Loop("do", back.cond, seg)]
            return True
        _to_continue(seg, head)
        if kind == "do":
            seg = seg + [If(negate(back.cond), [Break()])]
        lst[i:j + 1] = [Loop("forever", None, seg)]
        return True
    return False


def _loose_jumps(items) -> bool:
    """A break or continue outside any loop in items: it belongs to an enclosing
    loop, and a new loop around it would take it."""
    for s in items:
        if isinstance(s, (Break, Continue)):
            return True
        if isinstance(s, If) and (_loose_jumps(s.then) or _loose_jumps(s.els)):
            return True
    return False


def _shallow_refs(items, name) -> int:
    """Jumps to name in items, not counting those inside loops."""
    n = 0
    for s in items:
        n += targets(s).count(name)
        if isinstance(s, If):
            n += _shallow_refs(s.then, name) + _shallow_refs(s.els, name)
    return n


def _to_continue(items, head):
    for k, s in enumerate(items):
        if isinstance(s, Goto) and s.target == head:
            items[k] = Continue()
        elif isinstance(s, Branch) and s.target == head:
            items[k] = If(s.cond, [Continue()])
        elif isinstance(s, If):
            _to_continue(s.then, head)
            _to_continue(s.els, head)


def _to_break(items, after) -> bool:
    changed = False
    for k, s in enumerate(items):
        if isinstance(s, Goto) and s.target == after:
            items[k] = Break()
            changed = True
        elif isinstance(s, Branch) and s.target == after:
            items[k] = If(s.cond, [Break()])
            changed = True
        elif isinstance(s, If):
            changed |= _to_break(s.then, after)
            changed |= _to_break(s.els, after)
    return changed


def _breaks(root) -> bool:
    changed = False
    for lst in _lists(root):
        for k, s in enumerate(lst):
            if isinstance(s, Loop):
                j = _next_real(lst, k)
                if j < len(lst) and isinstance(lst[j], Label):
                    changed |= _to_break(s.body, lst[j].name)
    return changed


def _whiles(root) -> bool:
    changed = False
    for lst in _lists(root):
        for s in lst:
            if isinstance(s, Loop) and s.kind == "forever":
                body = [x for x in s.body]
                k = 0
                while k < len(body) and isinstance(body[k], Comment):
                    k += 1
                if k < len(body) and isinstance(body[k], If) and body[k].then == [Break()] and not body[k].els:
                    s.kind, s.cond = "while", negate(body[k].cond)
                    s.body = body[:k] + body[k + 1:]
                    changed = True
    return changed


def _tails(root) -> bool:
    """`if (c) { ... goto X; } else { ... goto X; } X:` style leftovers: a goto to
    the label that directly follows its statement is a no-op."""
    changed = False
    for lst in _lists(root):
        k = 0
        while k < len(lst):
            s = lst[k]
            j = _next_real(lst, k)
            if isinstance(s, Goto) and j < len(lst) and isinstance(lst[j], Label) and lst[j].name == s.target:
                del lst[k]
                changed = True
                continue
            if isinstance(s, If) and j < len(lst) and isinstance(lst[j], Label):
                changed |= _strip_trailing_goto(s, lst[j].name)
            k += 1
    return changed


def _strip_trailing_goto(s, name) -> bool:
    changed = False
    for body in (s.then, s.els):
        k = len(body) - 1
        while k >= 0 and isinstance(body[k], Comment):
            k -= 1
        if k >= 0:
            last = body[k]
            if isinstance(last, Goto) and last.target == name:
                del body[k]
                changed = True
            elif isinstance(last, If):
                changed |= _strip_trailing_goto(last, name)
    return changed


def structure(items: list) -> list:
    root = list(items)
    while True:
        total = refs(root)
        root = _drop_dead_labels(root, total)
        total = refs(root)
        done = True
        for _, kind, lst, i, j in _candidates(root, total):
            if _apply(kind, lst, i, j, total):
                done = False
                break
        if done:
            changed = _breaks(root) | _whiles(root) | _tails(root)
            if not changed:
                total = refs(root)
                return _drop_dead_labels(root, total)
