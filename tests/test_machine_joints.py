"""The cartridge inventory distinguishes actual sources from timing metadata."""
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

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

    def test_plain_machine_reads_in_an_unrelated_port(self):
        for width in (8, 16, 24):
            self.assertEqual(self.count("new_port.c", f"m_read{width}(s, 0xC00000u);"), 1)

    def test_defined_sources_and_nested_includes(self):
        self.assertEqual(self.count("new_port.c", "#define IMAGE 0xC00000u\nPF_GAME(IMAGE);"), 1)
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "addresses.h").write_text("#define IMAGE 0xC00000u\n#define BUFFER 0x7E2000u\n")
            (root / "shared.h").write_text('#include <addresses.h>\n#define SOURCE IMAGE\n')
            for name, expected in (("SOURCE", 1), ("BUFFER", 0)):
                text = f'#include "shared.h"\nPF_GAME({name});'
                self.assertEqual(cartridge_reads(root / "port.c", text), expected)

    def test_unknown_computed_sources_count_in_any_file(self):
        for expression in ("source + offset", "(uint32_t)bank << 16 | pointer", "0x7E0000u | unknown"):
            for call in (f"m_read8(s, {expression})", f"rom_read16(g, {expression})",
                         f"rom_data({expression}, bytes)", f"PF_GAME({expression})",
                         f"VidSnesTransfer t = {{ .source = {expression}, .size = 10 }}"):
                with self.subTest(call=call):
                    self.assertEqual(self.count("new_port.c", call), 1)

    def test_proven_ram_constants(self):
        for address in ("0x7E2000u", "0x7FFFFFu", "0x000100u", "0x3F1FFFu"):
            for call in (f"m_read8(s, {address})", f"PF_GAME({address})",
                         f"VidSnesTransfer t = {{ .source = {address} }}"):
                self.assertEqual(self.count("new_port.c", call), 0)
        self.assertEqual(self.count("new_port.c", "#define BUFFER 0x7E0000u\nPF_GAME(BUFFER + 16);"), 0)
        self.assertEqual(self.count("new_port.c", "enum { BANK = 0x7E, BUFFER = BANK << 16 }; PF_GAME(BUFFER);"), 0)

    def test_low_bank_rom_and_io_are_not_ram(self):
        for address in ("0x008000u", "0x3F8000u", "0x402000u", "0x002100u", "0x800100u"):
            self.assertEqual(self.count("new_port.c", f"PF_GAME({address});"), 1)

    def test_constant_casts_and_function_macros(self):
        self.assertEqual(self.count("new_port.c", "#define LOW(a) ((uint16_t)(a))\nPF_GAME(LOW(0x7E0100u));"), 0)
        self.assertEqual(self.count("new_port.c", "PF_GAME((uint16_t)0xC08000u);"), 1)
        self.assertEqual(self.count("new_port.c", "PF_GAME((uint16_t)unknown);"), 1)

    def test_conflicting_and_recursive_constants_stay_unknown(self):
        for definitions in ("#define BUFFER 0x7E0000u\n#define BUFFER 0xC00000u\n",
                            "#define BUFFER 0x7E0000u\n#undef BUFFER\n",
                            "#define BUFFER NEXT\n#define NEXT BUFFER\n"):
            self.assertEqual(self.count("new_port.c", definitions + "PF_GAME(BUFFER);"), 1)

    def test_comments_strings_and_definitions_are_not_reads(self):
        text = '// m_read8(s, 0xC00000u)\n/* PF_GAME(rom) */\n"PF_GAME(rom)";\n#define BUFFER 0x7E0000u\n'
        self.assertEqual(self.count("new_port.c", text), 0)
        self.assertEqual(self.count("wram.h", "uint8_t wram_read8(const GameState *g, uint32_t at);"), 0)
        self.assertEqual(self.count("new_port.c", "char marker = '//'; m_read8(s, 0xC00000u);"), 1)

    def test_read_wrappers_and_aliases_do_not_hide_sources(self):
        self.assertEqual(self.count("new_port.c", "#define READ(at) m_read8(s, at)\nREAD(0xC00000u);"), 1)
        self.assertEqual(self.count("new_port.c", "#define READ m_read8\nREAD(s, 0xC00000u);"), 1)

    def test_named_data_seams_still_count_machine_bypasses(self):
        for file in ("example_data.h", "packed_graphics.h", "assets.c", "decompress.c", "rom.h"):
            self.assertEqual(self.count(file, "rom_read8(g, address); rom_data(address, size);"), 0)
            self.assertEqual(self.count(file, "m_read8(s, 0xC00000u);"), 1)
