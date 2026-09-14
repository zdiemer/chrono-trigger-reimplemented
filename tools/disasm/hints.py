"""Loads analysis hints (re/hints.toml): entry points, jump tables, and overrides
that static analysis can't infer. Widths are written as m/x = 8 or 16."""

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
    data: list = field(default_factory=list)           # (start, end) inclusive, never code

    def is_data(self, addr: int) -> bool:
        return any(s <= addr <= e for s, e in self.data)


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
                                             parse(t["table"]) if "table" in t else None)
    for r in doc.get("returns", []):
        h.returns[parse(r["addr"])] = (_width(r.get("m"), r["addr"]), _width(r.get("x"), r["addr"]))
    for s in doc.get("state", []):
        h.state_at[parse(s["addr"])] = (_width(s.get("m"), s["addr"]), _width(s.get("x"), s["addr"]))
    for a in doc.get("noreturn", {}).get("addrs", []):
        h.noreturn.add(parse(a))
    for i in doc.get("inline_args", []):
        h.inline_args[parse(i["addr"])] = i["size"]
    for i in doc.get("indirect", []):
        h.indirect[parse(i["at"])] = [parse(t) for t in i["targets"]]
    for d in doc.get("data", []):
        h.data.append((parse(d["start"]), parse(d["end"])))
    return h
