"""tools/places.py: a run's places against the reference's, on made-up lists."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))

import places  # noqa: E402


class PlacesTest(unittest.TestCase):
    def test_parse(self):
        text = "noise\nlocation: frame 1 $000\nlocation: frame 51 $1B1\nother\n"
        self.assertEqual(places.parse(text), [(1, 0x000), (51, 0x1B1)])

    def test_all_on_time(self):
        ref = [(1, 0), (51, 0x1B1), (1669, 0)]
        k = places.Kept(ref, [(1, 0), (45, 0x1B1), (1690, 0)])
        self.assertEqual((k.kept, k.same, k.total, k.worst), (3, 3, 3, 21))
        self.assertIsNone(k.parted)

    def test_late_place_is_kept_in_order_but_not_on_time(self):
        ref = [(1, 0), (4845, 0x10F), (5804, 2)]
        k = places.Kept(ref, [(1, 0), (4798, 0x10F), (5799, 2)])
        self.assertEqual((k.kept, k.same, k.worst), (2, 3, -47))

    def test_another_place_ends_the_order(self):
        ref = [(1, 0), (3359, 0x1F0), (18716, 0x05)]
        k = places.Kept(ref, [(1, 0), (3359, 0x0A0), (18716, 0x05)])
        self.assertEqual((k.kept, k.same), (1, 1))
        self.assertEqual(k.parted, (3359, 0x1F0, (3359, 0x0A0)))
        self.assertIn("where the reference enters $1F0", k.summary())

    def test_a_run_that_stops_short(self):
        k = places.Kept([(1, 0), (100, 1)], [(1, 0)])
        self.assertEqual((k.kept, k.same), (1, 1))
        self.assertIn("no $001 at 100", k.summary())


if __name__ == "__main__":
    unittest.main()
