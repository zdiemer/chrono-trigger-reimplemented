"""Recorded outcomes on synthetic movies, logs and memory; no ROM."""
import pathlib
import io
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
import outcomes as o


class OutcomesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.movie = self.root / "oracle/movies/test.ctm"
        self.movie.parent.mkdir(parents=True)
        self.movie.write_text("# frames 600\n")
        self.end = self.root / "end.bin"
        self.end.write_bytes(bytes(0x22000))
        self.saved = SimpleNamespace(wram=[0, 1, 10], sram=[0, 1, 10])
        self.text = "location: frame 1 $000\nlocation: frame 400 $001\n"
        self.text += "".join("outcome-picture: %d %016x\n" % (f, f) for f in sorted(o.picture_frames(600)))
        o.capture(self.root, "test", self.movie, 600, self.text, 0, self.end, self.saved, "reference")
        self.run = o.read(self.root / "outcome-test.txt")
        self.ref = {k: v[:] for k, v in self.run.items() if k[1] != "run"}

    def exception(self, kind):
        return {("test", "strict", kind): (o.observed_digest(self.run["test", kind]), "known difference")}

    def test_complete_record_and_round_trip(self):
        o.validate(self.run, "test", self.movie, 600, "reference")
        o.validate(self.ref, "test", self.movie, 600)
        result = o.compare(self.ref, self.run, "test", 600)
        self.assertTrue(all(h == "match" for items in result.values() for h in items.values()))
        o.write(self.root / "ledger", self.ref, "# test")
        self.assertEqual(o.read(self.root / "ledger"), self.ref)

    def test_windows_log_line_endings(self):
        o.capture(self.root, "test", self.movie, 600, self.text.replace("\n", "\r\n"),
                  0, self.end, self.saved, "reference")
        self.assertEqual(o.read(self.root / "outcome-test.txt"), self.run)

    def test_two_frames_is_drift_three_is_difference(self):
        self.run["test", "pictures"] = [t if not t.startswith("300:") else "300:ffffffffffffffff"
                                        for t in self.run["test", "pictures"]]
        self.run["test", "pictures"] = [t if not t.startswith("302:") else "302:%016x" % 300
                                        for t in self.run["test", "pictures"]]
        self.assertEqual(o.compare(self.ref, self.run, "test", 600)["pictures"]["300"], "drift")
        self.run["test", "pictures"] = [t for t in self.run["test", "pictures"] if not t.startswith("302:")]
        self.assertEqual(o.compare(self.ref, self.run, "test", 600)["pictures"]["300"], "differ")

    def test_places_order_extra_and_timing(self):
        self.run["test", "places"] = ["1:0", "430:1", "450:2"]
        self.assertEqual(o.compare(self.ref, self.run, "test", 600)["places"],
                         {"0": "match", "1": "drift", "2": "differ"})
        self.run["test", "places"] = ["1:2", "400:1"]
        self.assertEqual(set(o.compare(self.ref, self.run, "test", 600)["places"].values()), {"differ"})

    def test_only_selected_end_bytes_affect_hash(self):
        state = bytearray(self.end.read_bytes())
        state[2] = 1  # unselected/timed cells do not affect the saved projection
        self.end.write_bytes(state)
        o.capture(self.root, "test", self.movie, 600, self.text, 0, self.end, self.saved, "strict")
        run = o.read(self.root / "outcome-test.txt")
        self.assertEqual(run["test", "game"], self.ref["test", "game"])
        state[1] = 1
        self.end.write_bytes(state)
        o.capture(self.root, "test", self.movie, 600, self.text, 0, self.end, self.saved, "strict")
        run = o.read(self.root / "outcome-test.txt")
        self.assertNotEqual(run["test", "game"], self.ref["test", "game"])
        self.assertEqual(run["test", "sram"], self.ref["test", "sram"])

    def test_missing_truncated_faulted_stale_rejected(self):
        for key, value in [("game", []), ("pictures", []), ("places", []),
                           ("run", ["reference", "1", "0"]), ("run", ["reference", "0", "1"])]:
            run = dict(self.run)
            run["test", key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                o.validate(run, "test", self.movie, 600, "reference")
        self.movie.write_text("# frames 600\n1 A\n")
        with self.assertRaises(ValueError):
            o.validate(self.ref, "test", self.movie, 600)

    def test_exception_cannot_waive_a_protected_match(self):
        result = {"pictures": {"1": "match", "300": "differ"}}
        base = {("test/strict", "pictures"): ["1", "300"]}
        exc = self.exception("pictures")
        self.assertTrue(o.ratchet(result, self.run, "test", "strict", base, exc, True))
        self.assertEqual(base["test/strict", "pictures"], ["1", "300"])

    def test_update_is_per_identity_and_preserves_omitted_movies(self):
        base = {("other/strict", "game"): ["end"]}
        result = {"pictures": {"1": "match", "300": "drift", "600": "differ"}}
        exc = self.exception("pictures")
        self.assertFalse(o.ratchet(result, self.run, "test", "strict", base, exc, True))
        self.assertEqual(base["test/strict", "pictures"], ["1", "300"])
        self.assertEqual(base["other/strict", "game"], ["end"])
        self.assertTrue(o.ratchet(result, self.run, "test", "native", base, exc))

    def test_exception_pins_black_frames_deleted_visits_and_changed_states(self):
        # Existing differences used to allow any further damage to these kinds.
        self.run["test", "game"] = ["a" * 64]
        self.run["test", "sram"] = ["b" * 64]
        self.run["test", "pictures"] = ["%d:%016x" % (f, f + 1000)
                                        for f in sorted(o.picture_frames(600))]
        self.run["test", "places"] = ["1:0", "450:1", "500:2"]
        result = o.compare(self.ref, self.run, "test", 600)
        exc = {key: value for kind in result for key, value in self.exception(kind).items()}
        base = {}
        self.assertFalse(o.ratchet(result, self.run, "test", "strict", base, exc, True))
        mutations = {"game": ["c" * 64], "sram": ["d" * 64], "places": ["1:0"],
                     "pictures": ["%d:0000000000000000" % f for f in sorted(o.picture_frames(600))]}
        for kind, values in mutations.items():
            run = dict(self.run)
            run["test", kind] = values
            changed = o.compare(self.ref, run, "test", 600)
            for update in (False, True):
                with self.subTest(kind=kind, update=update):
                    failures = o.ratchet(changed, run, "test", "strict", dict(base), exc, update)
                    self.assertTrue(any(kind + " changed beyond" in f for f in failures))
        # Fixing every difference in a kind is accepted and --update protects it.
        improved = dict(self.run)
        improved["test", "game"] = self.ref["test", "game"]
        self.assertFalse(o.ratchet(o.compare(self.ref, improved, "test", 600), improved,
                                   "test", "strict", base, exc, True))
        self.assertEqual(base["test/strict", "game"], ["end"])
        self.assertTrue(o.ratchet(result, self.run, "test", "strict", base, exc))

    def test_malformed_duplicate_rows_and_reasonless_exception(self):
        path = self.root / "bad"
        for text in ("test game a\ntest game b\n", "test\n"):
            path.write_text(text)
            with self.assertRaises(ValueError):
                o.read(path)
        path.write_text("test strict pictures\n")
        with self.assertRaises(ValueError):
            o.exceptions(path)
        for line in ("test strict pictures # unbounded", "test strict pictures " + "a" * 64):
            path.write_text(line)
            with self.assertRaises(ValueError):
                o.exceptions(path)

    def test_boundaries_and_duplicate_picture_frame(self):
        self.assertEqual(o.picture_frames(1), {1})
        self.assertEqual(o.checkpoints(300), [1, 300])
        with self.assertRaises(ValueError):
            o.pairs(["1:0000000000000000", "1:0000000000000000"], True)

    def test_record_merges_subset_and_failure_leaves_ledger_alone(self):
        ledger = self.root / "ledger"
        o.write(ledger, {("other", "game"): ["kept"]}, "# keep")
        self.movie.with_name("other.ctm").write_text("# frames 600\n")
        command = ["--record", "--reference", str(self.root), "--ledger", str(ledger), "--movies", "test"]
        with patch.object(o, "ROOT", self.root):
            self.assertEqual(o.main(command), 0)
            self.assertEqual(o.read(ledger)["other", "game"], ["kept"])
            before = ledger.read_bytes()
            self.run["test", "run"] = ["reference", "1", "0"]
            o.write(self.root / "outcome-test.txt", self.run, "# fault")
            self.assertEqual(o.main(command), 1)
            self.assertEqual(ledger.read_bytes(), before)

    def test_artifact_check_runs_no_executable(self):
        ledger, baseline, exc = [self.root / name for name in ("ledger", "baseline", "exceptions")]
        o.write(ledger, self.ref, "# reference")
        self.run["test", "run"] = ["strict", "0", "0"]
        o.write(self.root / "outcome-test.txt", self.run, "# strict")
        result = o.compare(self.ref, self.run, "test", 600)
        base = {}
        o.ratchet(result, self.run, "test", "strict", base, {}, True)
        o.write(baseline, base, "# ratchet")
        exc.write_text("# no exceptions\n")
        with patch.object(o, "ROOT", self.root), \
                patch("subprocess.run", side_effect=AssertionError("must not run anything")):
            self.assertEqual(o.main(["--check", "--runs", str(self.root), "--mode", "strict",
                                     "--ledger", str(ledger), "--baseline", str(baseline),
                                     "--exceptions", str(exc), "--movies", "test"]), 0)

    def test_recorded_membership_checked_before_filtering_and_without_movies(self):
        ledger, baseline, exc = [self.root / name for name in ("ledger", "baseline", "exceptions")]
        reference = dict(self.ref)
        reference.update({("other", kind): values for (_name, kind), values in self.ref.items()})
        o.write(ledger, reference, "# recorded")
        self.movie.with_name("other.ctm").write_text("# frames 600\n")
        other = {("other", kind): values for (_name, kind), values in self.run.items()}
        other["other", "run"] = ["strict", "0", "0"]
        o.write(self.root / "outcome-other.txt", other, "# intact other movie")
        base = {}
        o.ratchet(o.compare(reference, other, "other", 600), other, "other", "strict", base, {}, True)
        o.write(baseline, base, "# protected other movie")
        exc.write_text("# none\n")
        command = ["--check", "--runs", str(self.root), "--mode", "strict", "--ledger", str(ledger),
                   "--baseline", str(baseline), "--exceptions", str(exc)]
        read_text = pathlib.Path.read_text
        for header in (None, "# frames broken\n", "# frames 600garbage\n", "# frames 0\n",
                       "# frames 600\n# frames 2\n", "unreadable"):
            if header is None:
                self.movie.unlink()
            else:
                self.movie.write_text(header)
            def read(path, *args, **kwargs):
                if header == "unreadable" and path == self.movie:
                    raise PermissionError("test movie unreadable")
                return read_text(path, *args, **kwargs)
            for selection in ([], ["--movies", "other"]):
                with self.subTest(header=header, selection=selection), patch.object(o, "ROOT", self.root), \
                        patch.object(pathlib.Path, "read_text", read), \
                        patch("sys.stderr", new_callable=io.StringIO) as errors:
                    self.assertEqual(o.main(command + selection), 1)
                    self.assertIn("test", errors.getvalue())
                    self.assertRegex(errors.getvalue(), "missing recorded|malformed|unreadable")

    def test_failure_flushes_prior_movie_output_before_stderr(self):
        ledger, baseline, exc = [self.root / name for name in ("ledger", "baseline", "exceptions")]
        o.write(ledger, self.ref, "# reference")
        self.movie.with_name("zmissing.ctm").write_text("# frames 600\n")
        self.run["test", "run"] = ["strict", "0", "0"]
        o.write(self.root / "outcome-test.txt", self.run, "# strict")
        base = {}
        o.ratchet(o.compare(self.ref, self.run, "test", 600), self.run, "test", "strict", base, {}, True)
        o.write(baseline, base, "# ratchet")
        exc.write_text("# none\n")
        events = []

        class Buffered(io.StringIO):
            def flush(self):
                events.append(self.getvalue())
                self.seek(0)
                self.truncate()

        class Errors(io.StringIO):
            def write(self, text):
                events.append(text)
                return len(text)

        with patch.object(o, "ROOT", self.root), patch("sys.stdout", Buffered()), patch("sys.stderr", Errors()):
            rc = o.main(["--check", "--runs", str(self.root), "--mode", "strict", "--ledger", str(ledger),
                         "--baseline", str(baseline), "--exceptions", str(exc)])
        self.assertEqual(rc, 1)
        self.assertIn("test strict:", events[0])
        self.assertIn("outcomes: FAILED:", "".join(events).splitlines()[-1])


if __name__ == "__main__":
    unittest.main()
