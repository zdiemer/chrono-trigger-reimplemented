"""Resource pointer tables and the regions their entries claim (MATCHING.md, MD2).

The loaders find bulk data through tables of 24-bit pointers indexed by a
resource id: the code multiplies the id by three, reads the low word and the
bank byte with two long indexed loads, and either stores the pointer where the
decompressor at `$C3:0557` takes its source (`$0300`) or keeps it as a data
pointer. A `[[resource]]` hint states one such table (its first entry, how many
entries, what the entries point at and which entries name nothing), with the
reading code in its comment; this module turns it into typed `[[data]]`
regions:

- the table itself, a `ptr24` region;
- for `target = "lz"`, one `lz` region per distinct entry, running exactly as
  far as `tools/disasm/lz.py` consumes. An entry that isn't a stream is a
  problem unless the hint's `skip` lists it, so a wrong count or a wrong
  target type is reported rather than silently accepted;
- for `target = "text"`, one region per distinct entry running to and including
  its zero terminator, with the bytes between one terminator and the next
  string reported (zero when the strings tile their range);
- for `target = "sized"`, one region per distinct entry whose first two bytes
  are its length, as the sound driver's blocks are;
- for `target = "blob"`, one region per distinct entry of the hint's `type`,
  `size` bytes long, or (`extent = "next"`) running to the next entry's target
  in the same bank, with `end` giving the last one's.

Regions that land inside a hint typed `mixed` or `undetermined`, or inside one
with `fill = true`, split it, since those mean "whatever else is here";
anything else that overlaps is a problem. Nothing here reads the ROM's contents
beyond decompressing and measuring, and the expansion is a pure function of the
ROM and the hints.
"""

import bisect
from dataclasses import dataclass, field

from .hints import DataHint, Hints
from .lz import decompress
from .memmap import bus_to_offset, fmt, offset_to_bus

TARGETS = {"lz", "blob", "text", "sized"}


@dataclass
class Resource:
    at: int                  # bus address of the first entry
    count: int               # entries walked
    name: str | None         # name of the table region (None: the emitter names it)
    target: str              # "lz" or "blob"
    type: str = "bytes"      # data type of a blob entry's region
    size: int | None = None  # blob: bytes per entry
    ends: tuple = ()         # blob, extent = "next": inclusive end of the last one in each bank
    extent: str | None = None
    skip: tuple = ()         # entry indices this hint claims no region for (comment says why)
    kind: str = "ptr24"      # "ptr24", or "ptr16" with `bank` supplying the bank byte
    bank: int = 0            # ptr16: the bank the reading code puts on the offset
    slots: int | None = None # entries the table region covers (default: count)

    @property
    def stride(self) -> int:
        return 2 if self.kind == "ptr16" else 3

    @property
    def span(self) -> int:
        return self.stride * (self.slots if self.slots is not None else self.count)


@dataclass
class Report:
    tables: int = 0
    entries: int = 0
    skipped: int = 0
    streams: int = 0            # distinct stream regions
    stream_bytes: int = 0
    stream_output: int = 0
    blobs: int = 0
    blob_bytes: int = 0
    strings: int = 0            # distinct zero-terminated strings
    string_bytes: int = 0
    string_gaps: int = 0        # bytes between a string's terminator and the next entry's target
    table_bytes: int = 0
    split: int = 0              # mixed/undetermined regions split by a derived one
    duplicate_entries: int = 0  # entries naming a target another entry already named
    problems: list = field(default_factory=list)

    def summary(self) -> str:
        return (f"{self.tables} resource tables, {self.entries} entries ({self.skipped} skipped, "
                f"{self.duplicate_entries} repeated): {self.streams} streams of {self.stream_bytes} bytes "
                f"holding {self.stream_output}, {self.blobs} blobs of {self.blob_bytes} bytes, "
                f"{self.strings} strings of {self.string_bytes} bytes ({self.string_gaps} bytes between them); "
                f"{self.split} mixed regions split, {len(self.problems)} problems")


def _pointer(data: bytes, off: int, r: Resource) -> int | None:
    if r.kind == "ptr16":
        return bus_to_offset(r.bank << 16 | data[off] | data[off + 1] << 8)
    return bus_to_offset(data[off] | data[off + 1] << 8 | data[off + 2] << 16)


def _splittable(region: DataHint) -> bool:
    """A region derived ones may be carved out of: "several kinds, not split yet",
    or a `fill = true` background type."""
    return region.type in ("mixed", "undetermined") or bool(region.params.get("fill"))


def expand(hints: Hints, data: bytes) -> Report:
    """Add the regions the `[[resource]]` hints claim to `hints`, once."""
    if hints.resource_report is not None:
        return hints.resource_report
    report = Report()
    hints.resource_report = report
    _carve_fill(hints, report)
    if not hints.resources:
        return report

    new: dict[int, DataHint] = {}            # start offset -> region
    pending: list[tuple[int, Resource]] = [] # blob targets whose end is the next target
    text_spans: dict[int, list] = {}         # table -> (start, terminator) of its strings

    def add(region: DataHint) -> None:
        o = bus_to_offset(region.start)
        old = new.get(o)
        if old is None:
            new[o] = region
        elif old.end != region.end or old.type != region.type:
            report.problems.append(
                f"{fmt(region.start)}: claimed as {region.type} to {fmt(region.end)} and as "
                f"{old.type} to {fmt(old.end)}")

    for r in sorted(hints.resources, key=lambda r: r.at):
        report.tables += 1
        base = bus_to_offset(r.at)
        if base is None or base + max(r.span, r.stride * r.count) > len(data):
            report.problems.append(f"resource {r.name or fmt(r.at)}: {fmt(r.at)} x{r.count} isn't in the ROM")
            continue
        report.table_bytes += r.span
        add(DataHint(r.at, r.at + r.span - 1, r.kind, r.name, {"count": r.count}))
        seen = set()
        for k in range(r.count):
            report.entries += 1
            if k in r.skip:
                report.skipped += 1
                continue
            t = _pointer(data, base + r.stride * k, r)
            if t is None:
                report.problems.append(f"resource {r.name or fmt(r.at)}[{k}]: not a ROM pointer")
                continue
            if t in seen:
                report.duplicate_entries += 1
                continue
            seen.add(t)
            start = offset_to_bus(t)
            if r.target == "lz":
                got = decompress(data, t)
                if got is None or not got[0]:
                    report.problems.append(f"resource {r.name or fmt(r.at)}[{k}] -> {fmt(start)}: "
                                           "not a stream the decompressor consumes")
                    continue
                out, used = got
                if (t + used - 1) >> 16 != t >> 16:
                    report.problems.append(f"resource {r.name or fmt(r.at)}[{k}] -> {fmt(start)}: "
                                           "the stream crosses a bank")
                    continue
                report.streams += 1
                report.stream_bytes += used
                report.stream_output += len(out)
                add(DataHint(start, start + used - 1, "lz", None, {"output": len(out)}))
            elif r.target == "text":
                z = data.find(b"\0", t, (t | 0xFFFF) + 1)
                if z < 0:
                    report.problems.append(
                        f"resource {r.name or fmt(r.at)}[{k}] -> {fmt(start)}: no terminator before the bank ends")
                    continue
                report.strings += 1
                report.string_bytes += z - t + 1
                text_spans.setdefault(id(r), []).append((t, z))
                add(DataHint(start, offset_to_bus(z), "text", None, {}))
            elif r.target == "sized":
                end = t + 1 + (data[t] | data[t + 1] << 8)
                if end >= len(data):
                    report.problems.append(f"resource {r.name or fmt(r.at)}[{k}] -> {fmt(start)}: "
                                           "its length runs past the ROM")
                    continue
                report.blobs += 1
                report.blob_bytes += end - t + 1
                # A block can run into the next bank; a region stays within one.
                a = t
                while a <= end:
                    b = min(end, a | 0xFFFF)
                    add(DataHint(offset_to_bus(a), offset_to_bus(b), r.type, None, {}))
                    a = b + 1
            elif r.size:
                if (t + r.size - 1) >> 16 != t >> 16:
                    report.problems.append(f"resource {r.name or fmt(r.at)}[{k}] -> {fmt(start)}: "
                                           "the blob crosses a bank")
                    continue
                report.blobs += 1
                report.blob_bytes += r.size
                add(DataHint(start, start + r.size - 1, r.type, None, {}))
            else:
                pending.append((t, r))

    # A string table is consumed exactly when every string's terminator is the
    # byte before the next one starts; what is left over is reported.
    for spans in text_spans.values():
        spans.sort()
        for (_, z), (nxt, _) in zip(spans, spans[1:]):
            report.string_gaps += max(0, nxt - z - 1)

    # Blobs sized by the next target of the same table in the same bank; the
    # hint's `ends` give the last one's in each bank the table reaches.
    if pending:
        per_table: dict[int, set] = {}
        for t, r in pending:
            per_table.setdefault(id(r), set()).add(t)
        per_table = {k: sorted(v) for k, v in per_table.items()}
        for t, r in pending:
            starts = per_table[id(r)]
            i = bisect.bisect_right(starts, t)
            stop = starts[i] if i < len(starts) and starts[i] >> 16 == t >> 16 else None
            if stop is None:
                last = [bus_to_offset(e) for e in r.ends if bus_to_offset(e) >> 16 == t >> 16]
                stop = last[0] + 1 if last else None
            if stop is None or stop <= t:
                report.problems.append(f"resource {r.name or fmt(r.at)} -> {fmt(offset_to_bus(t))}: "
                                       "no end for the blob (needs size or end)")
                continue
            report.blobs += 1
            report.blob_bytes += stop - t
            add(DataHint(offset_to_bus(t), offset_to_bus(stop - 1), r.type, None, {}))

    _merge(hints, new, report)
    return report


def _carve_fill(hints: Hints, report: Report) -> None:
    """Replace each `fill = true` region (a background type for a whole bank: "every
    byte here that no other region claims is this") by the pieces the other hand
    hints leave. Derived regions carve it further, in `_merge`."""
    fills = [r for r in hints.data_regions if r.params.get("fill")]
    if not fills:
        return
    others = sorted((r for r in hints.data_regions if not r.params.get("fill")),
                    key=lambda r: r.start)
    spans = [(bus_to_offset(r.start), bus_to_offset(r.end), r) for r in others]
    out = list(others)
    for f in fills:
        s, e = bus_to_offset(f.start), bus_to_offset(f.end)
        at = s
        first = True
        for o, end, other in spans:
            if end < at or o > e:
                continue
            if o < s or end > e:
                report.problems.append(
                    f"{fmt(other.start)}-{fmt(other.end)} ({other.type}) crosses the edge of the "
                    f"fill region {fmt(f.start)}-{fmt(f.end)}")
                continue
            if o > at:
                out.append(DataHint(offset_to_bus(at), offset_to_bus(o - 1), f.type,
                                    f.name if first else None, dict(f.params)))
                first = False
            at = max(at, end + 1)
        if at <= e:
            out.append(DataHint(offset_to_bus(at), offset_to_bus(e), f.type,
                                f.name if first else None, dict(f.params)))
    out.sort(key=lambda r: r.start)
    hints.data_regions = out
    hints.data = [(r.start, r.end) for r in out]
    hints._data_starts = []
    hints._data_ends = []


def _merge(hints: Hints, new: dict[int, DataHint], report: Report) -> None:
    """Put the derived regions into `hints.data_regions`, splitting `mixed` and
    `undetermined` regions around them and reporting every other clash."""
    existing = sorted(hints.data_regions, key=lambda r: r.start)
    spans = []                      # (start off, end off inclusive, region)
    for reg in existing:
        spans.append((bus_to_offset(reg.start), bus_to_offset(reg.end), reg))
    starts = [s for s, _, _ in spans]
    covered: dict[int, list] = {}   # index into spans -> derived regions inside it
    keep = []
    for o in sorted(new):
        reg = new[o]
        end = bus_to_offset(reg.end)
        i = bisect.bisect_right(starts, o) - 1
        hit = None
        for j in range(max(0, i - 8), min(len(spans), i + 9)):
            s, e, old = spans[j]
            if o <= e and end >= s:
                hit = (j, s, e, old)
                break
        if hit is None:
            keep.append(reg)
            continue
        j, s, e, old = hit
        if s == o and e == end:
            continue                # already declared by hand
        if _splittable(old) and s <= o and end <= e:
            covered.setdefault(j, []).append(reg)
            keep.append(reg)
            continue
        report.problems.append(
            f"{fmt(reg.start)}-{fmt(reg.end)} ({reg.type}) overlaps the "
            f"{old.type} region {fmt(old.start)}-{fmt(old.end)}")

    out = []
    for j, (s, e, old) in enumerate(spans):
        inside = sorted(covered.get(j, []), key=lambda r: r.start)
        if not inside:
            out.append(old)
            continue
        report.split += 1
        at = s
        first = True
        for reg in inside:
            o, end = bus_to_offset(reg.start), bus_to_offset(reg.end)
            if o > at:
                out.append(DataHint(offset_to_bus(at), offset_to_bus(o - 1), old.type,
                                    old.name if first else None, dict(old.params)))
                first = False
            at = end + 1
        if at <= e:
            out.append(DataHint(offset_to_bus(at), offset_to_bus(e), old.type,
                                old.name if first else None, dict(old.params)))
    out.extend(keep)
    out.sort(key=lambda r: r.start)
    hints.data_regions = out
    hints.data = [(r.start, r.end) for r in out]
    hints._data_starts = []
    hints._data_ends = []
