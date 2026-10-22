"""The catalogue of resources: what the ports read from the cartridge, by name.

    python3 tools/assets.py build --rom roms/ct-usa.sfc [--reads reads.txt ...]
    python3 tools/assets.py check [--rom roms/ct-usa.sfc]

`re/assets.tsv` has a row for every resource the named accessors read (the
subsystems' `*_data.h` headers, `game/rom.h`): its name, its type (MD2's,
docs/MATCHING.md), the ROM range the original keeps it in, and the codec that
decodes it. It holds no ROM bytes, only those facts, so it is committed; the
asset pack (docs/PLATFORM.md, "The assets, as built") is made from it and the
user's ROM, and `game/data/asset_catalogue.c` is generated from it, so a build
without a ROM has the table.

A resource is a region the matching build extracts (docs/MATCHING.md, "Build-time
extraction"): a typed `[[data]]` region, the `[[resource]]` tables' targets
among them, or a jump table the analysis found. **build** chooses them:

- every region holding an address the accessors name (a `0xBBAAAA` constant in
  a `*_data.h` header), named after the accessor;
- every region holding an address a read log records (`ct_dev --asset-reads`,
  over the movies), which is how the accessors that follow a pointer the data
  holds are found;
- and, for a resource table any of these is part of, the table and every
  target it names, so a pointer read from it lands in the catalogue whether or
  not a movie followed it.

Rows already in the file keep their names. An address read that lies in no
extracted region is reported: it needs a `[[data]]` hint first (re/hints.toml).

**check** fails when `game/data/asset_catalogue.c` isn't what the file
generates (no ROM needed; tests/test_assets.py runs it), and, given the ROM,
when a row is no longer a region the extraction makes (the gate runs it).
"""

import argparse
import binascii
import bisect
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.disasm import hints as hints_mod  # noqa: E402
from tools.disasm.memmap import bus_to_offset, fmt, offset_to_bus, parse  # noqa: E402

TSV = ROOT / "re" / "assets.tsv"
C_OUT = ROOT / "game" / "data" / "asset_catalogue.c"
HEADERS = ["game/**/*_data.h", "game/data/packed_graphics.h"]

# The types re/assets.tsv spells, MD2's and the analysis' jump tables, in the
# order game/data/assets.h's indices take them.
TYPES = ["bytes", "words", "ptr16", "ptr24", "struct", "mixed", "undetermined", "lz", "text", "script", "gfx",
         "spc700", "padding", "header", "jump-table", "untyped", "code"]
HEADER = ("# The catalogue of resources: every one the ports' accessors read from the cartridge.\n"
          "# Written by tools/assets.py build, read by tools/assets.py check, the pack's import and\n"
          "# game/data/asset_catalogue.c (docs/PLATFORM.md, \"The assets, as built\"). Locations and types\n"
          "# only: no ROM bytes. `end` is inclusive; `codec` is what decodes it (`raw`: nothing).\n"
          "# name\ttype\tat\tend\tcodec\tfound by\n")


class Row:
    __slots__ = ("name", "type", "at", "end", "codec", "found")

    def __init__(self, name, type_, at, end, codec, found):
        self.name, self.type, self.at, self.end, self.codec, self.found = name, type_, at, end, codec, found

    def line(self) -> str:
        return "\t".join((self.name, self.type, fmt(self.at), fmt(self.end), self.codec, self.found))


def load(path: Path = TSV) -> list[Row]:
    rows = []
    if not path.exists():
        return rows
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        f = line.split("\t")
        if len(f) != 6:
            raise SystemExit(f"{path}:{n}: expected 6 tab-separated fields, got {len(f)}")
        rows.append(Row(f[0], f[1], parse(f[2]), parse(f[3]), f[4], f[5]))
    return rows


def problems_of(rows: list[Row]) -> list[str]:
    """What makes a catalogue unusable: names twice, rows overlapping or out of
    order, a range in no bank, a type game/data/assets.h doesn't know."""
    out, names = [], set()
    for i, r in enumerate(rows):
        if not re.fullmatch(r"[a-z][a-z0-9_.]*", r.name):
            out.append(f"{r.name}: a name is lower case letters, digits, '_' and '.'")
        if r.name in names:
            out.append(f"{r.name}: named twice")
        names.add(r.name)
        if r.type not in TYPES:
            out.append(f"{r.name}: type {r.type} isn't one of {', '.join(TYPES)}")
        if bus_to_offset(r.at) is None or r.end < r.at or (r.end >> 16) != (r.at >> 16):
            out.append(f"{r.name}: {fmt(r.at)}-{fmt(r.end)} isn't a range within one ROM bank")
        if i and rows[i - 1].end >= r.at:
            out.append(f"{r.name}: starts at {fmt(r.at)}, inside or before {rows[i - 1].name}")
    return out


# ---- what the matching build extracts ----------------------------------------------------------

def codec_of(region) -> str:
    form = region.params.get("format")
    if region.type == "lz":
        return f"lz:{form}" if form else "lz"
    return f"{region.type}:{form}" if form else "raw"


def extraction(rom: bytes):
    """The regions the matching build extracts: (start, end, type, codec, hint
    name), in address order, and the [[resource]] tables as (table start,
    [target starts])."""
    from tools.disasm import resources
    from tools.disasm.analyze import Analyzer
    from tools.disasm.memmap import Rom

    hints = hints_mod.load(ROOT / "re" / "hints.toml")
    an = Analyzer(Rom(rom), hints)
    an.run()
    regions = {}
    for r in hints.data_regions:
        if bus_to_offset(r.start) is None or r.type in (None, "ram_code"):
            continue
        regions[r.start] = (r.start, r.end, r.type, codec_of(r), r.name)
    for jt in an.tables.values():
        if jt.count and bus_to_offset(jt.table) is not None and jt.table not in regions:
            size = 3 if jt.kind == "long" else 2
            regions[jt.table] = (jt.table, jt.table + jt.count * size - 1, "jump-table", "raw", None)
    ordered = []
    for start in sorted(regions):
        reg = regions[start]
        if ordered and ordered[-1][1] >= start:
            continue                          # a jump table inside a typed region: the region holds it
        ordered.append(reg)
    tables = []
    for res in hints.resources:
        base = bus_to_offset(res.at)
        targets = []
        for k in range(res.count):
            if k in res.skip:
                continue
            t = resources._pointer(rom, base + res.stride * k, res)
            if t is not None:
                targets.append(offset_to_bus(t))
        tables.append((res.at, targets, res.name))
    # What isn't code or a region: the matching build extracts each run of it
    # as an untyped blob.
    taken = Taken(len(rom))
    for start, end, *_ in ordered:
        taken[bus_to_offset(start):bus_to_offset(end) + 1] = b"" * (end - start + 1)
    for pc, variants in an.code.items():
        o = bus_to_offset(pc)
        if o is not None:
            size = max(insn.size for insn in variants.values())
            taken[o:o + size] = b"" * len(taken[o:o + size])
            for k in range(size):
                taken.insn.setdefault(o + k, (o, size))
    return ordered, tables, taken


class Taken(bytearray):
    """A byte a ROM byte: 0 neither code nor a region, 1 a region, 2 code; and
    for each code byte, the instruction it is part of (offset, size)."""

    def __init__(self, n):
        super().__init__(n)
        self.insn = {}


def code_span(taken: Taken, at: int) -> tuple[int, int] | None:
    """The instruction a code byte at `at` is part of, as a range, or None."""
    o = bus_to_offset(at)
    if o is None or o not in taken.insn:
        return None
    start, size = taken.insn[o]
    return offset_to_bus(start), offset_to_bus(start + size - 1)


def untyped_span(taken: bytearray, at: int) -> tuple[int, int] | None:
    """The run of bytes neither code nor a region, within one bank, that `at`
    lies in, or None."""
    o = bus_to_offset(at)
    if o is None or taken[o]:
        return None
    lo, hi = o, o
    while lo & 0xFFFF and not taken[lo - 1]:
        lo -= 1
    while (hi + 1) & 0xFFFF and hi + 1 < len(taken) and not taken[hi + 1]:
        hi += 1
    return offset_to_bus(lo), offset_to_bus(hi)


# ---- what the accessors read ---------------------------------------------------------------------

FUNC = re.compile(r"^static\s+inline\s+[\w\s\*]+?\b(\w+)\s*\(", re.M)
ENUM_CONST = re.compile(r"\b([A-Z][A-Z0-9_]*)\s*=\s*0x([C-F][0-9A-Fa-f]{5})u?\b")
CONST = re.compile(r"0x([C-F][0-9A-Fa-f]{5})u?\b")
BANK_BASE = re.compile(r"\s*\|")
BANK_BASE_BEFORE = re.compile(r"(\|\s*\(?|_BANK\s*=)\s*$")


def accessor_constants() -> dict[int, str]:
    """Each ROM address a data header names, and the accessor (or constant) it
    is named in."""
    found = {}
    for pattern in HEADERS:
        for path in sorted(ROOT.glob(pattern)):
            text = re.sub(r"/\*.*?\*/|//[^\n]*", lambda m: " " * len(m.group(0)), path.read_text(encoding="utf-8"),
                          flags=re.S)
            funcs = [(m.start(), m.group(1)) for m in FUNC.finditer(text)]
            starts = [f[0] for f in funcs]
            for m in ENUM_CONST.finditer(text):
                if not m.group(1).endswith("_BANK"):
                    found.setdefault(int(m.group(2), 16), m.group(1).lower())
            for m in CONST.finditer(text):
                at = int(m.group(1), 16)
                if BANK_BASE.match(text, m.end()) or BANK_BASE_BEFORE.search(text, 0, m.start()):
                    continue                   # a bank, which a pointer the data holds goes in
                i = bisect.bisect_right(starts, m.start()) - 1
                if i >= 0:
                    found.setdefault(at, funcs[i][1])
    return found


def read_ranges(paths) -> list[tuple[int, int]]:
    """The ranges a read log (ct_dev --asset-reads) records: `BB:AAAA-BB:AAAA` a line."""
    out = []
    for p in paths:
        for line in Path(p).read_text().splitlines():
            line = line.split("#")[0].strip()
            if line:
                a, _, b = line.partition("-")
                out.append((parse(a), parse(b or a)))
    return out


def canonical(at: int) -> int | None:
    off = bus_to_offset(at)
    return None if off is None else offset_to_bus(off)


# ---- build ----------------------------------------------------------------------------------------

def build(rom: bytes, reads) -> tuple[list[Row], list[str]]:
    regions, tables, taken = extraction(rom)
    constants = accessor_constants()
    ranges = read_ranges(reads)
    # The untyped blobs the accessors read join the regions.
    blobs = {}
    read = {a for lo, hi in ranges for a in range(lo, hi + 1)}
    for at in set(constants) | read:
        span = untyped_span(taken, at)
        if span:
            blobs[span[0]] = (span[0], span[1], "untyped", "raw", None)
            continue
        # A reader that looks at a byte of code: the instruction it is part of.
        o = bus_to_offset(at)
        span = code_span(taken, at) if at in read and o is not None and taken[o] == 2 else None
        if span:
            blobs[span[0]] = (span[0], span[1], "code", "raw", None)
    regions = sorted(regions + list(blobs.values()))
    starts = [r[0] for r in regions]

    def region_of(at):
        i = bisect.bisect_right(starts, at) - 1
        return i if i >= 0 and regions[i][0] <= at <= regions[i][1] else None

    old = {r.at: r for r in load()}
    chosen: dict[int, str] = {}            # region index -> how it was found
    names: dict[int, str] = {}
    outside = []
    for at, accessor in sorted(constants.items()):
        i = region_of(at)
        if i is None:
            continue                       # a bank's base, an offset added to a pointer: the reads say
        chosen.setdefault(i, f"accessor {accessor}")
        names.setdefault(i, accessor)
    for a, b in ranges:
        for at in range(a, b + 1):
            c = canonical(at)
            i = region_of(c) if c is not None else None
            if i is None:
                if c is not None:          # code; work RAM a pointer names isn't the cartridge's
                    outside.append(at)
                continue
            chosen.setdefault(i, "read")
    # The resource tables any of these is part of, and everything they name.
    by_start = {r[0]: i for i, r in enumerate(regions)}
    table_names = set()
    for table, targets, tname in tables:
        members = [by_start[t] for t in [table] + targets if t in by_start]
        if not any(m in chosen for m in members):
            continue
        kinds = {regions[m][2] for m in members if regions[m][0] != table}
        kind = {"text": "strings", "lz": "streams"}.get(kinds.pop() if len(kinds) == 1 else "", "blobs")
        tname = tname or names.get(by_start.get(table)) or f"{kind}_{table:06x}"
        if tname in table_names:
            tname = f"{tname}_{table:06x}"
        table_names.add(tname)
        if by_start.get(table) is not None:
            names.setdefault(by_start[table], tname)
            chosen.setdefault(by_start[table], "resource table")
        for k, t in enumerate(targets):
            if t in by_start:
                chosen.setdefault(by_start[t], f"{tname} entry")
                names.setdefault(by_start[t], f"{tname}.{k}")
    rows, used = [], set()
    for i in sorted(chosen):
        start, end, type_, codec, hint_name = regions[i]
        prev = old.get(start)
        name = prev.name if prev else (hint_name or names.get(i) or f"{type_.replace('-', '_')}_{start:06x}")
        name = name.lower()
        if name in used:
            name = f"{name}_{start:06x}"
        used.add(name)
        rows.append(Row(name, type_, start, end, codec, chosen[i]))
    report = []
    if outside:
        runs, s = [], outside[0]
        for p, q in zip(outside, outside[1:] + [None]):
            if q != p + 1:
                runs.append((s, p))
                s = q
        report = [f"{fmt(a)}-{fmt(b)}" for a, b in runs]
    return rows, report


# ---- the C table ---------------------------------------------------------------------------------

def catalogue_version(rows: list[Row]) -> int:
    return binascii.crc32("\n".join(r.line().rsplit("\t", 1)[0] for r in rows).encode()) & 0xFFFFFFFF


def generate_c(rows: list[Row]) -> str:
    codecs = sorted({r.codec for r in rows} | {"raw"})
    out = ["/* Generated by tools/assets.py from re/assets.tsv: the catalogue of resources",
           " * (game/data/assets.h). Don't edit; edit the catalogue and run tools/assets.py build. */",
           '#include "data/assets.h"', "",
           "const char *const asset_type_names[] = {",
           "    " + ", ".join(f'"{t}"' for t in TYPES) + ",", "};", "",
           "const char *const asset_codec_names[] = {"]
    out += [f'    "{c}",' for c in codecs]
    out += ["};", "", "const AssetEntry asset_catalogue[] = {"]
    for r in rows:
        out.append(f'    {{ 0x{r.at:06X}u, {r.end - r.at + 1}u, "{r.name}", {TYPES.index(r.type)}, '
                   f'{codecs.index(r.codec)} }},')
    out += ["};", "", f"const uint16_t asset_catalogue_count = {len(rows)};", "",
            f"const uint32_t asset_catalogue_version = 0x{catalogue_version(rows):08X}u;", ""]
    return "\n".join(out)


def write(rows: list[Row]) -> None:
    TSV.write_text(HEADER + "".join(r.line() + "\n" for r in rows), encoding="utf-8", newline="\n")
    C_OUT.write_text(generate_c(rows), encoding="utf-8", newline="\n")


def read_rom(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) % 0x8000 == 0x200:
        data = data[0x200:]
    if binascii.crc32(data) & 0xFFFFFFFF != 0x2D206BF7:
        raise SystemExit(f"{path}: not Chrono Trigger (USA), CRC32 2D206BF7")
    return data


def check(rom_path: Path | None) -> list[str]:
    rows = load()
    out = problems_of(rows)
    if not C_OUT.exists() or C_OUT.read_text(encoding="utf-8") != generate_c(rows):
        out.append(f"{C_OUT.relative_to(ROOT)} isn't what re/assets.tsv generates: run tools/assets.py build")
    if rom_path is not None:
        regions, _, taken = extraction(read_rom(rom_path))
        by_start = {r[0]: r for r in regions}
        for r in rows:
            reg = by_start.get(r.at)
            if r.type in ("untyped", "code"):
                span = (untyped_span if r.type == "untyped" else code_span)(taken, r.at)
                reg = (span[0], span[1], r.type, "raw", None) if span else None
            if reg is None or reg[1] != r.end or reg[2] != r.type or reg[3] != r.codec:
                got = f"{fmt(reg[0])}-{fmt(reg[1])} {reg[2]} {reg[3]}" if reg else "nothing"
                out.append(f"{r.name}: {fmt(r.at)}-{fmt(r.end)} {r.type} {r.codec}, but the extraction has {got}")
    return out


def main():
    ap = argparse.ArgumentParser(prog="python3 tools/assets.py")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help="choose the resources and write re/assets.tsv and the C table")
    b.add_argument("--rom", type=Path, required=True)
    b.add_argument("--reads", nargs="*", default=[], help="read logs from ct_dev --asset-reads")
    c = sub.add_parser("check", help="the C table is up to date and, with --rom, every row a region")
    c.add_argument("--rom", type=Path)
    args = ap.parse_args()
    if args.cmd == "build":
        rows, outside = build(read_rom(args.rom), args.reads)
        bad = problems_of(rows)
        if bad:
            raise SystemExit("assets: " + "\n  ".join(bad))
        write(rows)
        size = sum(r.end - r.at + 1 for r in rows)
        print(f"assets: {len(rows)} resources, {size} bytes; {len(outside)} read ranges in no extracted region")
        for o in outside:
            print(f"  outside: {o}")
        return 1 if outside else 0
    bad = check(args.rom)
    for p in bad:
        print(f"assets: {p}")
    print("assets: " + ("FAILED" if bad else f"OK, {len(load())} resources"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
