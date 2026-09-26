"""The `index_reach` hints in re/hints.toml against the ROM (review item 78: a
hint the liveness analysis leans on is tied to the disassembly by a test).

Each must sit on the dispatch its bound is argued from -- ASL A with an 8-bit
A, TAX, JSR or JMP (abs,X), nothing jumping past the ASL -- and every word it
reaches must be code the analysis traced (tools/v_readers.py). Skipped without
the ROM; tests/test_dontcare_check.py exercises the same checks on a fake one."""
import unittest

from tools import v_readers

ROM = v_readers.ROOT / "roms" / "ct-usa.sfc"


@unittest.skipUnless(ROM.exists(), "needs the ROM (tools/rom.py stage)")
class IndexReachHints(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.an, _ = v_readers.analysis(ROM, v_readers.ROOT / "re" / "hints.toml")

    def test_there_are_hints(self):
        self.assertTrue(self.an.hints.index_reach)

    def test_each_sits_on_its_dispatch(self):
        self.assertEqual(v_readers.dispatch_findings(self.an), [])

    def test_every_word_reached_is_traced_code(self):
        targets, problems = v_readers.bounded_targets(self.an)
        self.assertEqual(problems, [])
        for dispatcher, reach in self.an.hints.index_reach.items():
            self.assertLessEqual(len(targets[dispatcher]), reach)


if __name__ == "__main__":
    unittest.main()
