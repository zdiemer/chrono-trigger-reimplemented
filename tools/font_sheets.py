"""Draw each font of a fonts text (frontend/fonts.h) as a sheet: every glyph on a
grid under its number in hex, a glyph's width marked under it where the font
has widths, in a neutral palette and scaled up without smoothing.

Made from the text, not the cartridge, so a sheet also shows what the text
decodes to. The text and the sheets come from the user's cartridge: write them
outside the repository, never commit them.

    python3 tools/font_sheets.py <pack>.fonts OUT_DIR [--scale N]
"""
import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

COLORS = {".": (90, 100, 112), "1": (255, 255, 255), "2": (176, 176, 176), "3": (16, 16, 16)}
PAPER, INK, WIDTH_MARK = (40, 44, 52), (255, 220, 120), (230, 80, 80)
COLUMNS, GAP = 16, 2

# A font name comes from a text's `[...]` header and becomes a file name, so it
# follows the asset catalogue's name pattern (tools/assets.py), and it may not
# contain ".." or a separator: the pattern allows dots, and `[../x]` would write
# outside OUT_DIR.
FONT_NAME = re.compile(r"[a-z][a-z0-9_.]*")

# Hex digits three pixels wide and five tall, for the labels.
DIGITS = {
    "0": "111101101101111", "1": "010110010010111", "2": "111001111100111", "3": "111001111001111",
    "4": "101101111001001", "5": "111100111001111", "6": "111100111101111", "7": "111001010010010",
    "8": "111101111101111", "9": "111101111001111", "A": "010101111101101", "B": "110101110101110",
    "C": "011100100100011", "D": "110101101101110", "E": "111100111100111", "F": "111100111100100",
}


def parse(text: str):
    """{font name: [(number, width or None, [pixel lines])]}, in the text's order."""
    fonts, glyphs, current = {}, None, None
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("["):
            name = line[1:-1]
            if not FONT_NAME.fullmatch(name) or ".." in name:
                raise SystemExit(f"line {n}: {name!r} isn't a font name: lower case letters, "
                                 "digits, '_' and '.', no '..'")
            glyphs = fonts.setdefault(name, [])
        elif line.startswith("glyph"):
            if glyphs is None:
                raise SystemExit(f"line {n}: glyph before any [font]")
            words = line.split()
            width = int(words[3]) if len(words) >= 4 and words[2] == "width" else None
            current = (int(words[1], 16), width, [])
            glyphs.append(current)
        elif current is None:
            raise SystemExit(f"line {n}: pixels before any glyph")
        else:
            current[2].append(line)
    return fonts


def png(width: int, height: int, rows) -> bytes:
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def sheet(glyphs, scale: int):
    """The sheet's width, height and rows of pixels."""
    size = max((len(lines) for _, _, lines in glyphs), default=8)
    label = 6                                   # a label's five lines and a gap
    cell_w, cell_h = max(size, 12) + GAP, label + size + 2 + GAP
    rows_of_cells = (len(glyphs) + COLUMNS - 1) // COLUMNS
    w, h = COLUMNS * cell_w + GAP, rows_of_cells * cell_h + GAP
    picture = [[PAPER] * w for _ in range(h)]
    for i, (number, width, lines) in enumerate(glyphs):
        x0, y0 = GAP + i % COLUMNS * cell_w, GAP + i // COLUMNS * cell_h
        for d, ch in enumerate(f"{number:02X}"):
            for k, on in enumerate(DIGITS[ch]):
                if on == "1":
                    picture[y0 + k // 3][x0 + 4 * d + k % 3] = INK
        for y, line in enumerate(lines):
            for x, ch in enumerate(line):
                picture[y0 + label + y][x0 + x] = COLORS.get(ch, (255, 0, 255))
        for x in range(min(width or 0, cell_w - GAP)):
            picture[y0 + label + size + 1][x0 + x] = WIDTH_MARK
    rows = [[px for px in row for _ in range(scale)] for row in picture for _ in range(scale)]
    return w * scale, h * scale, rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fonts", type=Path, help="the fonts text ct_native writes beside the asset pack")
    ap.add_argument("out", type=Path, help="the directory to write <font>.png into")
    ap.add_argument("--scale", type=int, default=4)
    args = ap.parse_args(argv)
    fonts = parse(args.fonts.read_text(encoding="utf-8"))
    args.out.mkdir(parents=True, exist_ok=True)
    for name, glyphs in fonts.items():
        w, h, rows = sheet(glyphs, args.scale)
        path = args.out / f"{name}.png"
        path.write_bytes(png(w, h, rows))
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
