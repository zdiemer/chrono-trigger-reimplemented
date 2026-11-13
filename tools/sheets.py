"""Draw the resources of a fonts or graphics text (frontend/fonts.h,
frontend/tiles.h) as sheets, a picture each, in a neutral palette and scaled
up without smoothing: a font's glyphs on a grid under their numbers in hex,
a glyph's width marked under it where the font has widths; a graphics
resource's tiles sixteen a row as the text has them, each row's first tile's
number beside it.

Made from the text, not the cartridge, so a sheet also shows what the text
decodes to. The texts and the sheets come from the user's cartridge: write
them outside the repository, never commit them.

    python3 tools/sheets.py <pack>.fonts OUT_DIR [--scale N]
    python3 tools/sheets.py <pack>.graphics OUT_DIR [--only NAME...] [--scale N]
"""
import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

PAPER, INK, WIDTH_MARK = (40, 44, 52), (255, 220, 120), (230, 80, 80)
CLEAR = (90, 100, 112)
COLUMNS, GAP, TILES_ACROSS, TILE = 16, 2, 16, 8
# A glyph's or a two-plane tile's four colors, and a four-plane tile's sixteen
# as a ramp from dark to light; color 0 is clear in both.
COLORS = {".": CLEAR, "1": (255, 255, 255), "2": (176, 176, 176), "3": (16, 16, 16)}
RAMP = {c: CLEAR if c == "." else (16 * i + 15,) * 3 for i, c in enumerate(".123456789ABCDEF")}

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
        elif line.startswith("packed"):
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
    colors = RAMP if tiles.planes == 4 else COLORS
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


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("text", type=Path, help="a fonts or graphics text ct_native writes beside the asset pack")
    ap.add_argument("out", type=Path, help="the directory to write <name>.png into")
    ap.add_argument("--only", nargs="+", metavar="NAME", help="draw only these resources")
    ap.add_argument("--scale", type=int, default=4)
    args = ap.parse_args(argv)
    args.out.mkdir(parents=True, exist_ok=True)
    for name, resource in parse(args.text.read_text(encoding="utf-8")).items():
        if args.only and name not in args.only:
            continue
        picture = font_sheet(resource.glyphs) if isinstance(resource, Font) else tile_sheet(resource)
        rows = [[px for px in row for _ in range(args.scale)] for row in picture for _ in range(args.scale)]
        path = args.out / f"{name}.png"
        path.write_bytes(png(len(picture[0]) * args.scale, len(picture) * args.scale, rows))
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
