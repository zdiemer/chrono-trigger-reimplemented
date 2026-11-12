"""A diagnostic printed before a crash must not excuse the failed process."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import native_check


class NativeExitTest(unittest.TestCase):
    stop = "frame 42: the program's C goes no further: $C2:A71E, a menu, runs only on the machine\n"

    def outcome(self, rc, text=None):
        return native_check.ended(self.stop if text is None else text, rc, 100)[0]

    def test_deliberate_stop(self):
        self.assertEqual(self.outcome(1), "crashed")

    def test_timeout_after_stop(self):
        self.assertEqual(self.outcome(None), "hung")

    def test_crash_after_stop(self):
        for rc in (-11, 0xC0000005, 3, 2):
            with self.subTest(rc=rc):
                self.assertEqual(self.outcome(rc), "crashed")

    def test_fatal_entry_after_stop(self):
        text = self.stop + "ct_native: frame 43: native_call reached, which only the machine has\n"
        self.assertEqual(self.outcome(3, text), "crashed")

    def test_failure_without_stop(self):
        self.assertEqual(self.outcome(1, ""), "crashed")

    def test_success_is_not_a_stop(self):
        self.assertEqual(self.outcome(0), "crashed")

    def test_unserved_cartridge_fails_even_with_a_picture(self):
        for text in ("asset outside the catalogue: $C0:1234 x1\n",
                     "asset outside the catalogue: cartridge reads\n"):
            self.assertEqual(self.outcome(0, text), "crashed")

    def test_missing_resource_stop_is_unserved(self):
        text = "frame 42: the program's C goes no further: $C2:A71E, a cartridge byte in no resource of the catalogue\n"
        self.assertEqual(self.outcome(1, text), "crashed")

class CompletionTest(unittest.TestCase):
    def test_missing_completion_is_failure(self):
        self.assertEqual(native_check.ended('', 0, 100)[0], 'incomplete')
        self.assertEqual(native_check.ended('ct_native: 99 frame(s); ok', 0, 100)[0], 'incomplete')
        self.assertEqual(native_check.ended('ct_native: 100 frame(s); ok', 0, 100)[0], 'end')

    def test_dark_exception_is_specific_to_place_and_start(self):
        self.assertTrue(native_check.dark_is_excepted((1, 20), 'from frame 20 in $001 to end'))
        self.assertFalse(native_check.dark_is_excepted((1, 20), 'from frame 19 in $001 to end'))
        self.assertFalse(native_check.dark_is_excepted((1, 20), 'from frame 20 in $002 to end'))
