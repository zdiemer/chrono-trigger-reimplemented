"""route_check's --resume: a run's directory is kept only for the same route,
mode, build and ROM that wrote its checkpoints."""
import os
import tempfile
import unittest

from tools.movies import route_check


class ResumeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.where = os.path.join(self.tmp.name, "run")
        self.drive = os.path.join(self.tmp.name, "ct_drive")
        self.rom = os.path.join(self.tmp.name, "rom.sfc")
        for path, data in ((self.drive, b"build one"), (self.rom, b"rom")):
            with open(path, "wb") as f:
                f.write(data)
        self.mode = route_check.parse_mode("on:on")

    def tearDown(self):
        self.tmp.cleanup()

    def stamp(self, route="telepod_lavos"):
        return route_check.run_stamp(route, self.mode, self.rom, self.drive)

    def leave_checkpoint(self):
        with open(os.path.join(self.where, "checkpoint"), "w") as f:
            f.write("x")

    def test_a_fresh_run_starts_empty(self):
        self.assertFalse(route_check.prepare(self.where, self.stamp(), resume=True))
        self.assertEqual(os.listdir(self.where), [route_check.STAMP])

    def test_the_same_run_resumes(self):
        route_check.prepare(self.where, self.stamp(), resume=True)
        self.leave_checkpoint()
        self.assertTrue(route_check.prepare(self.where, self.stamp(), resume=True))
        self.assertIn("checkpoint", os.listdir(self.where))

    def test_without_resume_it_starts_over(self):
        route_check.prepare(self.where, self.stamp(), resume=True)
        self.leave_checkpoint()
        self.assertFalse(route_check.prepare(self.where, self.stamp(), resume=False))
        self.assertNotIn("checkpoint", os.listdir(self.where))

    def test_another_build_starts_over(self):
        route_check.prepare(self.where, self.stamp(), resume=True)
        self.leave_checkpoint()
        with open(self.drive, "wb") as f:
            f.write(b"build two")
        self.assertFalse(route_check.prepare(self.where, self.stamp(), resume=True))
        self.assertNotIn("checkpoint", os.listdir(self.where))

    def test_another_route_starts_over(self):
        route_check.prepare(self.where, self.stamp(), resume=True)
        self.leave_checkpoint()
        self.assertFalse(route_check.prepare(self.where, self.stamp("beyond_time"), resume=True))


if __name__ == "__main__":
    unittest.main()
