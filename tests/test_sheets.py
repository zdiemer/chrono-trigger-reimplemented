"""tools/sheets.py draws a made-up fonts text and a made-up graphics text; no ROM needed."""
import struct
import tempfile
import unittest
from pathlib import Path

from tools import sheets

FONTS = """# made up
[tiny]
glyph 00 width 3
.123
3...
....
....
glyph 01
....
....
....
...1
"""

GRAPHICS = """# made up
[two]
# 17 tiles, 2 bit planes
""" + ("1....... " * 15 + "1.......\n") * 8 + "...3....\n" * 8 + """
[four]
# 1 tiles, 4 bit planes, packed
packed short
# 0000
""" + "F.......\n" * 8


def size_of(png: bytes):
    return struct.unpack(">II", png[16:24])


class Sheets(unittest.TestCase):
    def test_parse(self):
        parsed = sheets.parse(FONTS + GRAPHICS)
        glyphs = parsed["tiny"].glyphs
        self.assertEqual([(n, w) for n, w, _ in glyphs], [(0, 3), (1, None)])
        self.assertEqual(glyphs[1][2][3], "...1")
        two, four = parsed["two"], parsed["four"]
        self.assertEqual((two.planes, len(two.rows), len(two.rows[0][0]), len(two.rows[1][0])), (2, 2, 16, 1))
        self.assertEqual((four.planes, four.rows[0][0]), (4, ["F......."]))

    def test_pool(self):
        pool = sheets.parse("[pool]\n# tiles of two or three bit planes\n# 0000\n" + "7......3\n" * 8 + "rest 00 01\n")
        self.assertEqual((pool["pool"].planes, len(pool["pool"].rows)), (3, 1))
        self.assertEqual(sheets.tile_sheet(pool["pool"])[2][16 + sheets.GAP], sheets.RAMP8["7"])

    def test_wrong_text_refused_at_its_line(self):
        for text, line in (("[../x]\nglyph 00\n....\n", 1),           # out of OUT_DIR
                           ("# a\n[a/b]\n", 2), ("[a\\\\b]\n", 1), ("[a..b]\n", 1), ("[Tiny]\n", 1),
                           ("glyph 00\n", 1),                         # before any [name]
                           ("[tiny]\n\n....\n....\nglyph 00\n", 3)):  # pixels before any glyph
            with self.subTest(text=text), self.assertRaises(SystemExit) as refused:
                sheets.parse(text)
            self.assertTrue(str(refused.exception).startswith(f"line {line}: "), refused.exception)

    def test_sheets_written_where_told(self):
        with tempfile.TemporaryDirectory() as out:
            text = Path(out) / "made.text"
            text.write_text(FONTS + GRAPHICS, encoding="utf-8")
            sheets.main([str(text), str(Path(out) / "sheets"), "--scale", "2"])
            tiny = (Path(out) / "sheets" / "tiny.png").read_bytes()
            self.assertEqual(tiny[:8], b"\x89PNG\r\n\x1a\n")
            cell_w, cell_h = 12 + sheets.GAP, 6 + 4 + 2 + sheets.GAP
            self.assertEqual(size_of(tiny), (2 * (16 * cell_w + 2), 2 * (cell_h + 2)))
            wide = 16 + sheets.GAP + 16 * 8 + sheets.GAP
            two = (Path(out) / "sheets" / "two.png").read_bytes()
            self.assertEqual(size_of(two), (2 * wide, 2 * (2 * 9 + sheets.GAP)))
            sheets.main([str(text), str(Path(out) / "only"), "--only", "four"])
            self.assertEqual([p.name for p in (Path(out) / "only").iterdir()], ["four.png"])


def made_up_pack(resources) -> bytes:
    """An asset pack (frontend/assets.c) of the named made-up resources."""
    head = sum(9 + len(n) + 1 + len("bytes") for n in resources)
    directory, data, at = b"", b"", 20 + head
    for name, bytes_ in resources.items():
        directory += struct.pack("<IIB", at + len(data), len(bytes_), len(name)) + name.encode() + bytes([5]) + b"bytes"
        data += bytes_
    return b"CTASSETS" + struct.pack("<HHII", 1, len(resources), 0, 0) + directory + data


def pointer(name):
    at = next(r.at for r in sheets.load_catalogue() if r.name == name)
    return bytes([at & 0xFF, at >> 8 & 0xFF, at >> 16])


# A location's map: 16x16 of look 0, whose four entries are tile $100 in
# palette 1, as it is, flipped across, flipped down and in front; BG2 all
# look 1, which the set hasn't.
MAPS = ("[field_packed_map.0]\npacked short\nbg1 16x16\nbg2 16x16 drift 00\nbg3 16x16 none drift 00\n"
        "screens 13\ncolor-math 00\nmap bg1\n" + ("00 " * 15 + "00\n") * 16 + "map bg2\n" + ("01 " * 15 + "01\n") * 16
        + "attributes\n000000\n"
        "[tile_looks_address.0]\npacked short\n# look 000\n100:1:--- 100:1:h--\n100:1:-v- 100:1:--p\n"
        "[overworld_top_layer_tilemaps.0]\npacked short\n# row 0\n000:0:--- 000:0:h--\n")
TILE_GRAPHICS = "[graphics_block_address.5]\n# 1 tiles, 4 bit planes, packed\npacked short\n" + "F.......\n" * 7 + "......12\n"
PALETTES = "[location_palettes]\n# palette 0, row 0\n" + "31  0  0\n" * 14 + " 0 31  0\n"


class Maps(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.text = Path(self.dir.name) / "made.maps"
        self.text.write_text(MAPS, encoding="utf-8")
        self.text.with_suffix(".graphics").write_text(TILE_GRAPHICS, encoding="utf-8")
        self.text.with_suffix(".palettes").write_text(PALETTES, encoding="utf-8")
        record = bytes([0, 0, 0xFF, 0, 0, 0]) + bytes(8)
        self.text.with_suffix(".assets").write_bytes(made_up_pack({
            "location_records": record, "tile_set_blocks_place": bytes([5]) + b"\xFF" * 7,
            "field_packed_map": pointer("field_packed_map.0"), "tile_looks_address": pointer("tile_looks_address.0"),
            "graphics_block_address": b"\0" * 15 + pointer("graphics_block_address.5")}))

    def tearDown(self):
        self.dir.cleanup()

    def test_location_drawn_from_its_tiles_and_palette(self):
        picture = sheets.location_sheet(self.text, 0)
        self.assertEqual((len(picture[0]), len(picture)), (256, 256))
        red, green = (255, 0, 0), (0, 255, 0)
        self.assertEqual(picture[0][0], green)                     # color 15 of the location's palette 1
        self.assertEqual(picture[7][7], red)                       # color 2
        self.assertEqual(picture[0][15], green)                    # flipped across
        self.assertEqual(picture[15][0], green)                    # flipped down
        self.assertEqual(picture[0][1], sheets.PAPER)              # color 0 clear, and BG2's look missing

    def test_tilemap_drawn_with_a_resources_tiles(self):
        picture = sheets.tilemap_sheet(self.text, "overworld_top_layer_tilemaps.0", "graphics_block_address.5")
        self.assertEqual((len(picture[0]), len(picture)), (16, 8))
        self.assertEqual(picture[0][0], sheets.RAMP["F"])
        self.assertEqual(picture[0][15], sheets.RAMP["F"])

    def test_entries_read_and_refused(self):
        self.assertEqual(sheets.entry("3FF:7:hvp", 1), 0xFFFF)
        with self.assertRaises(SystemExit) as refused:
            sheets.entry("400:0:---", 9)
        self.assertTrue(str(refused.exception).startswith("line 9: "))


if __name__ == "__main__":
    unittest.main()
