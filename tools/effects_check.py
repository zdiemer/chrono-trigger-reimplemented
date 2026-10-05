"""Check that no port's logic reads what some port writes only as scaffolding.

    python3 tools/effects_check.py [--new] [--runs LOG...]
    python3 tools/effects_check.py --check [--runs LOG...]          (the gate)
    python3 tools/effects_check.py --write-baseline | --prune [--runs LOG...]
    python3 tools/effects_check.py --originals LOG...                (the original's reads, with their writes)

What a port writes inside `ORIGINAL_EFFECT(...)` (game/original_effects.h), or
in code under `#if ORIGINAL_EFFECTS`, is scaffolding: the native-shape build
(`CT_WITHOUT_ORIGINAL_EFFECTS`) compiles it out. Scaffolding is written and
never read back by the logic (docs/PORTING.md, "Scaffolding"). A port whose
logic reads a cell some port writes only as scaffolding reads something else
in the native build than in the build the gate runs: the battle's random draws
divided by the long arithmetic's leftover high word, which the arithmetic kept
only as scaffolding, and the menus drew a window's inside to the size the
frame's routine left, also scaffolding. Shadow mode runs the default build and
the native-shape build is compiled, never run, so neither sees it. Two checks
do, and this tool reads both.

**In the source** (always):

- a *scaffolding write* is a write of a work RAM cell -- a setter from
  game/vars.h (`set_<name>`), or `wram_write*`/`m_write*` at an address the
  source spells as a constant -- made inside an `ORIGINAL_EFFECT` or an
  `#if ORIGINAL_EFFECTS` block, or in a function called from one (to any
  depth: `leave_*` and `original_cell*` helpers are followed, and a setter
  passed to a helper as a function pointer, `leave_word(g, set_x, at, v)`, is
  followed to where the helper calls it);
- a *read* is a getter (`<name>(g, ...)`) or `wram_read*`/`m_read*` outside all
  of that, in any function the game reaches: a registry entry, a table's row,
  a callback, and what they call. Reads while a guard peeks (between
  `vars_peek(true)` and `vars_peek(false)`) and in registry guards are the
  runtime's question whether a port applies, not the logic, and don't count;
- a *finding* is a read of bytes some port writes as scaffolding. An accessor
  covers the element (and field, and byte) its constant arguments select, or
  every element where an argument isn't constant. Constants are followed
  through the helpers' parameters (`set_work_byte(g, LEFT_TURNED, 0)` is
  element 0 of `battle_work`), enum constants and `#define`s; a helper's
  branches are all taken, and an element past the variable's end drops out
  (so `original_cell8`'s two accessors each keep only the cells in their
  range). A read and a write of different variables meet only where their
  bytes overlap and the variables belong to one subsystem (or to subsystems
  sharing a page: field and event, scene and attract, menu, menu_object and overworld):
  work RAM is reused between modes.

A finding is reported whether or not some port also writes the cell outside
scaffolding, and says which: `[plain]` (also written plainly, somewhere) or
`[only]`. This check has no order, so it can't tell that the plain write is the
one the read sees; the battle's case was of that kind (other ports wrote the
high word plainly; the draw read what the arithmetic before it had left only
as scaffolding), and a rule of "only cells nothing writes plainly" would have
missed it. Nor does it see the original's still-generated code, which sets
most of what the entry wrappers read (`[wrapper]`: the read is in a function
that takes the machine, usually the calling convention). So it is broad.

**In runs** (`--runs LOG...`): `ct_dev --native on --scaffold-reads` marks
every work RAM byte with whether its latest write was scaffolding and prints a
`scaffold read:` line for each byte a port read while marked, by the reading
port and the one whose scaffolding wrote it (runtime/port/scaffold_reads.h).
That is the question with its order: at that read, the native build would have
found what was there before the scaffolding's write. tools/vblank_check.py's
shipping-mode runs take the flag, so the suite passes their logs. A finding is
a port and the variable the byte belongs to.

**The ratchet.** `--check` fails on a finding of either kind not in
tools/effects_baseline.txt: `FILE FUNCTION ACCESSOR[ELEMENT] VARIABLE [only]`
for the source (no line numbers, so an edit elsewhere doesn't move it; the
element where the source names it; `only` where no port writes the cell
plainly, so a plain write turned into scaffolding is a new line), `run PORT
VARIABLE` for a run, then an optional `# reason`. The baseline holds main's findings,
the source findings of the branches in flight when the check came in, and the
library's run findings. A new one is answered before it merges: the write is
the game's, so make it plain; or the read takes a value something else sets
(the original's caller, every call), and its line goes in with that reason.
A line nothing produces any more is reported, not failed, since the baseline
carries branches' lines until they merge; `--prune` drops them (the run lines
only when runs are read). `--write-baseline` adds today's findings and keeps
every line already there. `--new` lists what the baseline doesn't have.

The reading of the source is textual and conservative the way that matters: a
constant it can't follow widens a cell to the whole variable. A read through a
pointer, a struct copy or a function it can't resolve is missed; so is a
scaffolding write through `m_write*` at an address computed at run time; the
runs catch those where the library reaches them.
"""

import argparse
import collections
import pathlib
import re
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from tools import ram_map  # noqa: E402
from tools.dontcare_check import KEYWORDS, Source, matching, split_args  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tools" / "effects_baseline.txt"
# Generated declarations (no bodies) and the macro itself.
SKIP = {"vars.h", "ram.h", "original_effects.h"}
# Raw access by address: name -> (kind, bytes); the address is the second argument.
RAW = {
    "m_read8": ("r", 1), "m_read16": ("r", 2), "m_read24": ("r", 3),
    "m_write8": ("w", 1), "m_write16": ("w", 2),
    "wram_read8": ("r", 1), "wram_read16": ("r", 2),
    "wram_write8": ("w", 1), "wram_write16": ("w", 2),
}
FAMILIES = ({"field", "event"}, {"scene", "attract"}, {"menu", "menu_object"}, {"menu_object", "overworld"})
TYPE_WORDS = {"uint8_t", "uint16_t", "uint32_t", "int8_t", "int16_t", "int32_t", "int", "unsigned",
              "signed", "char", "short", "long", "bool", "size_t"}
TOKEN = re.compile(r"\s*(0[xX][0-9A-Fa-f]+[uUlL]*|\d+[uUlL]*|[A-Za-z_]\w*|->|<<|>>|<=|>=|==|!=|&&|\|\|"
                   r"|[-+*/%&|^~!<>?:()\[\].,{}=;'\"\\#])")
UNKNOWN = None
MAX_DEPTH = 12


def compatible(a: str, b: str) -> bool:
    """Whether two subsystems' uses of one byte can meet."""
    return a == b or any(a in f and b in f for f in FAMILIES)


def meets(rvar, rsub, wvar, wsub) -> bool:
    """Whether a read and a write of one byte are of the same cell: the same
    variable, or two of one subsystem (a record's field and a page kind's cell
    over it); a raw access, by address, meets any variable of its file's
    subsystem, or core's."""
    if rvar is not None and wvar is not None:
        return rvar == wvar or compatible(rsub, wsub)
    return compatible(rsub, wsub) or "core" in (rsub, wsub)


# ---- the source -------------------------------------------------------------------------------

class Unit:
    """A source file: its functions (dontcare_check.Source) and where its
    scaffolding is."""

    def __init__(self, path: pathlib.Path, rel: str):
        self.rel = rel
        self.src = Source(rel, path.read_text(encoding="utf-8"))
        self.spans = self._scaffold_spans()
        self.peeks = self._peek_spans()
        self.header = rel.endswith(".h")

    def _peek_spans(self) -> list:
        """Between vars_peek(true) and vars_peek(false): a guard or a wrapper
        looking at the state without it counting as the port's read."""
        t = self.src.text
        out = []
        for m in re.finditer(r"\bvars_peek\s*\(\s*true\s*\)", t):
            end = re.compile(r"\bvars_peek\s*\(\s*false\s*\)").search(t, m.end())
            if end:
                out.append((m.end(), end.start()))
        return out

    def in_peek(self, offset: int) -> bool:
        return any(a <= offset < b for a, b in self.peeks)

    def _scaffold_spans(self) -> list:
        t = self.src.text
        spans = []
        for m in re.finditer(r"\bORIGINAL_EFFECT\s*\(", t):
            try:
                spans.append((m.end() - 1, matching(t, m.end() - 1)))
            except ValueError:
                continue
        # #if ORIGINAL_EFFECTS ... #else/#endif, and the other spellings
        stack = []   # "S" scaffolding, "N" native only, "O" anything else
        start = None
        offset = 0
        for line in t.split("\n"):
            d = line.strip()
            kind = None
            if re.match(r"#\s*if\s+ORIGINAL_EFFECTS\b|#\s*ifndef\s+CT_WITHOUT_ORIGINAL_EFFECTS\b", d):
                kind = "S"
            elif re.match(r"#\s*if\s+!\s*ORIGINAL_EFFECTS\b|#\s*ifdef\s+CT_WITHOUT_ORIGINAL_EFFECTS\b", d):
                kind = "N"
            elif re.match(r"#\s*if", d):
                kind = "O"
            before = "S" in stack
            if kind:
                stack.append(kind)
            elif re.match(r"#\s*(else|elif)\b", d) and stack:
                stack[-1] = {"S": "N", "N": "S"}.get(stack[-1], "O")
            elif re.match(r"#\s*endif\b", d) and stack:
                stack.pop()
            after = "S" in stack
            if after and not before:
                start = offset + len(line)
            elif before and not after and start is not None:
                spans.append((start, offset))
                start = None
            offset += len(line) + 1
        return spans

    def in_scaffolding(self, offset: int) -> bool:
        return any(a <= offset < b for a, b in self.spans)

    def line(self, offset: int) -> int:
        return self.src.line(offset)


class Tree:
    def __init__(self, game: pathlib.Path, root: pathlib.Path):
        self.units = []
        for path in sorted(list(game.rglob("*.c")) + list(game.rglob("*.h"))):
            if path.name in SKIP and path.parent == game:
                continue
            rel = path.relative_to(root).as_posix()
            self.units.append(Unit(path, rel))
        self.globals = {}        # function name -> (unit, Func), non-static or in a header
        self.consts = {}         # enum constants and object-like #defines of headers
        for u in self.units:
            for name, f in u.src.funcs.items():
                if u.header or not f.static:
                    self.globals.setdefault(name, (u, f))
            if u.header:
                for name, expr in u.src.consts.items():
                    self.consts.setdefault(name, (u, expr))
                for name, (params, body) in u.src.defines.items():
                    if params is None and body:
                        self.consts.setdefault(name, (u, body))

    def resolve(self, unit: Unit, name: str):
        f = unit.src.funcs.get(name)
        if f is not None:
            return unit, f
        return self.globals.get(name)

    def const(self, unit: Unit, name: str):
        if name in unit.src.consts:
            return unit, unit.src.consts[name]
        d = unit.src.defines.get(name)
        if d and d[0] is None and d[1]:
            return unit, d[1]
        return self.consts.get(name)


# ---- expressions ------------------------------------------------------------------------------

def tokens(text: str) -> list:
    out, i = [], 0
    while i < len(text):
        m = TOKEN.match(text, i)
        if not m or m.end() == i:
            if text[i:].strip() == "":
                break
            return None
        out.append(m.group(1))
        i = m.end()
    return out


class Normalizer:
    """An expression in a function's terms: numbers for constants, `@p@` for
    the function's parameter p, None where anything else (a local, a member, a
    call) decides it."""

    def __init__(self, tree: Tree):
        self.tree = tree
        self.memo = {}

    def const_value(self, unit: Unit, name: str, seen: frozenset):
        hit = self.tree.const(unit, name)
        if hit is None or name in seen:
            return UNKNOWN
        cu, expr = hit
        key = (cu.rel, name)
        if key not in self.memo:
            text = self.normalize(cu, expr, (), seen | {name})
            self.memo[key] = evaluate(text) if text is not None else UNKNOWN
        return self.memo[key]

    def normalize(self, unit: Unit, text: str, params, seen=frozenset()):
        toks = tokens(text)
        if not toks:
            return UNKNOWN
        out = []
        i = 0
        while i < len(toks):
            t = toks[i]
            # a cast: ( type words [*] )
            if t == "(":
                j = i + 1
                while j < len(toks) and toks[j] in TYPE_WORDS:
                    j += 1
                if j > i + 1 and j < len(toks) and toks[j] == ")":
                    i = j + 1
                    continue
            if t in ("->", ".", "[", "]", "?", ":", "&&", "||", "!", "=", "{", "}", ";", "'", '"', "\\", "#",
                     ",", "==", "!=", "<", ">", "<=", ">="):
                return UNKNOWN
            if re.fullmatch(r"0[xX][0-9A-Fa-f]+[uUlL]*", t):
                out.append(str(int(t.rstrip("uUlL"), 16)))
            elif re.fullmatch(r"\d+[uUlL]*", t):
                out.append(str(int(t.rstrip("uUlL"))))
            elif re.fullmatch(r"[A-Za-z_]\w*", t):
                if i + 1 < len(toks) and toks[i + 1] == "(":
                    return UNKNOWN      # a call
                if t in params:
                    out.append("@%s@" % t)
                else:
                    v = self.const_value(unit, t, seen)
                    if v is UNKNOWN:
                        return UNKNOWN
                    out.append("(%d)" % v)
            elif t == "/":
                out.append("//")
            else:
                out.append(t)
            i += 1
        return " ".join(out)


def evaluate(text):
    """The value of a normalized expression without parameters, or None."""
    if text is None or "@" in text:
        return UNKNOWN
    if not re.fullmatch(r"[\d\s()+\-*/%&|^~<>]*", text):
        return UNKNOWN
    try:
        return int(eval(text, {"__builtins__": {}}, {}))   # noqa: S307 - digits and operators only
    except Exception:
        return UNKNOWN


def substitute(text, binding: dict):
    """A callee's normalized expression with its parameters replaced by the
    caller's arguments (normalized in the caller's terms)."""
    if text is None:
        return UNKNOWN
    out = text
    for p in set(re.findall(r"@(\w+)@", text)):
        arg = binding.get(p, UNKNOWN)
        if arg is UNKNOWN:
            return UNKNOWN
        out = out.replace("@%s@" % p, "(%s)" % arg)
    v = evaluate(out)
    return str(v) if v is not UNKNOWN else out


# ---- the accessors ----------------------------------------------------------------------------

class Accessors:
    def __init__(self, m: ram_map.RamMap):
        self.by_name = {}
        for _sub, groups in ram_map.accessors(m):
            for _var, _count, group in groups:
                for a in group:
                    self.by_name[a.name] = a
        self.vars = m.vars

    def lookup(self, name: str):
        """(accessor, is a write) for a getter or setter name, else None."""
        if name.startswith("set_") and name[4:] in self.by_name:
            return self.by_name[name[4:]], True
        if name in self.by_name:
            return self.by_name[name], False
        return None

    @staticmethod
    def layout(a, write: bool) -> list:
        """What each argument after the state is: page, index, at, row, column, v."""
        out = []
        for decl, arg in a.params(write):
            if arg == "v":
                out.append("v")
            elif a.var.on_instances() and arg == a.var.page.param() and decl == a.var.page.handle_decl():
                out.append("page")
            elif arg in ("at", "row", "column"):
                out.append(arg)
            else:
                out.append("index")
        return out

    @staticmethod
    def cell_bytes(a, args: dict) -> frozenset:
        """The work RAM bytes an access with these argument values (int or None) covers."""
        v = a.var
        out = set()
        idx = args.get("index")
        if idx is not None and not 0 <= idx < v.count:
            return frozenset()
        elements = range(v.count) if idx is None else [idx]
        if a.blob:
            at = args.get("at")
            if at is not None and not 0 <= at < a.size:
                return frozenset()
            inner = range(a.size) if at is None else [at]
        elif a.table:
            col = args.get("column")
            if col is not None and not 0 <= col < a.size:
                return frozenset()
            inner = range(a.size) if col is None else [col]
        else:
            inner = range(a.size)
        rows = [0]
        if a.table:
            row = args.get("row")
            if row is not None and not 0 <= row < v.rows:
                return frozenset()
            rows = range(v.rows) if row is None else [row]
        for base in v.bases():
            for r in rows:
                for e in elements:
                    for k in inner:
                        out.add(base + (r * v.row_stride if a.table else 0) + e * v.stride + a.offset + k)
        return frozenset(out)


def wram_address(addr: int):
    """A constant address as a work RAM byte, or None."""
    bank, low = addr >> 16 & 0xFF, addr & 0xFFFF
    if bank in (0x7E, 0x7F):
        return addr & 0xFFFFFF
    if (bank < 0x40 or 0x80 <= bank < 0xC0) and low < 0x2000:
        return 0x7E0000 | low
    return None


# ---- effects ----------------------------------------------------------------------------------

class Effect:
    __slots__ = ("kind", "target", "args", "scaffold", "site", "func")

    def __init__(self, kind, target, args, scaffold, site, func):
        self.kind = kind            # "r" or "w"
        self.target = target        # accessor name, or "@raw<n>" for a raw access of n bytes
        self.args = args            # tuple of (role, normalized expression or None)
        self.scaffold = scaffold
        self.site = site            # (file, line) of the getter, setter or raw access
        self.func = func            # the function it is in

    def key(self):
        return (self.kind, self.target, self.args, self.scaffold, self.site)


class Analysis:
    def __init__(self, tree: Tree, acc: Accessors):
        self.tree = tree
        self.acc = acc
        self.norm = Normalizer(tree)
        self.summaries = {}
        self.busy = set()
        self.called = set()       # (unit, func name) called from some function body
        self.referenced = set()   # names used other than as a call
        self.guards = set()       # registry guards: `.guard = name`
        self.wrappers = set()     # (file, function) of functions that take the machine
        for u in tree.units:
            text = u.src.text
            for f in u.src.funcs.values():
                head = text[max(text.rfind(";", 0, f.start), text.rfind("}", 0, f.start)) + 1:f.start]
                if re.search(r"\bSnes\s*\*", head):
                    self.wrappers.add((u.rel, f.name))
        for u in tree.units:
            self.guards |= set(re.findall(r"\.guard\s*=\s*([A-Za-z_]\w*)", u.src.text))
        self._scan_references()

    def _calls(self, unit: Unit, f):
        t = unit.src.text
        for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", t[f.start:f.end]):
            name = m.group(1)
            if name in KEYWORDS or name in TYPE_WORDS:
                continue
            at = f.start + m.start()
            prev = t[max(0, at - 2):at]
            if prev.endswith(".") or prev == "->":
                continue
            open_ = f.start + m.end() - 1
            try:
                close = matching(t, open_)
            except ValueError:
                continue
            yield name, split_args(t[open_ + 1:close]), at

    def _scan_references(self):
        names = set(self.tree.globals)
        for u in self.tree.units:
            names |= set(u.src.funcs)
        for u in self.tree.units:
            t = u.src.text
            defs = {f.name for f in u.src.funcs.values()}
            for m in re.finditer(r"\b([A-Za-z_]\w*)\b(?!\s*\()", t):
                n = m.group(1)
                if n in names:
                    # a function's name not followed by '(' : a pointer to it
                    # (a table row, a registry entry, a callback)
                    if n in defs and re.search(r"\b%s\s*\(" % re.escape(n), t[m.start():m.end() + 2]):
                        continue
                    self.referenced.add(n)
            for f in u.src.funcs.values():
                for name, _args, _at in self._calls(u, f):
                    hit = self.tree.resolve(u, name)
                    if hit:
                        self.called.add((hit[0].rel, name))

    def _fn_arg(self, unit: Unit, text: str, params):
        """What an argument names, when it is a function: an accessor or a game
        function by name, `@param:p` for the caller's own parameter p (passed
        on), else None."""
        text = text.strip()
        if not re.fullmatch(r"[A-Za-z_]\w*", text):
            return None
        if text in params:
            return "@param:" + text
        if self.acc.lookup(text) is not None or text in RAW or self.tree.resolve(unit, text) is not None:
            return text
        return None

    def _call(self, unit: Unit, name, norm, fns, scaffold, site, key, peek=False):
        """The effects of calling `name` with these arguments (normalized in the
        caller's terms, and what each names when it is a function)."""
        hit = self.acc.lookup(name)
        if hit is not None:
            a, write = hit
            if not write and peek:
                return
            layout = Accessors.layout(a, write)
            rest = norm[1:]
            args = tuple((role, rest[i] if i < len(rest) else UNKNOWN)
                         for i, role in enumerate(layout) if role not in ("v", "page"))
            yield Effect("w" if write else "r", a.name, args, scaffold, site, key)
            return
        if name in RAW:
            kind, width = RAW[name]
            yield Effect(kind, "@raw%d" % width, (("addr", norm[1] if len(norm) > 1 else UNKNOWN),),
                         scaffold, site, key)
            return
        if name.startswith("@param:"):
            # a call through the caller's own parameter: resolved where it is bound
            yield Effect("x", name[len("@param:"):], tuple(zip(fns, norm)), scaffold, site, key)
            return
        target = self.tree.resolve(unit, name)
        if target is None:
            return
        cu, cf = target
        binding, bound_fns = {}, {}
        for i, p in enumerate(cf.params):
            binding[p] = norm[i] if i < len(norm) else UNKNOWN
            bound_fns[p] = fns[i] if i < len(fns) else None
        for ce in self.summary(cu, cf):
            if ce.kind != "x":
                nargs = tuple((role, substitute(x, binding)) for role, x in ce.args)
                yield Effect(ce.kind, ce.target, nargs, ce.scaffold or scaffold, ce.site, ce.func)
                continue
            fn = bound_fns.get(ce.target)
            if fn is None:
                continue
            snorm = [substitute(x, binding) for _, x in ce.args]
            sfns = [bound_fns.get(f[len("@param:"):]) if f and f.startswith("@param:") else f for f, _ in ce.args]
            yield from self._call(unit, fn, snorm, sfns, ce.scaffold or scaffold, ce.site, ce.func)

    def summary(self, unit: Unit, f) -> list:
        key = (unit.rel, f.name)
        if key in self.summaries:
            return self.summaries[key]
        if key in self.busy or len(self.busy) > MAX_DEPTH:
            return []
        self.busy.add(key)
        params = tuple(f.params)
        effects = {}
        for name, args, at in self._calls(unit, f):
            scaffold = unit.in_scaffolding(at)
            site = (unit.rel, unit.line(at))
            norm = [self.norm.normalize(unit, a, params) for a in args]
            fns = [self._fn_arg(unit, a, params) for a in args]
            callee = "@param:" + name if name in params else name
            for e in self._call(unit, callee, norm, fns, scaffold, site, key, unit.in_peek(at)):
                effects.setdefault(e.key(), e)
        self.busy.discard(key)
        out = list(effects.values())
        self.summaries[key] = out
        return out

    def roots(self):
        """The functions nothing calls directly: entries, table rows, callbacks,
        and anything referenced by pointer."""
        for u in self.tree.units:
            for name, f in u.src.funcs.items():
                if name in self.guards:
                    continue    # the runtime's question whether a port applies, not the game's logic
                if (u.rel, name) not in self.called or name in self.referenced:
                    yield u, f

    def resolved(self):
        """Every effect reachable from a root, with its cells: [(Effect, bytes, var)]."""
        seen = set()
        out = []
        for u, f in self.roots():
            for e in self.summary(u, f):
                if e.kind == "x":
                    continue    # a call through a parameter nothing bound
                args = tuple((role, evaluate(x)) for role, x in e.args)
                k = (e.kind, e.target, args, e.scaffold, e.site)
                if k in seen:
                    continue
                seen.add(k)
                out.append((e, args))
        return out


def subsystem_of(rel: str) -> str:
    parts = rel.split("/")
    return parts[1] if len(parts) > 2 else "core"


# ---- findings ---------------------------------------------------------------------------------

class Finding:
    def __init__(self, read, read_name, var_name, writers, plain, wrapper):
        self.read = read            # Effect
        self.read_name = read_name  # what the read reads (accessor, or an address)
        self.var_name = var_name    # the variable written as scaffolding
        self.writers = writers      # sorted scaffolding write sites
        self.plain = plain          # sorted plain write sites of the same bytes
        self.wrapper = wrapper      # the read is in a function that takes the machine

    def key(self):
        read = self.read_name.replace("_unchecked", "")
        # a cell no port writes plainly any more is a new finding, not the old one
        return "%s %s %s %s%s" % (self.read.site[0], self.read.func[1], read, self.var_name,
                                  "" if self.plain else " only")

    def subsystem(self):
        return subsystem_of(self.read.site[0])

    def tags(self) -> str:
        return ("plain" if self.plain else "only") + (" wrapper" if self.wrapper else "")

    def describe(self) -> str:
        path, line = self.read.site
        w = ", ".join("%s:%d" % s for s in self.writers[:3]) + (" and %d more" % (len(self.writers) - 3)
                                                                if len(self.writers) > 3 else "")
        p = ("also written plainly at %s" % ", ".join("%s:%d" % s for s in self.plain[:2])
             + (" and %d more" % (len(self.plain) - 2) if len(self.plain) > 2 else "")
             if self.plain else "written only as scaffolding by the ports")
        return ("%s:%d: %s reads %s, which %s writes as scaffolding (%s) [%s]"
                % (path, line, self.read.func[1], self.read_name, w, p, self.tags()))


def analyse(root: pathlib.Path = ROOT, game: pathlib.Path | None = None, map_path: pathlib.Path | None = None,
            writes_only: bool = False):
    """The findings; with `writes_only`, the scaffolding writes instead, as
    {work RAM byte: [(site, variable, subsystem)]}."""
    m = ram_map.RamMap(map_path or ram_map.MAP)
    tree = Tree(game or root / "game", root)
    acc = Accessors(m)
    an = Analysis(tree, acc)

    def cells(e, args):
        """(bytes, variable name or None, subsystem) of an effect."""
        if e.target.startswith("@raw"):
            addr = args[0][1]
            if addr is None:
                return frozenset(), None, subsystem_of(e.site[0])
            base = wram_address(addr)
            if base is None:
                return frozenset(), None, subsystem_of(e.site[0])
            width = int(e.target[4:])
            return frozenset(base + k for k in range(width)), None, subsystem_of(e.site[0])
        a = acc.by_name[e.target]
        return Accessors.cell_bytes(a, dict(args)), a.var.name, a.var.subsystem

    writes_s = collections.defaultdict(list)   # byte -> [(site, var, subsystem)]
    writes_p = collections.defaultdict(list)
    reads = []
    for e, args in an.resolved():
        b, var, sub = cells(e, args)
        if not b:
            continue
        if e.kind == "w":
            for x in b:
                (writes_s if e.scaffold else writes_p)[x].append((e.site, var, sub))
        elif not e.scaffold:
            reads.append((e, dict(args), b, var, sub))
    if writes_only:
        return writes_s

    by_key = {}
    for e, args, b, var, sub in reads:
        hits = collections.defaultdict(set)
        plain = set()
        for x in b:
            for site, wvar, wsub in writes_s.get(x, ()):
                if meets(var, sub, wvar, wsub):
                    hits[wvar or "$%06X" % x].add(site)
            for site, wvar, wsub in writes_p.get(x, ()):
                if meets(var, sub, wvar, wsub):
                    plain.add(site)
        for wvar, sites in hits.items():
            name = e.target if not e.target.startswith("@raw") else "$%06X" % min(b)
            if args.get("index") is not None:
                name += "[%d]" % args["index"]   # the element, where the source says which
            f = Finding(e, name, wvar, sorted(sites), sorted(plain), e.func in an.wrappers)
            old = by_key.get(f.key())
            if old is None or f.read.site < old.read.site:
                by_key[f.key()] = f
    return sorted(by_key.values(), key=lambda f: (f.read.site, f.var_name))


# ---- the original's reads, seen in runs ------------------------------------------------------

ORIGINAL_READ = re.compile(r"^scaffold read by the original: \$([0-9A-F]{2}):([0-9A-F]{4}) at ([0-9A-F]{2}):([0-9A-F]{4}) "
                           r"\((\d+) times?, first at frame (\d+), written by (\S+)\)", re.M)
# The subsystem a port's name begins with, for picking its own write among a
# scratch byte's many.
NAME_SUBSYSTEMS = ("battle", "field", "event", "menu", "scene", "overworld", "attract", "sound", "console")


def original_reads(logs, root: pathlib.Path = ROOT, map_path: pathlib.Path | None = None, out=sys.stdout) -> int:
    """`ct_dev --scaffold-reads original`'s lines over the logs, by the byte and
    the instruction that read it, each with the source of the scaffolding
    write that left the byte (file:line, the writer's subsystem's where the byte
    has several): where the original's still-generated code reads what the
    native build leaves out, which is what a failure of the native-shape build
    is traced through. Returns how many (byte, instruction) pairs."""
    seen = {}
    for log in logs:
        text = pathlib.Path(log).read_text(errors="replace")
        for m in ORIGINAL_READ.finditer(text):
            bank, low, rbank, rlow, n, frame, writer = m.groups()
            key = (int(bank, 16) << 16 | int(low, 16), int(rbank, 16) << 16 | int(rlow, 16), writer)
            runs, total, names = seen.get(key, (0, 0, set()))
            seen[key] = (runs + 1, total + int(n), names | {pathlib.Path(log).stem})
    if not seen:
        return 0
    writes = analyse(root, root / "game", map_path, writes_only=True)
    for (byte, pc, writer), (runs, total, names) in sorted(seen.items(), key=lambda kv: (kv[0][2], kv[0][0])):
        sites = writes.get(byte, [])
        prefix = next((s for s in NAME_SUBSYSTEMS if writer.startswith(s + "_")), None)
        own = [s for s in sites if prefix and subsystem_of(s[0][0]) == prefix]
        where = ", ".join(sorted({"%s:%d" % s[0] for s in (own or sites)})[:4]) or "no write the source shows"
        print("$%02X:%04X read at %02X:%04X (%d times in %d run%s), written as scaffolding by %s: %s"
              % (byte >> 16, byte & 0xFFFF, pc >> 16, pc & 0xFFFF, total, runs, "" if runs == 1 else "s", writer,
                 where), file=out)
    return len(seen)


# ---- reads seen in runs -----------------------------------------------------------------------

SCAFFOLD_READ = re.compile(r"^scaffold read: \$([0-9A-F]{2}):([0-9A-F]{4}) by (\S+) \((\d+) times?, "
                           r"first at frame (\d+)(?:, written by (\S+))?\)", re.M)


class RunFinding:
    """A port's read, in a shipping-mode run, of a work RAM byte whose latest
    write was scaffolding (ct_dev --scaffold-reads): what the native build
    would have read stale there, in order, not just in the source."""

    def __init__(self, entry, var_name, sub):
        self.entry = entry
        self.var_name = var_name
        self.sub = sub
        self.addrs = set()
        self.count = 0
        self.movies = {}          # movie -> first frame
        self.writers = set()      # the ports whose scaffolding wrote it

    def key(self):
        return "run %s %s" % (self.entry, self.var_name)

    def subsystem(self):
        return self.sub

    def describe(self) -> str:
        where = ", ".join("%s from frame %d" % kv for kv in sorted(self.movies.items())[:3])
        more = " and %d more movie(s)" % (len(self.movies) - 3) if len(self.movies) > 3 else ""
        by = " (%s's)" % ", ".join(sorted(self.writers)) if self.writers else ""
        return ("run: %s read %s (%s) %d times where scaffolding%s had written it last, in %s%s"
                % (self.entry, self.var_name, ", ".join("$%02X:%04X" % (a >> 16, a & 0xFFFF)
                                                         for a in sorted(self.addrs)[:4]),
                   self.count, by, where, more))


def entry_subsystem(entry: str):
    first = entry.split("_")[0]
    return first if first in ram_map.SUBSYSTEMS else None


def run_findings(logs, m: ram_map.RamMap) -> list:
    """The `scaffold read:` lines of ct_dev logs, by port and variable."""
    out = {}
    for log in logs:
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        movie = re.sub(r"-ship$", "", pathlib.Path(log).stem)
        for mo in SCAFFOLD_READ.finditer(text):
            addr = int(mo.group(1), 16) << 16 | int(mo.group(2), 16)
            entry = mo.group(3)
            sub = entry_subsystem(entry)
            hits = m.lookup(addr, sub)
            var = hits[0][0].name if hits else "$%06X" % addr
            f = out.setdefault((entry, var), RunFinding(entry, var, sub or (hits[0][0].subsystem if hits else "core")))
            f.addrs.add(addr)
            f.count += int(mo.group(4))
            frame = int(mo.group(5))
            f.movies[movie] = min(frame, f.movies.get(movie, frame))
            if mo.group(6) and mo.group(6) != "?":
                f.writers.add(mo.group(6))
    return sorted(out.values(), key=lambda f: f.key())


# ---- the baseline -----------------------------------------------------------------------------

HEADER = """# Reads, outside scaffolding, of cells some port writes inside ORIGINAL_EFFECT
# (tools/effects_check.py). The native-shape build compiles those writes out,
# so each is a value that build may read stale (docs/PORTING.md, "Scaffolding").
# --check fails on a finding not listed here. A line is the read's file and
# function, what it reads, and the variable written as scaffolding, then an
# optional `# reason`. The findings of main and of the branches in flight when
# the check came in are listed unanswered; answering one -- making the write
# plain, or showing where the value the read takes comes from and saying so
# after the line -- is how the list empties. A new finding is answered the same
# way before it merges: a line added here says why.
"""


def read_baseline(path: pathlib.Path = BASELINE) -> dict:
    """{finding key: reason or ""}."""
    if not path.exists():
        return {}
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        body, _, reason = line.partition("#")
        body = " ".join(body.split())
        if body:
            out[body] = reason.strip()
    return out


def write_baseline(base: dict, path: pathlib.Path = BASELINE) -> None:
    width = max((len(k) for k in base), default=0)
    lines = [(k.ljust(width) + "   # " + base[k]) if base[k] else k for k in sorted(base)]
    path.write_text(HEADER + "\n" + "".join(line + "\n" for line in lines), encoding="utf-8", newline="\n")


def check(findings: list, base: dict, out=sys.stdout, baseline: pathlib.Path = BASELINE) -> int:
    """The ratchet: 1 when a finding isn't in the baseline. A baseline line
    nothing produces is reported, not failed: the baseline also holds what the
    branches in flight had (--prune drops them once they have merged)."""
    keys = {f.key() for f in findings}
    new = [f for f in findings if f.key() not in base]
    gone = sorted(set(base) - keys)
    for f in new:
        print("  " + f.describe(), file=out)
    if gone:
        print("  (%d baseline line%s no longer found here; --prune drops them)"
              % (len(gone), "" if len(gone) == 1 else "s"), file=out)
    runs = sum(1 for f in findings if isinstance(f, RunFinding))
    print("effects: %d new finding%s, %d accepted in the baseline (%d read%s in the source, %d in runs)"
          % (len(new), "" if len(new) == 1 else "s", len(keys & set(base)), len(findings) - runs,
             "" if len(findings) - runs == 1 else "s", runs), file=out)
    return 1 if new else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python3 tools/effects_check.py", description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", type=pathlib.Path, default=ROOT, help="the tree (game/ under it)")
    ap.add_argument("--map", type=pathlib.Path, default=None, help="the RAM map (default re/ram.toml under --root)")
    ap.add_argument("--baseline", type=pathlib.Path, default=BASELINE)
    ap.add_argument("--check", action="store_true", help="the gate's ratchet against the baseline")
    ap.add_argument("--new", action="store_true", help="list only the findings the baseline doesn't have")
    ap.add_argument("--write-baseline", action="store_true",
                    help="add today's findings to the baseline (lines already there are kept)")
    ap.add_argument("--prune", action="store_true", help="drop baseline lines nothing produces any more")
    ap.add_argument("--originals", nargs="+", metavar="LOG",
                    help="list the original's code's reads of scaffolding in these ct_dev --scaffold-reads original "
                         "logs, with the source of each write, and exit")
    ap.add_argument("--runs", nargs="*", default=[], metavar="LOG",
                    help="ct_dev --scaffold-reads logs (tools/vblank_check.py's shipping runs write them)")
    args = ap.parse_args(argv)
    root = args.root.resolve()
    map_path = args.map or root / "re" / "ram.toml"
    if args.originals:
        n = original_reads(args.originals, root, map_path)
        print("effects: %d read%s by the original's code of bytes scaffolding wrote last"
              % (n, "" if n == 1 else "s"))
        return 0
    findings = analyse(root, root / "game", map_path)
    if args.runs:
        findings += run_findings(args.runs, ram_map.RamMap(map_path))
    if args.write_baseline or args.prune:
        base = read_baseline(args.baseline)
        keys = {f.key() for f in findings}
        if args.prune:
            # a run's line goes only when runs were read
            base = {k: v for k, v in base.items() if k in keys or (k.startswith("run ") and not args.runs)}
        if args.write_baseline:
            for k in keys:
                base.setdefault(k, "")
        write_baseline(base, args.baseline)
        print("effects: %d line(s) in %s" % (len(base), args.baseline))
        return 0
    if args.check:
        return check(findings, read_baseline(args.baseline))
    base = read_baseline(args.baseline) if args.new else {}
    by_sub = collections.Counter()
    shown = [f for f in findings if f.key() not in base]
    for f in shown:
        print(f.describe())
        by_sub[f.subsystem()] += 1
    print("effects: %d finding%s%s" % (len(shown), "" if len(shown) == 1 else "s",
                                       " (" + ", ".join("%s %d" % kv for kv in sorted(by_sub.items())) + ")"
                                       if by_sub else ""))
    return 1 if shown else 0


if __name__ == "__main__":
    sys.exit(main())
