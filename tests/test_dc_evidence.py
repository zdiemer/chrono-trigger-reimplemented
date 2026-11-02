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


class EvidenceChildren(unittest.TestCase):
    def test_two_workers_run_at_most_two_children_and_keep_all_results(self):
        lock = threading.Lock()
        together = threading.Barrier(2)
        active = peak = 0
        started = []

        class Child:
            def __init__(self, args, **kwargs):
                nonlocal active, peak
                self.args = args
                self.returncode = 7 if "--poison-return" in args else 0
                with lock:
                    started.append(args)
                    active += 1
                    peak = max(peak, active)

            def __enter__(self):
                return self

            def __exit__(self, *args):
                pass

            def communicate(self, *args, **kwargs):
                nonlocal active
                together.wait(timeout=10)
                with lock:
                    active -= 1
                return (b"poison-site: C20000 calls 13, V inverted 11\n"
                        b"index-check: C20002 runs 7, largest X $000A, past $FE 0\n"
                        b"poison-return: C20003 returns 12 (4 to a bridge), C inverted 12\n"), None

            def poll(self):
                return self.returncode

        with tempfile.TemporaryDirectory() as tmp, \
                mock.patch.object(dc_evidence.subprocess, "Popen", Child), \
                mock.patch.object(dc_evidence, "same_trace", return_value=(True, "no divergence")):
            out = pathlib.Path(tmp)
            def movie(name):
                return dc_evidence.run_movie("ct_dev", "test.sfc", out, (name, name + ".ctm", 10),
                                             [0xC20000], [0xC20002], [0xC20003])
            with concurrent.futures.ThreadPoolExecutor(2) as pool:
                results = list(pool.map(movie, ("first", "second")))

        self.assertEqual(peak, 2)
        self.assertEqual(active, 0)
        self.assertEqual(len(started), 8)
        for name, failed, same, tail, poisoned, index, returned in results:
            self.assertEqual(failed, ["returns"])
            self.assertTrue(same)
            self.assertEqual(poisoned, {0xC20000: 11})
            self.assertEqual(index, {0xC20002: (7, 10, 0)})
            self.assertEqual(returned, {0xC20003: (12, 4)})


if __name__ == "__main__":
    unittest.main()
