"""The migration inventory includes suffixed address constants and page instances."""
import pathlib
import tempfile
import unittest
from unittest.mock import patch

from tools import ram_map, state_inventory


class StateInventoryTest(unittest.TestCase):
    def test_address_sites_include_c_integer_suffixes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            game = root / "game"
            game.mkdir()
            (game / "example.c").write_text(
                "/* 0x7E1234u\n wram_read8(g, 0); */\n"
                "a = 0x7E1234u;\nb = bank << 16u;\nc = a & 0xFF0000UL;\n"
                "d = RAM_PAGE_MENU;\nwram_read8(g, a);\n", encoding="utf-8")
            with patch.object(ram_map, "ROOT", root):
                sites = list(state_inventory.source_sites(game))
            self.assertEqual([line for _, line, kind, _ in sites if kind == "layout-expression"],
                             [3, 4, 5, 6])
            self.assertEqual([line for _, line, kind, _ in sites if kind == "raw-helper"], [7])

    def test_overlap_inventory_expands_instances_without_record_padding(self):
        m = ram_map.RamMap(text='''
[[page]]
name = "pages"
subsystem = "field"
meaning = "m"
source = "s"
why = "w"
instances = [{name = "low", d = 0x100}, {name = "high", d = 0x200}]
[[var]]
name = "value"
page = "pages"
dp = 2
subsystem = "field"
meaning = "m"
source = "s"
[[var]]
name = "record"
addr = "7E:0200"
stride = 4
subsystem = "menu"
meaning = "m"
source = "s"
[[var.field]]
name = "value"
offset = 2
meaning = "m"
''')
        owners, pairs = state_inventory.overlaps(m)
        self.assertEqual(pairs["record", "value"], [0x7E0202])
        self.assertNotIn(0x7E0200, owners)
        self.assertEqual(owners[0x7E0102], {"value"})
