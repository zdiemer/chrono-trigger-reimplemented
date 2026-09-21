"""The ratchet on ports that transliterate the assembly (tools/portstyle.py).

The measure and the baseline are checked here so CI holds them without a ROM:
the baseline names files that exist, every file on it is still over the
threshold (one rewritten to the bar leaves the list rather than sitting on it),
and `--check` passes on the committed tree. What the measure counts is checked
against written-out examples of both styles, so a change to the pattern that
stopped counting the failure mode would fail here rather than quietly emptying
the backlog.
"""
import pathlib
import unittest

from tools import portstyle


class TestMeasure(unittest.TestCase):
    def rate(self, src):
        return len(portstyle.OPS.findall(portstyle.strip_comments(src)))

    def test_counts_operations_on_the_emulated_registers(self):
        src = """
        m_lda16(s, 0xE000);
        m_ldx(s, 0x0740);
        m_tax(s);
        m_sep(s, CPU_FLAG_M);
        c->a = m_asl16(s, c->a);
        """
        self.assertEqual(self.rate(src), 7)     # five calls, two c->a

    def test_doesnt_count_talking_to_the_machine(self):
        """Memory access, call-outs and hand-backs are how any port reaches the
        machine; only the emulated registers are the failure mode."""
        src = """
        m_write16(s, m_abs(s, RAM_MENU_SLOT), value);
        v = m_read8(s, m_dp(s, cell));
        m_jsr(s, 0xC2053E, 0xC20553);
        m_stop_at(s, 0xC20541, 0);
        vid_set_scroll(pf, PF_AT(0xC20073), VID_BG1, x, y, 0);
        """
        self.assertEqual(self.rate(src), 0)

    def test_comments_dont_count(self):
        """A port that explains what the original did isn't transliterating."""
        self.assertEqual(self.rate("/* m_lda16 c->a */\n// m_tax(s);\nx = 1;\n"), 0)

    def test_a_reimplemented_routine_scores_under_the_threshold(self):
        src = """
        void menu_reset_sprites(Snes *s)
        {
            oam_hide_sprites(s, MENU_FIRST_FREE_SPRITE, SPRITES);
            if (!take_flag(s, RAM_MENU_CURSOR_HIDDEN))
                menu_place_cursor(s, m_read8(s, m_dp(s, MENU_SLOT)));
        }
        """
        lines = sum(1 for line in src.splitlines() if line.strip())
        self.assertLess(self.rate(src) / lines, portstyle.THRESHOLD)


class TestBaseline(unittest.TestCase):
    def setUp(self):
        self.rows = portstyle.measure()
        self.base = portstyle.read_baseline()

    def test_every_listed_file_exists(self):
        for path in self.base:
            self.assertTrue((portstyle.ROOT / path).exists(),
                            "%s is on the backlog but gone; delete its line" % path)

    def test_no_file_on_the_backlog_is_already_at_the_bar(self):
        """A file rewritten to the bar leaves the list, so the count of what is
        left means what it says."""
        for rate, _, _, path in self.rows:
            if path in self.base:
                self.assertGreater(rate, portstyle.THRESHOLD,
                                   "%s scores %.3f, at the bar: delete its line from %s"
                                   % (path, rate, portstyle.BASELINE.name))

    def test_the_committed_tree_passes_the_ratchet(self):
        import io
        out = io.StringIO()
        self.assertEqual(portstyle.check(self.rows, out=out), 0, out.getvalue())

    def test_the_backlog_is_what_the_plan_says_it_is(self):
        """The plan quotes the size; a wave that empties files must update it."""
        plan = (portstyle.ROOT / "docs/PLAN.md").read_text(encoding="utf-8")
        self.assertIn("%d of %d files" % (len(self.base), len(self.rows)), plan,
                      "docs/PLAN.md's Phase 3a no longer matches the baseline")


if __name__ == "__main__":
    unittest.main()
