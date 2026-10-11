"""Expressions and statements of a draft (tools/generate/show.py, docs/REIMPLEMENTATION.md M5).

A draft is lifted from the ROM into these: C locals for the registers the
routine computes with, instead of the emulated CPU the generated blocks run on.
The locals are the *liveness keys*:

    al, ah   the accumulator's low and high bytes (C local `a`, 16 bits)
    x, y     the index registers (C locals `x`, `y`)
    n v z c  the flags (C locals `negative`, `overflow`, `zero`, `carry`)
    t1 ...   temporaries a lifted instruction introduces

Everything else the original touches -- memory, the stack, D, DB, S and P's
other bits -- stays the machine's, read and written through runtime/port/
machine.h, so a call-out or the interpreter continuing sees what the original
would have left. Nothing here knows about the ROM.
"""

from dataclasses import dataclass, replace

FLAG_NAMES = {"n": "negative", "v": "overflow", "z": "zero", "c": "carry"}
REGISTERS = ("a", "x", "y")
ALL_KEYS = frozenset({"al", "ah", "x", "y", "n", "v", "z", "c"})


def keys_of(var: str, lo: bool = False) -> frozenset:
    """Liveness keys a variable covers ("a" is two: its low byte alone with lo)."""
    if var == "a":
        return frozenset({"al"} if lo else {"al", "ah"})
    return frozenset({var})


def c_name(var: str) -> str:
    return FLAG_NAMES.get(var, var)


def c_type(w: int) -> str:
    return {1: "bool", 8: "uint8_t", 16: "uint16_t", 24: "uint32_t", 32: "uint32_t"}.get(w, "int")


# ---- expressions -----------------------------------------------------------------------------


class Expr:
    w: int

    def children(self) -> tuple:
        return ()

    def rebuild(self, kids: tuple) -> "Expr":
        return self

    def walk(self):
        yield self
        for k in self.children():
            yield from k.walk()


@dataclass(frozen=True)
class Const(Expr):
    v: int
    w: int
    addr: str | None = None      # "bank0" (direct page, stack), "long": an address to name


@dataclass(frozen=True)
class Var(Expr):
    name: str                    # a, x, y, n, v, z, c, or a temporary
    w: int


@dataclass(frozen=True)
class Part(Expr):
    """A byte of the accumulator: (uint8_t)a or (uint8_t)(a >> 8)."""
    hi: bool
    w: int = 8


@dataclass(frozen=True)
class Load(Expr):
    w: int                       # 8, 16 or 24 bits read
    addr: Expr
    pure: bool = True            # plain memory (not I/O): may move and repeat

    def children(self):
        return (self.addr,)

    def rebuild(self, kids):
        return replace(self, addr=kids[0])


@dataclass(frozen=True)
class Mach(Expr):
    """Machine state read in place: C text such as `s->cpu.d` or `m_dp(s, 0x10)`."""
    text: str
    w: int


@dataclass(frozen=True)
class Fn(Expr):
    """A call to a pure helper: name(s, args...)."""
    name: str
    args: tuple
    w: int
    pure: bool = True

    def children(self):
        return self.args

    def rebuild(self, kids):
        return replace(self, args=tuple(kids))


@dataclass(frozen=True)
class Bin(Expr):
    op: str
    l: Expr
    r: Expr
    w: int

    def children(self):
        return (self.l, self.r)

    def rebuild(self, kids):
        return replace(self, l=kids[0], r=kids[1])


@dataclass(frozen=True)
class Un(Expr):
    op: str                      # "!" (logical, w=1) or "~"
    e: Expr
    w: int

    def children(self):
        return (self.e,)

    def rebuild(self, kids):
        return replace(self, e=kids[0])


@dataclass(frozen=True)
class Cast(Expr):
    e: Expr
    w: int

    def children(self):
        return (self.e,)

    def rebuild(self, kids):
        return replace(self, e=kids[0])


COMPARE = {"==", "!=", "<", ">=", ">", "<="}
INVERT = {"==": "!=", "!=": "==", "<": ">=", ">=": "<", ">": "<=", "<=": ">"}
MASK = {1: 1, 8: 0xFF, 16: 0xFFFF, 24: 0xFFFFFF, 32: 0xFFFFFFFF}


def uses(e: Expr) -> frozenset:
    """Liveness keys an expression reads."""
    out = set()
    for n in e.walk():
        if isinstance(n, Var):
            out |= keys_of(n.name)
        elif isinstance(n, Part):
            out.add("ah" if n.hi else "al")
    return frozenset(out)


def has_load(e: Expr) -> bool:
    return any(isinstance(n, (Load, Mach)) or (isinstance(n, Fn) and not n.pure) for n in e.walk())


def impure(e: Expr) -> bool:
    return any((isinstance(n, Load) and not n.pure) or isinstance(n, Mach) or (isinstance(n, Fn) and not n.pure)
               for n in e.walk())


def count_var(e: Expr, var: str) -> int:
    """Occurrences of a variable (for "a": the whole or a byte)."""
    n = 0
    for x in e.walk():
        if isinstance(x, Var) and x.name == var:
            n += 1
        elif isinstance(x, Part) and var == "a":
            n += 1
    return n


def substitute(e: Expr, var: str, value: Expr, lo_only: bool = False) -> Expr:
    """e with `var` replaced by value (for "a": bytes become casts of it). With
    lo_only (value is the low byte alone), only (uint8_t)a is replaced."""
    if isinstance(e, Var) and e.name == var:
        # the variable held the value at its width: keep that truncation
        return value if exact(value) or e.w not in (8, 16) else Cast(value, e.w)
    if isinstance(e, Part) and var == "a":
        if lo_only:
            return (value if exact(value) else Cast(value, 8)) if not e.hi else e
        return Cast(value, 8) if not e.hi else Cast(Bin(">>", value, Const(8, 8), 16), 8)
    kids = e.children()
    if not kids:
        return e
    return e.rebuild(tuple(substitute(k, var, value, lo_only) for k in kids))


def exact(e: Expr) -> bool:
    """Whether the C value of e is its value (no bits above its width, no sign)."""
    if isinstance(e, Bin):
        if e.op in COMPARE or e.op in ("&&", "||"):
            return True
        if e.op == "&":
            return exact(e.l) or exact(e.r) or (isinstance(e.r, Const) and e.r.v <= MASK[e.w])
        if e.op in ("|", "^"):
            return exact(e.l) and exact(e.r)
        if e.op == ">>":
            return exact(e.l)
        return False             # + - << can carry past the width
    if isinstance(e, Un):
        return e.op == "!"
    return True


# ---- simplification ------------------------------------------------------------------------


def _fold(op: str, a: int, b: int, w: int) -> int | None:
    m = MASK.get(w, 0xFFFFFFFF)
    if op == "+":
        return (a + b) & m
    if op == "-":
        return (a - b) & m
    if op == "&":
        return a & b
    if op == "|":
        return a | b
    if op == "^":
        return a ^ b
    if op == "<<":
        return (a << b) & m
    if op == ">>":
        return a >> b
    if op in COMPARE:
        return int({"==": a == b, "!=": a != b, "<": a < b, ">=": a >= b, ">": a > b, "<=": a <= b}[op])
    return None


def value_bits(e: Expr) -> int:
    """An upper bound on the bits e's value has (for dropping masks and casts)."""
    if isinstance(e, Const):
        return max(1, e.v.bit_length())
    if isinstance(e, Bin):
        if e.op in COMPARE or e.op in ("&&", "||"):
            return 1
        if e.op == "&":
            return min(value_bits(e.l), value_bits(e.r)) if exact(e.l) and exact(e.r) else e.w
        if e.op in ("|", "^") and exact(e):
            return max(value_bits(e.l), value_bits(e.r))
        if e.op == ">>" and isinstance(e.r, Const) and exact(e.l):
            return max(1, value_bits(e.l) - e.r.v)
        return e.w
    if isinstance(e, Un) and e.op == "!":
        return 1
    return e.w


def _modular(e: Expr, w: int) -> Expr:
    """e without the inner truncations to w bits or more that a truncation of
    the whole to w bits makes redundant (+ - & | ^ << work modulo 2^w)."""
    if isinstance(e, Cast) and e.w >= w:
        return _modular(e.e, w)
    if isinstance(e, Bin) and e.op in ("+", "-", "&", "|", "^") or \
            (isinstance(e, Bin) and e.op == "<<" and isinstance(e.r, Const)):
        kids = (_modular(e.l, w), e.r if e.op == "<<" else _modular(e.r, w))
        if kids[0] is not e.l or kids[1] is not e.r:
            return Bin(e.op, kids[0], kids[1], max(e.w, w) if e.w < w else e.w)
    return e


def simplify(e: Expr) -> Expr:
    kids = e.children()
    if kids:
        e = e.rebuild(tuple(simplify(k) for k in kids))
    if isinstance(e, Bin):
        l, r, op = e.l, e.r, e.op
        if isinstance(l, Const) and isinstance(r, Const):
            v = _fold(op, l.v, r.v, e.w)
            if v is not None:
                tag = (l.addr or r.addr) if op in ("+", "-", "|") else None
                return Const(v, 1 if op in COMPARE else e.w, tag)
        if op in ("+", "|", "^", "==", "!=", "&") and isinstance(l, Const) and not isinstance(r, Const):
            if op in ("==", "!="):
                return simplify(Bin(op, r, l, e.w))
            if not l.addr:
                return simplify(Bin(op, r, l, e.w))
        if isinstance(r, Const):
            if op in ("+", "-", "|", "^", "<<", ">>") and r.v == 0 and not r.addr:
                return l if l.w <= e.w else Cast(l, e.w)
            if op == "&" and r.v == 0:
                return Const(0, e.w)
            if op == "&" and exact(l) and r.v & MASK[min(l.w, 32)] == MASK[min(l.w, 32)] and l.w <= e.w:
                return l
            if op == "&" and exact(l) and (1 << value_bits(l)) - 1 & ~r.v == 0:
                return l
            if op == "+" and isinstance(l, Bin) and l.op == "+" and isinstance(l.r, Const) \
                    and not l.r.addr and not r.addr:
                return simplify(Bin("+", l.l, Const((l.r.v + r.v) & MASK[e.w], e.w), e.w))
            if op == "-" and not r.addr and r.v:
                if isinstance(l, Bin) and l.op == "+" and isinstance(l.r, Const) and not l.r.addr \
                        and l.r.v == r.v:
                    return l.l
            if op in (">=", "<") and r.v == 0 and not r.addr and exact(l):
                return Const(int(op == ">="), 1)      # unsigned
            if op in ("==", "!=") and e.w == 1 and l.w == 1:
                if (op == "!=") == (r.v == 0):
                    return l
                return simplify(Un("!", l, 1))
            if op in ("==", "!=") and isinstance(l, Bin) and l.op == "-" and r.v == 0 and exact(l.l) \
                    and exact(l.r) and l.w >= max(value_bits(l.l), value_bits(l.r)):
                # (a - b) wrapping is zero exactly when a == b
                return Bin(op, l.l, l.r, 1)
        if op in ("<<", ">>") and isinstance(r, Const) and isinstance(l, Bin) and l.op == op \
                and isinstance(l.r, Const) and (op == "<<" or exact(l.l)):
            return simplify(Bin(op, l.l, Const(l.r.v + r.v, 8), e.w))
        if op == "+" and isinstance(l, Const) and isinstance(r, Bin) and r.op == "+" and isinstance(r.r, Const) \
                and not r.r.addr:
            return simplify(Bin("+", Bin("+", l, r.r, e.w), r.l, e.w))
        return e
    if isinstance(e, Un):
        if e.op == "!":
            x = e.e
            if isinstance(x, Const):
                return Const(int(not x.v), 1)
            if isinstance(x, Un) and x.op == "!":
                return x.e
            if isinstance(x, Bin) and x.op in INVERT:
                return Bin(INVERT[x.op], x.l, x.r, 1)
            if x.w != 1:
                return Bin("==", x, Const(0, x.w), 1)
        if e.op == "~" and isinstance(e.e, Const):
            return Const(~e.e.v & MASK[e.w], e.w)
        return e
    if isinstance(e, Cast):
        x = _modular(e.e, e.w)
        if x is not e.e:
            return simplify(Cast(x, e.w))
        if isinstance(x, Const):
            return Const(x.v & MASK[e.w], e.w, x.addr)
        if isinstance(x, Cast):
            return simplify(Cast(x.e, min(e.w, x.w)))
        if exact(x) and value_bits(x) <= e.w:
            return x
        if isinstance(x, Bin) and x.op == "&" and isinstance(x.r, Const) and x.r.v & MASK[e.w] == MASK[e.w]:
            return simplify(Cast(x.l, e.w))
        return e
    return e


# ---- statements ------------------------------------------------------------------------------


class Stmt:
    def exprs(self) -> tuple:
        return ()

    def with_exprs(self, es: tuple) -> "Stmt":
        return self

    def uses(self) -> frozenset:
        out = frozenset()
        for e in self.exprs():
            out |= uses(e)
        return out

    def defs(self) -> frozenset:
        return frozenset()

    effect = False               # touches the machine: memory, the stack, machine state
    control = False


@dataclass
class Assign(Stmt):
    dst: str
    e: Expr
    lo: bool = False             # the accumulator's low byte only

    def exprs(self):
        return (self.e,)

    def with_exprs(self, es):
        return replace(self, e=es[0])

    def defs(self):
        return keys_of(self.dst, self.lo)


@dataclass
class Store(Stmt):
    w: int
    addr: Expr
    val: Expr
    effect = True

    def exprs(self):
        return (self.addr, self.val)

    def with_exprs(self, es):
        return replace(self, addr=es[0], val=es[1])


@dataclass
class Push(Stmt):
    w: int
    val: Expr
    effect = True

    def exprs(self):
        return (self.val,)

    def with_exprs(self, es):
        return replace(self, val=es[0])


@dataclass
class Pull(Stmt):
    dst: str | None
    w: int
    lo: bool = False
    effect = True

    def defs(self):
        return keys_of(self.dst, self.lo) if self.dst else frozenset()


@dataclass
class MachSet(Stmt):
    """Machine state the original changes that isn't a local: `template` with {}
    for the value (D, DB, S, P's interrupt and decimal bits)."""
    template: str
    e: Expr | None = None
    note: str = ""
    effect = True

    def exprs(self):
        return () if self.e is None else (self.e,)

    def with_exprs(self, es):
        return replace(self, e=es[0]) if es else self


@dataclass
class CallOut(Stmt):
    """A call the port makes with m_jsr/m_jsl: `inputs` are copied to the
    machine first, `outputs` read back after (liveness keys)."""
    site: int
    target: int | None           # static target, or None with target_expr
    long: bool
    m8: bool
    x8: bool
    inputs: frozenset
    outputs: frozenset
    target_expr: Expr | None = None
    may_def: frozenset = ALL_KEYS
    note: str = ""
    stub: str = ""
    a8: bool = False             # A's high byte is left to the machine (Draft.a8)
    effect = True

    def exprs(self):
        return () if self.target_expr is None else (self.target_expr,)

    def with_exprs(self, es):
        return replace(self, target_expr=es[0]) if es else self

    def uses(self):
        # what it reads back it also writes first: the callee may leave it alone;
        # and the machine's A is written whole, both bytes
        out = frozenset(self.inputs) | frozenset(self.outputs)
        out = out if self.a8 else _whole_a(out)
        if self.target_expr is not None:
            out |= uses(self.target_expr)
        return out

    def defs(self):
        return frozenset(self.outputs) if self.a8 else _whole_a(frozenset(self.outputs))


def _whole_a(keys: frozenset) -> frozenset:
    return keys | {"al", "ah"} if keys & {"al", "ah"} else keys


@dataclass
class Comment(Stmt):
    text: str


@dataclass
class Branch(Stmt):
    cond: Expr
    target: object
    control = True

    def exprs(self):
        return (self.cond,)

    def with_exprs(self, es):
        return replace(self, cond=es[0])


@dataclass
class Goto(Stmt):
    target: object
    control = True


@dataclass
class Switch(Stmt):
    e: Expr
    cases: list                  # [(value, target)]
    default: object
    control = True

    def exprs(self):
        return (self.e,)

    def with_exprs(self, es):
        return replace(self, e=es[0])


@dataclass
class Exit(Stmt):
    """Leave the logic: exit `kind` rts, rtl, rti, stop (hand back to the
    interpreter at `pc`, a constant or an expression) or noreturn."""
    kind: str
    pc: Expr | None = None
    pb: int | None = None
    m8: bool | None = None
    x8: bool | None = None
    note: str = ""
    live: frozenset = ALL_KEYS   # what the continuation may read
    at: int | None = None        # rts, rtl, rti: the return instruction
    control = True

    def exprs(self):
        return () if self.pc is None else (self.pc,)

    def with_exprs(self, es):
        return replace(self, pc=es[0]) if es else self

    def uses(self):
        return super().uses() | self.live

    def signature(self):
        pc = self.pc.v if isinstance(self.pc, Const) else (None if self.pc is None else "expr")
        return (self.kind, pc, self.pb, self.m8, self.x8)


def map_exprs(s: Stmt, f) -> Stmt:
    es = s.exprs()
    if not es:
        return s
    return s.with_exprs(tuple(f(e) for e in es))
