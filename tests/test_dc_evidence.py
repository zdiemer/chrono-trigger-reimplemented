"""Evidence workers must share the requested child-process budget."""
import concurrent.futures
import pathlib
import tempfile
import threading
import unittest
from unittest.mock import patch

from tools import dc_evidence


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
                patch.object(dc_evidence.subprocess, "Popen", Child), \
                patch.object(dc_evidence, "same_trace", return_value=(True, "no divergence")):
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
