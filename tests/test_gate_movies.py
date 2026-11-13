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
import json
import io


class GateMoviesTest(unittest.TestCase):
    def test_every_line_names_a_library_movie_with_its_reason(self):
        names, bad = suite.gate_movies()
        self.assertEqual(bad, [])
        self.assertTrue(names)
        self.assertEqual(len(names), len(set(names)), "a movie listed twice")

    def test_each_carried_fight_is_a_gate_movie_made_by_its_route(self):
        names, _ = suite.gate_movies()
        library = {row[0]: row[1] for row in outcomes.movie_library(names, set())}
        for movie, route in outcomes.fight_routes().items():
            with self.subTest(movie=movie):
                self.assertIn(movie, library)
                header = " ".join(line[1:].strip() for line in library[movie].read_text().splitlines()
                                  if line.startswith("#"))
                self.assertRegex(header, r"\b[Rr]oute:? %s\b" % route)

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
                    patch.object(suite.time, 'monotonic', side_effect=[0, *range(1, 19), elapsed]), \
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


class LongBatchTest(unittest.TestCase):
    def test_launch_failure_is_a_setup_error_even_after_gameplay_findings(self):
        for stage in range(3):
            with self.subTest(stage=stage), tempfile.TemporaryDirectory() as where:
                root = pathlib.Path(where)
                binary = root / 'ct_native'
                binary.touch()
                out = root / 'batch'
                output = io.StringIO()
                calls = []

                def launch(command, stdout, **kwargs):
                    calls.append(command)
                    if len(calls) > stage:
                        raise OSError('cannot launch')
                    stdout.write('a finding\n')
                    return SimpleNamespace(returncode=1)
                with patch.object(suite, 'all_routes', return_value=['first']), \
                        patch.object(suite.subprocess, 'run', side_effect=launch) as run, \
                        contextlib.redirect_stdout(output):
                    rc = suite.main(['--ct-native', str(binary), '--out', str(out), '--long'])
                self.assertEqual(rc, 1)
                self.assertEqual(run.call_count, stage + 1)
                self.assertIn('batch setup failed:', output.getvalue())
                self.assertNotIn('batch complete:', output.getvalue())
                self.assertNotIn('suite OK', output.getvalue())
                log = out / (('movies', 'outcomes', 'routes')[stage] + '.log')
                self.assertIn('cannot launch', log.read_text())

    def test_missing_binary_is_a_setup_error_without_launching_stages(self):
        with tempfile.TemporaryDirectory() as where, \
                patch.object(suite.subprocess, 'run') as run, \
                contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            suite.main(['--ct-native', str(pathlib.Path(where) / 'missing'), '--long'])
        self.assertEqual(error.exception.code, 2)
        run.assert_not_called()

    def test_a_stage_that_crashes_is_a_setup_error(self):
        for status, text in ((2, 'usage: native_check.py\n'), (1, 'Traceback (most recent call last):\n'), (1, '')):
            with self.subTest(status=status, text=text), tempfile.TemporaryDirectory() as where:
                root = pathlib.Path(where)
                binary = root / 'ct_native'
                binary.touch()
                output = io.StringIO()

                def crash(command, stdout, **kwargs):
                    stdout.write(text)
                    return SimpleNamespace(returncode=status)
                with patch.object(suite, 'all_routes', return_value=['first']), \
                        patch.object(suite.subprocess, 'run', side_effect=crash) as run, \
                        contextlib.redirect_stdout(output):
                    rc = suite.main(['--ct-native', str(binary), '--out', str(root / 'batch'), '--long'])
                self.assertEqual(rc, 1)
                self.assertEqual(run.call_count, 1)
                self.assertIn('batch setup failed:', output.getvalue())
                self.assertNotIn('batch complete:', output.getvalue())

    def test_findings_continue_without_becoming_a_gate_failure(self):
        def finding(command, stdout, **kwargs):
            stdout.write('a finding\n')
            return SimpleNamespace(returncode=1)

        with tempfile.TemporaryDirectory() as where:
            root = pathlib.Path(where)
            binary = root / 'ct_native'
            binary.touch()
            out = root / 'batch'
            output = io.StringIO()
            with patch.object(suite, 'gate_movies', side_effect=AssertionError('no sample')), \
                    patch.object(suite.shutil, 'which', side_effect=AssertionError('no CTest')), \
                    patch.object(suite, 'all_routes', return_value=['first', 'last']), \
                    patch.object(suite.subprocess, 'run', side_effect=finding) as run, \
                    patch.object(suite.time, 'monotonic', side_effect=[0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 7200]), \
                    contextlib.redirect_stdout(output):
                rc = suite.main(['--ct-native', str(binary), '--out', str(out), '--long'])
            self.assertEqual(rc, 0)
            self.assertEqual(run.call_count, 4)
            commands = [call.args[0] for call in run.call_args_list]
            self.assertNotIn('--movies', commands[0])
            self.assertNotIn('--movies', commands[1])
            self.assertEqual(commands[2][-2:], ['first', 'last'])
            self.assertNotIn('--timeout', commands[2])  # Keep the batch route timeout, not the fast gate's.
            self.assertIn('tools/wide_check.py', commands[3])  # the gate's sample at a wider view
            report = json.loads((out / 'findings.json').read_text())
            self.assertEqual([r['status'] for r in report], [1, 1, 1, 1])
            self.assertTrue(all((out / r['log']).exists() for r in report))
            self.assertIn('7200.0s', output.getvalue())
            self.assertIn('batch complete: 4 stage(s) with findings', output.getvalue())
            self.assertNotIn('suite OK', output.getvalue())

    def test_long_cannot_silently_narrow_the_library(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            suite.main(['--ct-native', 'unused', '--long', '--movies', 'sample'])
        self.assertEqual(error.exception.code, 2)

    def test_all_routes_are_runnable_and_include_the_fast_routes(self):
        routes = suite.all_routes()
        root = suite.ROOT / 'tools/movies/routes'
        self.assertTrue(routes)
        self.assertEqual(routes, sorted(p.stem for p in root.glob('*.py') if not p.stem.startswith('_')))
        self.assertTrue({'new_game_plus', 'menu_sweep', 'beyond_time', 'telepod_lavos'} <= set(routes))
        for name in routes:
            self.assertIn('__main__', (root / (name + '.py')).read_text())


if __name__ == "__main__":
    unittest.main()
