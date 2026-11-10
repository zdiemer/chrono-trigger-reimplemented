"""route_check's --resume: a run's directory is kept only for the same route,
mode, build and ROM that wrote its checkpoints."""
import os
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools.movies import route_check
from tools.movies import ctdrv
from tools.movies import campaign, addresses


class NativeModeTest(unittest.TestCase):
    def test_native_control_probe_waits_for_the_script_to_release_the_party(self):
        run = campaign.Run.__new__(campaign.Run)
        run.machine = unittest.mock.Mock(native="native")
        run.machine.b.return_value = 0
        self.assertIsNone(run.find_player())
        run.machine.b.assert_called_once_with(addresses.FIELD_PLAYER_CONTROL)
        run.machine.mark.assert_not_called()
        run.machine.probe.assert_not_called()

    def test_abandoned_driver_is_stopped_by_its_process_handle(self):
        machine = ctdrv.Machine.__new__(ctdrv.Machine)
        machine.process = unittest.mock.Mock()
        machine.command_log = None
        machine.process.poll.return_value = None
        machine.process.wait.side_effect = [subprocess.TimeoutExpired("driver", 2), 1]
        machine._abort_if_running()
        machine.process.stdin.close.assert_called_once()
        machine.process.kill.assert_called_once()

    def test_mode_is_distinct_from_shipping(self):
        mode = route_check.parse_mode("native")
        self.assertEqual(mode, ("native", None))
        self.assertEqual(route_check.mode_name(mode), "native")
        self.assertNotEqual(mode, route_check.parse_mode("on:on"))

    def test_native_client_uses_server_and_named_files(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(ctdrv.subprocess, "Popen") as popen:
            ctdrv.Machine(native="native", drive="native.exe", rom="rom.sfc", sram="save.srm")
        command = popen.call_args.args[0]
        self.assertEqual(command, ["native.exe", "--server", "--assets", "native.assets",
                                   "--rom", "rom.sfc", "--sram", "save.srm"])
        self.assertNotIn("--generated", command)

    def test_shipping_client_keeps_its_protocol(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(ctdrv.subprocess, "Popen") as popen:
            ctdrv.Machine(native="on", drive="drive.exe", rom="rom.sfc", sram="save.srm")
        self.assertEqual(popen.call_args.args[0],
                         ["drive.exe", "--native", "on", "--generated", "on", "rom.sfc", "save.srm"])

    def test_runner_passes_native_mode_and_records_the_outcome(self):
        def start(command, **kwargs):
            self.assertIn("native.exe", command)
            self.assertEqual(command[command.index("--native") + 1], "native")
            self.assertNotIn("--generated", command)
            kwargs["stdout"].write("checkpoint room frame 12\noutcome reached test frame 20\n")
            process = unittest.mock.Mock()
            process.wait.return_value = 0
            return process
        with tempfile.TemporaryDirectory() as where, patch.object(route_check, "run_stamp", return_value="test"), \
                patch.object(route_check.subprocess, "Popen", side_effect=start):
            result = route_check.run_one("test", ("native", None), where, "rom", "native.exe", 10)
        self.assertTrue(result["passed"])
        self.assertEqual(result["frame"], 20)
        self.assertEqual(result["phases"], [("room", 12)])

    def test_timeout_stops_the_route_process_tree(self):
        with tempfile.TemporaryDirectory() as where, patch.object(route_check, "run_stamp", return_value="test"), \
                patch.object(route_check.subprocess, "Popen") as start, \
                patch.object(route_check.subprocess, "run") as kill, \
                patch.object(route_check.os, "killpg", create=True) as killpg:
            start.return_value.pid = 12345
            start.return_value.wait.side_effect = [subprocess.TimeoutExpired("route", 1), 1]
            result = route_check.run_one("test", ("native", None), where, "rom", "native.exe", 1)
            if os.name == "nt":
                self.assertEqual(kill.call_args.args[0], ["taskkill", "/PID", "12345", "/T", "/F"])
            else:
                killpg.assert_called_once_with(12345, route_check.signal.SIGKILL)
        self.assertFalse(result["passed"])
        self.assertIn("timed out", result["what"])


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
