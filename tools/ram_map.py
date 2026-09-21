"""The RAM map: names for the game's variables in work RAM, and the header ports use.

    python3 tools/ram_map.py check                 # validate re/ram.toml
    python3 tools/ram_map.py header [--check]      # write (or compare) game/ram.h
    python3 tools/ram_map.py accessors [--check]   # game/vars.h and its RAM backing
    python3 tools/ram_map.py coverage [--open] [--subsystem NAME]
    python3 tools/ram_map.py lookup 7E:95D5 [--page battle]

`re/ram.toml` is the knowledge; its schema is at the top of the file and in
docs/DISASSEMBLY.md ("The RAM map"). Two things are generated from it and
committed, so a build without a ROM (CI's) has them, and tests/test_ram_map.py
fails when either is out of date:

- `game/ram.h`, the variables as addresses, for generated code and the ports
  not converted yet;
- `game/vars.h`, the variables as values a port reads and writes, with
  `runtime/port/vars_ram.c` backing them in emulated work RAM. This is what a
  port uses: it names state without saying where it lives, so the storage can
  move once a subsystem no longer shares it with generated code
  (docs/PLAN.md, "Phase 3a").

**Coverage** answers "how much of what game/ touches has a name". It counts,
over game/ with comments stripped:

- *low RAM*: four-digit literals `0x0100`-`0x1FFF`, the game's variables in
  the bank-0 mirror of `$7E:0000-$1FFF`;
- *long WRAM*: six-digit literals in banks `$7E` and `$7F`;
- and any `RAM_` name from the header, at the address it stands for.

A use is named when it falls inside a map entry (any element, any byte of a
field) of its file's subsystem, of `core`, or of the subsystem sharing its page
(field and event, scene and attract, menu and menu_object): work RAM is reused
between modes, so the
battle's name for a byte says nothing about the field's use of it. An address
is named when every subsystem using it has a name for it. The counts are of distinct addresses and of uses. They over-count
what is open in one way: a four-digit literal in range is not always an
address (a VRAM word address, a count, a mask), and the listing shows them for
a porter to judge. Absolute operands of `$2000` and up, which the battle and
menu code reach through a data bank of `$7E`, are not counted: nothing tells a
literal `0x95D5` apart from a constant without reading the code around it, so
they are listed only where a map entry already claims them (`--abs`).
"""

import argparse
import collections
import pathlib
import re
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parent.parent
MAP = ROOT / "re" / "ram.toml"
HEADER = ROOT / "game" / "ram.h"
GAME = ROOT / "game"

# game/'s directories, plus the tags for a directory whose code is two modes
# reusing the same bytes: `menu_object` is bank $C2's sprite-object mode (the
# one at $C2:0000), whose slots, palette and page cells lie under the menus
# proper's. A tag is not a directory, so nothing in game/ is scanned as one;
# it names bytes for the directory it shares with (SHARES).
SUBSYSTEMS = ("core", "data", "event", "field", "menu", "menu_object", "battle", "scene", "attract")
NAME = re.compile(r"^[a-z][a-z0-9_]*$")
ADDR = re.compile(r"^(7[EF]):([0-9A-Fa-f]{4})$")


class MapError(Exception):
    pass


def parse_addr(text, where):
    m = ADDR.match(text) if isinstance(text, str) else None
    if not m:
        raise MapError("%s: address %r is not BB:AAAA in bank 7E or 7F" % (where, text))
    return int(m.group(1), 16) << 16 | int(m.group(2), 16)


def fmt_addr(a):
    return "$%02X:%04X" % (a >> 16, a & 0xFFFF)


class Page:
    """A direct page the game sets while some code runs: D = `d`, or unknown."""

    def __init__(self, raw):
        where = "page %r" % raw.get("name")
        for key in ("name", "subsystem", "meaning", "source"):
            if not raw.get(key):
                raise MapError("%s: missing %s" % (where, key))
        unknown = set(raw) - {"name", "d", "subsystem", "meaning", "source", "why"}
        if unknown:
            raise MapError("%s: unknown keys %s" % (where, sorted(unknown)))
        self.name = raw["name"]
        if not NAME.match(self.name):
            raise MapError("%s: name is not lower_snake_case" % where)
        self.subsystem = raw["subsystem"]
        if self.subsystem not in SUBSYSTEMS:
            raise MapError("%s: subsystem %r is not one of %s" % (where, self.subsystem, SUBSYSTEMS))
        self.d = raw.get("d")
        if self.d is None:
            if not raw.get("why"):
                raise MapError("%s: a page whose D isn't known needs `why`" % where)
        elif not (isinstance(self.d, int) and 0 <= self.d <= 0x1F00):
            raise MapError("%s: d must be an integer in $0000-$1F00 (work RAM)" % where)
        self.meaning = raw["meaning"]
        self.source = raw["source"]
        self.why = raw.get("why")


class Image:
    """Work RAM holding a program image for some subsystems."""

    def __init__(self, raw):
        where = "image %r" % raw.get("name")
        for key in ("name", "start", "end", "subsystem", "meaning", "source"):
            if not raw.get(key):
                raise MapError("%s: missing %s" % (where, key))
        unknown = set(raw) - {"name", "start", "end", "subsystem", "meaning", "source"}
        if unknown:
            raise MapError("%s: unknown keys %s" % (where, sorted(unknown)))
        self.name = raw["name"]
        self.start = parse_addr(raw["start"], where)
        self.end = parse_addr(raw["end"], where)
        if self.end < self.start or self.end >> 16 != self.start >> 16:
            raise MapError("%s: end must follow start in the same bank" % where)
        subs = raw["subsystem"]
        self.subsystems = [subs] if isinstance(subs, str) else list(subs)
        for sub in self.subsystems:
            if sub not in SUBSYSTEMS:
                raise MapError("%s: subsystem %r is not one of %s" % (where, sub, SUBSYSTEMS))
        self.meaning = raw["meaning"]
        self.source = raw["source"]

    def contains(self, a, subsystem):
        return subsystem in self.subsystems and self.start <= a <= self.end


class NotRam:
    """Literals in one file that are addresses in another space (VRAM, the
    SPC700's memory) or plain numbers, so the coverage count can leave them out;
    or ([[offset]], with `bank`) four-digit offsets in bank $7F, which the code
    adds to the bank's base, so the count reads them as that bank's address."""

    def __init__(self, raw, kind="not_ram"):
        where = "%s %r" % (kind, raw.get("file"))
        keys = ("file", "values", "why") + (("bank",) if kind == "offset" else ())
        for key in keys:
            if not raw.get(key):
                raise MapError("%s: missing %s" % (where, key))
        unknown = set(raw) - set(keys)
        if unknown:
            raise MapError("%s: unknown keys %s" % (where, sorted(unknown)))
        self.file = raw["file"]
        if not (GAME / self.file).is_file():
            raise MapError("%s: no such file under game/" % where)
        self.values = set(raw["values"])
        if not all(isinstance(v, int) for v in self.values):
            raise MapError("%s: values must be integers" % where)
        self.why = raw["why"]
        self.bank = None
        if kind == "offset":
            if raw["bank"] not in ("7E", "7F"):
                raise MapError("%s: bank must be \"7E\" or \"7F\"" % where)
            self.bank = int(raw["bank"], 16)
            if not all(0 <= v <= 0xFFFF for v in self.values):
                raise MapError("%s: values must be 16-bit offsets" % where)


class Field:
    def __init__(self, raw, var):
        where = "%s field %r" % (var.name, raw.get("name"))
        for key in ("name", "offset", "meaning"):
            if key not in raw:
                raise MapError("%s: missing %s" % (where, key))
        unknown = set(raw) - {"name", "offset", "size", "meaning"}
        if unknown:
            raise MapError("%s: unknown keys %s" % (where, sorted(unknown)))
        self.name = raw["name"]
        if not NAME.match(self.name):
            raise MapError("%s: name is not lower_snake_case" % where)
        self.offset = raw["offset"]
        self.size = raw.get("size", 1)
        self.meaning = raw["meaning"]
        if not (isinstance(self.offset, int) and isinstance(self.size, int) and self.size > 0
                and 0 <= self.offset and self.offset + self.size <= var.stride):
            raise MapError("%s: offset/size must lie inside the element's stride ($%X)" % (where, var.stride))


class Var:
    """A variable, an array of them, or an array of records."""

    KEYS = {"name", "addr", "size", "count", "stride", "index", "page", "dp",
            "subsystem", "meaning", "source", "field"}

    def __init__(self, raw, pages):
        where = "var %r" % raw.get("name")
        for key in ("name", "subsystem", "meaning", "source"):
            if not raw.get(key):
                raise MapError("%s: missing %s" % (where, key))
        unknown = set(raw) - self.KEYS
        if unknown:
            raise MapError("%s: unknown keys %s" % (where, sorted(unknown)))
        self.name = raw["name"]
        if not NAME.match(self.name):
            raise MapError("%s: name is not lower_snake_case" % where)
        self.subsystem = raw["subsystem"]
        if self.subsystem not in SUBSYSTEMS:
            raise MapError("%s: subsystem %r is not one of %s" % (where, self.subsystem, SUBSYSTEMS))
        self.meaning = raw["meaning"]
        self.source = raw["source"]
        self.index = raw.get("index")
        self.size = raw.get("size", 1)
        self.count = raw.get("count", 1)
        self.stride = raw.get("stride", self.size)
        for key, v in (("size", self.size), ("count", self.count), ("stride", self.stride)):
            if not (isinstance(v, int) and v > 0):
                raise MapError("%s: %s must be a positive integer" % (where, key))
        if self.count > 1 and not self.index:
            raise MapError("%s: an array needs `index` (what selects an element)" % where)
        if self.stride < self.size:
            raise MapError("%s: stride $%X is smaller than size $%X" % (where, self.stride, self.size))
        self.fields = [Field(f, self) for f in raw.get("field", [])]
        if self.fields and "size" in raw:
            raise MapError("%s: a record's size is its stride; give fields instead" % where)
        if self.fields:
            self.size = self.stride
        seen = set()
        for f in self.fields:
            if f.name in seen:
                raise MapError("%s: field %r twice" % (where, f.name))
            seen.add(f.name)

        self.page = None
        self.dp = None
        if "page" in raw:
            if raw["page"] not in pages:
                raise MapError("%s: no page %r" % (where, raw["page"]))
            self.page = pages[raw["page"]]
        if self.page is not None and self.page.d is None:
            # D isn't known statically: the variable has an offset, not an address.
            if "addr" in raw:
                raise MapError("%s: page %r has no known D, so the variable can't have an address"
                               % (where, self.page.name))
            if not (isinstance(raw.get("dp"), int) and 0 <= raw["dp"] <= 0xFF):
                raise MapError("%s: needs dp, an offset $00-$FF in page %r" % (where, self.page.name))
            self.addr = None
            self.dp = raw["dp"]
        else:
            if "dp" in raw:
                raise MapError("%s: dp is derived from addr and the page's D; give addr" % where)
            if "addr" not in raw:
                raise MapError("%s: missing addr" % where)
            self.addr = parse_addr(raw["addr"], where)
            if self.end() > (self.addr | 0xFFFF) + 1:
                raise MapError("%s: runs past the end of bank $%02X" % (where, self.addr >> 16))
            if self.page is not None:
                self.dp = (self.addr & 0xFFFF) - self.page.d
                if self.addr >> 16 != 0x7E or not 0 <= self.dp <= 0xFF:
                    raise MapError("%s: %s is not in page %r (D = $%04X)"
                                   % (where, fmt_addr(self.addr), self.page.name, self.page.d))

    def span(self):
        return self.stride * (self.count - 1) + self.size

    def end(self):
        return self.addr + self.span()

    def contains(self, a):
        """The field (or None) when address `a` is inside this variable, else False."""
        if self.addr is None or not self.addr <= a < self.end():
            return False
        off = (a - self.addr) % self.stride
        if off >= self.size:
            return False
        for f in self.fields:
            if f.offset <= off < f.offset + f.size:
                return f
        return None


class RamMap:
    def __init__(self, path=MAP, text=None):
        doc = tomllib.loads(text if text is not None else path.read_text(encoding="utf-8"))
        unknown = set(doc) - {"page", "var", "image", "not_ram", "offset"}
        if unknown:
            raise MapError("unknown top-level keys %s" % sorted(unknown))
        self.pages = {}
        for raw in doc.get("page", []):
            p = Page(raw)
            if p.name in self.pages:
                raise MapError("page %r twice" % p.name)
            self.pages[p.name] = p
        self.vars = [Var(raw, self.pages) for raw in doc.get("var", [])]
        self.images = [Image(raw) for raw in doc.get("image", [])]
        self.not_ram = collections.defaultdict(set)
        for raw in doc.get("not_ram", []):
            n = NotRam(raw)
            self.not_ram[n.file] |= n.values
        self.offsets = collections.defaultdict(dict)
        for raw in doc.get("offset", []):
            n = NotRam(raw, "offset")
            for v in n.values:
                if v in self.not_ram[n.file]:
                    raise MapError("offset %r: $%04X is also listed as not work RAM" % (n.file, v))
                self.offsets[n.file][v] = n.bank
        names = collections.Counter(v.name for v in self.vars)
        dup = [n for n, c in names.items() if c > 1]
        if dup:
            raise MapError("names used twice: %s" % ", ".join(sorted(dup)))
        self._check_overlaps()
        self.macros = header_macros(self)   # also checks the generated names are unique
        accessors(self)                     # the same, for game/vars.h

    def _check_overlaps(self):
        """Work RAM is reused from one game mode to the next, so two entries may
        cover the same bytes when their subsystems differ (the battle's use of
        $7E:2000 is not the field's). Within one subsystem that is a mistake."""
        placed = sorted((v for v in self.vars if v.addr is not None), key=lambda v: v.addr)
        for i, a in enumerate(placed):
            for b in placed[i + 1:]:
                if b.addr >= a.end():
                    break
                if a.subsystem != b.subsystem:
                    continue
                if any(a.contains(x) is not False and b.contains(x) is not False
                       for x in range(b.addr, min(a.end(), b.end()))):
                    raise MapError("%s and %s overlap (both %s)" % (a.name, b.name, a.subsystem))

    def lookup(self, a, subsystem=None):
        """Entries naming address `a`, as (var, field); `subsystem` first."""
        hits = [(v, v.contains(a)) for v in self.vars if v.contains(a) is not False]
        hits.sort(key=lambda h: (h[0].subsystem != subsystem, h[0].addr))
        return hits


# ---- the header -----------------------------------------------------------------------------

def macro(*parts):
    return "RAM_" + "_".join(p.upper() for p in parts)


def comment(text, width=100, indent=""):
    words = " ".join(text.split()).replace("*/", "* /").split(" ")
    lines, cur = [], indent + "/*"
    for w in words:
        if len(cur) + 1 + len(w) > width - 3:
            lines.append(cur)
            cur = indent + " *"
        cur += " " + w
    lines.append(cur + " */")
    return lines


def header_macros(m):
    """[(section, [lines])] and the set of names; raises on a name clash."""
    names = set()

    def define(lines, name, value, note=None):
        if name in names:
            raise MapError("generated name %s twice" % name)
        names.add(name)
        text = "#define %s %s" % (name, value)
        if note:
            text = "%-44s /* %s */" % (text, note)
        lines.append(text)

    sections = []
    lines = []
    for p in sorted(m.pages.values(), key=lambda p: (p.d is None, p.d or 0, p.name)):
        lines += comment("%s: %s" % (p.name, p.meaning))
        if p.d is None:
            lines += comment("D is not known statically: %s" % p.why)
            lines.append("/* (no RAM_PAGE_%s: variables in it have offsets only) */" % p.name.upper())
        else:
            define(lines, "RAM_PAGE_" + p.name.upper(), "0x%04Xu" % p.d)
    sections.append(("direct pages", lines))

    by_sub = collections.defaultdict(list)
    for v in m.vars:
        by_sub[v.subsystem].append(v)
    for sub in SUBSYSTEMS:
        vs = sorted(by_sub.get(sub, []), key=lambda v: (v.addr is None, v.addr or 0, v.dp or 0, v.name))
        if not vs:
            continue
        lines = []
        for v in vs:
            desc = v.meaning
            if v.count > 1:
                desc += " [%d x $%X bytes, by %s]" % (v.count, v.stride, v.index)
            elif v.size > 1:
                desc += " [%d bytes]" % v.size
            lines += comment(desc)
            if v.addr is not None:
                define(lines, macro(v.name), "0x%06Xu" % v.addr)
            if v.count > 1:
                define(lines, macro(v.name, "count"), "%du" % v.count)
                define(lines, macro(v.name, "stride"), "0x%02Xu" % v.stride)
            if v.page is not None:
                where = ("page %s, D unknown" % v.page.name if v.page.d is None
                         else "page %s" % v.page.name)
                define(lines, macro(v.name, "dp"), "0x%02Xu" % v.dp, where)
            for f in v.fields:
                if v.addr is not None:
                    define(lines, macro(v.name, f.name), "0x%06Xu" % (v.addr + f.offset), f.meaning)
                else:
                    define(lines, macro(v.name, f.name, "dp"), "0x%02Xu" % (v.dp + f.offset), f.meaning)
        sections.append((sub, lines))
    return sections, names


def render_header(m):
    sections, _ = m.macros
    out = [
        "/* Names for the game's variables in work RAM, generated by tools/ram_map.py",
        " * from re/ram.toml. Don't edit this file: add to the map and regenerate",
        " * (python3 tools/ram_map.py header). docs/DISASSEMBLY.md, \"The RAM map\".",
        " *",
        " * RAM_<NAME> is a long address ($7E/$7F), for m_read8 and friends; an array's",
        " * or record's is element 0's, with _COUNT and _STRIDE beside it, and a",
        " * record's fields are RAM_<NAME>_<FIELD>. RAM_<NAME>_DP is the offset of a",
        " * variable the game reaches through a direct page (RAM_PAGE_<PAGE> is that",
        " * page's D), for m_dp. RAM_ABS gives the 16-bit operand of an absolute",
        " * access, for code that reaches work RAM through the data bank. */",
        "#ifndef CT_GAME_RAM_H",
        "#define CT_GAME_RAM_H",
        "",
        "#include <stdint.h>",
        "",
        "#define RAM_ABS(addr) ((uint16_t)((addr) & 0xFFFFu))",
    ]
    for title, lines in sections:
        out += ["", "/* ---- %s %s */" % (title, "-" * (86 - len(title))), ""]
        out += lines
    out += ["", "#endif", ""]
    return "\n".join(out)


# ---- typed accessors ------------------------------------------------------------------------
#
# The header a port includes to reach the game's state: a value per variable, an
# index per array element, a pair per field of a record, and nothing about
# layout. `runtime/port/vars_ram.c` satisfies it against emulated work RAM while
# the gate still compares bytes; the storage swaps behind it, per subsystem, once
# no generated code touches that subsystem's state (docs/PLAN.md, "Phase 3a").

VARS_HEADER = GAME / "vars.h"
BACKING = ROOT / "runtime" / "port" / "vars_ram.c"

# A variable this wide or narrower is one C value; a wider one is a buffer, and
# its bytes are reached one at a time.
WIDEST = 4
C_TYPE = {1: "uint8_t", 2: "uint16_t", 3: "uint32_t", 4: "uint32_t"}

# What the map's `index` calls an element, where it uses one of these words, so
# the accessor's parameter says what the caller passes. Most specific first.
INDEX_WORDS = ("slot", "member", "corner", "channel", "column", "level", "line", "layer", "row")


def index_param(index):
    """What to call the index parameter: the word the map's `index` leads with,
    where it is one of these, and `i` otherwise. Only the phrase's head counts --
    what follows a bracket or a comma explains the numbering rather than naming
    the thing ("by byte (two rows of five tiles per member)" is not by member)."""
    head = re.split(r"[(,]", index or "")[0].lower()
    words = set(re.findall(r"[a-z]+", head))
    for w in INDEX_WORDS:
        if w in words:
            return w
    return "i"


class Accessor:
    """One variable, or one field of a record: a value a port reads and writes.

    A value wider than WIDEST bytes is a buffer with no one C type, so its pair
    moves a byte at a time and says so in its name."""

    def __init__(self, var, field=None):
        self.var = var
        base = var.name if field is None else "%s_%s" % (var.name, field.name)
        self.addr = var.addr + (field.offset if field else 0)
        self.size = field.size if field else var.size
        self.meaning = field.meaning if field else var.meaning
        self.blob = self.size > WIDEST
        self.name = base + "_byte" if self.blob else base
        self.type = "uint8_t" if self.blob else C_TYPE[self.size]
        self.bytes_const = base.upper() + "_BYTES" if self.blob else None

    def params(self, write):
        """The parameter list after the state, as (declaration, argument) pairs."""
        out = []
        if self.var.count > 1:
            out.append(("unsigned " + index_param(self.var.index), index_param(self.var.index)))
        if self.blob:
            out.append(("unsigned at", "at"))
        if write:
            out.append((self.type + " v", "v"))
        return out

    def signature(self, write):
        state = "GameState *g" if write else "const GameState *g"
        args = ", ".join([state] + [d for d, _ in self.params(write)])
        return "%s %s%s(%s)" % ("void" if write else self.type, "set_" if write else "", self.name, args)

    def address(self):
        out = "0x%06Xu" % self.addr
        if self.var.count > 1:
            out += " + (uint32_t)%s * 0x%02Xu" % (index_param(self.var.index), self.var.stride)
        if self.blob:
            out += " + at"
        return out

    def note(self):
        """What the accessor is, for the comment above it. What an array is
        indexed by is on its count, once, rather than on every field."""
        return self.meaning + (" [%d bytes, one at a time]" % self.size if self.blob else "")


def accessors(m):
    """[(subsystem, [(var, count constant or None, [Accessor])])], in the header's
    order; raises on a name clash.

    The variables whose page has no known D are left out: their address is the
    caller's D plus an offset, so an accessor would need a page base nothing has
    a shape for yet (docs/PLAN.md, "Phase 3a")."""
    by_sub = collections.defaultdict(list)
    names, consts = set(), set()

    def take(name, seen, what):
        if name in seen:
            raise MapError("generated %s %s twice" % (what, name))
        seen.add(name)
        return name

    for v in m.vars:
        if v.addr is None:
            continue
        group = [Accessor(v, f) for f in v.fields] if v.fields else [Accessor(v)]
        for a in group:
            take(a.name, names, "accessor")
            take("set_" + a.name, names, "accessor")
            if a.bytes_const:
                take(a.bytes_const, consts, "constant")
        count = take(v.name.upper() + "_COUNT", consts, "constant") if v.count > 1 else None
        by_sub[v.subsystem].append((v, count, group))
    out = []
    for sub in SUBSYSTEMS:
        vs = by_sub.get(sub)
        if vs:
            out.append((sub, sorted(vs, key=lambda g: (g[0].addr, g[0].name))))
    return out


def render_vars_header(m):
    out = [
        "/* The game's state, as values a port reads and writes, generated by",
        " * tools/ram_map.py from re/ram.toml. Don't edit this file: add to the map and",
        " * regenerate (python3 tools/ram_map.py accessors). docs/DISASSEMBLY.md, \"The",
        " * RAM map\".",
        " *",
        " * There is a value per variable, an index per element of an array -- what the",
        " * map's `index` says selects one, never a byte offset -- and a pair per field of",
        " * a record. Nothing here says where the state lives or how it is arranged, which",
        " * is the point: a port written against these keeps working when the storage",
        " * moves. runtime/port/vars_ram.c is what backs them today, with the game's",
        " * variables in emulated work RAM so that shadow mode still compares bytes",
        " * (docs/PLAN.md, \"Phase 3a\"). A buffer wider than %d bytes has no one C value,"
        % WIDEST,
        " * so its bytes are read and written one at a time (`<name>_byte`). How many",
        " * elements an array has, and how many bytes such a buffer has, are named too,",
        " * so that a port that walks one need not know a number.",
        " *",
        " * game/ram.h names the same variables as addresses, for generated code and the",
        " * ports not converted yet. */",
        "#ifndef CT_GAME_VARS_H",
        "#define CT_GAME_VARS_H",
        "",
        "#include <stdint.h>",
        "",
        "#include \"state.h\"",
    ]
    for sub, groups in accessors(m):
        out += ["", "/* ---- %s %s */" % (sub, "-" * (86 - len(sub)))]
        for v, count, group in groups:
            out.append("")
            if count:
                out.append("enum { %s = %d };%s" % (count, v.count, "   /* by %s */" % v.index))
            for a in group:
                out += comment(a.note())
                if a.bytes_const:
                    out.append("enum { %s = %d };" % (a.bytes_const, a.size))
                out.append("%s;" % a.signature(False))
                out.append("%s;" % a.signature(True))
    out += ["", "#endif", ""]
    return "\n".join(out)


def render_backing(m):
    out = [
        "/* The game's state kept in emulated work RAM: the backing for game/vars.h,",
        " * generated by tools/ram_map.py from re/ram.toml. Don't edit this file: add to",
        " * the map and regenerate (python3 tools/ram_map.py accessors).",
        " *",
        " * This is the seam the platform services use, for state rather than hardware: an",
        " * interface game/ includes and a backend under runtime/ that satisfies it. Every",
        " * accessor reads and writes the address the original's code does, so a port that",
        " * uses them writes the same bytes and shadow mode still compares them. The",
        " * second backing, which holds the same values in its own storage, replaces this",
        " * file per subsystem once no generated code touches that subsystem's state",
        " * (docs/PLAN.md, \"Phase 3a\"); nothing in game/ changes with it. */",
        "#include \"vars.h\"",
        "",
        "#include \"port/machine.h\"",
        "#include \"port/vars_ram.h\"",
        "#include \"wram.h\"",
        "",
        "/* A value of the width the map gives, low byte first, as the 65816 stores it. */",
        "static uint8_t r8(const GameState *g, uint32_t a) { return m_read8(state_snes(g), a); }",
        "static uint16_t r16(const GameState *g, uint32_t a) { return m_read16(state_snes(g), a); }",
        "",
        "static uint32_t r24(const GameState *g, uint32_t a)",
        "{",
        "    return (uint32_t)m_read16(state_snes(g), a) | (uint32_t)m_read8(state_snes(g), a + 2) << 16;",
        "}",
        "",
        "static uint32_t r32(const GameState *g, uint32_t a)",
        "{",
        "    return (uint32_t)m_read16(state_snes(g), a) | (uint32_t)m_read16(state_snes(g), a + 2) << 16;",
        "}",
        "",
        "static void w8(GameState *g, uint32_t a, uint8_t v) { m_write8(state_snes(g), a, v); }",
        "static void w16(GameState *g, uint32_t a, uint16_t v) { m_write16(state_snes(g), a, v); }",
        "",
        "static void w24(GameState *g, uint32_t a, uint32_t v)",
        "{",
        "    m_write16(state_snes(g), a, (uint16_t)v);",
        "    m_write8(state_snes(g), a + 2, (uint8_t)(v >> 16));",
        "}",
        "",
        "static void w32(GameState *g, uint32_t a, uint32_t v)",
        "{",
        "    m_write16(state_snes(g), a, (uint16_t)v);",
        "    m_write16(state_snes(g), a + 2, (uint16_t)(v >> 16));",
        "}",
        "",
        "/* The escape in game/wram.h: work RAM the game locates at run time, which the",
        " * map has no name for yet. */",
        "uint8_t wram_read8(const GameState *g, uint32_t at) { return r8(g, at); }",
        "uint16_t wram_read16(const GameState *g, uint32_t at) { return r16(g, at); }",
        "void wram_write8(GameState *g, uint32_t at, uint8_t v) { w8(g, at, v); }",
        "void wram_write16(GameState *g, uint32_t at, uint16_t v) { w16(g, at, v); }",
    ]
    for sub, groups in accessors(m):
        out += ["", "/* ---- %s %s */" % (sub, "-" * (86 - len(sub))), ""]
        for _, _, group in groups:
            for a in group:
                w = 8 if a.blob else a.size * 8
                out.append("%s { return r%d(g, %s); }" % (a.signature(False), w, a.address()))
                out.append("%s { w%d(g, %s, v); }" % (a.signature(True), w, a.address()))
    out.append("")
    return "\n".join(out)


# ---- coverage -------------------------------------------------------------------------------

LITERAL = re.compile(r"\b0[xX]([0-9A-Fa-f]+)[uUlL]*\b")
# A name, and a constant offset added to it (not a multiple of a stride).
RAM_NAME = re.compile(r"\b(RAM_[A-Z0-9_]+)\b(?:\s*\+\s*(0[xX][0-9A-Fa-f]+|\d+)\b(?!\s*\*))?")
# A literal an `&` reaches is a bit mask, not an address: nothing ANDs with an
# address, and masking one to a range (`addr & 0x1FFF`) leaves the mask a mask.
# `&&` is not it, and nor is C's address-of, which never precedes a number.
MASK_BEFORE = re.compile(r"(?<![&|])&=?\s*$")
MASK_AFTER = re.compile(r"^\s*&(?!&)")


def is_mask(line, mo):
    """Whether the literal `mo` matched in `line` is a bit mask."""
    return bool(MASK_BEFORE.search(line[:mo.start()]) or MASK_AFTER.match(line[mo.end():]))


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", lambda mo: "\n" * mo.group(0).count("\n"), src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return re.sub(r'"(?:\\.|[^"\\])*"', '""', src)


# The registry is a table of routines' addresses, all of them identities.
NOT_SCANNED = {HEADER, GAME / "registry.c"}
# A bank's base, which code ORs an offset into: not a variable.
BANK_BASES = {0x7E0000, 0x7F0000}


def game_files():
    for p in sorted(GAME.rglob("*")):
        if p.suffix in (".c", ".h") and p not in NOT_SCANNED:
            yield p


def subsystem_of(path):
    rel = path.relative_to(GAME)
    return rel.parts[0] if len(rel.parts) > 1 else "(top)"


def name_values(m):
    """RAM_ name -> the address it stands for, for the names that are addresses."""
    values = {}
    for v in m.vars:
        if v.addr is None:
            continue
        values[macro(v.name)] = v.addr
        for f in v.fields:
            values[macro(v.name, f.name)] = v.addr + f.offset
    return values


def scan(m):
    """[(address, class, subsystem, path, line)] for every RAM use in game/.

    class is low, long or name (counted), abs (an absolute operand >= $2000,
    counted only where the map claims it) or image (inside a program image the
    file's subsystem runs, not counted)."""
    values = name_values(m)
    uses = []
    for p in game_files():
        sub = subsystem_of(p)
        rel = p.relative_to(GAME).as_posix()
        other = m.not_ram.get(rel, set())
        offsets = m.offsets.get(rel, {})
        text = strip_comments(p.read_text(encoding="utf-8"))
        for n, line in enumerate(text.splitlines(), 1):
            for mo in LITERAL.finditer(line):
                digits = mo.group(1)
                v = int(digits, 16)
                if is_mask(line, mo):
                    continue
                if v in other:
                    uses.append((v, "other", sub, p, n))
                    continue
                if len(digits) == 4 and v in offsets:
                    uses.append((offsets[v] << 16 | v, "long", sub, p, n))
                    continue
                if len(digits) == 4 and 0x0100 <= v <= 0x1FFF:
                    uses.append((0x7E0000 | v, "low", sub, p, n))
                elif len(digits) == 6 and v >> 16 in (0x7E, 0x7F) and v not in BANK_BASES:
                    image = any(im.contains(v, sub) for im in m.images)
                    uses.append((v, "image" if image else "long", sub, p, n))
                elif len(digits) == 4 and v >= 0x2000:
                    uses.append((0x7E0000 | v, "abs", sub, p, n))
            for mo in RAM_NAME.finditer(line):
                if mo.group(1) in values:
                    off = int(mo.group(2), 0) if mo.group(2) else 0
                    uses.append((values[mo.group(1)] + off, "name", sub, p, n))
    return uses


# Which entries can name a use in a file of a subsystem: its own, the shared
# state in core, and the subsystem that runs on the same page and objects.
SHARES = {"event": {"field"}, "field": {"event"}, "attract": {"scene"}, "scene": {"attract"},
          "menu": {"menu_object"}, "menu_object": {"menu"}}


def names_use(m, a, sub):
    """True when the map names address `a` as seen from subsystem `sub`: work RAM
    is reused between modes, so another mode's entry at the same bytes doesn't
    say what they mean here.

    A record of the sharing subsystem names only the bytes its fields cover.
    The rest of its elements is what nobody has read yet, and it would otherwise
    swallow the other mode's own cells inside the array's span (the menus' state
    at $7E:0D00, under the object mode's $40-byte slots)."""
    for v, f in m.lookup(a):
        if v.subsystem in (sub, "core"):
            return True
        if v.subsystem in SHARES.get(sub, ()) and (f is not None or not v.fields):
            return True
    return False


def region(u):
    """The coverage group of a use: literals by their form, names by where their
    address is, so a literal renamed to a RAM_ name stays in the group it was
    counted in (a name for $7E:2000-$FFFF stands for an absolute operand)."""
    cls, a = u[1], u[0]
    if cls != "name":
        return cls
    if 0x7E0100 <= a <= 0x7E1FFF:
        return "low"
    return "long" if a >> 16 == 0x7F else "abs"


def coverage(m, show_open=False, only_sub=None, show_abs=False, out=sys.stdout):
    uses = scan(m)
    counted = [u for u in uses if region(u) in ("low", "long")]
    use_named = {}
    for key in {(u[0], u[2]) for u in counted}:
        use_named[key] = names_use(m, *key)
    # An address is named when every subsystem that uses it has a name for it.
    named = {}
    for (a, sub), ok in use_named.items():
        named[a] = named.get(a, True) and ok
    distinct = set(named)
    n_named = sum(1 for a in distinct if named[a])
    uses_named = sum(1 for u in counted if use_named[(u[0], u[2])])
    print("Work RAM addresses game/ uses, as literals or RAM_ names:", file=out)
    for cls, label in (("low", "low RAM $0100-$1FFF"), ("long", "long $7E/$7F")):
        cu = [u for u in counted if region(u) == cls]
        ca = {u[0] for u in cu}
        print("  %-20s %4d addresses in %4d uses (%d by name), %4d of them named"
              % (label, len(ca), len(cu), sum(1 for u in cu if u[1] == "name"),
                 sum(1 for a in ca if named[a])), file=out)
    print("  %-20s %4d addresses in %4d uses: %d named (%.0f%%) in %d uses"
          % ("total", len(distinct), len(counted), n_named, 100.0 * n_named / max(1, len(distinct)),
             uses_named), file=out)
    other_uses = [u for u in uses if u[1] == "other"]
    print("  literals the map says are not work RAM (left out): %d in %d uses"
          % (len({(u[3], u[0]) for u in other_uses}), len(other_uses)), file=out)
    image_uses = [u for u in uses if u[1] == "image"]
    print("  program image addresses (routines in RAM, left out): %d in %d uses"
          % (len({u[0] for u in image_uses}), len(image_uses)), file=out)
    abs_uses = [u for u in uses if region(u) == "abs" and (u[1] == "name" or names_use(m, u[0], u[2]))]
    print("  absolute operands >= $2000 the map names (not in the total): %d addresses in %d uses (%d by name)"
          % (len({u[0] for u in abs_uses}), len(abs_uses), sum(1 for u in abs_uses if u[1] == "name")),
          file=out)

    open_by_sub = collections.defaultdict(lambda: collections.defaultdict(list))
    for u in counted:
        if not use_named[(u[0], u[2])]:
            open_by_sub[u[2]][u[0]].append(u)
    print("\nOpen (unnamed) addresses by the subsystem that uses them:", file=out)
    for sub in sorted(open_by_sub, key=lambda s: -len(open_by_sub[s])):
        n_uses = sum(len(x) for x in open_by_sub[sub].values())
        print("  %-8s %4d addresses, %4d uses" % (sub, len(open_by_sub[sub]), n_uses), file=out)
    if show_open:
        for sub in sorted(open_by_sub):
            if only_sub and sub != only_sub:
                continue
            print("\n%s:" % sub, file=out)
            for a in sorted(open_by_sub[sub]):
                us = open_by_sub[sub][a]
                files = collections.Counter(u[3].relative_to(GAME).as_posix() for u in us)
                print("  %s %3d  %s" % (fmt_addr(a), len(us),
                                        ", ".join("%s x%d" % kv for kv in sorted(files.items()))), file=out)
    if show_abs:
        print("\nAbsolute operands a map entry claims:", file=out)
        for a in sorted({u[0] for u in abs_uses}):
            v, f = m.lookup(a)[0]
            print("  %s %s%s" % (fmt_addr(a), v.name, "." + f.name if f else ""), file=out)
    return len(distinct), n_named


# ---- command line ---------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 tools/ram_map.py")
    ap.add_argument("--map", type=pathlib.Path, default=MAP, help="the map to read (default re/ram.toml)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check", help="validate re/ram.toml")
    h = sub.add_parser("header", help="write game/ram.h")
    h.add_argument("--check", action="store_true", help="exit 1 if game/ram.h is out of date")
    a = sub.add_parser("accessors", help="write game/vars.h and runtime/port/vars_ram.c")
    a.add_argument("--check", action="store_true", help="exit 1 if either is out of date")
    c = sub.add_parser("coverage", help="how much of what game/ uses the map names")
    c.add_argument("--open", action="store_true", help="list the unnamed addresses")
    c.add_argument("--subsystem", help="with --open: only this subsystem")
    c.add_argument("--abs", action="store_true", help="list absolute operands the map claims")
    lk = sub.add_parser("lookup", help="what the map says about an address")
    lk.add_argument("addr", help="BB:AAAA, or AAAA for $7E")
    lk.add_argument("--subsystem")
    args = ap.parse_args(argv)

    try:
        m = RamMap(args.map)
    except MapError as e:
        print("%s: %s" % (args.map, e), file=sys.stderr)
        return 1

    if args.cmd == "check":
        print("%s: %d pages, %d variables, %d images, not-RAM literals in %d files"
              % (args.map, len(m.pages), len(m.vars), len(m.images), len(m.not_ram)))
    elif args.cmd == "header":
        text = render_header(m)
        if args.check:
            current = HEADER.read_text(encoding="utf-8") if HEADER.exists() else ""
            if current != text:
                print("game/ram.h is out of date: python3 tools/ram_map.py header", file=sys.stderr)
                return 1
            print("game/ram.h is up to date")
        else:
            HEADER.write_text(text, encoding="utf-8", newline="\n")
            print("wrote %s" % HEADER.relative_to(ROOT).as_posix())
    elif args.cmd == "accessors":
        want = ((VARS_HEADER, render_vars_header(m)), (BACKING, render_backing(m)))
        for path, text in want:
            rel = path.relative_to(ROOT).as_posix()
            if args.check:
                current = path.read_text(encoding="utf-8") if path.exists() else ""
                if current != text:
                    print("%s is out of date: python3 tools/ram_map.py accessors" % rel, file=sys.stderr)
                    return 1
                print("%s is up to date" % rel)
            else:
                path.write_text(text, encoding="utf-8", newline="\n")
                print("wrote %s" % rel)
    elif args.cmd == "coverage":
        coverage(m, args.open, args.subsystem, args.abs)
    elif args.cmd == "lookup":
        t = args.addr.replace("$", "")
        a = int(t.replace(":", ""), 16)
        if ":" not in t:
            a |= 0x7E0000
        hits = m.lookup(a, args.subsystem)
        if not hits:
            print("%s: not in the map" % fmt_addr(a))
        for v, f in hits:
            off = a - v.addr
            elem = " element %d" % (off // v.stride) if v.count > 1 else ""
            print("%s: %s%s%s (%s) -- %s [%s]" % (fmt_addr(a), v.name, "." + f.name if f else "",
                                                 elem, v.subsystem, f.meaning if f else v.meaning, v.source))
    return 0


if __name__ == "__main__":
    sys.exit(main())
