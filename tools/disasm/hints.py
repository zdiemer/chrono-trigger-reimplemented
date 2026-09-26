"""Loads analysis hints (re/hints.toml): entry points, jump tables, and overrides
that static analysis can't infer. Widths are written as m/x = 8 or 16."""

import bisect
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

from .memmap import parse


@dataclass
class Entry:
    addr: int
    m: bool | None
    x: bool | None
    kind: str


@dataclass
class TableHint:
    count: int
    kind: str             # "abs" | "long"
    table: int | None     # overrides the operand, e.g. for (abs,X) with a biased index
    targets: list = field(default_factory=list)   # for tables filled at run time: the targets


# Types a [[data]] hint can give its region (docs/DISASSEMBLY.md).
DATA_TYPES = {
    "padding",      # one fill value
    "unused",       # bytes nothing runs or reads; the comment says why
    "bytes",        # a table of bytes a reader indexes
    "words",        # 16-bit values
    "ptr16",        # 16-bit addresses
    "ptr24",        # 24-bit addresses
    "struct",       # fixed-size records (size = record size)
    "lz",           # a stream in the format of the decompressor at $C3:0557
    "brr",          # BRR sample data
    "spc700",       # code and data uploaded to the sound CPU
    "gfx",          # graphics tiles
    "palette",      # colours
    "tilemap",      # tile maps
    "text",         # strings
    "script",       # interpreted command data (format = which interpreter)
    "mixed",        # several kinds, not split yet; the comment says what is known
    "undetermined", # not code, kind not known yet; the comment says why
    "header",       # the cartridge header and vectors
    "ram_code",     # bytes copied into RAM and run there (a [[ramcode]] section)
}


@dataclass
class DataHint:
    start: int
    end: int              # inclusive
    type: str | None      # None: untyped (only "never decode as code")
    name: str | None = None
    params: dict = field(default_factory=dict)


@dataclass
class RamCodeHint:
    name: str
    base: int             # run address
    source: int           # ROM address of the stream or the copied bytes
    format: str           # "lz", "copy", "writes" or "run"
    size: int | None      # bytes copied or written; lz: the part of the output the section covers
    end: int | None = None        # writes: last instruction of the storing code
    m: bool | None = None         # writes: widths at source
    x: bool | None = None
    over: str | None = None       # the section this one is written over at run time
    regs: dict | None = None      # run: registers the routine is called with (a, x, y, db, d)
    patch: str | None = None      # the section whose code this one patches for a while


@dataclass
class Hints:
    entries: list = field(default_factory=list)
    names: dict = field(default_factory=dict)          # addr -> name
    tables: dict = field(default_factory=dict)         # dispatcher addr -> TableHint
    returns: dict = field(default_factory=dict)        # function -> (m, x) after return
    state_at: dict = field(default_factory=dict)       # addr -> (m, x) forced
    noreturn: set = field(default_factory=set)
    inline_args: dict = field(default_factory=dict)    # function -> bytes skipped after JSR
    keep_resumes: dict = field(default_factory=dict)   # a `keeps` function -> where its caller's turn resumes
    keeps: dict = field(default_factory=dict)          # function -> (kept, read): register names it
                                                       # returns as its caller had them, and reads
    indirect: dict = field(default_factory=dict)       # JMP (abs)/JML [abs] site -> targets
    derive: dict = field(default_factory=dict)         # indirect or table site -> "deriver.part" (derive.py)
    header_vectors: bool = False                       # enter the NMI, IRQ and reset vectors of the header
    bounds: dict = field(default_factory=dict)         # indirect or table site -> argument kind (BOUNDS)
    # table site -> the entries its index can reach whatever the data, argued
    # from the dispatch (tools/v_readers.py); only a `derive` table has one
    index_reach: dict = field(default_factory=dict)
    nofollow: set = field(default_factory=set)         # indirect sites whose targets are entries, not traced
    data: list = field(default_factory=list)           # (start, end) inclusive, never code
    data_regions: list = field(default_factory=list)   # DataHint, sorted by start
    overlaps: dict = field(default_factory=dict)       # addr -> (m, x) whose decode the source writes
    ramcode: list = field(default_factory=list)        # RamCodeHint
    resources: list = field(default_factory=list)      # resources.Resource (expanded into data_regions)
    resource_report: object = None                     # resources.Report once expanded

    def is_data(self, addr: int) -> bool:
        if len(self._data_starts) != len(self.data):
            self._index_data()
        i = bisect.bisect_right(self._data_starts, addr) - 1
        return i >= 0 and addr <= self._data_ends[i]

    _data_starts: list = field(default_factory=list, repr=False)
    _data_ends: list = field(default_factory=list, repr=False)

    def _index_data(self):
        spans = sorted(self.data)
        self._data_starts = [s for s, _ in spans]
        ends = []
        top = -1
        for _, e in spans:                # running maximum, so nested or overlapping spans work
            top = max(top, e)
            ends.append(top)
        self._data_ends = ends


def _width(v, where: str) -> bool | None:
    if v is None:
        return None
    if v not in (8, 16):
        raise ValueError(f"{where}: width must be 8 or 16, got {v!r}")
    return v == 8


# How an indirect site's target set is argued (docs/MATCHING.md, C1): "guard"
# (code bounds the index before the dispatch), "layout" (the table ends where
# code or another table begins; the index isn't checked), "constant" (the
# targets are the constants every writer of the cell loads). A `derive` hint is
# argued by its data; a site with neither has no argument.
BOUNDS = {"guard", "layout", "constant"}


def _indices(spec) -> list:
    """Entry indices from a list of numbers and "first-last" ranges."""
    out = []
    for v in spec:
        if isinstance(v, int):
            out.append(v)
            continue
        a, _, b = str(v).partition("-")
        if not b:
            raise ValueError(f"bad entry index {v!r}; expected a number or \"first-last\"")
        out.extend(range(int(a), int(b) + 1))
    return out


def _bound(d: dict) -> str:
    if d["bound"] not in BOUNDS:
        raise ValueError(f"{d['at']}: bound must be one of {sorted(BOUNDS)}")
    return d["bound"]


def load(path: Path) -> Hints:
    h = Hints()
    if not path.exists():
        return h
    doc = tomllib.loads(path.read_text(encoding="utf-8"))
    h.header_vectors = bool(doc.get("header_vectors", False))

    for e in doc.get("entry", []):
        addr = parse(e["addr"])
        h.entries.append(Entry(addr, _width(e.get("m"), e["addr"]), _width(e.get("x"), e["addr"]),
                               e.get("kind", "hint")))
        if "name" in e:
            h.names[addr] = e["name"]
    for n in doc.get("name", []):
        h.names[parse(n["addr"])] = n["name"]
    for t in doc.get("jumptable", []):
        h.tables[parse(t["at"])] = TableHint(t["count"], t.get("kind", "abs"),
                                             parse(t["table"]) if "table" in t else None,
                                             [parse(x) for x in t.get("targets", [])])
    for r in doc.get("returns", []):
        h.returns[parse(r["addr"])] = (_width(r.get("m"), r["addr"]), _width(r.get("x"), r["addr"]))
    for s in doc.get("state", []):
        h.state_at[parse(s["addr"])] = (_width(s.get("m"), s["addr"]), _width(s.get("x"), s["addr"]))
    noreturn = doc.get("noreturn", {})
    for a in noreturn.get("addrs", []) if isinstance(noreturn, dict) else []:
        h.noreturn.add(parse(a))
    for n in noreturn if isinstance(noreturn, list) else []:     # [[noreturn]] addr, one per entry
        h.noreturn.add(parse(n["addr"]))
    for i in doc.get("inline_args", []):
        h.inline_args[parse(i["addr"])] = i["size"]
    for k in doc.get("keeps", []):
        h.keeps[parse(k["addr"])] = (tuple(k["regs"]), tuple(k.get("reads", ())))
        if "resume" in k:
            h.keep_resumes[parse(k["addr"])] = parse(k["resume"])
    for i in doc.get("indirect", []):
        if "targets" not in i and "derive" not in i:
            raise ValueError(f"indirect {i['at']}: needs targets or derive")
        h.indirect[parse(i["at"])] = [parse(t) for t in i.get("targets", [])]
        if "derive" in i:
            h.derive[parse(i["at"])] = i["derive"]
        if "bound" in i:
            h.bounds[parse(i["at"])] = _bound(i)
        if i.get("follow", True) is False:
            h.nofollow.add(parse(i["at"]))
    for t in doc.get("jumptable", []):
        if "derive" in t:
            h.derive[parse(t["at"])] = t["derive"]
        if "bound" in t:
            h.bounds[parse(t["at"])] = _bound(t)
        if "index_reach" in t:
            if "derive" not in t:
                raise ValueError(f"jumptable {t['at']}: index_reach is for a table derived from data")
            h.index_reach[parse(t["at"])] = t["index_reach"]
    for d in doc.get("data", []):
        start, end = parse(d["start"]), parse(d["end"])
        if end < start or start >> 16 != end >> 16:
            raise ValueError(f"data {d['start']}-{d['end']}: end must follow start in the same bank")
        kind = d.get("type")
        if kind is not None and kind not in DATA_TYPES:
            raise ValueError(f"data {d['start']}: unknown type {kind!r}")
        params = {k: v for k, v in d.items() if k not in ("start", "end", "type", "name")}
        h.data.append((start, end))
        h.data_regions.append(DataHint(start, end, kind, d.get("name"), params))
    h.data_regions.sort(key=lambda r: r.start)
    for r in doc.get("resource", []):
        from .resources import TARGETS, Resource
        target = r.get("target", "lz")
        if target not in TARGETS:
            raise ValueError(f"resource {r['at']}: target must be one of {sorted(TARGETS)}")
        kind = r.get("type", "bytes")
        if kind not in DATA_TYPES:
            raise ValueError(f"resource {r['at']}: unknown type {kind!r}")
        if target == "blob" and "size" not in r and r.get("extent") != "next":
            raise ValueError(f"resource {r['at']}: blob targets need a size or extent = \"next\"")
        entry = r.get("kind", "ptr24")
        if entry not in ("ptr16", "ptr24"):
            raise ValueError(f"resource {r['at']}: kind must be ptr16 or ptr24")
        if "format" in r:
            from .eventscript import LZ_FORMATS
            from .text import FORMATS
            if not ((target == "text" and r["format"] in FORMATS) or (target == "lz" and r["format"] in LZ_FORMATS)):
                raise ValueError(f"resource {r['at']}: format is for text targets, one of {FORMATS}, "
                                 f"or lz targets, one of {LZ_FORMATS}")
        if entry == "ptr16" and "bank" not in r:
            raise ValueError(f"resource {r['at']}: ptr16 entries need the bank the code supplies")
        ends = r.get("end", [])
        ends = [ends] if isinstance(ends, str) else list(ends)
        h.resources.append(Resource(parse(r["at"]), r["count"], r.get("name"), target, kind,
                                    r.get("size"), tuple(parse(e) for e in ends),
                                    r.get("extent"), tuple(_indices(r.get("skip", []))),
                                    entry, int(r.get("bank", 0), 16) if isinstance(r.get("bank"), str)
                                    else r.get("bank", 0), r.get("slots"), r.get("format")))
    for r in doc.get("ramcode", []):
        fmt_ = r.get("format", "lz")
        if fmt_ not in ("lz", "copy", "writes", "run"):
            raise ValueError(f"ramcode {r.get('name')}: format must be lz, copy, writes or run")
        if fmt_ in ("copy", "writes") and "size" not in r:
            raise ValueError(f"ramcode {r.get('name')}: {fmt_} needs a size")
        if fmt_ == "writes" and ("end" not in r or "m" not in r or "x" not in r):
            raise ValueError(f"ramcode {r.get('name')}: writes needs end, m and x")
        if fmt_ == "run" and ("m" not in r or "x" not in r):
            raise ValueError(f"ramcode {r.get('name')}: run needs m and x")
        h.ramcode.append(RamCodeHint(r["name"], parse(r["base"]), parse(r["source"]), fmt_, r.get("size"),
                                     parse(r["end"]) if "end" in r else None,
                                     _width(r.get("m"), r["name"]), _width(r.get("x"), r["name"]),
                                     r.get("over"), r.get("regs"), r.get("patch")))
    for o in doc.get("overlap", []):
        h.overlaps[parse(o["addr"])] = (_width(o.get("m"), o["addr"]), _width(o.get("x"), o["addr"]))
    return h
