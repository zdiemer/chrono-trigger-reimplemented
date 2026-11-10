"""The cartridge inventory distinguishes actual sources from timing metadata."""
import unittest

from tools.machine_joints import ROOT, cartridge_reads


class CartridgeJoints(unittest.TestCase):
    def count(self, file, source):
        return cartridge_reads(ROOT / "game" / file, source)

    def test_origins_and_ram_are_not_cartridge_sources(self):
        self.assertEqual(self.count("menu/display.c", "vid_send(pf, PF_AT(0xC226AB), PF_GAME(0x7E2000));"), 0)
        self.assertEqual(self.count("menu/display.c", "vid_send(pf, PF_AT(0xC226AB), menu_upload_data(source, size));"), 0)

    def test_fixed_and_dynamic_sources(self):
        self.assertEqual(self.count("menu/display.c", "PF_GAME(0xC226D4u)"), 1)
        self.assertEqual(self.count("battle/uploads.c", "PF_GAME(source)"), 1)
        self.assertEqual(self.count("event/memory.c", "wram_read8(g, address)"), 1)

    def test_accessors_use_the_store_not_the_machine(self):
        self.assertEqual(self.count("scene/scene_data.h", "return m_read16(s, table + offset);"), 1)
        self.assertEqual(self.count("scene/scene_data.h", "return rom_read16(g, table + offset);"), 0)
        self.assertEqual(self.count("scene/screen.c", "return rom_read16(g, table + offset);"), 1)
