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


if __name__ == "__main__":
    unittest.main()
