"""The GBA census's logic (targets/gba/host/census.py), with no cartridge: the
pictures compared and their mismatches told apart, gathered into clusters, and
the pacing read from the runner's index into slow frames, frames left out,
stretches and hangs."""
import importlib.util
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "targets/gba/host"))
spec = importlib.util.spec_from_file_location("gba_census", ROOT / "targets/gba/host/census.py")
census = importlib.util.module_from_spec(spec)
spec.loader.exec_module(census)

W, H = census.WIDTH, census.HEIGHT


def picture(colour=(0, 0, 0)):
    return bytearray(bytes(colour) * (W * H))


def paint(p, x0, y0, x1, y1, colour):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            p[(y * W + x) * 3:(y * W + x) * 3 + 3] = bytes(colour)
    return p


class PicturesTest(unittest.TestCase):
    def test_png_round_trip(self):
        rgb = bytes(i % 256 for i in range(4 * 3 * 3))
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "p.png"
            census.png_write(path, 4, 3, rgb)
            self.assertEqual(census.png_read(path), (4, 3, rgb))

    def test_colour_depths(self):
        # 8-bit channels to the GBA's 5 bits, the runner's 15-bit pixels (red low) to the same
        self.assertEqual(census.five_bits(bytes([0, 7, 8, 255])), bytes([0, 0, 1, 31]))
        self.assertEqual(census.from_15((1 | 2 << 5 | 31 << 10).to_bytes(2, "little")), bytes([1, 2, 31]))
        self.assertEqual(census.eight_bits(bytes([0, 31, 16])), bytes([0, 255, 132]))


class CompareTest(unittest.TestCase):
    def test_same(self):
        self.assertIsNone(census.compare(bytes(picture()), bytes(picture())))

    def test_where_it_differs(self):
        for way in (census.compare_slowly, census.compare_arrays) if census.numpy else (census.compare_slowly,):
            self.where_it_differs(way)

    def test_both_ways_the_same(self):
        if not census.numpy:
            self.skipTest("no numpy")
        import random
        rng = random.Random(5)
        a = picture((4, 4, 4))
        for _ in range(40):
            x, y = rng.randrange(230), rng.randrange(150)
            paint(a, x, y, x + rng.randrange(9), y + rng.randrange(9), (rng.randrange(32), rng.randrange(32), 3))
        b = bytearray(a)
        for _ in range(40):
            x, y = rng.randrange(230), rng.randrange(150)
            paint(b, x, y, x + rng.randrange(9), y + rng.randrange(9), (rng.randrange(32), 7, rng.randrange(32)))
        self.assertEqual(census.compare_slowly(bytes(a), bytes(b), W, H), census.compare_arrays(bytes(a), bytes(b), W, H))

    def where_it_differs(self, compare):
        a = picture()
        b = paint(picture(), 10, 20, 12, 21, (20, 0, 0))
        d = compare(bytes(b), bytes(a), W, H)["picture"]   # the GBA shows a square ct_native doesn't
        self.assertEqual(d["pixels"], 6)
        self.assertEqual(d["box"], [10, 20, 12, 21])
        self.assertEqual(d["steps"], 20)
        self.assertEqual(d["cells"], [(20 // 8) * 30 + 1])

    def kinds(self, gba, native, before):
        found = census.mismatches_of(gba, native, before, None, census.compare(gba, native))
        return [(kind, part["pixels"]) for kind, part in found]

    def test_kinds(self):
        native = bytes(paint(picture(), 0, 0, 239, 79, (10, 10, 10)))
        before = bytes(picture())
        # every line the frame's or the one before's: torn
        torn = bytes(paint(picture(), 0, 40, 239, 79, (10, 10, 10)))
        self.assertEqual(self.kinds(torn, native, before), [("torn", 240 * 40)])
        # the whole picture the frame before's
        self.assertEqual(self.kinds(before, native, before), [("frame", 240 * 80)])
        # a step off: shade
        dim = bytes(paint(picture(), 0, 0, 239, 79, (9, 10, 10)))
        self.assertEqual(self.kinds(dim, native, before), [("colour", 240 * 80)])
        # and a stray square on top of it, whose colour is elsewhere on its lines unchanged
        stray = bytes(paint(bytearray(dim), 100, 100, 110, 110, (31, 0, 0)))
        self.assertEqual(self.kinds(stray, native, before), [("picture", 121), ("colour", 240 * 80)])
        # more than SHADE_STEPS off
        far = bytes(paint(bytearray(dim), 0, 0, 9, 0, (10, 10, 10 - census.SHADE_STEPS - 1)))
        self.assertEqual(self.kinds(far, native, before), [("picture", 10), ("colour", 240 * 80 - 10)])



def mismatch(frame, box, pixels=10, kind="picture", cells=(0,)):
    return {"frame": frame, "gba": frame + 100, "box": box, "pixels": pixels, "cells": list(cells), "steps": 5, "kind": kind}


class ClusterTest(unittest.TestCase):
    def test_close_in_time_and_cells(self):
        clusters = census.cluster([
            mismatch(10, [0, 0, 7, 7]), mismatch(12, [4, 4, 9, 9], pixels=50),
            mismatch(13, [200, 150, 239, 159], cells=(599,)),   # elsewhere on the screen
            mismatch(40, [0, 0, 7, 7]),                  # later than the gap
            mismatch(41, [0, 0, 7, 7], kind="colour"),   # another kind
        ])
        self.assertEqual([(c["kind"], c["first"], c["last"], c["gba_frames"]) for c in clusters],
                         [("picture", 10, 12, 2), ("picture", 13, 13, 1), ("picture", 40, 40, 1), ("colour", 41, 41, 1)])
        self.assertEqual(clusters[0]["visibility"], 60)
        self.assertEqual(clusters[0]["box"], [0, 0, 9, 9])
        self.assertEqual(clusters[0]["worst"]["frame"], 12)
        self.assertEqual(clusters[0]["typical"]["frame"], 12)


class PacingTest(unittest.TestCase):
    def test_slow_and_left_out(self):
        # (GBA frame, frames begun, frame shown, picture)
        index = [(1, 1, 0, 0), (2, 2, 1, 0), (3, 3, 2, 0), (4, 3, 2, 0), (5, 3, 2, 0), (6, 5, 3, 0),
                 (7, 6, 5, 0), (8, 7, 6, 0), (9, 8, 7, 0)]
        took, left_out, hangs = census.pacing(index)
        self.assertEqual(took[3], 3)          # frame 3 began in GBA frame 3 and frame 4 in 6
        self.assertEqual(took[4], 0)          # began in the same GBA frame as frame 5
        self.assertEqual(left_out, [4])       # its picture never showed
        self.assertEqual(hangs, [])
        stretches = census.stretches(took, left_out)
        self.assertEqual(len(stretches), 1)
        s = stretches[0]
        self.assertEqual((s["first"], s["last"], s["slow"], s["left_out"], s["longest"], s["cost"]), (3, 4, 1, 1, 3, 1))

    def test_hang(self):
        index = [(n, 5 if n < 300 else n - 294, 4, 0) for n in range(1, 400)]
        took, left_out, hangs = census.pacing(index)
        self.assertEqual(len(hangs), 1)
        self.assertEqual((hangs[0]["gba_from"], hangs[0]["gba_to"], hangs[0]["frame"]), (1, 300, 5))


class ClosestTest(unittest.TestCase):
    def test_closest_picture(self):
        a = bytes(picture())
        b = bytes(paint(picture(), 0, 0, 239, 9, (5, 5, 5)))
        c = bytes(paint(picture(), 0, 0, 239, 99, (5, 5, 5)))
        where = {census.zlib.crc32(a): [100], census.zlib.crc32(c): [105]}
        lines = {100: census.line_crcs(a), 105: census.line_crcs(c)}
        self.assertEqual(census.closest_picture(a, 102, where, lines), (True, 100))
        self.assertEqual(census.closest_picture(b, 102, where, lines), (False, 100))   # 150 of its lines
        self.assertEqual(census.closest_picture(a, 102 + census.UNMATCHED_WINDOW + 1, where, lines)[0], False)


if __name__ == "__main__":
    unittest.main()
