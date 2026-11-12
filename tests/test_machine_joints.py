"""The machine inventory distinguishes actual interfaces from names and metadata."""
import unittest
import re
from unittest.mock import patch
from pathlib import Path
from tempfile import TemporaryDirectory

from tools import machine_joints
from tools.machine_joints import ROOT, cartridge_reads, machine_functions, held_wait_counts, restartable_loops, machine_boundaries


class HeldLoops(unittest.TestCase):
    def test_loop_and_checkpoint_must_share_a_function(self):
        source = '''
void loop(Snes *s) { for (;;) { step(s); if (!native_poll_held(s, 1, HEAD, ready)) return; } }
void idle(Snes *s) { do { pass(s); } while (native_idle_held(s)); }
void one_pass(Snes *s) { native_poll_held(s, 1, HEAD, ready); }
void just_a_loop(Snes *s) { while (ready(s)) pass(s); }
void commented(Snes *s) { /* while (native_idle_held(s)) {} */ }
void quoted(Snes *s) { log("while (native_idle_held(s)) {}"); }
bool native_idle_held(Snes *s);
'''
        self.assertEqual(restartable_loops(source), {'loop', 'idle'})

    def test_compatibility_alias_is_not_a_remaining_dispatch_loop(self):
        registry = '''const NativeEntry game_held_waits[] = {
 { .addr = 0xC0EC71, .fn = idle_entry, .loop = idle, .idle = true, .hold = FIELD },
 { .addr = 0xFDEAED, .fn = warp, .hold = WARP },
 { .addr = 0xFDEAED, .fn = warp_idle, .hold = WARP },
 { .addr = 0xC000BD, .fn = field_next, .hold = FIELD },
 /* An old address 0xC01234 is not an entry. */
};'''
        self.assertEqual(held_wait_counts(registry, {'idle', 'warp'}), (2, 2))
        self.assertEqual(held_wait_counts(registry, set()), (4, 0))

    def test_unknown_loop_metadata_does_not_hide_an_entry(self):
        registry = '''const NativeEntry game_held_waits[] = {
 { .addr = 0xC0EC71, .fn = known, .loop = missing, .hold = FIELD },
};'''
        self.assertEqual(held_wait_counts(registry, {'known'}), (1, 0))
        self.assertEqual(held_wait_counts(registry, {'missing'}), (1, 0))


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

    def test_audited_sites_are_exact_and_still_present(self):
        audited = dict(machine_joints.AUDITED_SOURCES)
        live = set()
        with patch.dict(machine_joints.AUDITED_SOURCES, {}, clear=True):
            for file in {key[0] for key in audited}:
                path = ROOT / file
                for _, operation, expression in machine_joints.cartridge_sources(path, path.read_text(encoding="utf-8")):
                    live.add((file, operation, re.sub(r"\s+", "", expression)))
        self.assertFalse(set(audited) - live, "remove stale proofs when their sources change")
        self.assertEqual(self.count("new_port.c", "wram_read8(g, in_line_bank(at));"), 1)
        self.assertEqual(self.count("menu/line_effects.c", "wram_read8(g, in_line_bank(other));"), 1)


class MachineFunctions(unittest.TestCase):
    def test_definitions_with_the_machine(self):
        source = ("static void draw(Snes *s, uint8_t x)\n{\n}\n"
                  "uint16_t total(const GameState *g)\n{\n}\n"
                  "void hand_back(GameState *g,\n                Snes *s)\n{\n}\n")
        self.assertEqual(machine_functions(source), 2)

    def test_declarations_calls_and_comments_are_not_definitions(self):
        source = ("void draw(Snes *s);\n"
                  "/* void old(Snes *s) { } */\n"
                  "void f(GameState *g)\n{\n    draw(state_snes(g));\n}\n")
        self.assertEqual(machine_functions(source), 0)


class MachineBoundaries(unittest.TestCase):
    def test_own_stops_and_holds_are_boundaries(self):
        text = """
            m_stop_at(s, head, dc); m_stop_at_own(s, head, dc);
            m_hold_at(s, head, dc, hold); m_hold_at_own(s, head, dc, hold);
            native_dont_care_at_stop(s, head, dc);
            native_own_dont_care_at_stop(s, head, dc);
        """
        self.assertEqual(machine_boundaries(text), {"stops at the original's addresses": 6})

    def test_call_apis_and_machine_wrappers(self):
        text = """
            m_jsr(s, site, target); m_jsl(s, site, target);
            m_jsr_dc(s, site, target, dc); m_jsl_dc(s, site, target, dc);
            native_call(s, site, target, false); native_call_dc(s, site, target, false, dc);
            native_yield_call_dc(s, site, target, false, dc, continues);
            native_jump_call_dc(s, site, target, back, dc);
            native_wait_call(s, site, target, false, dc, wake);
            native_call_stop(site, target, dc); m_jsr_stack_bytes(s, last);
        """
        self.assertEqual(machine_boundaries(text), {"call-outs to entries by address": 9})

    def test_address_and_continuation_lookups(self):
        text = "native_hand_entry(s); native_hand_entry_at(s, at); native_continuation_entry_at(s, at);"
        self.assertEqual(machine_boundaries(text), {"siblings found in the registry": 3})

    def test_helper_names_do_not_establish_a_machine_call(self):
        text = """
            bool call_out_as(Snes *s) { checked_c_call(s); return true; }
            void call_out_far(Snes *s) { checked_c_call(s); }
            call_out_as(s); call_out_far(s);
        """
        self.assertEqual(machine_boundaries(text), {})
        self.assertEqual(machine_boundaries(text.replace('checked_c_call(s); return', 'm_jsr(s, site, target); return')),
                         {"call-outs to entries by address": 1})

    def test_comments_and_quoted_calls_are_not_boundaries(self):
        text = """
            // m_jsr(s, site, target);
            /* m_hold_at_own(s, head, dc, hold); */
            log("native_hand_entry_at(s, at)");
        """
        self.assertEqual(machine_boundaries(text), {})

    def test_declarations_do_not_hide_the_calls_in_their_bodies(self):
        text = """
            void native_call(Snes *s, uint32_t site, uint32_t target, bool far);
            const NativeEntry *native_hand_entry(const Snes *s);
            static inline void m_jsr(Snes *s, uint32_t site, uint32_t target)
            { native_call(s, site, target, false); }
        """
        self.assertEqual(machine_boundaries(text), {"call-outs to entries by address": 1})
        self.assertEqual(machine_boundaries('return native_hand_entry_at(s, at);'),
                         {"siblings found in the registry": 1})

    def test_macro_forwarding_contains_the_boundary(self):
        text = '#define CALL(s, target) m_jsr(s, site, target)\nCALL(s, target);'
        self.assertEqual(machine_boundaries(text), {"call-outs to entries by address": 1})

    def test_space_before_parameters(self):
        text = 'm_hold_at_own \n (s, head, dc, hold); native_hand_entry_at \t (s, at);'
        self.assertEqual(machine_boundaries(text),
                         {"stops at the original's addresses": 1, "siblings found in the registry": 1})
