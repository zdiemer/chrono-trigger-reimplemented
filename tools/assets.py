"""Validate the native asset catalogue or regenerate its C table; no ROM required.

`crc ROM` records the CRC-32 of every resource's bytes in the catalogue's order,
read from the cartridge by its own file offsets: the reference an imported pack
is checked against (frontend/assets.c), made independently of the importer.
With the ROM staged, `check` holds the recorded value to the catalogue."""
import argparse
import binascii
import re
import sys
import tomllib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.cartridge import bus_to_offset, fmt, parse
TSV = ROOT / "re" / "assets.tsv"
C_OUT = ROOT / "game" / "data" / "asset_catalogue.c"
NAMES_OUT = ROOT / "game" / "data" / "asset_names.h"
VIEWS = ROOT / "re" / "asset_views.toml"
ROM = ROOT / "roms" / "ct-usa.sfc"

# The catalogue's line recording the CRC-32 of its resources' bytes.
BYTES_CRC = re.compile(r"^# bytes crc32 ([0-9A-F]{8})$", re.M)

# The types re/assets.tsv spells, MD2's and the analysis' jump tables, in the
# order game/data/assets.h's indices take them.
TYPES = ["bytes", "words", "ptr16", "ptr24", "struct", "mixed", "undetermined", "lz", "text", "script", "gfx",
         "spc700", "padding", "header", "jump-table", "untyped", "code"]

# A palette's codec: N colors a row, M rows a palette where the game takes them
# in groups (frontend/palettes.h writes and reads them as text).
PALETTE_CODEC = re.compile(r"palette:[1-9][0-9]*(x[1-9][0-9]*)?")

# A font's codec: 8x8 glyphs of two bitplanes, 16 bytes a glyph, or the
# proportional 12x12 ones, 24 bytes of each glyph's left eight pixels and then
# 24 a pair of their right four; and the widths of a font's glyphs from $XX on,
# a byte each (frontend/fonts.h writes and reads them as text).
FONT_BYTES = {"font:8x8": 16, "font:12x12": 36}
WIDTHS_CODEC = re.compile(r"font-widths:([a-z][a-z0-9_.]*):([0-9A-F]{2})")

# Tiles' codecs: 8x8 tiles of two or four bitplanes, 16 or 32 bytes a tile,
# and the same packed (frontend/tiles.h writes and reads them as text).
TILE_BYTES = {"tiles:2bpp": 16, "tiles:4bpp": 32}
# Other packed resources' codecs: a location's map, a tile set's looks, and
# grids of tilemap entries, bytes, hex digits or colors N a row, and streams
# without a text of their own yet (frontend/maps.h writes and reads them); a
# location's event script and a world map's lists of places
# (frontend/scripts.h); a world map's scene's object scripts (frontend/commands.h).
PACKED_CODEC = re.compile(r"lz(:event|:place-lists|:object|:2bpp|:4bpp|:8bpp|:4bpp-chunky|:field-map|:looks|:chunky"
                          r"|:(tilemap|bytes|colors):[1-9][0-9]*|:nibbles:([2468]|[1-9][0-9]*[02468]))?")

# Text's codecs: a string, names of N glyphs each, the dictionary
# (frontend/text.h writes and reads them as text).
TEXT_CODEC = re.compile(r"text:(dialog|dialog-dictionary|names:[1-9][0-9]*)")

# Tables of records: sprites' records of N bytes, the locations' exits
# (frontend/records.h writes and reads them as text).
STRUCT_CODEC = re.compile(r"struct:(sprite-records:(5|10)|records:[1-9][0-9]*|location-(exits|chests))")

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

def data_views():
    """Named asset views of bytes that remain executable in the matching ROM."""
    with VIEWS.open("rb") as f:
        return [Row(v["name"], v["type"], parse(v["at"]), parse(v["end"]), "raw", v["purpose"])
                for v in tomllib.load(f)["view"]]

# The types of a region not yet typed: each such row's `found` says `unread`,
# that no accessor names it and no movie's read lands in it; one that is read
# is typed from its readers.
UNTYPED = ("mixed", "untyped", "undetermined")


def problems_of(rows: list[Row]) -> list[str]:
    """What makes a catalogue unusable: names twice, rows overlapping or out of
    order, a range in no bank, a type game/data/assets.h doesn't know, a
    palette that isn't whole colors, a font that isn't whole glyphs, tiles
    that aren't whole tiles, a packed stream of a codec that isn't one, a
    region left untyped that something reads."""
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
        if r.codec.startswith("palette") and (not PALETTE_CODEC.fullmatch(r.codec) or r.type != "words"
                                              or (r.end - r.at + 1) % 2):
            out.append(f"{r.name}: a palette is words, an even number of bytes, its codec palette:N or palette:NxM")
        size = r.end - r.at + 1
        if r.codec.startswith("font:") and (r.codec not in FONT_BYTES or r.type != "gfx"
                                            or size % (FONT_BYTES[r.codec] * (2 if r.codec == "font:12x12" else 1))):
            out.append(f"{r.name}: a font is gfx, whole glyphs (pairs of them for font:12x12), "
                       f"its codec one of {', '.join(FONT_BYTES)}")
        if r.codec.startswith("font-widths"):
            m = WIDTHS_CODEC.fullmatch(r.codec)
            font = next((f for f in rows if m and f.name == m[1]), None)
            glyphs = font and font.codec in FONT_BYTES and (font.end - font.at + 1) // FONT_BYTES[font.codec]
            if not m or r.type != "bytes" or not glyphs or int(m[2], 16) + size > glyphs:
                out.append(f"{r.name}: widths are bytes, their codec font-widths:FONT:XX, a width for each "
                           f"of FONT's glyphs from $XX on and no more")
        if r.codec in TILE_BYTES and (r.type != "gfx" or size % TILE_BYTES[r.codec]):
            out.append(f"{r.name}: tiles are gfx, whole tiles of {TILE_BYTES[r.codec]} bytes")
        if r.codec == "tiles:plane-pool" and r.type != "gfx":
            out.append(f"{r.name}: a pool of tiles is gfx")
        if r.codec.startswith("struct") and (not STRUCT_CODEC.fullmatch(r.codec) or r.type != "struct"
                                             or (r.codec.startswith(("struct:sprite-records", "struct:records"))
                                                 and size % int(r.codec.split(":")[2]))):
            out.append(f"{r.name}: a table of records is struct, its codec struct:sprite-records:N or "
                       f"struct:records:N (whole records), struct:location-exits or struct:location-chests")
        if (r.codec.startswith("lz") or r.type == "lz") and (not PACKED_CODEC.fullmatch(r.codec) or r.type != "lz"):
            out.append(f"{r.name}: a packed stream is lz, its codec lz or lz: and its kind (frontend/maps.h)")
        if (r.codec.startswith("text") or r.type == "text") and (not TEXT_CODEC.fullmatch(r.codec) or r.type != "text"
                or (r.codec.startswith("text:names:") and size % int(r.codec.split(":")[2]))):
            out.append(f"{r.name}: text is text, its codec text:dialog, text:dialog-dictionary or text:names:N, "
                       f"whole names of N glyphs")
        if r.type in UNTYPED and not r.found.startswith("unread"):
            out.append(f"{r.name}: a {r.type} row is read ({r.found}): type it from its readers, or found by `unread`")
        if i and rows[i - 1].end >= r.at:
            out.append(f"{r.name}: starts at {fmt(r.at)}, inside or before {rows[i - 1].name}")
    return out

def recorded_bytes_crc() -> int:
    m = BYTES_CRC.search(TSV.read_text(encoding="utf-8"))
    if not m:
        raise SystemExit(f"{TSV}: no `# bytes crc32 XXXXXXXX` line: run tools/assets.py crc ROM")
    return int(m[1], 16)


def bytes_crc(rows: list[Row], rom: bytes) -> int:
    """The CRC-32 of the rows' bytes, one after another in the catalogue's order."""
    crc = 0
    for r in rows:
        at = bus_to_offset(r.at)
        crc = binascii.crc32(rom[at:at + r.end - r.at + 1], crc)
    return crc & 0xFFFFFFFF


def cartridge(path: Path) -> bytes:
    data = path.read_bytes()
    return data[len(data) % 0x8000:]   # a copier's header, if it has one


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
    out += ["};", "",
            f"const uint16_t asset_catalogue_count = {len(rows)};", "",
            f"const uint32_t asset_catalogue_version = 0x{catalogue_version(rows):08X}u;", "",
            f"const uint32_t asset_catalogue_bytes_crc32 = 0x{recorded_bytes_crc():08X}u;", ""]
    return "\n".join(out)



def named_rows(rows: list[Row]):
    """The rows an accessor names (game/data/asset_names.h): every one but a
    stream's entries, which their tables' pointers lead to."""
    return [(i, r) for i, r in enumerate(rows) if "." not in r.name]


def generate_names(rows: list[Row]) -> str:
    out = ["/* Generated by tools/assets.py from re/assets.tsv: the catalogue's resources by",
           " * name, for the accessors that read them (game/rom.h, asset_read8). Don't edit;",
           " * edit the catalogue and run tools/assets.py generate. */",
           "#ifndef CT_GAME_DATA_ASSET_NAMES_H", "#define CT_GAME_DATA_ASSET_NAMES_H", "", "enum {"]
    out += [f"    ASSET_{r.name.upper()} = {i}," for i, r in named_rows(rows)]
    out += ["};", "", "#endif", ""]
    return "\n".join(out)


def check() -> list[str]:
    rows = load()
    out = problems_of(rows)
    for view in data_views():
        if not any((r.name, r.at, r.end, r.type, r.codec) ==
                   (view.name, view.at, view.end, view.type, view.codec) for r in rows):
            out.append(f"{view.name}: declared data view is missing or stale in re/assets.tsv")
    if not C_OUT.exists() or C_OUT.read_text(encoding="utf-8") != generate_c(rows):
        out.append(f"{C_OUT.relative_to(ROOT)} isn't what re/assets.tsv generates: run tools/assets.py generate")
    if not NAMES_OUT.exists() or NAMES_OUT.read_text(encoding="utf-8") != generate_names(rows):
        out.append(f"{NAMES_OUT.relative_to(ROOT)} isn't what re/assets.tsv generates: run tools/assets.py generate")
    if ROM.exists() and bytes_crc(rows, cartridge(ROM)) != recorded_bytes_crc():
        out.append(f"{TSV.relative_to(ROOT)}: the recorded bytes crc32 isn't the catalogue's: run tools/assets.py crc ROM")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('command', choices=('check', 'generate', 'crc'))
    ap.add_argument('rom', nargs='?', type=Path, default=ROM)
    args = ap.parse_args()
    if args.command == 'crc':
        crc = bytes_crc(load(), cartridge(args.rom))
        text = TSV.read_text(encoding="utf-8")
        line = f"# bytes crc32 {crc:08X}"
        text = BYTES_CRC.sub(line, text) if BYTES_CRC.search(text) else text.replace("# name\t", line + "\n# name\t", 1)
        TSV.write_text(text, encoding="utf-8", newline="\n")
        print(line)
        return 0
    if args.command == 'generate':
        rows = load()
        errors = problems_of(rows)
        if errors:
            raise SystemExit('\n'.join(errors))
        C_OUT.write_text(generate_c(rows), encoding='utf-8', newline='\n')
        NAMES_OUT.write_text(generate_names(rows), encoding='utf-8', newline='\n')
    errors = check()
    for error in errors:
        print(error)
    print('assets: ' + ('FAILED' if errors else 'OK'))
    return int(bool(errors))

if __name__ == '__main__':
    sys.exit(main())
