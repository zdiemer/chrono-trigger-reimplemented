"""The gate's sample of the movie library (tools/gate_movies.txt, read by
tools/suite.py): every line names a movie in the library and says why it is
there, so the default gate can't silently run fewer movies than it lists.
Needs no ROM.
"""
import unittest
import pathlib
import tempfile
from unittest.mock import patch

from tools import suite


class NativeRoutesTest(unittest.TestCase):
    def test_exceptions_have_known_names_and_reasons(self):
        suite.native_route_exceptions()

    def test_invalid_or_duplicate_exceptions_fail(self):
        with tempfile.TemporaryDirectory() as where:
            path = pathlib.Path(where) / "exceptions.txt"
            for text in ("no_such_route # reason", "beyond_time", "beyond_time #",
                         "beyond_time # first\nbeyond_time # second"):
                path.write_text(text)
                with self.subTest(text=text), self.assertRaises(ValueError):
                    suite.native_route_exceptions(path)

    def test_full_route_exception_does_not_exempt_fast_prefix(self):
        with tempfile.TemporaryDirectory() as where:
            path = pathlib.Path(where) / "exceptions.txt"
            path.write_text("beyond_time # ending diverges\n")
            exceptions = suite.native_route_exceptions(path)
            self.assertNotIn("beyond_time:form2", exceptions)

    def test_long_gate_keeps_the_fast_prefix_of_an_excepted_ending(self):
        selected = suite.native_route_selection(True, {"beyond_time": "ending timeout"})
        self.assertIn("beyond_time", selected)  # Still reported with its reason.
        self.assertIn("beyond_time:form2", selected)
        self.assertIn("telepod_lavos", selected)
        self.assertNotIn("telepod_lavos:shell", selected)  # Its full run covers it.

    def test_both_excepted_endings_keep_their_passing_prefixes(self):
        exceptions = {"beyond_time": "ending timeout", "telepod_lavos": "ending not run"}
        selected = suite.native_route_selection(True, exceptions)
        runnable = [route for route in selected if route not in exceptions]
        self.assertTrue(set(suite.FAST_ROUTES).issubset(runnable))
        self.assertTrue(set(exceptions).issubset(selected))  # Reasons remain reportable.


class GateMoviesTest(unittest.TestCase):
    def test_every_line_names_a_library_movie_with_its_reason(self):
        names, bad = suite.gate_movies()
        self.assertEqual(bad, [])
        self.assertTrue(names)
        self.assertEqual(len(names), len(set(names)), "a movie listed twice")

    def test_the_sample_is_part_of_the_library(self):
        names, _ = suite.gate_movies()
        library = suite.collect("all", set())
        self.assertEqual(suite.collect("all", None), library)  # dc_evidence's default selection
        self.assertLess(len(names), len(library))
        self.assertEqual(sorted(m[0] for m in suite.collect("all", set(names))), sorted(names))

    def test_the_trace_identity_movie_is_sampled(self):
        """The trace check plays new-game itself, but the sample keeps it so the
        shadow runs cover the same play."""
        names, _ = suite.gate_movies()
        self.assertIn("new-game", names)

    def test_preflight_rejects_missing_or_bad_recorded_and_sample_movies(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            movies = root / "oracle/movies"
            movies.mkdir(parents=True)
            (movies / "good.ctm").write_text("# frames 1\n")
            damaged = movies / "damaged.ctm"
            sample, ledger = root / "sample", root / "ledger"
            read_text = pathlib.Path.read_text
            for source in ("recorded", "sample"):
                sample.write_text("good # selected\n" + ("damaged # coverage\n" if source == "sample" else ""))
                ledger.write_text("damaged game digest\n" if source == "recorded" else "# empty\n")
                for contents in (None, "# frames broken\n", "# frames 1oops\n", "unreadable"):
                    damaged.unlink(missing_ok=True)
                    if contents is not None:
                        damaged.write_text(contents)
                    def read(path, *args, **kwargs):
                        if contents == "unreadable" and path == damaged:
                            raise PermissionError("damaged movie unreadable")
                        return read_text(path, *args, **kwargs)
                    with self.subTest(source=source, contents=contents), \
                            patch.object(suite, "ROOT", root), patch.object(suite, "GATE_MOVIES", sample), \
                            patch.object(suite.outcomes, "LEDGER", ledger), patch.object(pathlib.Path, "read_text", read):
                        # Even an explicit subset cannot hide broken membership.
                        with self.assertRaisesRegex((ValueError, OSError), "damaged"):
                            suite.select_movies("all", ["good"])

    def test_collect_does_not_skip_a_malformed_header(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            movies = root / "oracle/movies"
            movies.mkdir(parents=True)
            (movies / "damaged.ctm").write_text("# frames broken\n")
            with patch.object(suite, "ROOT", root), self.assertRaisesRegex(ValueError, "malformed"):
                suite.collect("all", set())


if __name__ == "__main__":
    unittest.main()
