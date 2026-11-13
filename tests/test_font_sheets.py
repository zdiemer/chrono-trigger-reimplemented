"""tools/font_sheets.py draws a made-up fonts text; no ROM needed."""
import struct
import tempfile
import unittest
from pathlib import Path

from tools import font_sheets

TEXT = """# made up
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


class FontSheets(unittest.TestCase):
    def test_parse(self):
        glyphs = font_sheets.parse(TEXT)["tiny"]
        self.assertEqual([(n, w) for n, w, _ in glyphs], [(0, 3), (1, None)])
        self.assertEqual(glyphs[1][2][3], "...1")

    def test_refused_font_name(self):
        # The name becomes a file name: ../x would write outside OUT_DIR, and a
        # separator isn't a catalogue name either.
        for name in ("../x", "a/b", "Dialog", "", "a..b"):
            with tempfile.TemporaryDirectory() as out:
                fonts = Path(out) / "bad.fonts"
                fonts.write_text("[%s]\nglyph 00\n....\n" % name, encoding="utf-8")
                with self.assertRaises(SystemExit) as raised:
                    font_sheets.main([str(fonts), str(Path(out) / "sheets")])
                self.assertIn("isn't a font name", str(raised.exception))
                self.assertFalse((Path(out) / "sheets").exists())

    def test_glyph_before_any_font(self):
        with self.assertRaises(SystemExit) as raised:
            font_sheets.parse("glyph 00\n....\n")
        self.assertIn("glyph before any [font]", str(raised.exception))

    def test_sheet_written_where_told(self):
        with tempfile.TemporaryDirectory() as out:
            fonts = Path(out) / "made.fonts"
            fonts.write_text(TEXT, encoding="utf-8")
            font_sheets.main([str(fonts), str(Path(out) / "sheets"), "--scale", "2"])
            data = (Path(out) / "sheets" / "tiny.png").read_bytes()
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            width, height = struct.unpack(">II", data[16:24])
            cell_w, cell_h = 12 + font_sheets.GAP, 6 + 4 + 2 + font_sheets.GAP
            self.assertEqual((width, height), (2 * (16 * cell_w + 2), 2 * (cell_h + 2)))


if __name__ == "__main__":
    unittest.main()
