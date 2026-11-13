"""Draw the resources of a fonts or graphics text (frontend/fonts.h,
frontend/tiles.h) as sheets, a picture each, in a neutral palette and scaled
up without smoothing: a font's glyphs on a grid under their numbers in hex,
a glyph's width marked under it where the font has widths; a graphics
resource's tiles sixteen a row as the text has them, each row's first tile's
number beside it (the battlefield's pool, its tiles of two and three
planes, in eight greys).

From a maps text (frontend/maps.h), draw a map assembled: a location's map
as the field shows it, BG2 under BG1, each map tile the tile set's look (of
its first 256, or its second where the tile's properties say so for the
layer) and each look's entries the tile set's tiles in the location's
palette, flipped and in front as they say (the location's record, its tile
set's blocks and the pointers to them read from the pack beside the text,
the rest from the texts beside it, <pack>.graphics and <pack>.palettes); a
world map (location $1F0 on) the same from its two planes and their tile
groups; or a tilemap's entries drawn with a graphics resource's tiles, in
the neutral palette. tests/frontend/map_pictures.py compares what it draws
with the game's own pictures.

Made from the text, not the cartridge, so a sheet also shows what the text
decodes to. The texts and the sheets come from the user's cartridge: write
them outside the repository, never commit them.

    python3 tools/sheets.py <pack>.fonts OUT_DIR [--scale N]
    python3 tools/sheets.py <pack>.graphics OUT_DIR [--only NAME...] [--scale N]
    python3 tools/sheets.py <pack>.maps OUT_DIR --location N [--scale N]
    python3 tools/sheets.py <pack>.maps OUT_DIR --tilemap NAME --tiles NAME [--scale N]
"""
import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.assets import load as load_catalogue

PAPER, INK, WIDTH_MARK = (40, 44, 52), (255, 220, 120), (230, 80, 80)
CLEAR = (90, 100, 112)
COLUMNS, GAP, TILES_ACROSS, TILE = 16, 2, 16, 8
# A glyph's or a two-plane tile's four colors, and a four-plane tile's sixteen
# as a ramp from dark to light; color 0 is clear in both.
COLORS = {".": CLEAR, "1": (255, 255, 255), "2": (176, 176, 176), "3": (16, 16, 16)}
RAMP = {c: CLEAR if c == "." else (16 * i + 15,) * 3 for i, c in enumerate(".123456789ABCDEF")}
# The battlefield's pool's tiles of two or three planes: eight colors, dark to light.
RAMP8 = {c: CLEAR if c == "." else (36 * i + 3,) * 3 for i, c in enumerate(".1234567")}

# Hex digits three pixels wide and five tall, for the labels.
DIGITS = {
    "0": "111101101101111", "1": "010110010010111", "2": "111001111100111", "3": "111001111001111",
    "4": "101101111001001", "5": "111100111001111", "6": "111100111101111", "7": "111001010010010",
    "8": "111101111101111", "9": "111101111001111", "A": "010101111101101", "B": "110101110101110",
    "C": "011100100100011", "D": "110101101101110", "E": "111100111100111", "F": "111100111100100",
}


class Font:
    """A font: [(number, width or None, [pixel lines])]."""
    def __init__(self):
        self.glyphs = []


class Tiles:
    """A graphics resource: its bit planes (its text's comment says), and its
    rows of tiles, each eight lines of the row's tiles' pixels; and the line
    its pixels start at."""
    def __init__(self, line: int):
        self.planes, self.rows, self.lines, self.first_line = 2, [], [], line


PLANES = re.compile(r"#\s*\d+ tiles, (\d) bit planes")
POOL = re.compile(r"#\s*tiles of two or three bit planes")
# A resource's name, as the catalogue spells them (tools/assets.py): it names
# the sheet's file, so it can't climb out of the directory or into another.
NAME = re.compile(r"[a-z][a-z0-9_.]*")


def parse(text: str):
    """{name: Font or Tiles}, in the text's order."""
    out, name = {}, None
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        planes = PLANES.match(raw.strip())
        if planes and name:
            out[name] = out[name] or Tiles(n)
            out[name].planes = int(planes[1])
        if POOL.match(raw.strip()) and name:
            out[name] = out[name] or Tiles(n)
            out[name].planes = 3
        if not line:
            continue
        if line.startswith("["):
            name = line[1:-1]
            if not line.endswith("]") or not NAME.fullmatch(name) or ".." in name:
                raise SystemExit(f"line {n}: a name is [name], lower case letters, digits, '_' and '.'")
            out[name] = None
        elif name is None:
            raise SystemExit(f"line {n}: a line before any [name]")
        elif line.startswith("glyph"):
            if isinstance(out[name], Tiles) and out[name].rows + [out[name].lines] != [[]]:
                raise SystemExit(f"line {out[name].first_line}: a line of pixels before any glyph")
            font = out[name] = out[name] if isinstance(out[name], Font) else Font()
            words = line.split()
            width = int(words[3]) if len(words) >= 4 and words[2] == "width" else None
            font.glyphs.append((int(words[1], 16), width, []))
        elif line.startswith("packed") or line.startswith("rest"):
            continue
        elif isinstance(out[name], Font):
            out[name].glyphs[-1][2].append(line)
        else:
            tiles = out[name] = out[name] or Tiles(n)
            if not tiles.rows and not tiles.lines:
                tiles.first_line = n
            tiles.lines.append(line.split())
            if len(tiles.lines) == TILE:
                tiles.rows.append(tiles.lines)
                tiles.lines = []
    return {k: v for k, v in out.items() if v is not None}


def png(width: int, height: int, rows) -> bytes:
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def label(picture, x0, y0, number: int, digits: int):
    for d, ch in enumerate(f"{number:0{digits}X}"):
        for k, on in enumerate(DIGITS[ch]):
            if on == "1":
                picture[y0 + k // 3][x0 + 4 * d + k % 3] = INK


def font_sheet(glyphs):
    """A font's picture: rows of pixels."""
    size = max((len(lines) for _, _, lines in glyphs), default=8)
    under = 6                                   # a label's five lines and a gap
    cell_w, cell_h = max(size, 12) + GAP, under + size + 2 + GAP
    rows_of_cells = (len(glyphs) + COLUMNS - 1) // COLUMNS
    w, h = COLUMNS * cell_w + GAP, rows_of_cells * cell_h + GAP
    picture = [[PAPER] * w for _ in range(h)]
    for i, (number, width, lines) in enumerate(glyphs):
        x0, y0 = GAP + i % COLUMNS * cell_w, GAP + i // COLUMNS * cell_h
        label(picture, x0, y0, number, 2)
        for y, line in enumerate(lines):
            for x, ch in enumerate(line):
                picture[y0 + under + y][x0 + x] = COLORS.get(ch, (255, 0, 255))
        for x in range(min(width or 0, cell_w - GAP)):
            picture[y0 + under + size + 1][x0 + x] = WIDTH_MARK
    return picture


def tile_sheet(tiles: Tiles):
    """A graphics resource's picture: its tiles as the text lays them out,
    each row's first tile's number to its left."""
    colors = RAMP if tiles.planes == 4 else RAMP8 if tiles.planes == 3 else COLORS
    margin = 4 * 4 + GAP                        # four digits and a gap
    w, h = margin + TILES_ACROSS * TILE + GAP, len(tiles.rows) * (TILE + 1) + GAP
    picture = [[PAPER] * w for _ in range(h)]
    first = 0
    for r, row in enumerate(tiles.rows):
        y0 = GAP + r * (TILE + 1)
        label(picture, GAP, y0 + 1, first, 4)
        for y, line in enumerate(row):
            for k, tile in enumerate(line):
                for x, ch in enumerate(tile):
                    picture[y0 + y][margin + k * TILE + x] = colors.get(ch.upper(), (255, 0, 255))
        first += len(row[0])
    return picture


# ---- Maps assembled -----------------------------------------------------------------------------

PIXELS = ".123456789ABCDEF"
ENTRY = re.compile(r"([0-9A-Fa-f]{3}):([0-7]):([h-])([v-])([p-])")
FRONT, ACROSS, DOWN = 0x2000, 0x4000, 0x8000


def entry(cell: str, n: int) -> int:
    """A tilemap entry's word from its text, TTT:P:hvp."""
    m = ENTRY.fullmatch(cell)
    if not m or int(m[1], 16) > 0x3FF:
        raise SystemExit(f"line {n}: a tilemap entry is TTT:P:hvp, the tile 000-3FF")
    return (int(m[1], 16) | int(m[2]) << 10 | (FRONT if m[5] == "p" else 0) | (ACROSS if m[3] == "h" else 0)
            | (DOWN if m[4] == "v" else 0))


def parse_lines(text: str):
    """{name: [(line number, line)]}: each resource's lines, comments and the
    `packed` line dropped."""
    out, name = {}, None
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line or line.startswith("packed"):
            continue
        if line.startswith("["):
            name = line[1:-1]
            if not line.endswith("]") or not NAME.fullmatch(name) or ".." in name:
                raise SystemExit(f"line {n}: a name is [name], lower case letters, digits, '_' and '.'")
            out[name] = []
        elif name is None:
            raise SystemExit(f"line {n}: a line before any [name]")
        else:
            out[name].append((n, line))
    return out


def field_map(lines):
    """A location's map's layers, {layer: (columns, rows, cells)} for BG1
    and BG2, and its tiles' attributes, three bytes a BG1 tile."""
    sizes, cells, layer = {}, {"attributes": []}, None
    for _, line in lines:
        words = line.split()
        if words[0] in ("bg1", "bg2", "bg3") and len(words) > 1 and "x" in words[1]:
            sizes[words[0]] = tuple(int(v) for v in words[1].split("x"))
        elif words[0] == "map":
            layer = words[1]
            cells[layer] = []
        elif words[0] == "attributes":
            layer = "attributes"
        elif layer in ("bg1", "bg2"):
            cells[layer] += [int(c, 16) for c in words]
        elif layer == "attributes":
            cells[layer] += [bytes.fromhex(c) for c in words]
    return {k: sizes[k] + (cells.get(k, []),) for k in ("bg1", "bg2")}, cells["attributes"]


def entries(lines, looks: bool):
    """A tilemap's rows of entries; or a tile set's looks, four entries each
    (top left, top right, bottom left, bottom right)."""
    rows = [[entry(c, n) for c in line.split()] for n, line in lines if not line.startswith("rest")]
    if not looks:
        return rows
    return [(top[k], top[k + 1], bottom[k], bottom[k + 1])
            for top, bottom in zip(rows[0::2], rows[1::2]) for k in range(0, len(top), 2)]


def tile_pixels(tiles, k: int):
    """A graphics resource's tile k: eight lines of eight colors, or None."""
    r, i = divmod(k, TILES_ACROSS)
    if tiles is None or r >= len(tiles.rows) or i >= len(tiles.rows[r][0]):
        return None
    return [[PIXELS.index(c.upper()) for c in tiles.rows[r][y][i]] for y in range(TILE)]


def read_pack(path: Path):
    """{name: bytes} of an asset pack (frontend/assets.c): "CTASSETS", the
    format, the count, the catalogue's version and the ROM's CRC-32, then a
    directory entry a resource."""
    data = path.read_bytes()
    if data[:8] != b"CTASSETS":
        raise SystemExit(f"{path}: not an asset pack")
    count, at, out = struct.unpack_from("<H", data, 10)[0], 20, {}
    for _ in range(count):
        place, n, name_len = struct.unpack_from("<IIB", data, at)
        name = data[at + 9:at + 9 + name_len].decode()
        at += 9 + name_len
        at += 1 + data[at]
        out[name] = data[place:place + n]
    return out


def pointed_at(pack, table: str, index: int):
    """The name of the resource a table of long pointers names at `index`."""
    word = pack[table][3 * index:3 * index + 3]
    at = word[0] | word[1] << 8 | word[2] << 16
    return next((r.name for r in load_catalogue() if r.at == at), None)


def draw_entry(picture, x0, y0, word, tiles, colors, front):
    """An 8x8 tilemap entry at x0, y0 where it is in front or not as `front`
    says: its tile, flipped as it says, color 0 clear."""
    pixels = tiles(word & 0x3FF) if bool(word & FRONT) == front else None
    if pixels is None:
        return
    palette = colors(word >> 10 & 7)
    for y in range(TILE):
        row = pixels[TILE - 1 - y if word & DOWN else y]
        for x in range(TILE):
            c = row[TILE - 1 - x if word & ACROSS else x]
            if c:
                picture[y0 + y][x0 + x] = palette[c]


def neutral(planes: int):
    return [RAMP[c] for c in PIXELS] if planes == 4 else [COLORS[c] for c in ".123"]


# A tile's properties (its attributes' first byte) say which half of the
# tile set's 512 looks BG1's and BG2's map tile takes (game/field/streaming.c).
SECOND_HALF = {"bg1": 0x01, "bg2": 0x02}
LOOKS_HALF = 0x100
# BG1's and BG2's tiles in VRAM: the tile set's six blocks of 128 from tile $100.
FIRST_BLOCK_TILE, BLOCK_TILES, TILE_SET_BLOCKS_IN_VRAM, MAP_TILE = 0x100, 128, 6, 16


class FieldLocation:
    """A location's map as the field draws it, from the texts beside a pack:
    its record, its tile set's blocks and the pointers to them from the
    pack, the map, the looks, the tiles and the palette from the texts."""

    def __init__(self, text: Path, location: int):
        pack = read_pack(text.with_suffix(".assets"))
        maps = parse_lines(text.read_text(encoding="utf-8"))
        graphics = parse(text.with_suffix(".graphics").read_text(encoding="utf-8"))
        palettes = parse_lines(text.with_suffix(".palettes").read_text(encoding="utf-8"))["location_palettes"]
        record = pack["location_records"][14 * location:14 * location + 14]
        tile_set, palette, number = record[1], record[3], record[4] | record[5] << 8
        self.layers, self.attributes = field_map(maps[pointed_at(pack, "field_packed_map", number)])
        self.looks = entries(maps[pointed_at(pack, "tile_looks_address", tile_set)], True)
        self.blocks = [graphics.get(pointed_at(pack, "graphics_block_address", b)) if b != 0xFF else None
                       for b in pack["tile_set_blocks"][8 * tile_set:8 * tile_set + TILE_SET_BLOCKS_IN_VRAM]]
        words = [int(r) | int(g) << 5 | int(b) << 10 for _, line in palettes for r, g, b in [line.split()[:3]]]
        self.colors = words[palette * 7 * 15:(palette + 1) * 7 * 15]

    def look(self, layer: str, column: int, row: int):
        """The look a layer's map tile takes, its four entries, or None."""
        w, h, cells = self.layers[layer]
        column, row = column % w, row % h
        look = cells[row * w + column]
        bg1_w = self.layers["bg1"][0]
        at = row * bg1_w + column
        if column < bg1_w and at < len(self.attributes) and self.attributes[at][0] & SECOND_HALF[layer]:
            look |= LOOKS_HALF
        return self.looks[look] if look < len(self.looks) else None

    def tile(self, t: int):
        n, k = divmod(t - FIRST_BLOCK_TILE, BLOCK_TILES)
        return tile_pixels(self.blocks[n], k) if 0 <= n < len(self.blocks) else None

    def pixel(self, layer: str, x: int, y: int):
        """The layer's pixel at x, y of its map: (palette, color, in front),
        or None where it is clear."""
        look = self.look(layer, x // MAP_TILE, y // MAP_TILE)
        if look is None:
            return None
        word = look[(y % MAP_TILE) // TILE * 2 + (x % MAP_TILE) // TILE]
        pixels = self.tile(word & 0x3FF)
        if pixels is None:
            return None
        tx, ty = x % TILE, y % TILE
        c = pixels[TILE - 1 - ty if word & DOWN else ty][TILE - 1 - tx if word & ACROSS else tx]
        return (word >> 10 & 7, c, bool(word & FRONT)) if c else None

    def color(self, palette: int, c: int):
        """A BGR555 color of the background's palettes: 1-7 are the
        location's, color 0 of each the backdrop's (None)."""
        return self.colors[(palette - 1) * 15 + c - 1] if palette and c else None

    def picture(self, layers, width: int, height: int, color=None):
        """Rows of colors (None where every layer is clear) of the layers
        named, each with the map pixel its top left shows, BG2 under BG1 and
        each layer's tiles in front over the others' behind; a color is
        color(palette, color), the location's palettes' unless given."""
        color = color or self.color
        out = []
        for y in range(height):
            row = []
            for x in range(width):
                best, rank = None, -1
                for layer, (left, top) in layers:
                    px = self.pixel(layer, left + x, top + y)
                    if px is None:
                        continue
                    r = (2 if px[2] else 0) + (1 if layer == "bg1" else 0)
                    if r > rank:
                        best, rank = px, r
                row.append(None if best is None else color(best[0], best[1]))
            out.append(row)
        return out


# ---- The world maps -----------------------------------------------------------------------------

FIRST_WORLD_MAP = 0x1F0
# A world map's record: a byte naming a stream of each table, $80 and up none
# (game/overworld/setup.c): the map's seven blocks of tiles first, then
# the palette, the tile groups and the planes.
WORLD_RECORDS, WORLD_TILE_BLOCKS = 0xC6FD00, 7
RECORD_PALETTE, RECORD_TILE_GROUPS, RECORD_MAPS, NO_STREAM = 0x0A, 0x10, 0x11, 0x80
# A plane is 96 x 64 map tiles, a byte each, picking one of 256 tile groups;
# BG1 shows the land's plane and BG2 the one over it. Where each plane's
# groups and map are in work RAM (game/menu/menu_data.h's plane tables) says
# where they are in the streams unpacked there.
PLANE_COLUMNS, PLANE_ROWS, GROUPS_A_PLANE = 96, 64, 256
WORLD_GROUPS_AT, WORLD_MAPS_AT = 0x3000, 0x4000


def rom_bytes(pack, at: int, n: int) -> bytes:
    """n bytes of the cartridge at a bus address, from the resource of the
    pack's that holds them."""
    for r in load_catalogue():
        data = pack.get(r.name)
        if data is not None and r.at <= at and at + n <= r.at + len(data):
            return data[at - r.at:at - r.at + n]
    raise SystemExit(f"no resource of the pack holds ${at >> 16:02X}:{at & 0xFFFF:04X}")


class WorldMap:
    """A world map's two scrolling planes as the overworld draws them, from
    the texts beside a pack: its record and the plane tables from the pack;
    the planes, the tile groups, the tiles and the palette from the texts."""

    def __init__(self, text: Path, location: int):
        pack = read_pack(text.with_suffix(".assets"))
        maps = parse_lines(text.read_text(encoding="utf-8"))
        graphics = parse(text.with_suffix(".graphics").read_text(encoding="utf-8"))
        at = int.from_bytes(rom_bytes(pack, WORLD_RECORDS + 2 * (location - FIRST_WORLD_MAP), 2), "little")
        record = rom_bytes(pack, WORLD_RECORDS & 0xFF0000 | at, RECORD_MAPS + 1)

        def stream(table: str, field: int):
            n = record[field]
            return None if n & NO_STREAM else pointed_at(pack, table, n)

        self.blocks = [graphics.get(stream("overworld_map_graphics", b) or "") for b in range(WORLD_TILE_BLOCKS)]
        groups = entries(maps[stream("overworld_tile_groups", RECORD_TILE_GROUPS)], True)
        planes = [int(c, 16) for _, line in maps[stream("overworld_maps", RECORD_MAPS)] for c in line.split()]
        table = pack["menu_plane_groups"]
        word = [int.from_bytes(table[2 * k:2 * k + 2], "little") for k in range(6)]
        self.layers = {}
        for plane, layer in enumerate(("bg1", "bg2")):
            g0 = (word[plane] - WORLD_GROUPS_AT) // GROUP_BYTES
            m0 = word[3 + plane] - WORLD_MAPS_AT
            self.layers[layer] = (groups[g0:g0 + GROUPS_A_PLANE], planes[m0:m0 + PLANE_COLUMNS * PLANE_ROWS])
        colors = maps.get(stream("menu_scene_stream_c6fea0", RECORD_PALETTE) or "", [])
        self.colors = [int(c[0:2]) | int(c[3:5]) << 5 | int(c[6:8]) << 10 for _, line in colors for c in line.split()]

    def pixel(self, layer: str, x: int, y: int):
        """The plane's pixel at x, y of the map, round its edges: (palette,
        color, in front), or None where it is clear."""
        groups, cells = self.layers[layer]
        column, row = x // MAP_TILE % PLANE_COLUMNS, y // MAP_TILE % PLANE_ROWS
        word = groups[cells[row * PLANE_COLUMNS + column]][(y % MAP_TILE) // TILE * 2 + (x % MAP_TILE) // TILE]
        n, k = divmod(word & 0x3FF, BLOCK_TILES)
        pixels = tile_pixels(self.blocks[n], k) if n < len(self.blocks) else None
        if pixels is None:
            return None
        tx, ty = x % TILE, y % TILE
        c = pixels[TILE - 1 - ty if word & DOWN else ty][TILE - 1 - tx if word & ACROSS else tx]
        return (word >> 10 & 7, c, bool(word & FRONT)) if c else None

    def color(self, palette: int, c: int):
        at = 16 * palette + c
        return self.colors[at] if c and at < len(self.colors) else None

    picture = FieldLocation.picture


GROUP_BYTES = 8


def place_at(text: Path, location: int):
    """A location's map, or a world map's."""
    return WorldMap(text, location) if location >= FIRST_WORLD_MAP else FieldLocation(text, location)


def rgb(word):
    """A BGR555 color as the sheets show it, 0-255 each."""
    return tuple((word >> s & 31) * 255 // 31 for s in (0, 5, 10))


def location_sheet(text: Path, location: int):
    """A location's map: BG2 under BG1 over BG1's size (BG2 repeating where
    it is smaller), each layer's tiles in front over those behind."""
    place = place_at(text, location)
    if isinstance(place, WorldMap):
        columns, rows = PLANE_COLUMNS, PLANE_ROWS
    else:
        columns, rows, _ = place.layers["bg1"]
    ramp = neutral(4)   # palette 0 isn't the location's: drawn in greys
    picture = place.picture((("bg2", (0, 0)), ("bg1", (0, 0))), MAP_TILE * columns, MAP_TILE * rows,
                            lambda p, c: rgb(place.color(p, c)) if place.color(p, c) is not None else ramp[c])
    return [[PAPER if c is None else c for c in row] for row in picture]


def tilemap_sheet(text: Path, name: str, tiles_name: str):
    """A tilemap's entries drawn with a graphics resource's tiles from 0, in
    the neutral palette, those behind before those in front."""
    rows = entries(parse_lines(text.read_text(encoding="utf-8"))[name], False)
    tiles = parse(text.with_suffix(".graphics").read_text(encoding="utf-8"))[tiles_name]
    colors = neutral(tiles.planes)
    picture = [[PAPER] * (TILE * max(len(r) for r in rows)) for _ in range(TILE * len(rows))]
    for front in (False, True):
        for y, row in enumerate(rows):
            for x, word in enumerate(row):
                draw_entry(picture, TILE * x, TILE * y, word, lambda t: tile_pixels(tiles, t), lambda p: colors, front)
    return picture


def write_png(path: Path, picture, scale: int):
    rows = [[px for px in row for _ in range(scale)] for row in picture for _ in range(scale)]
    path.write_bytes(png(len(picture[0]) * scale, len(picture) * scale, rows))
    print(path)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("text", type=Path, help="a fonts or graphics text ct_native writes beside the asset pack")
    ap.add_argument("out", type=Path, help="the directory to write <name>.png into")
    ap.add_argument("--only", nargs="+", metavar="NAME", help="draw only these resources")
    ap.add_argument("--location", type=lambda v: int(v, 0), help="a maps text: draw this location's map")
    ap.add_argument("--tilemap", metavar="NAME", help="a maps text: draw this tilemap ...")
    ap.add_argument("--tiles", metavar="NAME", help="... with this graphics resource's tiles")
    ap.add_argument("--scale", type=int, default=4)
    args = ap.parse_args(argv)
    args.out.mkdir(parents=True, exist_ok=True)
    if args.location is not None:
        write_png(args.out / f"location_{args.location:03x}.png", location_sheet(args.text, args.location), args.scale)
        return 0
    if args.tilemap:
        if not NAME.fullmatch(args.tilemap) or ".." in args.tilemap or not args.tiles:
            raise SystemExit("--tilemap takes a resource's name, and --tiles a graphics resource's")
        write_png(args.out / f"{args.tilemap}.png", tilemap_sheet(args.text, args.tilemap, args.tiles), args.scale)
        return 0
    for name, resource in parse(args.text.read_text(encoding="utf-8")).items():
        if args.only and name not in args.only:
            continue
        picture = font_sheet(resource.glyphs) if isinstance(resource, Font) else tile_sheet(resource)
        write_png(args.out / f"{name}.png", picture, args.scale)
    return 0


if __name__ == "__main__":
    sys.exit(main())
