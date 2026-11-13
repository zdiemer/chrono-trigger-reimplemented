"""The authored asset catalogue and committed native lookup table agree; no ROM needed."""
import unittest

from tools import assets


def row(name, at, end, type_="bytes"):
    return assets.Row(name, type_, at, end, "raw", "accessor x")


class Catalogue(unittest.TestCase):
    def test_committed_catalogue_and_table_agree(self):
        self.assertEqual(assets.check(), [])

    def test_rules(self):
        good = [row("a", 0xC00000, 0xC0000F), row("b.1", 0xC00010, 0xC00010)]
        self.assertEqual(assets.problems_of(good), [])
        self.assertTrue(assets.problems_of([row("a", 0xC00000, 0xC0000F), row("a", 0xC00020, 0xC00021)]))
        self.assertTrue(assets.problems_of([row("a", 0xC00000, 0xC0000F), row("b", 0xC0000F, 0xC00011)]))
        self.assertTrue(assets.problems_of([row("a", 0xC0FFF0, 0xC10001)]))       # two banks
        self.assertTrue(assets.problems_of([row("a", 0x7E0000, 0x7E0001)]))       # not the cartridge
        self.assertTrue(assets.problems_of([row("A", 0xC00000, 0xC00001)]))
        self.assertTrue(assets.problems_of([row("a", 0xC00000, 0xC00001, "blob")]))
        palette = assets.Row("p", "words", 0xC00000, 0xC0001D, "palette:15x7", "read")
        self.assertEqual(assets.problems_of([palette]), [])
        for bad in (("words", 0xC0001C, "palette:15"), ("bytes", 0xC0001D, "palette:15"),
                    ("words", 0xC0001D, "palette:0"), ("words", 0xC0001D, "palette")):
            self.assertTrue(assets.problems_of([assets.Row("p", bad[0], 0xC00000, bad[1], bad[2], "read")]))

    def test_fonts(self):
        def font(codec, end, type_="gfx", name="f", at=0xC00000):
            return assets.Row(name, type_, at, end, codec, "read")
        self.assertEqual(assets.problems_of([font("font:8x8", 0xC0000F), font("font:12x12", 0xC00057, name="g", at=0xC00010)]), [])
        for bad in (font("font:8x8", 0xC0000E), font("font:12x12", 0xC00023), font("font:8x8", 0xC0000F, "bytes"),
                    font("font:9x9", 0xC0000F)):
            self.assertTrue(assets.problems_of([bad]))
        glyphs = font("font:8x8", 0xC0003F)                       # four glyphs
        for codec, end, ok in (("font-widths:f:01", 0xC00042, True), ("font-widths:f:02", 0xC00042, False),
                               ("font-widths:g:00", 0xC00040, False), ("font-widths:f:1", 0xC00040, False)):
            widths = assets.Row("w", "bytes", 0xC00040, end, codec, "read")
            self.assertEqual(assets.problems_of([glyphs, widths]) == [], ok, codec)

    def test_packed(self):
        def packed(codec, type_="lz"):
            return assets.Row("p", type_, 0xC00000, 0xC0000F, codec, "read")
        for codec in ("lz", "lz:event", "lz:4bpp", "lz:field-map", "lz:looks", "lz:tilemap:32", "lz:bytes:96",
                      "lz:nibbles:4", "lz:colors:16"):
            self.assertEqual(assets.problems_of([packed(codec)]), [], codec)
        for bad in (packed("lz:nibbles:3"), packed("lz:tilemap"), packed("lz:tilemap:0"), packed("lz:maps"),
                    packed("lz:looks", "gfx"), packed("raw")):
            self.assertTrue(assets.problems_of([bad]), (bad.type, bad.codec))

    def test_version_follows_the_rows_not_how_they_were_found(self):
        a = [row("a", 0xC00000, 0xC0000F)]
        b = [assets.Row("a", "bytes", 0xC00000, 0xC0000F, "raw", "read")]
        self.assertEqual(assets.catalogue_version(a), assets.catalogue_version(b))
        self.assertNotEqual(assets.catalogue_version(a), assets.catalogue_version([row("a", 0xC00000, 0xC0000E)]))


if __name__ == "__main__":
    unittest.main()
