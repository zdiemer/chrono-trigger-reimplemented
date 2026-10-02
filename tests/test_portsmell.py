"""The rubric's mechanical items (tools/portsmell.py).

Every check is exercised against written-out examples of the smell and of the
legitimate form beside it, so a change to a pattern that stopped catching its
rubric item fails here rather than quietly emptying the baseline. The baseline
itself is checked the way tools/portstyle.py's is: it names files that exist, it
carries no line for a file that is already clean, and `--check` passes on the
committed tree, all without a ROM so CI holds it.
"""
import unittest

from tools import portsmell


class TestChecks(unittest.TestCase):
    def hits(self, check, src, path="game/field/walk.c"):
        fn = dict((name, fn) for name, fn, _i, _m in portsmell.CHECKS)[check]
        return fn(path, src)

    def test_quoted_write_sequence_is_a_finding(self):
        src = "/* STX $2102 : STZ $4374 : STA $420B */\nvoid f(void) { }\n"
        self.assertTrue(self.hits("asm-in-comments", src))

    def test_a_hardware_register_write_is_a_finding_on_its_own(self):
        self.assertTrue(self.hits("asm-in-comments", "/* then STA $420B starts it */\n"))

    def test_naming_one_call_site_is_provenance_not_a_quotation(self):
        """CLAUDE.md asks every port to carry its address, and the sound ports
        name the call they stand in for."""
        self.assertFalse(self.hits("asm-in-comments", "/* $C1:B967 the turn's action */\n"))
        self.assertFalse(self.hits("asm-in-comments", "/* resent by the loop's JSR $09DA */\n"))
        self.assertFalse(self.hits("asm-in-comments", "// a BMI/BPL spelled out\n"))

    def test_admitted_ignorance(self):
        self.assertTrue(self.hits("admitted-ignorance", "/* a history of values, presumably */\n"))
        self.assertTrue(self.hits("admitted-ignorance", "// what reads the pair isn't ported\n"))
        self.assertFalse(self.hits("admitted-ignorance",
                                   "/* Hold the script until the gesture finishes. */\n"))

    def test_twin_functions_that_differ_by_an_encoding(self):
        src = ("void loop_animation(Game *g)\n{\n}\n"
               "void loop_animation_0(Game *g)\n{\n}\n")
        self.assertTrue(self.hits("twin-functions", src))

    def test_a_numbered_name_that_has_no_twin_is_not_a_finding(self):
        self.assertFalse(self.hits("twin-functions", "void party_member_2(Game *g)\n{\n}\n"))

    def test_cpu_arithmetic_written_out(self):
        self.assertTrue(self.hits("cpu-arithmetic", "static unsigned add_with_carry(unsigned a, unsigned b)\n{\n}\n"))
        self.assertTrue(self.hits("cpu-arithmetic", "static uint16_t rotate_left(uint16_t v)\n{\n}\n"))
        self.assertFalse(self.hits("cpu-arithmetic", "static uint16_t damage_dealt(uint16_t a, uint16_t b)\n{\n}\n"))

    def test_registers_carried_out_to_a_wrapper(self):
        self.assertTrue(self.hits("register-out-params",
                                  "static void build_frame(Game *g, FrameTrace *trace)\n{\n}\n"))
        self.assertTrue(self.hits("register-out-params",
                                  "static void walk(Game *g, bool *started_for_wrapper)\n{\n}\n"))
        self.assertFalse(self.hits("register-out-params",
                                   "static void walk(Game *g, FieldObject *self, uint8_t step)\n{\n}\n"))

    def test_the_scaffolding_switch_used_as_a_value(self):
        self.assertTrue(self.hits("scaffolding-as-value", "return ORIGINAL_EFFECTS && overflows16(v);\n"))
        self.assertFalse(self.hits("scaffolding-as-value", "ORIGINAL_EFFECT(set_scratch(g, v));\n"))

    def test_the_pc_read_and_the_pc_set(self):
        self.assertTrue(self.hits("pc-identity", "if (s->cpu.pc == sites[row]) return true;\n"))
        self.assertFalse(self.hits("pc-identity", "s->cpu.pc = done;\n"),
                         "setting where the machine resumes is how an idle loop's port exits")

    def test_sign_bit_of_a_difference(self):
        self.assertTrue(self.hits("sign-bit-compare", "if (!((uint16_t)(x - LIMIT) & 0x8000))\n"))
        self.assertFalse(self.hits("sign-bit-compare", "if ((int16_t)(x - LIMIT) < 0)\n"))

    def test_a_call_out_into_one_of_our_own_ports(self):
        entries = {0xC20553: "menu_draw_window"}
        self.assertTrue(portsmell.sibling_through_machine(
            "game/menu/display.c", "m_jsr(s, 0xC2053E, 0xC20553);\n", entries))
        self.assertFalse(portsmell.sibling_through_machine(
            "game/menu/display.c", "m_jsr(s, 0xC2053E, 0xC29999);\n", entries))

    def test_stale_provenance(self):
        self.assertTrue(self.hits("stale-provenance", '# source = "game/battle/gone.c"\n', "re/ram.toml"))
        self.assertFalse(self.hits("stale-provenance", '# source = "tools/portsmell.py"\n', "re/ram.toml"))

    def test_an_enum_nothing_takes_as_a_type(self):
        src = "typedef enum { MENU_SOUND_CURSOR = 0 } MenuSoundNobodyTakes;\n"
        self.assertTrue(self.hits("enum-types-nothing", src))

    def test_a_flag_noted_in_the_logic_s_loop_or_game_branch(self):
        loop = ("static void boost(GameState *g)\n{\n"
                "    for (unsigned p = 0; p < PIECES; p++) {\n"
                "        add_boost(g, p);\n"
                "        ORIGINAL_EFFECT(battle_leave_multiply_carry(g, p > 1));\n"
                "    }\n}\n")
        self.assertTrue(self.hits("flag-note-in-logic", loop))
        branch = ("static void heal(GameState *g)\n{\n"
                  "    if (battle_action_state(g) & ABSORBED)\n"
                  "        ORIGINAL_EFFECT(leave_carry(g, true));\n}\n")
        self.assertTrue(self.hits("flag-note-in-logic", branch))
        plain = ("static void boost(GameState *g)\n{\n"
                 "    if (battle_action_state(g) & ABSORBED) {\n"
                 "        battle_leave_multiply_carry(g, true);\n    }\n}\n")
        self.assertTrue(self.hits("flag-note-in-logic", plain),
                        "a leave helper called plainly in a logic function is still the note")

    def test_a_flag_worked_out_once_or_by_the_machine_is_not_a_finding(self):
        after = ("static void boost(GameState *g)\n{\n"
                 "    for (unsigned p = 0; p < PIECES; p++)\n        add_boost(g, p);\n"
                 "    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, true));\n}\n")
        self.assertFalse(self.hits("flag-note-in-logic", after))
        cell = ("static void boost(GameState *g)\n{\n"
                "    for (unsigned p = 0; p < PIECES; p++)\n"
                "        ORIGINAL_EFFECT(battle_leave_word(g, 0x0C, p));\n}\n")
        self.assertFalse(self.hits("flag-note-in-logic", cell), "a scratch cell is not a flag")
        machine = ("void entry(Snes *s)\n{\n"
                   "    if (!came_back)\n        ORIGINAL_EFFECT(leave_carry(s, false));\n"
                   "    if (outcome == ENDED)\n        leave_flags_of_measuring(s, a, b);\n}\n")
        self.assertFalse(self.hits("flag-note-in-logic", machine),
                         "a machine condition, and a wrapper handing back its registers")
        helper = ("static void leave_cost_carry(Snes *s, uint8_t t)\n{\n"
                  "    if (t > 3)\n        ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_C, true));\n}\n")
        self.assertFalse(self.hits("flag-note-in-logic", helper), "the leaves helper is the legitimate form")
        loop = ("static void f(GameState *g)\n{\n    for (;;) {\n"
                "        ORIGINAL_EFFECT(leave_carry(g, true));\n    }\n}\n")
        self.assertFalse(self.hits("flag-note-in-logic", loop, "game/battle/bridge.c"))
        self.assertFalse(self.hits("flag-note-in-logic", loop, "game/event/leaves.c"))

    def test_a_return_address_pushed_by_hand(self):
        self.assertTrue(self.hits("return-address-pushed", "m_push16(s, 0xAC5C);\n"))
        self.assertTrue(self.hits("return-address-pushed",
                                  "m_push16(s, (uint16_t)(site + MENU_JSR_SIZE - 1));\n"))
        self.assertTrue(self.hits("return-address-pushed", "m_push16(s, READ_CONTROLLER_RETURN);\n"))

    def test_a_register_saved_or_a_push_in_a_bridge_is_not_a_finding(self):
        self.assertFalse(self.hits("return-address-pushed", "m_push16(s, s->cpu.d);\nm_push16(s, c->a);\n"))
        self.assertFalse(self.hits("return-address-pushed", "m_push16(s, (uint16_t)kind);\n"))
        self.assertFalse(self.hits("return-address-pushed", "m_push16(s, 0xAC5C);\n", "game/menu/bridge.c"))
        self.assertFalse(self.hits("return-address-pushed", "m_push16(s, 0xAC5C);\n",
                                   "game/menu/script_bridge.c"))

    def test_a_generated_header_is_not_a_port(self):
        """game/ram.h and game/vars.h come from re/ram.toml, so a finding in one
        is a finding in the map and can't be fixed in the file."""
        found = portsmell.scan()
        self.assertNotIn(("game/vars.h", "asm-in-comments"), found)
        self.assertNotIn(("game/ram.h", "asm-in-comments"), found)


class TestBaseline(unittest.TestCase):
    def setUp(self):
        self.found = portsmell.scan()
        self.base = portsmell.read_baseline()

    def test_every_listed_file_exists(self):
        for path, name in self.base:
            self.assertTrue((portsmell.ROOT / path).exists(),
                            "%s is on the backlog for %s but gone; delete its line" % (path, name))

    def test_every_listed_check_is_a_check(self):
        names = {name for name, _fn, _i, _m in portsmell.CHECKS}
        for _path, name in self.base:
            self.assertIn(name, names)

    def test_no_line_for_a_file_that_is_clean(self):
        for (path, name), allowed in self.base.items():
            if allowed:
                self.assertIn((path, name), self.found,
                              "%s is clean for %s: delete its line from %s"
                              % (path, name, portsmell.BASELINE.name))

    def test_the_committed_tree_passes_the_ratchet(self):
        import io
        out = io.StringIO()
        self.assertEqual(portsmell.check(self.found, out=out), 0, out.getvalue())

    def test_the_skill_points_at_every_check(self):
        """A check exists so a reader doesn't have to look for its item: the
        rubric must name the tool where the item now lives."""
        skill = (portsmell.ROOT / ".claude/skills/review-port/SKILL.md").read_text(encoding="utf-8")
        for name, _fn, _items, _msg in portsmell.CHECKS:
            self.assertIn(name, skill, "%s isn't named in the review rubric" % name)


if __name__ == "__main__":
    unittest.main()
