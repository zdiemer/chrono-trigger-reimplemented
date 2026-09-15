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
    indirect: dict = field(default_factory=dict)       # JMP (abs)/JML [abs] site -> targets
    derive: dict = field(default_factory=dict)         # indirect or table site -> "deriver.part" (derive.py)
    header_vectors: bool = False                       # enter the NMI, IRQ and reset vectors of the header
    data: list = field(default_factory=list)           # (start, end) inclusive, never code
    data_regions: list = field(default_factory=list)   # DataHint, sorted by start
    overlaps: dict = field(default_factory=dict)       # addr -> (m, x) whose decode the source writes
    ramcode: list = field(default_factory=list)        # RamCodeHint

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
    for i in doc.get("indirect", []):
        if "targets" not in i and "derive" not in i:
            raise ValueError(f"indirect {i['at']}: needs targets or derive")
        h.indirect[parse(i["at"])] = [parse(t) for t in i.get("targets", [])]
        if "derive" in i:
            h.derive[parse(i["at"])] = i["derive"]
    for t in doc.get("jumptable", []):
        if "derive" in t:
            h.derive[parse(t["at"])] = t["derive"]
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
