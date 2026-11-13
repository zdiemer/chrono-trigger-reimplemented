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
        self.movie = self.root / "movies/test.ctm"
        self.movie.parent.mkdir(parents=True)
        self.movie.write_text("# frames 600\n")
        self.end = self.root / "end.bin"
        self.end.write_bytes(bytes(0x22000))
        self.saved = SimpleNamespace(wram=[0, 1, 10], sram=[0, 1, 10])
        self.text = "location: frame 1 $000\nlocation: frame 400 $001\n"
        o.capture(self.root, "test", self.movie, 600, self.text, 0, self.end, self.saved, "native")
        self.run = o.read(self.root / "outcome-test.txt")
        self.ref = {k: v[:] for k, v in self.run.items() if k[1] != "run"}

    def exception(self, kind):
        return {("test", "native", kind): (o.observed_digest(self.run["test", kind]), "known difference")}

    def test_complete_record_and_round_trip(self):
        o.validate(self.run, "test", self.movie, 600, "native")
        o.validate(self.ref, "test", self.movie, 600)
        result = o.compare(self.ref, self.run, "test", 600)
        self.assertTrue(all(h == "match" for items in result.values() for h in items.values()))
        o.write(self.root / "ledger", self.ref, "# test")
        self.assertEqual(o.read(self.root / "ledger"), self.ref)

    def test_windows_log_line_endings(self):
        o.capture(self.root, "test", self.movie, 600, self.text.replace("\n", "\r\n"),
                  0, self.end, self.saved, "native")
        self.assertEqual(o.read(self.root / "outcome-test.txt"), self.run)


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
        o.capture(self.root, "test", self.movie, 600, self.text, 0, self.end, self.saved, "native")
        run = o.read(self.root / "outcome-test.txt")
        self.assertEqual(run["test", "game"], self.ref["test", "game"])
        state[1] = 1
        self.end.write_bytes(state)
        o.capture(self.root, "test", self.movie, 600, self.text, 0, self.end, self.saved, "native")
        run = o.read(self.root / "outcome-test.txt")
        self.assertNotEqual(run["test", "game"], self.ref["test", "game"])
        self.assertEqual(run["test", "sram"], self.ref["test", "sram"])

    def test_missing_truncated_faulted_stale_rejected(self):
        for key, value in [("game", []), ("places", []), ("places", []),
                           ("run", ["native", "1", "0"]), ("run", ["native", "0", "1"])]:
            run = dict(self.run)
            run["test", key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                o.validate(run, "test", self.movie, 600, "native")
        self.movie.write_text("# frames 600\n1 A\n")
        with self.assertRaises(ValueError):
            o.validate(self.ref, "test", self.movie, 600)

    def test_exception_cannot_waive_a_protected_match(self):
        result = {"places": {"0": "match", "1": "differ"}}
        base = {("test/native", "places"): ["0", "1"]}
        exc = self.exception("places")
        self.assertTrue(o.ratchet(result, self.run, "test", "native", base, exc, True))
        self.assertEqual(base["test/native", "places"], ["0", "1"])

    def test_update_is_per_identity_and_preserves_omitted_movies(self):
        base = {("other/native", "game"): ["end"]}
        result = {"places": {"0": "match", "1": "drift", "2": "differ"}}
        exc = self.exception("places")
        self.assertFalse(o.ratchet(result, self.run, "test", "native", base, exc, True))
        self.assertEqual(base["test/native", "places"], ["0", "1"])
        self.assertEqual(base["other/native", "game"], ["end"])
        self.assertFalse(o.ratchet(result, self.run, "test", "native", base, exc))

    def test_exception_pins_black_frames_deleted_visits_and_changed_states(self):
        # Existing differences used to allow any further damage to these kinds.
        self.run["test", "game"] = ["a" * 64]
        self.run["test", "sram"] = ["b" * 64]
        self.run["test", "places"] = ["1:0", "450:1", "500:2"]
        result = o.compare(self.ref, self.run, "test", 600)
        exc = {key: value for kind in result for key, value in self.exception(kind).items()}
        base = {}
        self.assertFalse(o.ratchet(result, self.run, "test", "native", base, exc, True))
        mutations = {"game": ["c" * 64], "sram": ["d" * 64], "places": ["1:0"]}
        for kind, values in mutations.items():
            run = dict(self.run)
            run["test", kind] = values
            changed = o.compare(self.ref, run, "test", 600)
            for update in (False, True):
                with self.subTest(kind=kind, update=update):
                    failures = o.ratchet(changed, run, "test", "native", dict(base), exc, update)
                    self.assertTrue(any(kind + " changed beyond" in f for f in failures))
        # Fixing every difference in a kind is accepted and --update protects it.
        improved = dict(self.run)
        improved["test", "game"] = self.ref["test", "game"]
        self.assertFalse(o.ratchet(o.compare(self.ref, improved, "test", 600), improved,
                                   "test", "native", base, exc, True))
        self.assertEqual(base["test/native", "game"], ["end"])
        self.assertTrue(o.ratchet(result, self.run, "test", "native", base, exc))

    def test_malformed_duplicate_rows_and_reasonless_exception(self):
        path = self.root / "bad"
        for text in ("test game a\ntest game b\n", "test\n"):
            path.write_text(text)
            with self.assertRaises(ValueError):
                o.read(path)
        path.write_text("test native places\n")
        with self.assertRaises(ValueError):
            o.exceptions(path)
        for line in ("test native places # unbounded", "test native places " + "a" * 64):
            path.write_text(line)
            with self.assertRaises(ValueError):
                o.exceptions(path)



    def test_a_carried_fight_needs_no_exception_but_keeps_its_protected_matches(self):
        self.run["test", "game"] = ["a" * 64]
        self.run["test", "places"] = ["1:0", "400:1", "900:0"]
        result = o.compare(self.ref, self.run, "test", 600)
        base = {("test/native", "places"): ["0", "1"], ("test/native", "game"): [],
                ("test/native", "sram"): ["end"]}
        self.assertTrue(o.ratchet(result, self.run, "test", "native", base, {}))
        self.assertFalse(o.ratchet(result, self.run, "test", "native", base, {}, carried=True))
        failures = o.ratchet(result, self.run, "test", "native", base, self.exception("game"), carried=True)
        self.assertTrue(any("delete it" in f for f in failures))
        self.run["test", "places"] = ["1:0", "520:1"]
        lost = o.compare(self.ref, self.run, "test", 600)
        self.assertTrue(any("places regressed" in f for f in
                            o.ratchet(lost, self.run, "test", "native", base, {}, carried=True)))

    def test_fight_routes_name_known_routes_with_reasons(self):
        routes = self.root / "routes"
        routes.mkdir()
        (routes / "duel.py").write_text("")
        (routes / "_helper.py").write_text("")
        path = self.root / "fights"
        path.write_text("# header\nbattle-duel duel  # won, field in hand\n")
        self.assertEqual(o.fight_routes(path, routes), {"battle-duel": "duel"})
        self.assertEqual(o.fight_retries(path, routes), {"duel": 0})
        path.write_text("battle-duel duel retries=3  # a coin-flip boss\n")
        self.assertEqual(o.fight_retries(path, routes), {"duel": 3})
        for text in ("battle-duel duel\n", "battle-duel _helper  # a helper\n",
                     "battle-duel nowhere  # missing\n", "a duel  # x\na duel  # twice\n",
                     "battle-duel duel retries=many  # bad\n", "battle-duel duel 3  # bad\n"):
            path.write_text(text)
            with self.subTest(text=text), self.assertRaises(ValueError):
                o.fight_routes(path, routes)
        self.assertTrue(o.fight_routes())

    def test_artifact_check_runs_no_executable(self):
        ledger, baseline, exc = [self.root / name for name in ("ledger", "baseline", "exceptions")]
        o.write(ledger, self.ref, "# reference")
        self.run["test", "run"] = ["native", "0", "0"]
        o.write(self.root / "outcome-test.txt", self.run, "# strict")
        result = o.compare(self.ref, self.run, "test", 600)
        base = {}
        o.ratchet(result, self.run, "test", "native", base, {}, True)
        o.write(baseline, base, "# ratchet")
        exc.write_text("# no exceptions\n")
        with patch.object(o, "ROOT", self.root), \
                patch("subprocess.run", side_effect=AssertionError("must not run anything")):
            self.assertEqual(o.main(["--check", "--runs", str(self.root),
                                     "--ledger", str(ledger), "--baseline", str(baseline),
                                     "--exceptions", str(exc), "--movies", "test"]), 0)

    def test_recorded_membership_checked_before_filtering_and_without_movies(self):
        ledger, baseline, exc = [self.root / name for name in ("ledger", "baseline", "exceptions")]
        reference = dict(self.ref)
        reference.update({("other", kind): values for (_name, kind), values in self.ref.items()})
        o.write(ledger, reference, "# recorded")
        self.movie.with_name("other.ctm").write_text("# frames 600\n")
        other = {("other", kind): values for (_name, kind), values in self.run.items()}
        other["other", "run"] = ["native", "0", "0"]
        o.write(self.root / "outcome-other.txt", other, "# intact other movie")
        base = {}
        o.ratchet(o.compare(reference, other, "other", 600), other, "other", "native", base, {}, True)
        o.write(baseline, base, "# protected other movie")
        exc.write_text("# none\n")
        command = ["--check", "--runs", str(self.root), "--ledger", str(ledger),
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



if __name__ == "__main__":
    unittest.main()
