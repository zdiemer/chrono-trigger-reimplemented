"""tools/drift.py: a run's timelines against the reference's, on made-up traces."""

import pathlib
import struct
import sys
import tempfile
import types
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))

import drift  # noqa: E402

WRAM, SRAM = 0x20000, 0x2000
# A made-up saved game: four cells of the party's records, and the place.
SAVED = types.SimpleNamespace(wram=[0x2600, 0x2601, 0x2602, 0x2603, 0x0100, 0x0101])
NAMES = {0x2600: "character_records", 0x2601: "character_records", 0x2602: "inventory_items",
         0x2603: "inventory_items"}


def write_trace(path, frames):
    """A .cttr of work RAM and save RAM: `frames` is [(input, {offset: byte})],
    each frame's writes on top of the last."""
    regions = [(0, b"WRAM", WRAM), (2, b"SRAM", SRAM)]
    producer = b"test"
    header = bytearray(b"CTTR") + struct.pack("<HHIHH", 1, 16 + 16 * len(regions) + len(producer), 0,
                                              len(regions), len(producer))
    for rid, name, size in regions:
        header += struct.pack("<BBHI8s", rid, 0, 0, size, name)
    header += producer
    out = bytearray(header)
    for n, (pad, writes) in enumerate(frames):
        body = struct.pack("<IHBBHHQ", n, pad, 1 if n == 0 else 0, 0, 0, 0, 0)
        if n == 0:
            wram = bytearray(WRAM)
            for o, v in writes.items():
                wram[o] = v
            body += struct.pack("<QI", 0, 1) + struct.pack("<II", 0, WRAM) + bytes(wram)
            body += struct.pack("<QI", 0, 1) + struct.pack("<II", 0, SRAM) + bytes(SRAM)
        else:
            body += struct.pack("<QI", 0, len(writes))
            for o, v in sorted(writes.items()):
                body += struct.pack("<II", o, 1) + bytes([v])
            body += struct.pack("<QI", 0, 0)
        out += struct.pack("<I", len(body)) + body
    path.write_bytes(bytes(out))


def play(length, changes, pad=None):
    """Frames with `changes` ({frame run: {offset: byte}}) and the input `pad`
    gives a frame ({frame run: buttons})."""
    pad = pad or {}
    return [(pad.get(n + 1, 0), changes.get(n + 1, {})) for n in range(length)]


class DriftTest(unittest.TestCase):
    def timeline(self, frames):
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "run.cttr"
            write_trace(path, frames)
            return drift.Timeline(path, SAVED)

    def test_reads_milestones_states_and_the_clock(self):
        t = self.timeline(play(40, {10: {0x0100: 0x05}, 20: {0x2600: 7}, 21: {0x0400: 3}, 30: {0x29AE: 0x22}}))
        self.assertEqual(t.events["place"], [(1, 0), (10, 5)])
        self.assertEqual(t.events["song"], [(1, 0), (30, 0x22)])
        self.assertEqual([f for f, _s in t.states], [1, 20])
        self.assertEqual((t.clock_at(20), t.clock_at(21)), (0, 3))

    def test_a_state_held_a_frame_is_a_step(self):
        t = self.timeline(play(40, {10: {0x2600: 1}, 11: {0x2600: 2}}))
        self.assertEqual([s[0] for _f, s in t.states], [0, 2])

    def test_the_same_play(self):
        frames = play(40, {10: {0x0100: 5}, 20: {0x2600: 7}})
        out = drift.classify(self.timeline(frames), self.timeline(frames), NAMES)
        self.assertEqual(out["verdict"], "same")

    def test_drift_a_load_late_and_a_press_on_another_moment(self):
        # the run enters the place 3 frames late, then its records change otherwise
        ref = self.timeline(play(60, {10: {0x0100: 5}, 40: {0x2600: 7}}, {38: 0x0080}))
        run = self.timeline(play(60, {13: {0x0100: 5}, 43: {0x2600: 9}}, {38: 0x0080}))
        out = drift.classify(ref, run, NAMES)
        self.assertEqual(out["verdict"], "drift")
        self.assertEqual((out["parting_frame"], out["d"]), (40, 3))
        self.assertEqual(out["set_by"].what, "place $005 entered")
        self.assertEqual(out["variables"], ["character_records"])
        self.assertEqual(out["press"], 35)       # the A at 38 meets the run's game where the reference's was at 35
        text = drift.report("m", out, ref, run, None)
        self.assertIn("shift in force: +3 frames, since place $005 entered lands +3 frames", text)

    def test_aligned_when_nothing_moved(self):
        ref = self.timeline(play(60, {10: {0x0100: 5}, 40: {0x2602: 1}}))
        run = self.timeline(play(60, {10: {0x0100: 5}, 40: {0x2602: 2}}))
        out = drift.classify(ref, run, NAMES)
        self.assertEqual(out["verdict"], "aligned")
        self.assertEqual(out["variables"], ["inventory_items"])

    def test_another_place(self):
        ref = self.timeline(play(60, {10: {0x0100: 5}}))
        run = self.timeline(play(60, {12: {0x0100: 6}}))
        out = drift.classify(ref, run, NAMES)
        self.assertEqual(out["parting_frame"], 10)
        self.assertIn("place $006 entered at frame 12, where the reference has place $005 entered", out["parting"])

    def test_a_run_that_ends_behind(self):
        ref = self.timeline(play(60, {10: {0x2600: 1}, 50: {0x2600: 2}}))
        run = self.timeline(play(60, {14: {0x2600: 1}}))
        out = drift.classify(ref, run, NAMES)
        self.assertEqual(out["verdict"], "drift")
        self.assertIn("ends before its last 1", out["why"])

    def test_location_triples_are_named(self):
        self.assertEqual(drift.cell_names({}, [0x0103, 0x0107, 0x2600]), ["location", "location_previous", "$7E:2600"])


if __name__ == "__main__":
    unittest.main()
