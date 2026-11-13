"""A diagnostic printed before a crash must not excuse the failed process."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import native_check


class NativeExitTest(unittest.TestCase):
    stop = "frame 42: the program's C goes no further: $C2:A71E, a menu in an invalid state\n"

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
    def test_picture_pin_rejects_missing_changed_or_reordered_checkpoints(self):
        text = 'outcome-picture: 1 0123456789abcdef\noutcome-picture: 2 fedcba9876543210\n'
        expected = native_check.outcomes.observed_digest(['1:0123456789abcdef', '2:fedcba9876543210'])
        self.assertTrue(native_check.picture_matches(text, expected))
        for bad in ('', text.replace('abcdef', 'abcdee'), '\n'.join(reversed(text.splitlines()))):
            self.assertFalse(native_check.picture_matches(bad, expected))
        first = native_check.outcomes.observed_digest(['1:0123456789abcdef'])
        self.assertTrue(native_check.picture_matches(text.replace('fedcba', 'aaaaaa'), first, until=2))
        self.assertFalse(native_check.picture_matches(text, first))

    def test_missing_completion_is_failure(self):
        self.assertEqual(native_check.ended('', 0, 100)[0], 'incomplete')
        self.assertEqual(native_check.ended('ct_native: 99 frame(s); ok', 0, 100)[0], 'incomplete')
        self.assertEqual(native_check.ended('ct_native: 100 frame(s); ok', 0, 100)[0], 'end')

    def test_dark_exception_is_specific_to_place_and_start(self):
        self.assertTrue(native_check.dark_is_excepted((1, 20), 'from frame 20 in $001 to end'))
        self.assertFalse(native_check.dark_is_excepted((1, 20), 'from frame 19 in $001 to end'))
        self.assertFalse(native_check.dark_is_excepted((1, 20), 'from frame 20 in $002 to end'))
