"""The gate's sample of the movie library (tools/gate_movies.txt, read by
tools/suite.py): every line names a movie in the library and says why it is
there, so the default gate can't silently run fewer movies than it lists.
Needs no ROM.
"""
import unittest
import pathlib
import tempfile
from unittest.mock import patch

from tools import suite, outcomes
from types import SimpleNamespace
import contextlib
import io


class GateMoviesTest(unittest.TestCase):
    def test_every_line_names_a_library_movie_with_its_reason(self):
        names, bad = suite.gate_movies()
        self.assertEqual(bad, [])
        self.assertTrue(names)
        self.assertEqual(len(names), len(set(names)), "a movie listed twice")

    def test_the_sample_is_part_of_the_library(self):
        names, _ = suite.gate_movies()
        library = outcomes.movie_library([], set())
        self.assertEqual(outcomes.movie_library([], set()), library)
        self.assertLess(len(names), len(library))
        self.assertEqual(sorted(m[0] for m in outcomes.movie_library(names, set())), sorted(names))




class GateFailureTest(unittest.TestCase):
    def run_gate(self, elapsed, failure=0):
        with tempfile.TemporaryDirectory() as where:
            root = pathlib.Path(where)
            binary = root / 'ct_native'
            binary.touch()
            (root / 'CTestTestfile.cmake').touch()
            output = io.StringIO()
            with patch.object(suite, 'gate_movies', return_value=(['sample'], [])), \
                    patch.object(suite.shutil, 'which', return_value='ctest'), \
                    patch.object(suite.subprocess, 'run', return_value=SimpleNamespace(returncode=failure)), \
                    patch.object(suite.time, 'monotonic', side_effect=[0, *range(1, 15), elapsed]), \
                    contextlib.redirect_stdout(output):
                rc = suite.main(['--ct-native', str(binary), '--out', str(root / 'runs')])
            return rc, output.getvalue().splitlines()

    def test_failure_cannot_report_suite_ok(self):
        rc, lines = self.run_gate(20, failure=1)
        self.assertEqual(rc, 1)
        self.assertTrue(lines[-1].startswith('FAILED:'))

    def test_budget_includes_unit_test_time(self):
        rc, lines = self.run_gate(600)
        self.assertEqual(rc, 1)
        self.assertIn('wall-time budget', lines[-1])
        rc, lines = self.run_gate(599.9)
        self.assertEqual(rc, 0)
        self.assertEqual(lines[-1], 'suite OK')
        self.assertIn('unit tests included', lines[-2])

    def test_empty_or_duplicate_sample_fails(self):
        with tempfile.TemporaryDirectory() as where:
            path = pathlib.Path(where) / 'sample.txt'
            name = suite.gate_movies()[0][0]
            for text in ('# no sample', f'{name} # one\n{name} # duplicate\n'):
                path.write_text(text)
                with patch.object(suite, 'GATE_MOVIES', path):
                    self.assertTrue(suite.gate_movies()[1])


if __name__ == "__main__":
    unittest.main()
