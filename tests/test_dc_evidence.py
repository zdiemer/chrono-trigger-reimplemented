"""The evidence movie pool must bound actual game processes, not groups of them."""

import concurrent.futures
import pathlib
import tempfile
import threading
import unittest
from unittest import mock

from tools import dc_evidence


class EvidenceProcessBudget(unittest.TestCase):
    def test_two_workers_never_start_more_than_two_processes(self):
        barrier = threading.Barrier(2)
        lock = threading.Lock()
        active, peak, calls = 0, 0, []

        class Process:
            def __init__(self, command, **kwargs):
                nonlocal active, peak
                with lock:
                    active += 1
                    peak = max(peak, active)
                    calls.append(command)
                self.returncode = 1 if "--index-check" in command else 0

            def communicate(self):
                nonlocal active
                barrier.wait(timeout=10)
                with lock:
                    active -= 1
                return b"", None

        with tempfile.TemporaryDirectory() as tmp, \
                mock.patch.object(dc_evidence.subprocess, "Popen", Process), \
                mock.patch.object(dc_evidence, "same_trace", return_value=(True, "no divergence")), \
                concurrent.futures.ThreadPoolExecutor(2) as pool:
            out = pathlib.Path(tmp)
            results = list(pool.map(
                lambda name: dc_evidence.run_movie(
                    "ct_dev", "rom", out, (name, name + ".ctm", 1), [1], [2], [3]),
                ["first", "second"]))

        self.assertEqual(peak, 2)
        self.assertEqual(active, 0)
        self.assertEqual(len(calls), 8)
        for result in results:
            self.assertEqual(result[1], ["index"])
            self.assertTrue(result[2])


if __name__ == "__main__":
    unittest.main()
