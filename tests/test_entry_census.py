"""tools/census.py on made-up census files (no ROM needed): the counts by
entry and by how, the entries only run as C, the wait loops apart, and the
callers holding entries to the registry.

    python3 -m unittest discover tests
"""

import io
import pathlib
import tempfile
import unittest

from tools import census

FILE = """# census: how each registered entry was entered (dev/census.h)
machine 000500 console_vblank_stub stop 00FF10 console_vblank_vector 10
machine 00FF10 console_vblank_vector vector 00FF10 - 10
machine C05A93 event_run_object_script call C05A46 field_run_queued_turns 7
machine C05A93 event_run_object_script return C05A93 event_run_object_script 1
machine C0EC71 field_wait_for_frame_loop wait C0EC71 field_wait_for_frame_loop 500
c C05A93 event_run_object_script - C00097 field_frame 3
c 7E31BF scene_multiply - 7E398A scene_run_frame_tasks 40
"""


class EntryCensus(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        path = pathlib.Path(self.dir.name) / "census-a.txt"
        path.write_text(FILE)
        self.c = census.Census()
        self.c.read(path, "a")
        self.c.read(path, "b")

    def test_counts(self):
        script = ("C05A93", "event_run_object_script")
        self.assertEqual(self.c.machine[script], 16)
        self.assertEqual(self.c.by_kind[script, "call"], 14)
        self.assertEqual(self.c.as_c[script], 6)
        self.assertEqual(self.c.movies[script], {"a", "b"})

    def test_waits_apart(self):
        self.assertEqual(len(self.c.entered(waits=True)), 4)
        self.assertEqual(len(self.c.entered(waits=False)), 3)

    def test_report(self):
        out = io.StringIO()
        self.c.report(out, all_entries=True, top=10)
        text = out.getvalue()
        self.assertIn("4 entries entered by the machine (1 of them only as wait-loop passes)", text)
        self.assertIn("1 entries only run as C", text)
        self.assertIn("field_run_queued_turns C05A46", text)
        self.assertIn("wait loops still made on the machine: 1, passes 1000", text)


if __name__ == "__main__":
    unittest.main()
