"""The fallback measure's reader, ratchet and refusal check (tools/fallbacks.py).

Run on written-out files in the format dev/fallbacks.c writes, so they need no
ROM; and the committed baseline and allowlist are checked for form: every line
carries its reason.
"""
import io
import pathlib
import tempfile
import unittest

from tools import fallbacks

RUN = """# fallbacks
insn unported C21000 2 100 C21000 -
insn unported C21002 3 100 C21000 -
insn handback C0ABCD 1 50 C0ABCD C00097
insn handback 00ABCE 2 10 C0ABCD C00097
insn refused C04867 2 7 C04867 C04867
event callout C00097 C0ABCD 5 field_frame
refusal C04867 guard 7 event_cmd_set_solidity
state C04867 guard frame 3502 A=0108 X=0108 Y=02CA S=06F9 D=0100 DB=00 P=24 E=0, ROM
"""


class FallbacksTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.dir.name)
        (self.root / "movie.txt").write_text(RUN)

    def tearDown(self):
        self.dir.cleanup()

    def measure(self):
        return fallbacks.gather([self.root])

    def test_mirrors_count_as_their_bank(self):
        """$00:ABCE is $C0:ABCE: one routine, and its bytes counted once."""
        rows = self.measure().routines()
        self.assertEqual(set(rows), {"C2:1000", "C0:ABCD", "C0:4867"})
        n, nbytes, kinds, ports = rows["C0:ABCD"]
        self.assertEqual(n, 60)
        self.assertEqual(sorted(nbytes), [0xC0ABCD, 0xC0ABCE, 0xC0ABCF])
        self.assertEqual(dict(ports), {"handback": {"C0:0097"}})

    def test_totals_are_distinct_bytes_and_instructions(self):
        line = fallbacks.totals_line(self.measure())
        self.assertIn("267 instructions", line)
        self.assertIn("unported 0.0K/200", line)

    def baseline(self, text):
        path = self.root / "baseline.txt"
        path.write_text(text)
        return path

    def check(self, baseline):
        out = io.StringIO()
        return fallbacks.check(self.measure(), baseline, out), out.getvalue()

    def test_the_ratchet_passes_on_its_own_update(self):
        path = self.root / "baseline.txt"
        fallbacks.update(self.measure(), path)
        ok, out = self.check(path)
        self.assertTrue(ok, out)

    def test_a_new_routine_fails(self):
        ok, out = self.check(self.baseline("C2:1000 200 5  # no port yet\nC0:ABCD 60 3  # handback\n"))
        self.assertFalse(ok)
        self.assertIn("new: C0:4867", out)

    def test_a_new_byte_fails_and_a_few_more_instructions_dont(self):
        text = "C0:4867 7 2  # refused\nC0:ABCD 60 3  # handback\n"
        ok, out = self.check(self.baseline(text + "C2:1000 190 4  # no port yet\n"))
        self.assertFalse(ok)
        self.assertIn("rose: C2:1000", out)
        ok, out = self.check(self.baseline(text + "C2:1000 190 5  # no port yet\n"))
        self.assertTrue(ok, out)

    def test_a_line_without_its_reason_fails(self):
        ok, out = self.check(self.baseline("C2:1000 200 5\nC0:ABCD 60 3  # x\nC0:4867 7 2  # x\n"))
        self.assertFalse(ok)
        self.assertIn("malformed", out)

    def refusals(self, allow):
        path = self.root / "allow.txt"
        path.write_text(allow)
        out = io.StringIO()
        return fallbacks.refusals(self.measure(), path, out, False), out.getvalue()

    def test_a_refusal_fails_unless_allowed_with_a_reason(self):
        ok, out = self.refusals("")
        self.assertFalse(ok)
        self.assertIn("REFUSED C0:4867 event_cmd_set_solidity guard", out)
        self.assertIn("frame 3502", out)
        ok, out = self.refusals("C04867 event_cmd_set_solidity guard\n")
        self.assertFalse(ok)
        ok, out = self.refusals("C04867 event_cmd_set_solidity guard  # a movie starts mid-scene\n")
        self.assertTrue(ok, out)

    def test_the_committed_files_carry_their_reasons(self):
        for path, read in ((fallbacks.BASELINE, fallbacks.read_baseline),
                           (fallbacks.ALLOWLIST, fallbacks.read_allowlist)):
            if path.exists():
                _rows, bad = read(path)
                self.assertEqual(bad, [], path.name)


if __name__ == "__main__":
    unittest.main()
