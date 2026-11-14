"""The RAM map: names for the game's variables in work RAM, and the header ports use.

    python3 tools/ram_map.py check                 # validate re/ram.toml
    python3 tools/ram_map.py header [--check]      # write (or compare) game/ram.h
    python3 tools/ram_map.py accessors [--check]   # native accessors and address views
    python3 tools/ram_map.py storage              # typed, blob and image-backed variables
    python3 tools/ram_map.py coverage [--open] [--subsystem NAME]
    python3 tools/ram_map.py lookup 7E:95D5 [--page battle]

`re/ram.toml` is the knowledge; its schema is at the top of the file and in
docs/STATE.md ("The RAM map"). Two things are generated from it and
committed, so a build without a ROM (CI's) has them, and tests/test_ram_map.py
fails when one is out of date:

- `game/ram.h`, legacy addresses for wrappers and format clients;
- `game/vars.h`, the value/record interface used by game logic;
- `runtime/port/vars_pages.h`, handles for declared page instances;
- `runtime/port/vars_ram.c`, native accessors with indexed bounds checks;
- `runtime/port/vars_host.h` and `vars_host.c`, owning structs and explicit
  little-endian address projection. `host_alias` composes views of owners;
  page owners allocate record arrays. `storage --list` distinguishes typed
  fields from raw buffers and the remaining image-backed cells.

**Coverage** answers "how much of what game/ touches has a name". It counts,
over game/ with comments stripped:

- *low RAM*: four-digit literals `0x0100`-`0x1FFF`, the game's variables in
  the bank-0 mirror of `$7E:0000-$1FFF`;
- *long WRAM*: six-digit literals in banks `$7E` and `$7F`;
- and any `RAM_` name from the header, at the address it stands for.

A use is named when it falls inside a map entry (any element, any byte of a
field) of its file's subsystem, of `core`, or of the subsystem sharing its page
(field and event, scene and attract, menu, menu_object and overworld): work RAM is reused
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
# it names bytes for the directory it shares with (SHARES). `overworld` is a
# directory of its own (the world map walked on foot, game/overworld/) that runs
# in that same mode, on its page and in its slots, so it shares with both.
SUBSYSTEMS = ("core", "data", "event", "field", "menu", "menu_object", "overworld", "battle", "scene", "attract",
              "warp")
NAME = re.compile(r"^[a-z][a-z0-9_]*$")
ADDR = re.compile(r"^(7[EF]):([0-9A-Fa-f]{4})$")


class MapError(Exception):
    pass


# A word of a name that reads as an address or a byte rather than a meaning:
# two to four hex digits with a digit among them, or a pair of hex letters.
ADDRESS_WORD = re.compile(r"^(?=[0-9a-f]*[0-9])[0-9a-f]{2,4}$|^[a-f]{2}$")


def stuttering(name):
    """The word a generated name says twice in a row (`..._mode_mode`), or None."""
    words = name.split("_")
    for a, b in zip(words, words[1:]):
        if a == b:
            return a
    return None


def parse_addr(text, where):
    m = ADDR.match(text) if isinstance(text, str) else None
    if not m:
        raise MapError("%s: address %r is not BB:AAAA in bank 7E or 7F" % (where, text))
    return int(m.group(1), 16) << 16 | int(m.group(2), 16)


def fmt_addr(a):
    return "$%02X:%04X" % (a >> 16, a & 0xFFFF)


def camel(name):
    return "".join(w.capitalize() for w in name.split("_"))


def alias_names(value):
    """A view names one owner, or several owners in its serialized layout."""
    return [value] if isinstance(value, str) else (value or [])


def valid_alias(value):
    names = alias_names(value)
    return (bool(names) and isinstance(names, list) and
            all(isinstance(n, str) and NAME.fullmatch(n) for n in names) and
            len(names) == len(set(names)))


# The highest D a page of work RAM can have: its 256 bytes end at $1FFF.
LAST_D = 0x1F00


class Instance:
    """One place a page kind's cells are: a named page at a fixed D
    (`{ name, d }`), or an array of `count` pages from `base`, `stride` bytes
    apart (`{ name, base, count, stride }`), whose index the name says."""

    def __init__(self, raw, where):
        if not isinstance(raw, dict) or not isinstance(raw.get("name"), str):
            raise MapError("%s: an instance is { name, d } or { name, base, count, stride }" % where)
        self.name = raw["name"]
        where = "%s instance %r" % (where, self.name)
        if not NAME.match(self.name):
            raise MapError("%s: name is not lower_snake_case" % where)
        keys = set(raw)
        if keys == {"name", "d"}:
            self.d, self.base, self.count, self.stride = raw["d"], None, None, None
            self.array = False
        elif keys == {"name", "base", "count", "stride"}:
            self.d = None
            self.base, self.count, self.stride = raw["base"], raw["count"], raw["stride"]
            self.array = True
            for key in ("count", "stride"):
                if not (isinstance(raw[key], int) and raw[key] > 0):
                    raise MapError("%s: %s must be a positive integer" % (where, key))
        else:
            raise MapError("%s: give d, or base, count and stride" % where)
        for d in (self.ds()[0], self.ds()[-1]) if self.array else (self.d,):
            if not (isinstance(d, int) and 0 <= d <= LAST_D):
                raise MapError("%s: D must be in $0000-$%04X (work RAM)" % (where, LAST_D))

    def ds(self):
        """Every D this instance is, in index order."""
        if not self.array:
            return [self.d]
        return [self.base + i * self.stride for i in range(self.count)]

    def describe(self):
        if not self.array:
            return "%s (D = $%04X)" % (self.name, self.d)
        return "%s 0-%d (D = $%04X + $%X each)" % (self.name, self.count - 1, self.base, self.stride)


class Page:
    """A direct page the game sets while some code runs: D = `d`; or a page
    kind, whose code runs with D on any of its `instances`."""

    def __init__(self, raw):
        where = "page %r" % raw.get("name")
        for key in ("name", "subsystem", "meaning", "source"):
            if not raw.get(key):
                raise MapError("%s: missing %s" % (where, key))
        unknown = set(raw) - {"name", "d", "instances", "subsystem", "meaning", "source", "why"}
        if unknown:
            raise MapError("%s: unknown keys %s" % (where, sorted(unknown)))
        self.name = raw["name"]
        if not NAME.match(self.name):
            raise MapError("%s: name is not lower_snake_case" % where)
        self.subsystem = raw["subsystem"]
        if self.subsystem not in SUBSYSTEMS:
            raise MapError("%s: subsystem %r is not one of %s" % (where, self.subsystem, SUBSYSTEMS))
        self.d = raw.get("d")
        self.instances = []
        if self.d is None:
            if not raw.get("why"):
                raise MapError("%s: a page whose D isn't fixed needs `why`" % where)
            if not isinstance(raw.get("instances"), list) or not raw["instances"]:
                raise MapError("%s: a page whose D isn't fixed lists its instances" % where)
            self.instances = [Instance(i, where) for i in raw["instances"]]
            if len(self.instances) == 1 and not self.instances[0].array:
                raise MapError("%s: one page is a fixed page, not a page kind: give d" % where)
            seen, at = set(), {}
            for i in self.instances:
                if i.name in seen:
                    raise MapError("%s: instance %r twice" % (where, i.name))
                seen.add(i.name)
                for d in i.ds():
                    if d in at:
                        # Name the bytes once: two instances at one D are one set of cells.
                        raise MapError("%s: instances %r and %r are both D = $%04X"
                                       % (where, at[d], i.name, d))
                    at[d] = i.name
        else:
            if "instances" in raw:
                raise MapError("%s: a page with a fixed d has no instances" % where)
            if not (isinstance(self.d, int) and 0 <= self.d <= LAST_D):
                raise MapError("%s: d must be an integer in $0000-$%04X (work RAM)" % (where, LAST_D))
        self.meaning = raw["meaning"]
        self.source = raw["source"]
        self.why = raw.get("why")

    # ---- the handle a variable on a page kind is reached by ----------------------------------
    #
    # Named instances only: an enum. One array and nothing else: its index. A mix:
    # a struct of which instance and, for an array, which element of it.

    def shape(self):
        if not self.instances:
            return None
        if all(not i.array for i in self.instances):
            return "named"
        if len(self.instances) == 1:
            return "array"
        return "mixed"

    def type_name(self):
        return None if self.shape() == "array" else camel(self.name) + "Page"

    def param(self):
        """The handle's parameter name."""
        return self.instances[0].name if self.shape() == "array" else "page"

    def handle_decl(self):
        return ("unsigned " if self.shape() == "array" else self.type_name() + " ") + self.param()

    def constant(self, inst):
        """The enum constant of a named instance, or of an instance's kind in a mix."""
        return "%s_%s" % (self.name.upper(), inst.name.upper())

    def count_constant(self, inst):
        return self.name.upper() + "_COUNT" if self.shape() == "array" else self.constant(inst) + "_COUNT"

    def of_name(self):
        return self.name + "_page_of"

    def maker(self, inst):
        """In a mix, the function that makes the handle of an instance."""
        return "%s_%s" % (self.name, inst.name)

    def ds(self):
        return [d for i in self.instances for d in i.ds()]


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
        unknown = set(raw) - {"name", "offset", "size", "meaning", "host_alias", "buffer", "elements"}
        if unknown:
            raise MapError("%s: unknown keys %s" % (where, sorted(unknown)))
        self.name = raw["name"]
        if not NAME.match(self.name):
            raise MapError("%s: name is not lower_snake_case" % where)
        self.offset = raw["offset"]
        self.size = raw.get("size", 1)
        self.meaning = raw["meaning"]
        self.elements = Elements(raw.get("elements"), where)
        self.buffer = raw.get("buffer", False)
        if not isinstance(self.buffer, bool):
            raise MapError("%s: buffer must be boolean" % where)
        self.host_alias = raw.get("host_alias")
        if self.host_alias is not None and not valid_alias(self.host_alias):
            raise MapError("%s: host_alias must name an owning field" % where)
        if not (isinstance(self.offset, int) and isinstance(self.size, int) and self.size > 0
                and 0 <= self.offset and self.offset + self.size <= var.stride):
            raise MapError("%s: offset/size must lie inside the element's stride ($%X)" % (where, var.stride))


class Elements:
    """The game elements in a serialized buffer, independently of its byte API.

    A slice can start inside an element when another view owns its other byte.
    Only the declared slice is projected; unused edge bits are never observable.
    """

    def __init__(self, raw, where):
        self.name = None
        self.whole = False
        if raw is None:
            return
        if not isinstance(raw, dict) or set(raw) - {"name", "size", "start", "meaning", "whole"}:
            raise MapError("%s: elements needs name, size and meaning" % where)
        self.name = raw.get("name")
        self.size = raw.get("size")
        self.start = raw.get("start", 0)
        self.meaning = raw.get("meaning")
        # The accessors take an element, not a byte: the ports read and write
        # the buffer's elements at their own width.
        self.whole = raw.get("whole", False)
        if self.whole not in (False, True) or (self.whole and self.start):
            raise MapError("%s: whole elements start at the buffer's first byte" % where)
        if (not isinstance(self.name, str) or not NAME.fullmatch(self.name) or
                self.size not in (1, 2, 3, 4) or not isinstance(self.start, int) or
                not 0 <= self.start < self.size or not self.meaning):
            raise MapError("%s: invalid element layout" % where)

    def count(self, size):
        return (self.start + size + self.size - 1) // self.size


class Var:
    """A variable, an array of them, or an array of records."""

    KEYS = {"name", "addr", "size", "count", "stride", "index", "page", "dp",
            "subsystem", "meaning", "source", "field", "rows", "row_stride", "unchecked", "reuses", "host_module", "host_alias", "host_blob", "elements",
            "overlays", "same_value"}

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
        self.elements = Elements(raw.get("elements"), where)
        self.record = None
        self.host_module = raw.get("host_module")
        if self.host_module is not None and not (isinstance(self.host_module, str) and NAME.fullmatch(self.host_module)):
            raise MapError("%s: host_module must name its C storage owner" % where)
        self.host_alias = raw.get("host_alias")
        self.host_blob = raw.get("host_blob", "")
        if not isinstance(self.host_blob, str):
            raise MapError("%s: host_blob must explain its unstructured storage" % where)
        self.host_owner = None
        self.host_owners = []
        # An owner the original keeps in bytes it also uses for other modes'
        # variables, which the reimplementation stores separately: it names the
        # owners whose bytes it stands beside (_check_host_owners).
        self.overlays = raw.get("overlays")
        if self.overlays is not None:
            if not valid_alias(self.overlays):
                raise MapError("%s: overlays names the owners whose bytes the original shares" % where)
            if not self.host_module:
                raise MapError("%s: only an owner (host_module) can stand beside another" % where)
            self.overlays = alias_names(self.overlays)
        else:
            self.overlays = []
        if self.host_alias is not None:
            if not valid_alias(self.host_alias):
                raise MapError("%s: host_alias must name an owning variable" % where)
            if self.host_module:
                raise MapError("%s: an alias has no independent host_module" % where)
        # An alias that is another name for its owner's value, not a view of
        # bytes another mode may have written last (owner_value).
        self.same_value = raw.get("same_value", False)
        if self.same_value not in (False, True) or (self.same_value and self.host_alias is None):
            raise MapError("%s: same_value marks an alias that names its owner's own value" % where)
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
        # A table of rows: the variable (its elements, if an array) is one row
        # of `size` bytes, a byte a column, repeated `rows` times `row_stride`
        # bytes apart, the way the field keeps a map's tables a row of 256
        # bytes apart with other tables' rows between.
        self.rows = raw.get("rows")
        self.row_stride = raw.get("row_stride")
        if (self.rows is None) != (self.row_stride is None):
            raise MapError("%s: a table of rows needs both rows and row_stride" % where)
        if self.rows is not None:
            for key, v in (("rows", self.rows), ("row_stride", self.row_stride)):
                if not (isinstance(v, int) and v > 0):
                    raise MapError("%s: %s must be a positive integer" % (where, key))
            if "field" in raw:
                raise MapError("%s: a table of rows has columns of bytes, not fields" % where)
            if self.stride * (self.count - 1) + self.size > self.row_stride:
                raise MapError("%s: a row ($%X bytes) is wider than row_stride $%X"
                               % (where, self.stride * (self.count - 1) + self.size, self.row_stride))
            if self.count > 1 and index_param(self.index) in ("row", "column"):
                raise MapError("%s: the index of a table of rows can't be called row or column" % where)
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
            # D is one of the page's instances: the variable has an offset, and
            # an address in each instance.
            if "addr" in raw:
                raise MapError("%s: page %r has no fixed D, so the variable can't have an address"
                               % (where, self.page.name))
            if not (isinstance(raw.get("dp"), int) and 0 <= raw["dp"] <= 0xFF):
                raise MapError("%s: needs dp, an offset $00-$FF in page %r" % (where, self.page.name))
            self.addr = None
            self.dp = raw["dp"]
            if self.dp + self.span() > 0x100:
                raise MapError("%s: runs past the end of page %r" % (where, self.page.name))
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

        # A variable whose bytes its own subsystem uses for something else at
        # another time (a map's data decompressed where its tiles' looks go
        # after): it names the other (or a list of the others, where its bytes
        # are more than one's), and says so in its meaning.
        reuses = raw.get("reuses")
        if isinstance(reuses, str):
            reuses = [reuses]
        if reuses is not None and not (isinstance(reuses, list) and reuses
                                       and all(isinstance(r, str) and r.strip() for r in reuses)):
            raise MapError("%s: reuses names a variable" % where)
        self.reuses = tuple(reuses or ())

        # A variable some marked scaffolding reaches out of range on purpose
        # (a `leave_stray_*` write the gate needs): it gets a second, unchecked
        # pair of accessors, and says why.
        self.unchecked = raw.get("unchecked")
        if self.unchecked is not None:
            if not (isinstance(self.unchecked, str) and self.unchecked.strip()):
                raise MapError("%s: unchecked says why, as a sentence" % where)
            if not self.indexed():
                raise MapError("%s: unchecked, but nothing indexes it" % where)

    def indexed(self):
        """Whether an accessor of this variable takes anything it could be out of
        range in: an element, a byte, a row and column, or an array of pages."""
        return (self.count > 1 or self.rows is not None or (self.size > WIDEST and not self.fields)
                or any(f.size > WIDEST for f in self.fields)
                or (self.on_instances() and self.page.shape() != "named"))

    def row_span(self):
        """The bytes one row takes, from its first element's start."""
        return self.stride * (self.count - 1) + self.size

    def span(self):
        if self.rows is None:
            return self.row_span()
        return self.row_stride * (self.rows - 1) + self.row_span()

    def end(self):
        return self.addr + self.span()

    def on_instances(self):
        """Whether the variable is on a page kind, at an offset in each instance."""
        return self.addr is None

    def bases(self):
        """Where the variable is: its address, or its address in every instance."""
        if not self.on_instances():
            return [self.addr]
        return [0x7E0000 | (d + self.dp) for d in self.page.ds()]

    def contains(self, a):
        """The field (or None) when address `a` is inside this variable, else False."""
        for base in self.bases():
            if not base <= a < base + self.span():
                continue
            off = a - base
            if self.rows is not None:
                off %= self.row_stride
                if off >= self.row_span():
                    continue
            off %= self.stride
            if off >= self.size:
                continue
            for f in self.fields:
                if f.offset <= off < f.offset + f.size:
                    return f
            return None
        return False


class RamMap:
    def __init__(self, path=MAP, text=None):
        doc = tomllib.loads(text if text is not None else path.read_text(encoding="utf-8"))
        unknown = set(doc) - {"page", "var", "owner", "record", "image", "not_ram", "offset"}
        if unknown:
            raise MapError("unknown top-level keys %s" % sorted(unknown))
        self.pages = {}
        for raw in doc.get("page", []):
            p = Page(raw)
            if p.name in self.pages:
                raise MapError("page %r twice" % p.name)
            self.pages[p.name] = p
        self.vars = [Var(raw, self.pages) for raw in doc.get("var", [])]
        # Storage records can serve several existing API views without adding
        # a new game accessor or changing the variable-count denominator.
        self.owners = [Var(raw, self.pages) for raw in doc.get("owner", [])]
        for v in self.owners:
            v.storage_only = True
            if not v.host_module or v.host_alias:
                raise MapError("%s: an owner needs host_module and cannot alias a variable" % v.name)
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
        names = collections.Counter(v.name for v in self.vars + self.owners)
        dup = [n for n, c in names.items() if c > 1]
        if dup:
            raise MapError("names used twice: %s" % ", ".join(sorted(dup)))
        self._check_overlaps()
        self._check_host_owners()
        self.records = doc.get("record", [])
        self._check_records()
        self.macros = header_macros(self)   # also checks the generated names are unique
        for _, groups in accessors(self):   # the same, for game/vars.h; and no name says a word twice
            for _, _, group in groups:
                for a in group:
                    stutter = stuttering(a.name)
                    if stutter:
                        raise MapError("%s: says %r twice; name a record's field for its concept, not for "
                                       "the record it sits in" % (a.name, stutter))

    def address_shaped(self):
        """The variables whose names carry a word shaped like an address or a
        byte ($1E, $BC): named for where they are, not for what they hold."""
        return [v.name for v in self.vars if any(ADDRESS_WORD.match(w) for w in v.name.split("_"))]

    def _check_records(self):
        """Gather independently addressed fields into game records.

        The serialized layout belongs to each owner. A record only determines
        the native layout; it never changes an accessor or allocates an alias.
        """
        owners = {v.name: v for v in self.vars + self.owners if v.host_module}
        types, names = set(), set()
        for r in self.records:
            if set(r) != {"name", "type", "module", "count", "meaning", "members"}:
                raise MapError("record needs name, type, module, count, meaning and members")
            if (not NAME.fullmatch(r["name"]) or not NAME.fullmatch(r["module"]) or
                    not re.fullmatch(r"[A-Z][A-Za-z0-9]*", r["type"]) or
                    not isinstance(r["count"], int) or r["count"] < 1 or not r["meaning"]):
                raise MapError("invalid record declaration")
            key = (r["module"], r["name"])
            if key in names or r["type"] in types:
                raise MapError("record name/type declared twice")
            names.add(key)
            types.add(r["type"])
            fields = set()
            if not r["members"]:
                raise MapError("a record needs members")
            for member in r["members"]:
                if set(member) - {"owner", "name", "flatten", "array"} or "owner" not in member:
                    raise MapError("record member needs owner and name, or flatten")
                v = owners.get(member["owner"])
                if v is None or v.record:
                    raise MapError("%s: record member needs an unbound storage owner" % member["owner"])
                count = len(v.bases()) if v.on_instances() else v.count
                array = member.get("array", False)
                if not isinstance(array, bool) or (array and (r["count"] != 1 or v.on_instances())):
                    raise MapError("an array member belongs to one record")
                if v.rows or (not array and count != r["count"]) or (v.on_instances() and v.count != 1):
                    raise MapError("record members must have the same element count")
                flatten = member.get("flatten", False)
                if not isinstance(flatten, bool) or (flatten and (not v.fields or "name" in member or array)):
                    raise MapError("only record fields can be flattened")
                declared = [f.name for f in v.fields if not f.host_alias] if flatten else [member.get("name", "")]
                if any(not NAME.fullmatch(n) or n in fields for n in declared):
                    raise MapError("record members must have distinct field names")
                fields.update(declared)
                v.record = (r, member)
        for r in self.records:
            if any(v.host_module == r["module"] and v.name == r["name"] and not v.record for v in owners.values()):
                raise MapError("record name collides with an unbound owner")

    def _check_host_owners(self):
        by_name = {v.name: v for v in self.vars + self.owners}
        cells = {}
        for v in self.vars + self.owners:
            if v.host_alias:
                for name in alias_names(v.host_alias):
                    owner = by_name.get(name)
                    if owner is None or not owner.host_module or owner.host_alias:
                        raise MapError("%s: host_alias needs a direct, selected owner" % v.name)
                    v.host_owners.append(owner)
                v.host_owner = v.host_owners[0]
            elif v.host_module:
                v.host_owner = v
                v.host_owners = [v]
            fields = {f.name: f for f in v.fields}
            for f in v.fields:
                if not f.host_alias:
                    continue
                covered = set()
                for name in alias_names(f.host_alias):
                    owner = fields.get(name)
                    if not v.host_module or owner is None or owner.host_alias:
                        raise MapError("%s.%s: host_alias needs a direct, selected owning field" % (v.name, f.name))
                    covered.update(range(owner.offset, owner.offset + owner.size))
                if not set(range(f.offset, f.offset + f.size)) <= covered:
                    raise MapError("%s.%s: alias lies outside its owning field" % (v.name, f.name))
            cells[v.name] = set(variable_bytes(v))
        # Each byte has at most one base owner. An owner with `overlays` keeps
        # its own storage beside the owners it names: the original reuses their
        # bytes for it in another mode, and the address view writes through to
        # every owner of a byte (render_host_backing). Every two owners of a
        # byte are declared beside each other: one names the other, or both
        # name a third that the byte's original memory also belongs to.
        owned = {}
        holders = collections.defaultdict(list)
        for v in self.vars + self.owners:
            if not v.host_module:
                continue
            mine = list(variable_bytes(v, owners_only=True))
            if len(mine) != len(set(mine)):
                raise MapError("%s: %s" % (v.name, "an owner's fields overlap" if v.overlays
                                           else "struct storage needs one owner at %s" % fmt_addr(
                                               collections.Counter(mine).most_common(1)[0][0])))
            for addr in mine:
                holders[addr].append(v)
                if v.overlays:
                    continue
                if addr in owned:
                    raise MapError("%s: struct storage needs one owner at %s" % (v.name, fmt_addr(addr)))
                owned[addr] = v
        sharing = collections.defaultdict(set)
        for addr, vs in holders.items():
            if len(vs) > 1:
                names = frozenset(v.name for v in vs)
                for v in vs:
                    sharing[v.name] |= names - {v.name}
        for v in self.vars + self.owners:
            for name in v.overlays:
                other = by_name.get(name)
                if other is None or not other.host_module:
                    raise MapError("%s: overlays names an owner, not %r" % (v.name, name))
                if name not in sharing[v.name]:
                    raise MapError("%s: shares no byte with %s, which it says it overlays" % (v.name, name))
        groups = {frozenset(v.name for v in vs) for vs in holders.values() if len(vs) > 1}
        for group in groups:
            for a in group:
                for b in group:
                    if a >= b:
                        continue
                    va, vb = by_name[a], by_name[b]
                    linked = (b in va.overlays or a in vb.overlays or
                              bool(set(va.overlays) & set(vb.overlays)))
                    if not linked:
                        raise MapError("%s: overlays must name %s, whose bytes it shares" %
                                       ((va if va.overlays else vb).name, b if va.overlays else a))
        overlaid = {addr for v in self.vars + self.owners if v.overlays
                    for addr in variable_bytes(v, owners_only=True)}
        for v in self.vars:
            if v.host_alias:
                covered = set().union(*(cells[o.name] for o in v.host_owners))
                if not cells[v.name] <= covered:
                    raise MapError("%s: alias lies outside its owning variable" % v.name)
            if v.overlays:
                continue
            for addr in cells[v.name]:
                if addr in owned and owned[addr] not in v.host_owners and addr not in overlaid:
                    raise MapError("%s: struct storage needs one owner at %s; declare its alias" % (v.name, fmt_addr(addr)))

    def _check_overlaps(self):
        """Work RAM is reused from one game mode to the next, so two entries may
        cover the same bytes when their subsystems differ (the battle's use of
        $7E:2000 is not the field's). Within one subsystem, name the reuse or
        explicitly share a selected host owner (validated separately).

        A variable on a page kind is at its offset in every instance. Those
        instances may be the elements of a record array of the same subsystem
        (a sequence channel's page is its record, `scene_channels`), so against
        one, only a record's named fields count: the rest of its element is
        what the page kind's variables name."""
        placed = sorted(((base, v) for v in self.vars for base in v.bases()), key=lambda p: p[0])

        def claims(v, x, other):
            f = v.contains(x)
            if f is None and v.fields and other.on_instances():
                return False
            return f is not False

        by_name = {v.name: v for v in self.vars}

        def reused(v):
            # the names v reuses, following a chain of them (a cell put to a
            # third use reuses the second)
            seen, todo = set(), list(v.reuses)
            while todo:
                name = todo.pop()
                if name in seen:
                    continue
                seen.add(name)
                other = by_name.get(name)
                if other is not None:
                    todo.extend(other.reuses)
            return seen

        for i, (a_base, a) in enumerate(placed):
            a_end = a_base + a.span()
            for b_base, b in placed[i + 1:]:
                if b_base >= a_end:
                    break
                shared_owner = (b.name in alias_names(a.host_alias) or a.name in alias_names(b.host_alias)
                                or bool(set(alias_names(a.host_alias)) & set(alias_names(b.host_alias))))
                if a.subsystem != b.subsystem or a is b or shared_owner or b.name in reused(a) or a.name in reused(b):
                    continue
                if any(claims(a, x, b) and claims(b, x, a)
                       for x in range(b_base, min(a_end, b_base + b.span()))):
                    raise MapError("%s and %s overlap (both %s)" % (a.name, b.name, a.subsystem))

    def lookup(self, a, subsystem=None):
        """Entries naming address `a`, as (var, field); `subsystem` first."""
        hits = [(v, v.contains(a)) for v in self.vars if v.contains(a) is not False]
        hits.sort(key=lambda h: (h[0].subsystem != subsystem, min(h[0].bases())))
        return hits


def variable_bytes(v, owners_only=False):
    """Declared bytes, including every page instance but excluding record padding."""
    for base in v.bases():
        for row in range(v.rows or 1):
            for element in range(v.count):
                at = base + row * (v.row_stride or 0) + element * v.stride
                for offset, size in ([(f.offset, f.size) for f in v.fields if not owners_only or not f.host_alias]
                                     if v.fields else [(0, v.size)]):
                    yield from range(at + offset, at + offset + size)


def access_units(v):
    """Each value variable `v` names, as (first byte, end, element start, width):
    a scalar is one value of its size; a buffer or table is a run of its
    elements, an element cut by the buffer's edge keeping only the bytes inside
    it. A buffer that names no elements is a run of bytes of width 0: nothing
    says what they are, so no buffer's label can type them."""
    for base in v.bases():
        for row in range(v.rows or 1):
            for element in range(v.count):
                at = base + row * (v.row_stride or 0) + element * v.stride
                for f in v.fields or [None]:
                    a = Accessor(v, f)
                    start = at + a.offset
                    if not (a.blob or a.table):
                        yield start, start + a.size, start, a.size
                        continue
                    unit = a.elements.size if a.elements.name else 1
                    skip = a.elements.start if a.elements.name else 0
                    # Elements wider than a byte type the buffer only where its
                    # accessors move whole elements: a port that reads a word
                    # element a byte at a time reads bytes.
                    typed = bool(a.elements.name) and (a.elements.size == 1 or a.whole)
                    x = start - skip
                    while x < start + a.size:
                        yield max(x, start), min(x + unit, start + a.size), x, unit if typed else 0
                        x += unit


def storage_holders(m):
    """Each owned byte's holders: {owner name: (owner field, the width of the
    value holding it, the value's first byte)}. A byte has one holder, or more
    where owners stand beside each other (`overlays`). A scalar field is one
    value of its size; a labelled buffer is a run of its elements; an
    unlabelled buffer is width 0 (bytes nothing gives a meaning), which no value
    matches."""
    holder = collections.defaultdict(dict)
    for a in owner_accessors(m):
        label = a.var.name + ("." + a.field.name if a.field else "")
        buffer = a.blob or a.table or a.var.host_blob
        unit = a.elements.size if a.elements.name else None
        for base in a.var.bases():
            for row in range(a.var.rows or 1):
                for element in range(a.var.count):
                    start = base + row * (a.var.row_stride or 0) + element * a.var.stride + a.offset
                    for addr in range(start, start + a.size):
                        if not buffer:
                            held = (label, a.size, start)
                        elif unit is None:
                            held = (label, 0, None)
                        else:
                            # the element this byte is in, and the element's width
                            first = addr - (addr - start + a.elements.start) % unit
                            held = (label, unit, first)
                        holder[addr][a.var.name] = held
    return holder


def storage_kinds(m):
    """Classify actual backing, not merely the presence of ownership metadata.

    A variable is typed only if each of its values is held by exactly one
    typed value of its own width and place: a scalar owner field, or one whole
    element of an owner buffer whose elements are that width. A byte buffer
    with no elements is a blob; so is any value split across owner fields or
    elements, misaligned with one, or a fragment of a wider one (a word kept in
    two byte fields, a byte of a word field, a byte read out of half a word),
    including where another mode's alias reads memory the original reuses. A
    partial blob dependency makes the whole variable blob-backed.
    """
    holder = storage_holders(m)
    return {v.name: variable_storage(v, holder) for v in m.vars}


def variable_storage(v, holder):
    """One variable's kind and the buffers it depends on, given storage_holders."""
    if not (v.host_module or v.host_owner):
        return "image", []
    dependencies = set()
    names = [o.name for o in v.host_owners]
    for lo, hi, start, width in access_units(v):
        held = []
        for addr in range(lo, hi):
            mine = [h for name, h in holder.get(addr, {}).items() if name in names]
            held.append(mine[0] if len(mine) == 1 else None if not mine else ("", -1, None))
        if any(h is None for h in held):
            continue
        whole = ({h[0] for h in held} == {held[0][0]} and {h[1] for h in held} == {width} and
                 {h[2] for h in held} == {start})
        if not whole:
            dependencies |= {h[0] for h in held}
    return ("blob" if dependencies else "typed"), sorted(dependencies)


def storage_counts(m):
    """Image, blob and typed counts; one vote per map variable, not per byte."""
    counts = collections.Counter(kind for kind, _ in storage_kinds(m).values())
    return counts["image"], counts["blob"], counts["typed"]


def storage_report(m, out=sys.stdout, details=False):
    kinds = storage_kinds(m)
    counts = collections.Counter(kind for kind, _ in kinds.values())
    print("native state: %d variables; %d image-backed; %d blob-backed; %d typed struct-backed"
          % (len(m.vars), counts["image"], counts["blob"], counts["typed"]), file=out)
    for sub in SUBSYSTEMS:
        vs = [v for v in m.vars if v.subsystem == sub]
        if vs:
            counts = collections.Counter(kinds[v.name][0] for v in vs)
            print("  %-12s %4d image-backed; %4d blob-backed; %4d typed / %d"
                  % (sub, counts["image"], counts["blob"], counts["typed"], len(vs)), file=out)
    if details:
        for v in m.vars:
            kind, dependencies = kinds[v.name]
            if kind != "typed":
                print("  %s\t%s\t%s" % (kind, v.name, ", ".join(dependencies)), file=out)


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
            lines += comment("D is not fixed: %s. Instances: %s"
                             % (p.why.rstrip("."), "; ".join(i.describe() for i in p.instances)))
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
            if v.rows is not None:
                desc += " [%d rows, $%X bytes apart]" % (v.rows, v.row_stride)
            lines += comment(desc)
            if v.addr is not None:
                define(lines, macro(v.name), "0x%06Xu" % v.addr)
            if v.count > 1:
                define(lines, macro(v.name, "count"), "%du" % v.count)
                define(lines, macro(v.name, "stride"), "0x%02Xu" % v.stride)
            if v.rows is not None:
                define(lines, macro(v.name, "rows"), "%du" % v.rows)
                define(lines, macro(v.name, "row_stride"), "0x%02Xu" % v.row_stride)
            if v.page is not None:
                where = ("page %s, D per instance" % v.page.name if v.page.d is None
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
        " * (python3 tools/ram_map.py header). docs/STATE.md, \"The RAM map\".",
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
# layout. The native backing uses module fields and the address projection;
# no emulator accessor backend remains.

VARS_HEADER = GAME / "vars.h"
BACKING = ROOT / "runtime" / "port" / "vars_ram.c"
HOST_HEADER = ROOT / "runtime" / "port" / "vars_host.h"
HOST_BACKING = ROOT / "runtime" / "port" / "vars_host.c"
FLAT_BACKING = ROOT / "runtime" / "port" / "vars_flat.c"

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

    def __init__(self, var, field=None, unchecked=False):
        self.var = var
        self.field = field
        base = var.name if field is None else "%s_%s" % (var.name, field.name)
        self.unchecked = unchecked
        self.offset = field.offset if field else 0
        self.addr = None if var.on_instances() else var.addr + self.offset
        self.size = field.size if field else var.size
        self.meaning = field.meaning if field else var.meaning
        self.elements = field.elements if field else var.elements
        # A table of rows is a byte by row and column, whatever its row's size.
        self.table = var.rows is not None
        self.blob = (self.size > WIDEST or (field and field.buffer)) and not self.table
        # A buffer of whole elements: its accessors take an element's index.
        self.whole = self.blob and bool(self.elements.name) and self.elements.whole
        if self.whole and self.size % self.elements.size:
            raise MapError("%s: whole elements must divide the buffer" % base)
        self.name = (base + "_byte" if self.blob and not self.whole else base) + ("_unchecked" if unchecked else "")
        self.type = (C_TYPE[self.elements.size] if self.whole else
                     "uint8_t" if self.blob or self.table else C_TYPE[self.size])
        # The unchecked pair shares the checked one's constants.
        named = not unchecked
        self.bytes_const = base.upper() + "_BYTES" if self.blob and named else None
        self.rows_const = base.upper() + "_ROWS" if self.table and named else None
        self.columns_const = base.upper() + "_COLUMNS" if self.table and named else None

    def params(self, write):
        """The parameter list after the state, as (declaration, argument) pairs."""
        out = []
        if self.var.on_instances():
            out.append((self.var.page.handle_decl(), self.var.page.param()))
        if self.var.count > 1:
            out.append(("unsigned " + index_param(self.var.index), index_param(self.var.index)))
        if self.whole:
            out.append(("unsigned item", "item"))
        elif self.blob:
            out.append(("unsigned at", "at"))
        if self.table:
            out += [("unsigned row", "row"), ("unsigned column", "column")]
        if write:
            out.append((self.type + " v", "v"))
        return out

    def signature(self, write):
        state = "GameState *g" if write else "const GameState *g"
        args = ", ".join([state] + [d for d, _ in self.params(write)])
        return "%s %s%s(%s)" % ("void" if write else self.type, "set_" if write else "", self.name, args)

    def address(self):
        if self.var.on_instances():
            p = self.var.page
            out = "%s(%s) + 0x%02Xu" % (page_base_name(p), p.param(), self.var.dp + self.offset)
        else:
            out = "0x%06Xu" % self.addr
        if self.var.count > 1:
            out += " + (uint32_t)%s * 0x%02Xu" % (index_param(self.var.index), self.var.stride)
        if self.whole:
            out += " + (uint32_t)item * %du" % self.elements.size
        elif self.blob:
            out += " + at"
        if self.table:
            out += " + (uint32_t)row * 0x%02Xu + column" % self.var.row_stride
        return out

    def bounds(self):
        """What each index the accessor takes must be below, as (argument, limit,
        what the limit is): the page kind's instances, the array's count, a
        buffer's bytes, a table's rows and columns. The backing checks them
        (CT_ACCESSOR_CHECKS) unless the accessor is the unchecked pair."""
        out = []
        if self.unchecked:
            return out
        v = self.var
        if v.on_instances():
            p, shape = v.page, v.page.shape()
            if shape == "array":
                out.append((p.param(), "%du" % p.instances[0].count, "instances"))
            elif shape == "mixed":
                out.append((p.param() + ".index", "%s(%s)" % (page_count_name(p), p.param()), "instances"))
        if v.count > 1:
            out.append((index_param(v.index), "%du" % v.count, "count"))
        if self.whole:
            out.append(("item", "%du" % (self.size // self.elements.size), self.elements.name))
        elif self.blob:
            out.append(("at", "%du" % self.size, "bytes"))
        if self.table:
            out += [("row", "%du" % v.rows, "rows"), ("column", "%du" % self.size, "columns")]
        return out

    def note(self):
        """What the accessor is, for the comment above it. What an array is
        indexed by is on its count, once, rather than on every field."""
        if self.table:
            out = self.meaning + " [a byte by row and column]"
        else:
            out = self.meaning + (" [%d %s of %d bytes, one at a time]" % (
                self.size // self.elements.size, self.elements.name, self.elements.size) if self.whole else
                " [%d bytes, one at a time]" % self.size if self.blob else "")
        if self.unchecked:
            out += " Unchecked, reaching past the variable into what follows it: %s" % self.var.unchecked
        return out


def page_kinds(m):
    """The pages whose variables take an instance handle, in the map's order."""
    return [p for p in m.pages.values() if p.instances]


def page_base_name(p):
    return p.name + "_page_base"


def page_count_name(p):
    return p.name + "_page_count"


def accessors(m):
    """[(subsystem, [(var, count constant or None, [Accessor])])], in the header's
    order; raises on a name clash.

    A variable on a page kind takes the instance as its first parameter
    (Page.handle_decl): which of the page's places the caller means, never a D."""
    by_sub = collections.defaultdict(list)
    names, consts = set(), set()

    def take(name, seen, what):
        if name in seen:
            raise MapError("generated %s %s twice" % (what, name))
        seen.add(name)
        return name

    for p in page_kinds(m):
        take(p.of_name(), names, "function")
        if p.type_name():
            take(p.type_name(), names, "type")
        for i in p.instances:
            if p.shape() == "mixed":
                take(p.maker(i), names, "function")
            if p.shape() != "array":
                take(p.constant(i), consts, "constant")
            if i.array:
                take(p.count_constant(i), consts, "constant")
    for v in m.vars:
        if v.on_instances() and v.count > 1 and index_param(v.index) == v.page.param():
            raise MapError("%s: its index and its page's instance are both called %r" % (v.name, v.page.param()))
        group = [Accessor(v, f) for f in v.fields] if v.fields else [Accessor(v)]
        if v.unchecked:
            group += [Accessor(v, f, unchecked=True) for f in v.fields] if v.fields else [Accessor(v, unchecked=True)]
        for a in group:
            take(a.name, names, "accessor")
            take("set_" + a.name, names, "accessor")
            if a.bytes_const:
                take(a.bytes_const, consts, "constant")
            if a.rows_const:
                take(a.rows_const, consts, "constant")
                take(a.columns_const, consts, "constant")
        count = take(v.name.upper() + "_COUNT", consts, "constant") if v.count > 1 else None
        by_sub[v.subsystem].append((v, count, group))
    out = []
    for sub in SUBSYSTEMS:
        vs = by_sub.get(sub)
        if vs:
            out.append((sub, sorted(vs, key=lambda g: (g[0].on_instances(), g[0].addr or 0,
                                                       g[0].page.name if g[0].on_instances() else "",
                                                       g[0].dp or 0, g[0].name))))
    return out


def render_handles(m):
    """game/vars.h's handle types: how a port names an instance of each page kind."""
    out = []
    for p in page_kinds(m):
        title = "page kind %s" % p.name
        out += ["", "/* ---- %s %s */" % (title, "-" * (86 - len(title))), ""]
        out += comment("%s. The instances: %s." % (p.meaning.rstrip("."),
                                                    "; ".join(i.name for i in p.instances)))
        shape = p.shape()
        if shape == "named":
            out.append("typedef enum { %s } %s;" % (", ".join(p.constant(i) for i in p.instances), p.type_name()))
        elif shape == "array":
            i = p.instances[0]
            out.append("enum { %s = %d };   /* by %s */" % (p.count_constant(i), i.count, i.name))
        else:
            kind = p.type_name() + "Kind"
            out.append("typedef enum { %s } %s;" % (", ".join(p.constant(i) for i in p.instances), kind))
            out.append("typedef struct {")
            out.append("    %s kind;" % kind)
            out.append("    uint8_t index;   /* which one, for %s */"
                       % " and ".join(i.name for i in p.instances if i.array))
            out.append("} %s;" % p.type_name())
            for i in p.instances:
                if i.array:
                    out.append("enum { %s = %d };" % (p.count_constant(i), i.count))
            for i in p.instances:
                if i.array:
                    out.append("static inline %s %s(unsigned %s) { return (%s){ %s, (uint8_t)%s }; }"
                               % (p.type_name(), p.maker(i), i.name, p.type_name(), p.constant(i), i.name))
                else:
                    out.append("static inline %s %s(void) { return (%s){ %s, 0 }; }"
                               % (p.type_name(), p.maker(i), p.type_name(), p.constant(i)))
    return out


def render_vars_header(m):
    out = [
        "/* The game's state, as values a port reads and writes, generated by",
        " * tools/ram_map.py from re/ram.toml. Don't edit this file: add to the map and",
        " * regenerate (python3 tools/ram_map.py accessors). docs/STATE.md, \"The",
        " * RAM map\".",
        " *",
        " * There is a value per variable, an index per element of an array -- what the",
        " * map's `index` says selects one, never a byte offset -- and a pair per field of",
        " * a record. Nothing here says where the state lives or how it is arranged, which",
        " * is the point: a port written against these keeps working when the storage",
        " * moves. runtime/port/vars_ram.c backs them with native owners and an address",
        " * projection for format, bulk and unchecked accesses",
        " * (docs/PLAN.md, \"The game's state as C\"). A buffer wider than %d bytes has no one C value,"
        % WIDEST,
        " * so its bytes are read and written one at a time (`<name>_byte`). How many",
        " * elements an array has, and how many bytes such a buffer has, are named too,",
        " * so that a port that walks one need not know a number.",
        " *",
        " * Some cells are on a direct page the caller chooses: the same offsets on more",
        " * than one page (a page kind). Their accessors take which instance of the page",
        " * the port means -- a named one, an element of an array of pages, or either --",
        " * never a D or an offset. The handle types come first; an entry wrapper finds",
        " * the instance its caller's D is with <page>_page_of (runtime/port/vars_pages.h).",
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
    out += render_handles(m)
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
                if a.rows_const:
                    out.append("enum { %s = %d, %s = %d };" % (a.rows_const, a.var.rows, a.columns_const, a.size))
                out.append("%s;" % a.signature(False))
                out.append("%s;" % a.signature(True))
    out += ["", "#endif", ""]
    return "\n".join(out)


def render_backing(m, flat=False):
    if flat:
        out = [
            "/* The state accessors over work RAM as the original lays it out",
            " * (CT_FLAT_STATE: runtime/port/vars_host.h), generated for game/vars.h by",
            " * tools/ram_map.py from re/ram.toml. Don't edit this file: add to the map and",
            " * regenerate (python3 tools/ram_map.py accessors).",
            " *",
            " * Every accessor reads and writes the original's bytes at its address, so",
            " * owners the typed state keeps apart (re/ram.toml's `overlays`) share them",
            " * as the original's one byte; a build whose state must fit the original's",
            " * memory (the GBA's) uses it. The storage is the backing's",
            " * (vars_host_init, vars_host_read8, vars_host_write8). */",
            "#ifdef CT_FLAT_STATE"]
    else:
        out = [
            "/* Native state accessors and their address views, generated for game/vars.h",
            " * generated by tools/ram_map.py from re/ram.toml. Don't edit this file: add to",
            " * the map and regenerate (python3 tools/ram_map.py accessors).",
            " *",
            " * Module fields are the only backing for owned cells. Unmoved variables and",
            " * unchecked tails use state_host's address view, never a duplicate image. */",
            "#ifndef CT_FLAT_STATE"]
    out += [
        "#include \"vars.h\"",
        "",
        "#include \"port/vars_pages.h\"",
        "#include \"port/vars_ram.h\"",
        "#include \"wram.h\"",
        "",
        "/* Reads while a guard looks (vars_peek): they take none of the machine's time. */",
        "static bool peeking;",
        "void vars_peek(bool on) { peeking = on; }",
        "bool vars_peeking(void) { return peeking; }",
        "",
        "static unsigned looks;   /* PORT_CHECK's nesting, over a peek begun outside it */",
        "static bool before_the_looks;",
        "",
        "void vars_peek_enter(void)",
        "{",
        "    if (looks++ == 0)",
        "        before_the_looks = peeking;",
        "    peeking = true;",
        "}",
        "",
        "bool vars_peek_leave(bool value)",
        "{",
        "    if (looks && --looks == 0)",
        "        peeking = before_the_looks;",
        "    return value;",
        "}",
        "",
        "#include \"port/state_host.h\"",
        "#include \"port/vars_host.h\"",
        "",
        "/* Each access the game's time, as the machine's bus counts it (state_host.c),",
        "   but for a guard's look, which takes none of it (vars_peek). */",
        "static uint8_t r8(const GameState *g, uint32_t a)",
        "{",
        "    if (!peeking)",
        "        state_host_game_access(g, 8);",
        "    return state_host_read8(g, a);",
        "}",
        "static void w8(GameState *g, uint32_t a, uint8_t v)",
        "{",
        "    if (!peeking)",
        "        state_host_game_access(g, 8);",
        "    state_host_write8(g, a, v);",
        "}",
        "",
        "static uint16_t r16(const GameState *g, uint32_t a) { return (uint16_t)(r8(g, a) | r8(g, a + 1) << 8); }",
        "static uint32_t r24(const GameState *g, uint32_t a) { return (uint32_t)r16(g, a) | (uint32_t)r8(g, a + 2) << 16; }",
        "static uint32_t r32(const GameState *g, uint32_t a) { return (uint32_t)r16(g, a) | (uint32_t)r16(g, a + 2) << 16; }",
        "",
        "static void w16(GameState *g, uint32_t a, uint16_t v)",
        "{",
        "    w8(g, a, (uint8_t)v);",
        "    w8(g, a + 1, (uint8_t)(v >> 8));",
        "}",
        "",
        "static void w24(GameState *g, uint32_t a, uint32_t v)",
        "{",
        "    w16(g, a, (uint16_t)v);",
        "    w8(g, a + 2, (uint8_t)(v >> 16));",
        "}",
        "",
        "static void w32(GameState *g, uint32_t a, uint32_t v)",
        "{",
        "    w16(g, a, (uint16_t)v);",
        "    w16(g, a + 2, (uint16_t)(v >> 16));",
        "}",
        "",
        "/* The escape in game/wram.h: work RAM the game locates at run time, which the",
        " * map has no name for yet. It has no bound to check. */",
        "uint8_t wram_read8(const GameState *g, uint32_t at) { return r8(g, at); }",
        "uint16_t wram_read16(const GameState *g, uint32_t at) { return r16(g, at); }",
        "void wram_write8(GameState *g, uint32_t at, uint8_t v) { w8(g, at, v); }",
        "void wram_write16(GameState *g, uint32_t at, uint16_t v) { w16(g, at, v); }",
    ]
    if flat:
        out += render_flat_owners(m)
    out += render_page_backing(m)
    checked = checked_vars(m)
    out += render_range_checks(m, checked)
    ids = {v.name: k for k, v in enumerate(checked)}
    ov = Overlays.of(m)
    for sub, groups in accessors(m):
        out += ["", "/* ---- %s %s */" % (sub, "-" * (86 - len(sub))), ""]
        for _, _, group in groups:
            if group[0].var.host_owner and not flat:
                for a in group:
                    out += render_host_accessor(a, ids, ov)
                continue
            for a in group:
                w = width_of(a) * 8
                for write in (False, True):
                    name = ("set_" if write else "") + a.name
                    checks = "".join("IN_RANGE(g, %d, \"%s\", \"%s\", %s, %s); "
                                     % (ids[a.var.name], name, what, arg, limit)
                                     for arg, limit, what in a.bounds())
                    body = ("w%d(g, %s, v);" if write else "return r%d(g, %s);") % (w, a.address())
                    out.append("%s { %s%s }" % (a.signature(write), checks, body))
    out += ["", "#endif", ""]
    return "\n".join(out)


def render_flat_owners(m):
    """For the flat state: the runs of work RAM no owner keeps, whose bytes
    read 0, and the overlays' diagnostics, which one byte for every owner
    leaves nothing to count."""
    owned = Overlays.of(m).at
    runs, start = [], None
    for addr in range(0x7E0000, 0x800001):
        free = addr < 0x800000 and addr not in owned
        if free and start is None:
            start = addr
        elif not free and start is not None:
            runs.append((start, addr))
            start = None
    out = ["", "/* ---- the bytes no owner keeps -------------------------------------------------------- */", "",
           "/* Each run's first address and the one after it, in order. */",
           "static const uint32_t unowned[][2] = {"]
    out += ["    { 0x%06Xu, 0x%06Xu }," % run for run in runs]
    out += ["};", "",
            "bool vars_flat_owned(uint32_t addr)",
            "{",
            "    unsigned lo = 0, hi = sizeof unowned / sizeof unowned[0];",
            "    if (addr < 0x7E0000u || addr >= 0x800000u)",
            "        return false;",
            "    while (lo < hi) {",
            "        unsigned mid = (lo + hi) / 2;",
            "        if (addr >= unowned[mid][1])",
            "            lo = mid + 1;",
            "        else if (addr < unowned[mid][0])",
            "            hi = mid;",
            "        else",
            "            return false;",
            "    }",
            "    return true;",
            "}", "",
            "/* One byte for every owner: no last writer to keep, no crossing to count. */",
            "void vars_host_claim(VarsHost *h, uint32_t addr, unsigned bytes, unsigned place)",
            "{",
            "    (void)h; (void)addr; (void)bytes; (void)place;",
            "}", "",
            "#ifndef CT_LEAN",
            "void vars_host_check(const VarsHost *h, uint32_t addr, unsigned bytes, unsigned place)",
            "{",
            "    (void)h; (void)addr; (void)bytes; (void)place;",
            "}",
            "#endif", "",
            "unsigned vars_host_crossings(const VarsHost *h,",
            "                             void (*each)(const char *reader, const char *writer, uint32_t times, void *ctx),",
            "                             void *ctx)",
            "{",
            "    (void)h; (void)each; (void)ctx;",
            "    return 0;",
            "}", "",
            "void vars_host_crossings_reset(VarsHost *h) { (void)h; }"]
    return out


def page_ordinal(p):
    if p.shape() != "mixed":
        return "(unsigned)" + p.param()
    offset = 0
    terms = []
    for i in p.instances:
        terms.append("page.kind == %s ? %du + page.index : " % (p.constant(i), offset))
        offset += len(i.ds())
    return "(" + "".join(terms) + "0u)"


def host_member(a, root="h", element=None, row="row", byte="at", instance=None):
    v = a.var
    if v.record:
        record, binding = v.record
        member = "%s->%s.%s" % (root, record["module"], record["name"])
        index = (instance if instance is not None else page_ordinal(v.page)) if v.on_instances() else (element or index_param(v.index))
        if record["count"] > 1:
            member += "[%s]" % index
        if not binding.get("flatten"):
            member += "." + binding["name"]
            if binding.get("array") and v.count > 1:
                member += "[%s]" % index
    else:
        member = "%s->%s.%s" % (root, v.host_module, v.name)
        if v.on_instances():
            member += "[%s]" % (instance if instance is not None else page_ordinal(v.page))
        if v.rows:
            member += "[%s]" % row
        if v.count > 1:
            member += "[%s]" % (element or index_param(v.index))
    if v.fields:
        # Accessor names are unique and derived from the field name.
        member += "." + a.field.name
    if (a.blob or a.table) and not a.elements.name:
        member += "[%s]" % byte
    return member


def element_member(a, member, byte):
    e = a.elements
    offset = "(%s + %du)" % (byte, e.start)
    return "%s.%s[%s / %du]" % (member, e.name, offset, e.size), "%s %% %du" % (offset, e.size)


def width_of(a):
    """The bytes one access of an accessor moves."""
    if a.whole:
        return a.elements.size
    return 1 if a.blob or a.table else a.size


def owner_value(a):
    """For an alias marked `same_value`, each of whose values is exactly one
    value of its one owner (the same width at the same place, an element for an
    element): the owner's accessor and the C index of the owner's element for
    the alias's; else None. Such an alias is another name for the owner's
    value: it reads and writes the owner's own field, beside other modes'
    fields as the owner does, where other aliases of shared bytes read the
    address view (whichever owner wrote them last)."""
    v = a.var
    if (not v.same_value or not v.host_alias or len(v.host_owners) != 1 or v.on_instances() or v.rows or
            (a.field and a.field.host_alias) or a.blob or a.elements.name):
        return None
    owner = v.host_owner
    if owner.rows or owner.on_instances():
        return None
    for f in owner.fields or [None]:
        if f and f.host_alias:
            continue
        oa = Accessor(owner, f)
        if oa.blob or oa.elements.name or oa.size != a.size:
            continue
        first, rest = divmod(a.addr - oa.addr, owner.stride)
        if rest or first < 0 or first + v.count > owner.count or (v.count > 1 and v.stride != owner.stride):
            continue
        if owner.count == 1:
            return oa, None
        index = index_param(v.index) if v.count > 1 else None
        return oa, ("%du" % first if index is None else "%du + %s" % (first, index) if first else index)
    return None


def render_host_accessor(a, ids, ov):
    out = []
    same = owner_value(a) if ov.touches(a) else None
    if a.var.same_value and ov.touches(a) and not same and not a.unchecked:
        raise MapError("%s: same_value needs an alias that is one owner value, element for element" % a.name)
    if same:
        # Another name for one owner value: the owner's own field, checked and
        # claimed as the owner's accessor does.
        oa, index = same
        member = host_member(oa, element=index)
        width = a.size
        for write in (False, True):
            out += [a.signature(write), "{"]
            name = ("set_" if write else "") + a.name
            out += ['    IN_RANGE(g, %d, "%s", "%s", %s, %s);' % (ids[a.var.name], name, what, arg, limit)
                    for arg, limit, what in a.bounds()]
            bad = " || ".join("%s >= %s" % (arg, limit) for arg, limit, _ in Accessor(a.var, a.field).bounds())
            if bad:
                fallback = ("w%d(g, %s, v); return;" if write else "return r%d(g, %s);") % (width * 8, a.address())
                out += ["    if (%s) { %s }" % (bad, fallback)]
            out += ["    if (!peeking)", "        state_host_game_access(g, %d);" % (width * 8)]
            out += ["    %sVarsHost *h = state_host_vars%s(g);" % ("" if write else "const ", "" if write else "_const")]
            place = ov.place(oa)
            if write:
                out += ["    %s = %s;" % (member, "v & 0xFFFFFFu" if width == 3 else "v"),
                        "    vars_host_claim(h, %s, %du, %s);" % (a.address(), width, place)]
            else:
                out += ["    if (!peeking)",
                        "        vars_host_check(h, %s, %du, %s);" % (a.address(), width, place),
                        "    return %s;" % member]
            out += ["}"]
        return out
    overlaid = ov.touches(a) and not (a.var.host_alias or (a.field and a.field.host_alias))
    claim = "    vars_host_claim(h, %s, %du, %s);" % (a.address(), width_of(a), ov.place(a)) if overlaid else None
    check = ("    if (!peeking)\n        vars_host_check(h, %s, %du, %s);" % (a.address(), width_of(a), ov.place(a))
             if overlaid else None)
    width = width_of(a)
    for write in (False, True):
        out += [a.signature(write), "{"]
        name = ("set_" if write else "") + a.name
        out += ['    IN_RANGE(g, %d, "%s", "%s", %s, %s);' % (ids[a.var.name], name, what, arg, limit)
                for arg, limit, what in a.bounds()]
        # Invalid indices keep the address-view behavior, never index a C array
        # out of bounds. Deliberate unchecked tails take that same path, silently.
        bad = " || ".join("%s >= %s" % (arg, limit) for arg, limit, _ in Accessor(a.var, a.field).bounds())
        if a.var.on_instances() and a.var.page.shape() == "named":
            page_bad = "(unsigned)page >= %du" % len(a.var.page.instances)
            bad = page_bad + (" || " + bad if bad else "")
        if bad:
            fallback = ("w%d(g, %s, v); return;" if write else "return r%d(g, %s);") % (width * 8, a.address())
            out += ["    if (%s) { %s }" % (bad, fallback)]
        out += ["    if (!peeking)", "        state_host_game_access(g, %d);" % (width * 8)]
        out += ["    %sVarsHost *h = state_host_vars%s(g);" % ("" if write else "const ", "" if write else "_const")]
        # Composed and page views use only the struct projection. Validation
        # proves that every byte has one of the explicitly named owners.
        # So do views over bytes owners keep beside each other: the address
        # view reads whichever wrote last, as the original's one byte would.
        complex_view = (len(a.var.host_owners) > 1 or a.var.on_instances() or
                        a.var.host_owner.on_instances() or ov.touches(a))
        if a.var.host_alias or (a.field and a.field.host_alias):
            owner = a.var.host_owner
            if complex_view:
                out += ["    uint32_t address = %s;" % a.address()]
                if not write:
                    out += ["    %s value = 0;" % a.type, "    uint8_t byte = 0;"]
                for byte in range(width):
                    if write:
                        out += ["    vars_host_write8(h, address + %du, (uint8_t)(v >> %d));" % (byte, byte * 8)]
                    else:
                        out += ["    vars_host_read8(h, address + %du, &byte);" % byte,
                                "    value |= (%s)((%s)byte << %d);" % (a.type, a.type, byte * 8)]
                if not write:
                    out += ["    return value;"]
                out += ["}"]
                continue
            offset = "%du" % (a.addr - owner.addr)
            if a.var.count > 1:
                offset += " + (uint32_t)%s * %du" % (index_param(a.var.index), a.var.stride)
            if a.table:
                offset += " + (uint32_t)row * %du + column" % a.var.row_stride
            elif a.blob:
                offset += " + at"
            out += ["    uint32_t offset = %s;" % offset]
            if not write:
                out += ["    %s value = 0;" % a.type]
            for byte in range(width):
                if write:
                    out += ["    vars_host_%s_write8(h, offset + %du, (uint8_t)(v >> %d));" % (owner.name, byte, byte * 8)]
                else:
                    out += ["    value |= (%s)((%s)vars_host_%s_read8(h, offset + %du) << %d);" %
                            (a.type, a.type, owner.name, byte, byte * 8)]
            if not write:
                out += ["    return value;"]
            out += ["}"]
            continue
        member = host_member(a, byte="column" if a.table else "at")
        if check and not write:
            out += [check]
        if a.whole:
            item = "%s.%s[item]" % (member, a.elements.name)
            out += ["    %s = v;" % item if write else "    return %s;" % item]
            if write and claim:
                out += [claim]
            out += ["}"]
            continue
        if a.elements.name:
            byte = "column" if a.table else "at" if a.blob else "0u"
            if not write:
                out += ["    %s value = 0;" % a.type]
            for i in range(width):
                item, shift = element_member(a, member, "(%s + %du)" % (byte, i))
                if write:
                    out += ["    %s = (%s)((%s & ~((uint32_t)0xFFu << (%s * 8))) | (uint32_t)(uint8_t)(v >> %d) << (%s * 8));" %
                            (item, C_TYPE[a.elements.size], item, shift, i * 8, shift)]
                else:
                    out += ["    value |= (%s)((%s)(uint8_t)(%s >> (%s * 8)) << %d);" %
                            (a.type, a.type, item, shift, i * 8)]
            if write and claim:
                out += [claim]
            if not write:
                out += ["    return value;"]
            out += ["}"]
            continue
        out += ["    %s" % (member + " = " + ("v & 0xFFFFFFu" if width == 3 else "v") + ";" if write else "return " + member + ";")]
        if write and claim:
            out += [claim]
        out += ["}"]
    return out


def host_groups(m):
    groups = collections.defaultdict(list)
    for v in m.vars + m.owners:
        if v.host_module:
            groups[v.host_module].append(v)
    for r in m.records:
        groups[r["module"]]
    return sorted(groups.items())


def host_declaration(v, name, dims="", indent="    ", flatten=False):
    out = []
    if v.fields:
        if not flatten:
            out += [indent + "struct {"]
        inner = indent if flatten else indent + "    "
        for f in v.fields:
            if f.host_alias:
                continue
            a = Accessor(v, f)
            if a.elements.name:
                e = a.elements
                out += [inner + "struct { %s %s[%d]; } %s;" %
                        (C_TYPE[e.size], e.name, e.count(f.size), f.name)]
            else:
                out += [inner + "%s %s%s;" % (a.type, f.name,
                        "[%d]" % f.size if a.blob or a.table else "")]
        if not flatten:
            out += [indent + "} %s%s;" % (name, dims)]
    elif v.elements.name:
        e = v.elements
        out += [indent + "struct { %s %s[%d]; } %s%s;" %
                (C_TYPE[e.size], e.name, e.count(v.size), name, dims)]
    else:
        blob = v.size > WIDEST or v.rows
        out += [indent + "%s %s%s%s;" % ("uint8_t" if blob else C_TYPE[v.size], name, dims,
                "[%d]" % v.size if blob else "")]
    return out


def render_host_header(m):
    out = ["/* Module-owned native state; generated by tools/ram_map.py accessors.",
           " * The address view serializes fields explicitly, independent of C padding",
           " * and host byte order. Unowned addresses have no storage. */",
           "#ifndef CT_PORT_VARS_HOST_H", "#define CT_PORT_VARS_HOST_H", "",
           "#include <stdbool.h>", "#include <stdint.h>", ""]
    groups = host_groups(m)
    owners = {v.name: v for v in m.vars + m.owners if v.host_module}
    for r in m.records:
        out += ["/* %s */" % r["meaning"], "typedef struct {"]
        for binding in r["members"]:
            v = owners[binding["owner"]]
            dims = "[%d]" % v.count if binding.get("array") and v.count > 1 else ""
            out += host_declaration(v, binding.get("name"), dims,
                                    flatten=binding.get("flatten", False))
        out += ["} %s;" % r["type"], ""]
    for module, vs in groups:
        out += ["typedef struct {"]
        for r in m.records:
            if r["module"] == module:
                out += ["    %s %s%s;" % (r["type"], r["name"], "[%d]" % r["count"] if r["count"] > 1 else "")]
        for v in vs:
            if v.record:
                continue
            dims = ("[%d]" % len(v.bases()) if v.on_instances() else "")
            dims += ("[%d]" % v.rows if v.rows else "") + ("[%d]" % v.count if v.count > 1 else "")
            out += host_declaration(v, v.name, dims)
        if not any(not v.record for v in vs) and not any(r["module"] == module for r in m.records):
            out += ["    uint8_t unused; /* C has no empty structs. */"]
        out += ["} VarsHost%s;" % camel(module), ""]
    out += ["#ifdef CT_FLAT_STATE",
            "/* Work RAM as the original lays it out (runtime/port/vars_flat.c): its two",
            " * 64 KiB banks, a byte for each address, where the backing keeps them",
            " * (runtime/port/vars_flat_host.c, or a target's). The typed records above",
            " * are left unused. */",
            "typedef struct {", "    uint8_t *bank[2];", "} VarsHost;",
            "/* Whether the map gives the address an owner: bytes no owner keeps read 0",
            " * and drop writes, as the typed state's address view does. */",
            "bool vars_flat_owned(uint32_t addr);",
            "#else"]
    out += ["typedef struct {"]
    for module, _ in groups:
        out += ["    VarsHost%s %s;" % (camel(module), module)]
    if not groups:
        out += ["    uint8_t unused; /* C has no empty structs. */"]
    ov = Overlays.of(m)
    slots = len(ov.pages) * 256
    if slots:
        out += ["    /* For each byte owners keep beside each other, the owner that wrote it",
                "     * last, which the address view reads: its place among the byte's",
                "     * owners, from 1; 0 when every owner holds it. */",
                "    uint8_t overlay_writer[%d];" % slots]
    out += ["    /* The reads that found another owner's bytes, by reader: how often, and",
            "     * the owner that wrote them the first time (vars_host_crossings). A",
            "     * diagnostic tally, kept with the state it describes; the lean build",
            "     * (CT_LEAN) keeps none. */",
            "#ifndef CT_LEAN",
            "    uint32_t crossed[%d];" % (len(ov.placed) + 1),
            "    uint16_t crossed_writer[%d];" % (len(ov.placed) + 1),
            "#endif"]
    out += ["} VarsHost;", ""]
    for v in alias_owners(m):
        out += ["/* Byte views of %s's storage; no independent alias fields. */" % v.name,
                "uint8_t vars_host_%s_read8(const VarsHost *h, uint32_t offset);" % v.name,
                "void vars_host_%s_write8(VarsHost *h, uint32_t offset, uint8_t value);" % v.name]
    out += ["#endif"]
    out += ["", "/* An owner beside others (re/ram.toml's `overlays`) wrote these bytes. */",
            "void vars_host_claim(VarsHost *h, uint32_t addr, unsigned bytes, unsigned place);",
            "/* An owner beside others reads these bytes: count any another owner wrote last.",
            " * The lean build counts nothing, and reports no crossings. */",
            "#ifdef CT_LEAN",
            "#define vars_host_check(h, addr, bytes, place) ((void)(h))",
            "#else",
            "void vars_host_check(const VarsHost *h, uint32_t addr, unsigned bytes, unsigned place);",
            "#endif",
            "/* The reads that found another owner's bytes: each reader, the owner that",
            " * wrote them and how often, through `each`; returns how many readers. */",
            "unsigned vars_host_crossings(const VarsHost *h,",
            "                             void (*each)(const char *reader, const char *writer, uint32_t times, void *ctx),",
            "                             void *ctx);",
            "void vars_host_crossings_reset(VarsHost *h);"]
    out += ["", "void vars_host_init(VarsHost *h);",
            "bool vars_host_read8(const VarsHost *h, uint32_t addr, uint8_t *value);",
            "bool vars_host_write8(VarsHost *h, uint32_t addr, uint8_t value);",
            "/* `len` bytes of work RAM from `addr`, as vars_host_read8 reads each (0 where",
            " * no owner keeps one), in one call: what a send names by address. */",
            "void vars_host_read_span(const VarsHost *h, uint32_t addr, uint8_t *out, uint32_t len);",
            "", "#endif", ""]
    return "\n".join(out)


def placements(m):
    """Every owner accessor in each of its page instances, as (accessor,
    instance, first byte): the address view's cases, numbered from 1."""
    return [(a, instance, base + a.offset) for a in owner_accessors(m)
            for instance, base in enumerate(a.var.bases())]


def accessor_bytes(a, base):
    """The addresses an accessor's value covers from one of its bases, over
    every row and element."""
    for row in range(a.var.rows or 1):
        for element in range(a.var.count):
            start = base + row * (a.var.row_stride or 0) + element * a.var.stride
            yield from range(start, start + a.size)


class Overlays:
    """Where owners stand beside each other (`overlays`): each byte's
    placements, base owner first, and the bytes more than one owner keeps.
    The address view writes such a byte to every owner and reads it from the
    one that wrote it last; each owner's own accessors use its own field."""

    @classmethod
    def of(cls, m):
        """The map's overlays, worked out once."""
        if getattr(m, "_overlays", None) is None:
            m._overlays = cls(m)
        return m._overlays

    def __init__(self, m):
        self.placed = placements(m)
        owners = collections.defaultdict(list)
        self.first = {}
        for number, (a, instance, base) in enumerate(self.placed, 1):
            self.first.setdefault((a.var.name, a.field.name if a.field else None), number)
            for addr in accessor_bytes(a, base):
                owners[addr].append((bool(a.var.overlays), number))
        self.at = {addr: [n for _, n in sorted(ns)] for addr, ns in owners.items()}
        self.shared = {addr for addr, ns in self.at.items() if len(ns) > 1}
        # Each 256-byte page with a shared byte keeps a last writer per byte.
        self.pages = sorted({(addr - 0x7E0000) >> 8 for addr in self.shared})
        # The owners of each shared byte, as a set shared by the bytes with the same owners.
        self.sets = {}
        self.touched = {}
        for addr in sorted(self.shared):
            self.sets.setdefault(tuple(self.at[addr]), len(self.sets) + 1)

    def touches(self, a):
        key = (a.var.name, a.field.name if a.field else None)
        if key not in self.touched:
            self.touched[key] = any(addr in self.shared for base in a.var.bases()
                                    for addr in accessor_bytes(a, base))
        return self.touched[key]

    def place(self, a):
        """The C expression for an owner accessor's placement number."""
        first = self.first[(a.var.name, a.field.name if a.field else None)]
        if a.var.on_instances():
            return "%du + %s" % (first, page_ordinal(a.var.page))
        return "%du" % first


def owner_accessors(m):
    return ([a for _, vs in accessors(m) for v, _, aa in vs if v.host_module for a in aa
             if not a.unchecked and not (a.field and a.field.host_alias)] +
            [Accessor(v, f) for v in m.owners for f in (v.fields or [None])
             if not (f and f.host_alias)])


def alias_owners(m):
    names = {v.host_owner.name for v in m.vars
             if (v.host_alias or any(f.host_alias for f in v.fields)) and
             len(v.host_owners) == 1 and not v.on_instances() and not v.host_owner.on_instances()}
    return sorted((v for v in m.vars + m.owners if v.name in names), key=lambda v: v.name)


def projection_statement(a, write, byte="offset", instance=None):
    member = host_member(a, element="element", byte=byte, instance=instance)
    if a.elements.name:
        member, byte = element_member(a, member, byte)
        if write:
            return "%s = (%s)((%s & ~((uint32_t)0xFFu << (%s * 8))) | (uint32_t)value << (%s * 8));" % (
                member, C_TYPE[a.elements.size], member, byte, byte)
        return "*value = (uint8_t)(%s >> (%s * 8));" % (member, byte)
    if a.blob or a.table:
        return "%s = value;" % member if write else "*value = %s;" % member
    if write:
        return "%s = (%s)((%s & ~((uint32_t)0xFFu << (%s * 8))) | (uint32_t)value << (%s * 8));" % (
            member, a.type, member, byte, byte)
    return "*value = (uint8_t)(%s >> (%s * 8));" % (member, byte)


def projection_indices(a):
    out = []
    if a.var.rows:
        out += ["unsigned row = offset / %du;" % a.var.row_stride, "offset %%= %du;" % a.var.row_stride]
    if a.var.count > 1:
        out += ["unsigned element = offset / %du;" % a.var.stride, "offset %%= %du;" % a.var.stride]
    return out


def render_host_backing(m):
    out = ["/* Address projection of module fields; generated by tools/ram_map.py accessors. */",
           '#include "port/vars_host.h"', "#include <string.h>", "#ifndef CT_FLAT_STATE", "",
           "void vars_host_init(VarsHost *h)", "{", "    memset(h, 0x55, sizeof *h);"]
    groups = owner_accessors(m)
    # 24-bit values live in uint32_t, whose unused high byte is always zero.
    for a in groups:
        if a.elements.name and a.elements.size == 3:
            if a.var.on_instances():
                out += ["    for (unsigned instance = 0; instance < %d; instance++)" % len(a.var.bases())]
            if a.var.rows:
                out += ["    for (unsigned row = 0; row < %d; row++)" % a.var.rows]
            if a.var.count > 1:
                out += ["    for (unsigned i = 0; i < %d; i++)" % a.var.count]
            out += ["    for (unsigned item = 0; item < %d; item++)" % a.elements.count(a.size),
                    "        %s.%s[item] = 0x555555u;" %
                    (host_member(a, element="i", instance="instance"), a.elements.name)]
        elif a.size == 3 and not a.table and not a.blob and not a.elements.name:
            if a.var.on_instances():
                out += ["    for (unsigned instance = 0; instance < %d; instance++)" % len(a.var.bases())]
            if a.var.count > 1:
                out += ["    for (unsigned i = 0; i < %d; i++)" % a.var.count]
            out += ["    %s%s = 0x555555u;" % ("    " if a.var.count > 1 or a.var.on_instances() else "", host_member(a, element="i", instance="instance"))]
    if Overlays.of(m).shared:
        out += ["    memset(h->overlay_writer, 0, sizeof h->overlay_writer);"]
    out += ["    vars_host_crossings_reset(h);", "}", ""]
    # Aliases use named owners, with offsets in the owner's serialized layout.
    # No host pointer cast, union type punning, padding or endian assumption.
    for v in alias_owners(m):
        aa = [a for a in groups if a.var is v]
        for write in (False, True):
            out += [("void vars_host_%s_write8(VarsHost *h, uint32_t offset, uint8_t value)" if write else
                     "uint8_t vars_host_%s_read8(const VarsHost *h, uint32_t offset)") % v.name, "{"]
            # Page owners use the projection; these helpers are for fixed
            # owners, including rows and strided arrays.
            if v.rows:
                out += ["    unsigned row = offset / %du;" % v.row_stride, "    offset %%= %du;" % v.row_stride]
            if v.count > 1:
                out += ["    unsigned element = offset / %du;" % v.stride, "    offset %%= %du;" % v.stride]
            for a in aa:
                out += ["    if (offset >= %du && offset < %du) {" % (a.offset, a.offset + a.size)] if a.offset else ["    if (offset < %du) {" % a.size]
                out += ["        unsigned byte = offset - %du;" % a.offset]
                statement = projection_statement(a, write, "byte")
                if not write:
                    statement = statement.replace("*value = ", "return ")
                out += ["        " + statement]
                if write:
                    out += ["        return;"]
                out += ["    }"]
            out += ["    return;" if write else "    return 0;", "}", ""]
    # Two indexed loads choose the field. Only populated pages get a byte table;
    # zero selects no storage. Stride/record holes have no owner.
    ov = Overlays.of(m)
    placed = ov.placed

    def byte_table(name, values):
        kind = "uint32_t" if max(values.values(), default=0) > 0xFFFF else "uint16_t"
        pages = {}
        for addr, n in values.items():
            pages.setdefault((addr - 0x7E0000) >> 8, [0] * 256)[addr & 255] = n
        if not pages:
            return []
        lines = ["static const uint16_t %s_pages[512] = {" % name]
        lines += ["    [%d] = %d," % (page, i) for i, page in enumerate(sorted(pages), 1)]
        lines += ["};", "", "static const %s %s_fields[][256] = {" % (kind, name), "    { 0 },"]
        for page in sorted(pages):
            lines += ["    { /* $%06X */" % (0x7E0000 + page * 256)]
            for at in range(0, 256, 16):
                lines += ["        " + ", ".join(str(n) for n in pages[page][at:at + 16]) + ","]
            lines += ["    },"]
        lines += ["};", "", "static unsigned %s_at(uint32_t addr)" % name, "{",
                  "    if (addr < 0x7E0000u || addr >= 0x800000u)", "        return 0;",
                  "    return %s_fields[%s_pages[(addr - 0x7E0000u) >> 8]][addr & 255u];" % (name, name), "}", ""]
        return lines

    out += byte_table("projection", {addr: ns[0] for addr, ns in ov.at.items()})
    if ov.shared:
        sets = ov.sets
        out += ["/* The owners of each byte owners keep beside each other: a set of them",
                " * (overlay_owner_sets) per byte of each page that has one. */",
                "static const uint16_t overlay_pages[512] = {"]
        out += ["    [%d] = %d," % (page, k) for k, page in enumerate(ov.pages, 1)]
        out += ["};", "", "static const uint16_t overlay_fields[][256] = {", "    { 0 },"]
        for page in ov.pages:
            row = [sets[tuple(ov.at[0x7E0000 + page * 256 + b])] if 0x7E0000 + page * 256 + b in ov.shared else 0
                   for b in range(256)]
            out += ["    { /* $%06X */" % (0x7E0000 + page * 256)]
            for at in range(0, 256, 16):
                out += ["        " + ", ".join(str(n) for n in row[at:at + 16]) + ","]
            out += ["    },"]
        widest = max(len(k) for k in sets)
        out += ["};", "", "static const uint16_t overlay_owner_sets[%d][%d] = {" % (len(sets) + 1, widest + 1),
                "    { 0 },"]
        out += ["    { %s, 0 }," % ", ".join(str(n) for n in k) for k in sets]
        out += ["};", "",
                "/* The byte's place in overlay_writer, from 1, or 0 when one owner keeps it. */",
                "static unsigned overlay_at(uint32_t addr, unsigned *set)", "{",
                "    if (addr < 0x7E0000u || addr >= 0x800000u)", "        return 0;",
                "    unsigned page = overlay_pages[(addr - 0x7E0000u) >> 8];",
                "    *set = overlay_fields[page][addr & 255u];",
                "    return *set ? (page - 1u) * 256u + (addr & 255u) + 1u : 0u;", "}", ""]
    out += ["#ifndef CT_LEAN", "/* What each owner field is called, for the crossings report. */",
            "static const char *const place_names[%d] = {" % (len(placed) + 1), "    \"\","]
    out += ["    \"%s%s\"," % (a.var.name + ("." + a.field.name if a.field else ""),
                               "[%d]" % instance if a.var.on_instances() else "")
            for a, instance, _ in placed]
    out += ["};", "#endif", ""]
    for write in (False, True):
        out += ["static void place_%s8(%sVarsHost *h, unsigned place, uint32_t addr, uint8_t %svalue)" %
                ("write" if write else "read", "" if write else "const ", "" if write else "*"), "{"]
        if not placed:
            out += ["    (void)h; (void)place; (void)addr; (void)value;"]
        else:
            out += ["    switch (place) {"]
            for number, (a, instance, base) in enumerate(placed, 1):
                out += ["    case %d: {" % number, "        uint32_t offset = addr - 0x%06Xu;" % base]
                out += ["        " + line for line in projection_indices(a)]
                out += ["        " + projection_statement(a, write, instance=str(instance)), "        return;", "    }"]
            out += ["    default: break;", "    }"]
        out += ["}", ""]
    out += ["bool vars_host_read8(const VarsHost *h, uint32_t addr, uint8_t *value)", "{"]
    if placed:
        out += ["    unsigned place = projection_at(addr);", "    if (!place)", "        return false;"]
        if ov.shared:
            out += ["    unsigned set, slot = overlay_at(addr, &set);",
                    "    if (slot && h->overlay_writer[slot - 1])",
                    "        place = overlay_owner_sets[set][h->overlay_writer[slot - 1] - 1u];"]
        out += ["    place_read8(h, place, addr, value);", "    return true;", "}", ""]
    else:
        out += ["    (void)h; (void)addr; (void)value;", "    return false;", "}", ""]
    out += ["bool vars_host_write8(VarsHost *h, uint32_t addr, uint8_t value)", "{"]
    if placed:
        out += ["    unsigned place = projection_at(addr);", "    if (!place)", "        return false;"]
        if ov.shared:
            out += ["    unsigned set, slot = overlay_at(addr, &set);", "    if (slot) {",
                    "        /* Every owner of the byte takes the write, as the original's one byte does. */",
                    "        for (const uint16_t *owner = overlay_owner_sets[set]; *owner; owner++)",
                    "            place_write8(h, *owner, addr, value);",
                    "        h->overlay_writer[slot - 1] = 0;",
                    "        return true;", "    }"]
        out += ["    place_write8(h, place, addr, value);", "    return true;", "}", ""]
    else:
        out += ["    (void)h; (void)addr; (void)value;", "    return false;", "}", ""]
    out += ["void vars_host_read_span(const VarsHost *h, uint32_t addr, uint8_t *out, uint32_t len)", "{",
            "    for (uint32_t i = 0; i < len; i++) {",
            "        uint8_t value = 0;",
            "        vars_host_read8(h, addr + i, &value);",
            "        out[i] = value;",
            "    }",
            "}", ""]
    out += ["void vars_host_claim(VarsHost *h, uint32_t addr, unsigned bytes, unsigned place)", "{"]
    if ov.shared:
        out += ["    for (unsigned i = 0; i < bytes; i++) {",
                "        unsigned set, slot = overlay_at(addr + i, &set);",
                "        if (!slot)",
                "            continue;",
                "        for (unsigned k = 0; overlay_owner_sets[set][k]; k++)",
                "            if (overlay_owner_sets[set][k] == place)",
                "                h->overlay_writer[slot - 1] = (uint8_t)(k + 1u);",
                "    }", "}", ""]
    else:
        out += ["    (void)h; (void)addr; (void)bytes; (void)place;", "}", ""]
    out += ["#ifdef CT_LEAN", "unsigned vars_host_crossings(const VarsHost *h,",
            "                             void (*each)(const char *reader, const char *writer, uint32_t times, void *ctx),",
            "                             void *ctx)", "{", "    (void)h; (void)each; (void)ctx;", "    return 0;", "}", "",
            "void vars_host_crossings_reset(VarsHost *h) { (void)h; }", "#else"]
    out += ["void vars_host_check(const VarsHost *h, uint32_t addr, unsigned bytes, unsigned place)", "{"]
    if ov.shared:
        out += ["    /* The tally is the diagnostics' own, not the game's: a read counts it. */",
                "    VarsHost *tally = (VarsHost *)h;",
                "    for (unsigned i = 0; i < bytes; i++) {",
                "        unsigned set, slot = overlay_at(addr + i, &set);",
                "        unsigned writer = slot && h->overlay_writer[slot - 1] ?",
                "            overlay_owner_sets[set][h->overlay_writer[slot - 1] - 1u] : 0u;",
                "        if (writer && writer != place) {",
                "            if (!tally->crossed[place]++)",
                "                tally->crossed_writer[place] = (uint16_t)writer;",
                "            return;", "        }", "    }", "}", ""]
    else:
        out += ["    (void)h; (void)addr; (void)bytes; (void)place;", "}", ""]
    out += ["unsigned vars_host_crossings(const VarsHost *h,",
            "                             void (*each)(const char *reader, const char *writer, uint32_t times, void *ctx),",
            "                             void *ctx)", "{", "    unsigned readers = 0;",
            "    for (unsigned place = 1; place < sizeof h->crossed / sizeof h->crossed[0]; place++) {",
            "        if (!h->crossed[place])", "            continue;", "        readers++;",
            "        if (each)",
            "            each(place_names[place], place_names[h->crossed_writer[place]], h->crossed[place], ctx);",
            "    }", "    return readers;", "}", "",
            "void vars_host_crossings_reset(VarsHost *h)", "{",
            "    memset(h->crossed, 0, sizeof h->crossed);", "    memset(h->crossed_writer, 0, sizeof h->crossed_writer);", "}",
            "#endif", "", "#endif", ""]
    return "\n".join(out)


def checked_vars(m):
    """The variables with an accessor whose index the backing checks, in the
    header's order: the rows of the out-of-range counts."""
    return [v for _, groups in accessors(m) for v, _, group in groups if any(a.bounds() for a in group)]


def render_range_checks(m, checked):
    """The bounds check every indexed accessor makes, and the counts it keeps."""
    out = ["", "/* ---- bounds %s */" % ("-" * 76), ""]
    out += comment("An accessor's index past what re/ram.toml declares reaches whatever comes next in "
                   "the compatibility image. CT_ACCESSOR_CHECKS counts such accesses per variable, "
                   "with the first few indices and their source. An explicitly documented `_unchecked` "
                   "accessor is reserved for a declared extended range; native storage migrations must "
                   "resolve these ranges before removing the compatibility image.")
    out += ["#ifdef CT_ACCESSOR_CHECKS", "",
            "static VarsRangeFault range_faults[] = {"]
    out += ["    { .var = \"%s\" }," % v.name for v in checked] or ["    { .var = 0 },"]
    out += ["};",
            "",
            "static void out_of_range(const GameState *g, unsigned var, const char *accessor, const char *bound,",
            "                         unsigned index, unsigned limit)",
            "{",
            "    (void)g;",
            "    VarsRangeFault *f = &range_faults[var];",
            "    if (f->kept < VARS_RANGE_HITS_KEPT) {",
            "        uint32_t pc = 0; /* Native state has no emulated program counter. */",
            "        f->first[f->kept++] = (VarsRangeHit){ accessor, bound, index, limit, pc };",
            "    }",
            "    f->count++;",
            "}",
            "",
            "#define IN_RANGE(g, var, accessor, bound, index, limit) \\",
            "    ((unsigned)(index) < (unsigned)(limit) ? (void)0 \\",
            "                                           : out_of_range(g, var, accessor, bound, (unsigned)(index), (unsigned)(limit)))",
            "",
            "const VarsRangeFault *vars_range_faults(unsigned *count)",
            "{",
            "    *count = %d;" % len(checked),
            "    return range_faults;",
            "}",
            "",
            "void vars_range_reset(void)",
            "{",
            "    for (unsigned i = 0; i < sizeof range_faults / sizeof range_faults[0]; i++) {",
            "        range_faults[i].count = 0;",
            "        range_faults[i].kept = 0;",
            "    }",
            "}"]
    counted = [p for p in page_kinds(m) if p.shape() == "mixed"
               and any(v.on_instances() and v.page is p for v in m.vars)]
    counts = []
    for p in counted:
        conditional = not any(v.host_owner and v.on_instances() and v.page is p for v in m.vars)
        if conditional:
            counts += ["#ifdef CT_ACCESSOR_CHECKS"]
        counts += ["", "/* How many instances the kind of a %s handle has. */" % p.name,
                "static inline unsigned %s(%s %s)" % (page_count_name(p), p.type_name(), p.param()), "{",
                "    switch (%s.kind) {" % p.param()]
        for i in p.instances:
            counts += ["    case %s:" % p.constant(i), "        return %du;" % (i.count if i.array else 1)]
        counts += ["    }", "    return 0u;", "}"]
        if conditional:
            counts += ["#endif"]
    out += ["",
            "#else",
            "",
            "#define IN_RANGE(g, var, accessor, bound, index, limit) ((void)0)",
            "",
            "const VarsRangeFault *vars_range_faults(unsigned *count)",
            "{",
            "    *count = 0;",
            "    return 0;",
            "}",
            "",
            "void vars_range_reset(void) {}",
            "",
            "#endif"]
    out += counts
    return out


PAGES_HEADER = ROOT / "runtime" / "port" / "vars_pages.h"


def of_signature(p):
    out = "%s *%s" % ("unsigned" if p.shape() == "array" else p.type_name(), p.param())
    return "bool %s(uint16_t d, %s)" % (p.of_name(), out)


def render_pages_header(m):
    out = [
        "/* Which instance of a page kind a D is: the mapping an entry wrapper uses to",
        " * turn its caller's direct page into the handle game/vars.h's accessors take,",
        " * generated by tools/ram_map.py from re/ram.toml's page instances. Don't edit",
        " * this file: add to the map and regenerate (python3 tools/ram_map.py",
        " * accessors). docs/STATE.md.",
        " *",
        " * Each <page>_page_of returns false for a D that is no declared instance, and",
        " * counts it in vars_page_refusals. Instances are declared by the map,",
        " * and must remain valid while native storage moves to owned state.",
        " *",
        " * Only a wrapper uses these; the port's logic is given the handle. */",
        "#ifndef CT_PORT_VARS_PAGES_H",
        "#define CT_PORT_VARS_PAGES_H",
        "",
        "#include <stdbool.h>",
        "#include <stdint.h>",
        "",
        "#include \"vars.h\"",
    ]
    for p in page_kinds(m):
        out.append("")
        out += comment("%s: %s" % (p.name, "; ".join(i.describe() for i in p.instances)))
        out.append("%s;" % of_signature(p))
    out += [
        "",
        "/* The D values each page kind's <page>_page_of refused, in this process. */",
        "typedef struct {",
        "    const char *page;",
        "    unsigned long refused;",
        "    uint16_t last_d;   /* the latest one */",
        "} VarsPageRefusals;",
        "const VarsPageRefusals *vars_page_refusals(unsigned *count);",
        "",
        "#endif",
        "",
    ]
    return "\n".join(out)


def render_page_backing(m):
    kinds = page_kinds(m)
    out = ["", "/* ---- page kinds %s */" % ("-" * 72)]
    if not kinds:
        out += ["", "const VarsPageRefusals *vars_page_refusals(unsigned *count)", "{",
                "    *count = 0;", "    return 0;", "}"]
        return out
    out += ["",
            "/* The D values each <page>_page_of turned away. */",
            "static VarsPageRefusals refusals[] = {"]
    out += ["    { \"%s\", 0, 0 }," % p.name for p in kinds]
    out += ["};",
            "",
            "static bool refuse(unsigned kind, uint16_t d)",
            "{",
            "    refusals[kind].refused++;",
            "    refusals[kind].last_d = d;",
            "    return false;",
            "}",
            "",
            "const VarsPageRefusals *vars_page_refusals(unsigned *count)",
            "{",
            "    *count = %d;" % len(kinds),
            "    return refusals;",
            "}"]

    def matches(i):
        """The C condition that D is instance i (an element of it, for an array)."""
        if not i.array:
            return "d == 0x%04Xu" % i.d
        last = i.ds()[-1]
        rel = "(unsigned)(d - 0x%04Xu)" % i.base if i.base else "d"
        parts = ([] if i.base == 0 else ["d >= 0x%04Xu" % i.base]) + ["d <= 0x%04Xu" % last]
        if i.stride > 1 and i.count > 1:
            parts.append("%s %% 0x%02Xu == 0" % (rel, i.stride))
        return " && ".join(parts)

    def element(i):
        rel = "(unsigned)(d - 0x%04Xu)" % i.base if i.base else "(unsigned)d"
        return "%s / 0x%02Xu" % (rel, i.stride) if i.count > 1 else "0"

    used = {v.page.name for v in m.vars if v.on_instances()}
    for k, p in enumerate(kinds):
        shape, param = p.shape(), p.param()
        out += ["", "/* %s */" % p.name]
        if p.name not in used:
            pass   # no variable on it yet, so nothing needs its base
        elif shape == "array":
            i = p.instances[0]
            out += ["static uint32_t %s(unsigned %s) { return 0x7E%04Xu + (uint32_t)%s * 0x%02Xu; }"
                    % (page_base_name(p), param, i.base, param, i.stride)]
        else:
            out += ["static uint32_t %s(%s %s)" % (page_base_name(p), p.type_name(), param), "{",
                    "    switch (%s) {" % (param if shape == "named" else param + ".kind")]
            for i in p.instances:
                out.append("    case %s:" % p.constant(i))
                if i.array:
                    out.append("        return 0x7E%04Xu + (uint32_t)%s.index * 0x%02Xu;" % (i.base, param, i.stride))
                else:
                    out.append("        return 0x7E%04Xu;" % i.d)
            out += ["    }", "    return 0x7E%04Xu;   /* no instance: the first's */" % p.ds()[0], "}"]
        # which instance a D is
        out += ["", "%s" % of_signature(p), "{"]
        for i in p.instances:
            out.append("    if (%s) {" % matches(i))
            if shape == "array":
                out.append("        *%s = %s;" % (param, element(i)))
            elif shape == "named":
                out.append("        *%s = %s;" % (param, p.constant(i)))
            else:
                out.append("        *%s = (%s){ %s, %s };" % (
                    param, p.type_name(), p.constant(i),
                    "(uint8_t)(%s)" % element(i) if i.array else "0"))
            out += ["        return true;", "    }"]
        out += ["    return refuse(%d, d);" % k, "}"]
    return out


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
          "menu": {"menu_object", "overworld"}, "menu_object": {"menu", "overworld"},
          "overworld": {"menu", "menu_object"}}


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
    st = sub.add_parser("storage", help="native variables by image, blob or typed backing")
    st.add_argument("--list", action="store_true", help="list untyped variables and their blob owners")
    h = sub.add_parser("header", help="write game/ram.h")
    h.add_argument("--check", action="store_true", help="exit 1 if game/ram.h is out of date")
    a = sub.add_parser("accessors", help="write game/vars.h, runtime/port/vars_pages.h, vars_ram.c, vars_flat.c and vars_host.[ch]")
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

    if args.cmd == "storage":
        storage_report(m, details=args.list)
    elif args.cmd == "check":
        print("%s: %d pages, %d variables, %d images, not-RAM literals in %d files"
              % (args.map, len(m.pages), len(m.vars), len(m.images), len(m.not_ram)))
        shaped = m.address_shaped()
        if shaped:
            print("%d names shaped like addresses, waiting for a meaning: %s" % (len(shaped), ", ".join(shaped)))
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
        want = ((VARS_HEADER, render_vars_header(m)), (PAGES_HEADER, render_pages_header(m)),
                (BACKING, render_backing(m)), (HOST_HEADER, render_host_header(m)),
                (HOST_BACKING, render_host_backing(m)), (FLAT_BACKING, render_backing(m, flat=True)))
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
            base = max(b for b in v.bases() if b <= a)
            off = a - base
            elem = " element %d" % (off // v.stride) if v.count > 1 else ""
            if v.on_instances():
                elem += " (page %s, D = $%04X)" % (v.page.name, (base & 0xFFFF) - v.dp)
            print("%s: %s%s%s (%s) -- %s [%s]" % (fmt_addr(a), v.name, "." + f.name if f else "",
                                                 elem, v.subsystem, f.meaning if f else v.meaning, v.source))
    return 0


if __name__ == "__main__":
    sys.exit(main())
