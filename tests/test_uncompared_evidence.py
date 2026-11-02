import unittest

from tools.uncompared_evidence import compared


class ComparisonEvidenceTest(unittest.TestCase):
    def row(self, calls=1, matched=1, suffix=""):
        return ("  7E3000 scene_program_start calls %d matched %d mismatched 0 interrupted 0%s\n"
                % (calls, matched, suffix))

    def test_requires_an_actual_comparison(self):
        self.assertTrue(compared(self.row(), "scene_program_start"))
        self.assertFalse(compared(self.row(0, 0), "scene_program_start"))
        self.assertFalse(compared(self.row(1, 0, " abandoned 1"), "scene_program_start"))
        self.assertFalse(compared(self.row(1, 0), "scene_program_start"))
        self.assertFalse(compared(self.row(), "scene_first_frame"))

    def test_another_entries_failure_also_fails_the_run(self):
        text = self.row() + "  7E5978 scene_title_set_up calls 1 matched 0 mismatched 1 interrupted 0\n"
        self.assertFalse(compared(text, "scene_program_start"))
        self.assertFalse(compared(self.row() + "page refused: test\n", "scene_program_start"))


if __name__ == "__main__":
    unittest.main()
