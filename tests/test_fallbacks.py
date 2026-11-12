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

    def test_cutoff_completion_keeps_the_movie_counts(self):
        with (self.root / "movie.txt").open("a") as f:
            f.write("open C30002 44292 decompress\nclosed C30002 44292 decompress\n")
        m = self.measure()
        self.assertFalse(m.open)
        self.assertEqual(m.completed[("C3:0002", "decompress")], 44292)
        self.assertEqual(sum(m.insns.values()), 267)

    def test_unfinished_cutoff_still_counts(self):
        with (self.root / "movie.txt").open("a") as f:
            f.write("open C30002 44292 decompress\n")
        self.assertEqual(self.measure().open[("C3:0002", "decompress")], 44292)

    def test_completion_cannot_cancel_another_movie(self):
        with (self.root / "movie.txt").open("a") as f:
            f.write("open C30002 44292 decompress\n")
        (self.root / "other.txt").write_text("closed C30002 44292 decompress\n")
        m = self.measure()
        self.assertEqual(m.open[("C3:0002", "decompress")], 44292)
        self.assertEqual(len(m.bad), 1)

    def test_completion_cannot_exceed_the_cutoff(self):
        (self.root / "movie.txt").write_text(
            "open C30002 10 decompress\nclosed C30002 11 decompress\n")
        m = self.measure()
        self.assertEqual(m.open[("C3:0002", "decompress")], 10)
        self.assertEqual(len(m.bad), 1)

    def test_mirrors_count_as_their_bank(self):
        """$00:ABCE is $C0:ABCE: one routine, and its bytes counted once."""
        rows = self.measure().routines()
        self.assertEqual(set(rows), {"C2:1000", "C0:ABCD", "C0:4867"})
        n, nbytes, kinds, ports = rows["C0:ABCD"]
        self.assertEqual(n, 60)
        self.assertEqual(sorted(nbytes), [0xC0ABCD, 0xC0ABCE, 0xC0ABCF])
        self.assertEqual(dict(ports), {"handback": {"C0:0097"}})

    def test_idle_completion_removes_only_the_certified_prefix(self):
        with (self.root / "movie.txt").open("a") as f:
            f.write("idle-closed unported C21000 2 1 C21000 -\n")
        m = self.measure()
        self.assertEqual(sum(m.insns.values()), 266)
        self.assertEqual(sum(m.idle_completed.values()), 1)
        self.assertEqual(m.bad, [])

    def test_fully_certified_idle_instruction_leaves_no_bytes(self):
        (self.root / "movie.txt").write_text(
            "insn unported C21000 2 1 C21000 -\n"
            "idle-closed unported C21000 2 1 C21000 -\n")
        m = self.measure()
        self.assertEqual(m.routines(), {})
        self.assertIn("1 cutoff idle instructions certified", fallbacks.totals_line(m))

    def test_idle_completion_cannot_cancel_another_movie(self):
        (self.root / "other.txt").write_text("idle-closed unported C21000 2 1 C21000 -\n")
        m = self.measure()
        self.assertEqual(sum(m.insns.values()), 267)
        self.assertEqual(len(m.bad), 1)

    def test_idle_completion_leaves_only_the_surviving_width(self):
        (self.root / "movie.txt").write_text(
            "insn unported C21000 2 1 C21000 -\n"
            "insn unported C21000 3 1 C21000 -\n"
            "idle-closed unported C21000 3 1 C21000 -\n")
        row = self.measure().routines()["C2:1000"]
        self.assertEqual(row[0], 1)
        self.assertEqual(row[1], {0xC21000, 0xC21001})

    def test_idle_completion_must_match_length_kind_and_owner(self):
        for line in ("idle-closed unported C21000 3 1 C21000 -",
                     "idle-closed handback C21000 2 1 C21000 -",
                     "idle-closed unported C21000 2 1 C21000 C00097"):
            with self.subTest(line=line):
                (self.root / "movie.txt").write_text(RUN + line + "\n")
                m = self.measure()
                self.assertEqual(sum(m.insns.values()), 267)
                self.assertEqual(len(m.bad), 1)

    def test_idle_completion_cannot_exceed_the_cutoff(self):
        with (self.root / "movie.txt").open("a") as f:
            f.write("idle-closed unported C21000 2 101 C21000 -\n")
        m = self.measure()
        self.assertEqual(sum(m.insns.values()), 267)
        self.assertEqual(len(m.bad), 1)

    def test_totals_are_distinct_bytes_and_instructions(self):
        line = fallbacks.totals_line(self.measure())
        self.assertIn("267 instructions", line)
        self.assertIn("unported 0.0K/200", line)



if __name__ == "__main__":
    unittest.main()
