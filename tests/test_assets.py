"""The catalogue of resources (re/assets.tsv) and the C table generated from it
(game/data/asset_catalogue.c). The table is committed so that a build without a
ROM has it; this fails when it is out of date (regenerate with `python3
tools/assets.py build --rom <rom>`) or when the catalogue breaks its own rules.
Whether every row is still a region the matching build extracts needs the ROM:
the gate checks that (`tools/assets.py check --rom`).
"""
import unittest

from tools import assets


def row(name, at, end, type_="bytes"):
    return assets.Row(name, type_, at, end, "raw", "accessor x")


class Catalogue(unittest.TestCase):
    def test_committed_catalogue_and_table_agree(self):
        self.assertEqual(assets.check(None), [])

    def test_rules(self):
        good = [row("a", 0xC00000, 0xC0000F), row("b.1", 0xC00010, 0xC00010)]
        self.assertEqual(assets.problems_of(good), [])
        self.assertTrue(assets.problems_of([row("a", 0xC00000, 0xC0000F), row("a", 0xC00020, 0xC00021)]))
        self.assertTrue(assets.problems_of([row("a", 0xC00000, 0xC0000F), row("b", 0xC0000F, 0xC00011)]))
        self.assertTrue(assets.problems_of([row("a", 0xC0FFF0, 0xC10001)]))       # two banks
        self.assertTrue(assets.problems_of([row("a", 0x7E0000, 0x7E0001)]))       # not the cartridge
        self.assertTrue(assets.problems_of([row("A", 0xC00000, 0xC00001)]))
        self.assertTrue(assets.problems_of([row("a", 0xC00000, 0xC00001, "blob")]))

    def test_version_follows_the_rows_not_how_they_were_found(self):
        a = [row("a", 0xC00000, 0xC0000F)]
        b = [assets.Row("a", "bytes", 0xC00000, 0xC0000F, "raw", "read")]
        self.assertEqual(assets.catalogue_version(a), assets.catalogue_version(b))
        self.assertNotEqual(assets.catalogue_version(a), assets.catalogue_version([row("a", 0xC00000, 0xC0000E)]))


if __name__ == "__main__":
    unittest.main()
