"""The check that no port's logic reads what a port writes only as scaffolding
(tools/effects_check.py).

A made-up tree with a made-up RAM map stands for the two cases that motivated
it (a leftover the arithmetic kept only as scaffolding, read by the next
routine as game input) and for what it must not report: another element of an
array, a guard's peek, a variable of another subsystem over the same bytes.
The committed tree is checked against its baseline too, so CI holds the
ratchet without a ROM.
"""
import pathlib
import tempfile
import textwrap
import unittest

from tools import effects_check, ram_map

MAP = """
[[var]]
name = "battle_high_word"
addr = "7E:002E"
size = 2
subsystem = "battle"
meaning = "the long arithmetic's leftover high word"
source = "test"

[[var]]
name = "battle_work"
addr = "7E:0040"
count = 8
index = "cell"
subsystem = "battle"
meaning = "cells"
source = "test"

[[var]]
name = "field_scratch_word"
addr = "7E:002E"
size = 2
subsystem = "field"
meaning = "another mode's use of the same bytes"
source = "test"

[[var]]
name = "menu_window_inside"
addr = "7E:0100"
size = 2
subsystem = "menu"
meaning = "the inside's size"
source = "test"
"""

FILES = {
    "game/original_effects.h": "#define ORIGINAL_EFFECT(...) ((void)(__VA_ARGS__))\n",
    "game/battle/arith.c": """
        enum { LEFT_TURNED = 0, LEFT_OTHER = 1 };

        static void leave_high(GameState *g, uint16_t high) { set_battle_high_word(g, high); }
        static void set_work_byte(GameState *g, unsigned cell, uint8_t v) { set_battle_work(g, cell, v); }
        static void leave_by(GameState *g, void (*set)(GameState *, unsigned, uint8_t), unsigned at, uint8_t v)
        {
            set(g, at, v);
        }

        uint16_t battle_long_multiply(GameState *g, uint16_t a, uint16_t b)
        {
            uint32_t p = (uint32_t)a * b;
            ORIGINAL_EFFECT(leave_high(g, (uint16_t)(p >> 16)));
            ORIGINAL_EFFECT(set_work_byte(g, LEFT_TURNED, 0));
            ORIGINAL_EFFECT(leave_by(g, set_battle_work, 3, 0));
            return (uint16_t)p;
        }
        """,
    "game/battle/random.c": """
        uint8_t battle_draw(GameState *g, uint8_t limit)
        {
            return (uint8_t)(battle_high_word(g) % limit);
        }

        uint8_t battle_other_cell(GameState *g) { return battle_work(g, LEFT_OTHER_CELL); }
        uint8_t battle_any_cell(GameState *g, unsigned i) { return battle_work(g, i); }
        uint8_t battle_third_cell(GameState *g) { return battle_work(g, 3); }

        static bool looks_right(Snes *s)
        {
            vars_peek(true);
            bool ok = battle_high_word(state_of(s)) < 10;
            vars_peek(false);
            return ok;
        }
        static bool guard_reads(Snes *s) { return battle_high_word(state_of(s)) != 0; }
        """,
    "game/battle/random.h": "enum { LEFT_OTHER_CELL = 1 };\n",
    "game/field/walk.c": """
        uint16_t field_walk(GameState *g) { return field_scratch_word(g); }
        """,
    "game/menu/window.c": """
        static void frame(GameState *g, uint16_t w)
        {
        #if ORIGINAL_EFFECTS
            set_menu_window_inside(g, w - 2);
        #endif
        }

        void menu_draw_window(GameState *g, uint16_t w)
        {
            frame(g, w);
            draw_inside(g, menu_window_inside(g));
        }
        """,
    "game/registry.c": """
        static const NativeEntry entries[] = {
            { .addr = 0xC10000, .name = "x", .guard = guard_reads },
        };
        """,
}


class TestTree(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        root = pathlib.Path(cls.tmp.name)
        for rel, text in FILES.items():
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(textwrap.dedent(text), encoding="utf-8")
        (root / "ram.toml").write_text(MAP, encoding="utf-8")
        cls.root = root
        cls.findings = effects_check.analyse(root, root / "game", root / "ram.toml")
        cls.keys = {f.key() for f in cls.findings}

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_a_leftover_read_as_game_input(self):
        """battle's case: the draw reads the high word the multiply keeps only as scaffolding."""
        self.assertIn("game/battle/random.c battle_draw battle_high_word battle_high_word only", self.keys)
        f = next(f for f in self.findings if f.read.func[1] == "battle_draw")
        self.assertEqual(f.writers, [("game/battle/arith.c", 4)])
        self.assertEqual(f.plain, [])
        self.assertIn("[only]", f.describe())

    def test_a_block_under_if_original_effects(self):
        """menus' case: the inside drawn to the size only the frame's scaffolding wrote."""
        self.assertIn("game/menu/window.c menu_draw_window menu_window_inside menu_window_inside only", self.keys)

    def test_constants_follow_helpers_into_elements(self):
        """set_work_byte(g, LEFT_TURNED, 0) is element 0: element 1 isn't reported, any element is."""
        self.assertFalse(any(k.startswith("game/battle/random.c battle_other_cell ") for k in self.keys))
        self.assertIn("game/battle/random.c battle_any_cell battle_work battle_work only", self.keys)

    def test_a_setter_passed_to_a_helper(self):
        """leave_by(g, set_battle_work, 3, 0) writes element 3 through its parameter."""
        self.assertIn("game/battle/random.c battle_third_cell battle_work[3] battle_work only", self.keys)

    def test_peeks_and_guards_are_not_the_logic(self):
        funcs = {f.read.func[1] for f in self.findings}
        self.assertNotIn("looks_right", funcs)
        self.assertNotIn("guard_reads", funcs)

    def test_other_subsystems_bytes_are_another_cell(self):
        self.assertFalse(any(f.read.func[1] == "field_walk" for f in self.findings))

    def test_nothing_else(self):
        self.assertEqual(len(self.findings), 4, "\n".join(f.describe() for f in self.findings))

    def test_a_cell_losing_its_plain_writer_is_a_new_finding(self):
        """The key says `only` where no port writes the cell plainly: a plain write
        turned into scaffolding is a new line, not the old one."""
        f = next(f for f in self.findings if f.read.func[1] == "battle_draw")
        was = f.plain
        f.plain = [("game/battle/arith.c", 1)]
        try:
            self.assertEqual(f.key(), "game/battle/random.c battle_draw battle_high_word battle_high_word")
        finally:
            f.plain = was

    def test_ratchet(self):
        out = open(pathlib.Path(self.tmp.name) / "out.txt", "w", encoding="utf-8")
        with out:
            self.assertEqual(effects_check.check(self.findings, {}, out=out), 1)
            base = {k: "" for k in self.keys}
            self.assertEqual(effects_check.check(self.findings, base, out=out), 0)
            base["run gone_port some_var"] = "a line of a branch in flight"
            self.assertEqual(effects_check.check(self.findings, base, out=out), 0)   # stale lines don't fail

    def test_baseline_keeps_reasons(self):
        path = pathlib.Path(self.tmp.name) / "base.txt"
        effects_check.write_baseline({"a b c d": "", "run p v": "the caller sets it first"}, path)
        self.assertEqual(effects_check.read_baseline(path), {"a b c d": "", "run p v": "the caller sets it first"})


class TestRuns(unittest.TestCase):
    def test_reads_scaffold_read_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = pathlib.Path(tmp) / "gato-ship.log"
            log.write_text("native routines (on mode):\n"
                           "scaffold read: $7E:002E by battle_draw (3 times, first at frame 120)\n"
                           "scaffold read: $7E:002F by battle_draw (2 times, first at frame 121)\n",
                           encoding="utf-8")
            (f,) = effects_check.run_findings([log], ram_map.RamMap(text=MAP))
        self.assertEqual(f.key(), "run battle_draw battle_high_word")   # battle's name for $2E, not field's
        self.assertEqual(f.count, 5)
        self.assertEqual(f.movies, {"gato": 120})


class TestCommittedTree(unittest.TestCase):
    def test_no_new_findings_in_the_source(self):
        findings = effects_check.analyse()
        base = effects_check.read_baseline()
        new = [f.describe() for f in findings if f.key() not in base]
        self.assertEqual(new, [], "answer these or add them to tools/effects_baseline.txt with a reason")


if __name__ == "__main__":
    unittest.main()
