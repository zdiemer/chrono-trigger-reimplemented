"""The unreached ratchet fails closed; these fixtures need no cartridge."""
import copy
import json
import pathlib
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tools import unreached as u, unreached_fuzz as f
from tools.disasm.analyze import Analyzer
from tools.disasm.hints import Hints
from tests.test_disasm import FakeRom


class UnreachedTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        (self.root / 'guard.c').write_text('guard bounds the selector\n')
        self.row = dict(subsystem='event', entry='C01000', name='operation', digest='reach',
                        runs=[[0xC01000, 0xC01002]])
        self.records = [
            ['movie', 'movie', 'movie-sha', 'measured'],
            ['port', 'event', 'C01000', 'operation', 'reach', '3', 'C01000-C01002', 'inventory'],
            ['safe', 'C01000', 'C01000-C01002', '3',
             'guard.c=' + u.file_hash(self.root / 'guard.c'), 'guard rejects it']]

    def check(self, records=None, rows=None, movies=None, **kwargs):
        return u.check_records(rows if rows is not None else [self.row],
                               records if records is not None else self.records,
                               movies if movies is not None else {'movie': 'movie-sha'},
                               {}, {}, root=self.root, **kwargs)

    def test_runs_and_instruction_widths(self):
        spans = [[1, 3], [8, 8]]
        self.assertEqual(u.parse_spans(u.spans_text(spans)), spans)
        self.assertEqual(u.runs(u.expand(spans)), spans)
        for bad in ('000003-000001', '000001-000003,000002-000005', 'C00000-C100000'):
            with self.assertRaises(ValueError):
                u.parse_spans(bad)
        row = {'instructions': {'C01000': [3, 3, 2, 2]}}
        self.assertEqual(u.touched(row, {0xC01000: {(True, False)}}), {0xC01000, 0xC01001})
        self.assertEqual(u.touched(row, {0xC01000: {(False, False)}}), {0xC01000, 0xC01001, 0xC01002})

    def test_library_manifest(self):
        self.assertEqual(self.check(), [])
        self.assertEqual(self.check(movies={'movie': 'movie-sha', 'new': 'new-sha'}), [])
        self.assertTrue(self.check(movies={}))
        self.assertTrue(self.check(movies={'movie': 'edited'}))
        self.assertTrue(self.check(records=self.records[1:]))

    def test_added_reach_and_measured_coverage_loss(self):
        self.assertTrue(self.check(rows=[dict(self.row, digest='grown')]))
        self.assertTrue(self.check(rows=[dict(self.row, entry='C02000')]))
        self.assertEqual(self.check(rows=[]), [])
        row = dict(self.row, runs=[[0xC01000, 0xC01003]])
        self.assertTrue(self.check(rows=[row], measured=True))
        self.assertEqual(self.check(rows=[dict(self.row, runs=[])], measured=True), [])

    def test_proofs_must_be_complete_disjoint_and_current(self):
        self.assertTrue(self.check(records=self.records[:-1]))
        self.assertTrue(self.check(records=self.records + [self.records[-1]]))
        bad = copy.deepcopy(self.records)
        bad[-1][2] = 'C01000-C01003'
        bad[-1][3] = '4'
        self.assertTrue(self.check(records=bad))
        (self.root / 'guard.c').write_text('guard removed\n')
        self.assertTrue(self.check())

    def test_fuzz_must_match_and_recipe_must_be_current(self):
        records = self.records[:-1] + [['fuzz', 'C01000', 'C01000-C01002', '3',
                                       'case', '1', '1000', '999', 'fingerprint', '0', 'boundary']]
        with patch.object(f, 'fingerprint', return_value='fingerprint'):
            def check():
                return u.check_records([self.row], records, {'movie': 'movie-sha'}, {'case': {'name': 'operation'}}, {}, root=self.root)
            self.assertEqual(check(), [])
            records[-1][7] = '0'
            self.assertTrue(check())
            records[-1][7] = '999'
            records[-1][9] = '1'
            self.assertTrue(check())
            records[-1][9] = '0'
            records[-1][8] = 'obsolete'
            self.assertTrue(check())

    def test_log_diagnostics_disqualify_evidence(self):
        result = dict(exit=0, matched=1, mismatched=0)
        self.assertTrue(f.clean(result, 'all compared'))
        for diagnostic in ('accessor out of range', 'page refused', 'asset outside the catalogue'):
            self.assertFalse(f.clean(result, diagnostic))
        for change in (dict(matched=0), dict(mismatched=1), dict(exit=1)):
            self.assertFalse(f.clean(dict(result, **change), ''))

    def test_fresh_evidence_requires_the_claimed_bytes(self):
        rows = [dict(self.row, instructions={'C01000': [3, 3, 2, 2]})]
        records = [['fuzz', 'C01000', 'C01000-C01002', '3', 'case', '1', '20', '20', 'hash', '0', 'tested']]
        result = dict(exit=0, matched=20, mismatched=0, seed=1, iterations=20, fingerprint='hash',
                      implementation=f.implementation_hash(self.root))
        (self.root / 'case.json').write_text(json.dumps(result))
        (self.root / 'case.log').write_text('matched 20, mismatched 0')
        (self.root / 'case.cov').write_text('C0:1000 16 16\n')

        def check():
            return u.check_fuzz_evidence(rows, records, self.root, root=self.root)
        self.assertEqual(check(), [])
        # A run of other C is not evidence for this tree's.
        (self.root / 'case.json').write_text(json.dumps(dict(result, implementation='other')))
        self.assertTrue(check())
        (self.root / 'case.json').write_text(json.dumps(result))
        # A successful comparison in the narrow width did not execute the third byte.
        (self.root / 'case.cov').write_text('C0:1000 8 16\n')
        self.assertTrue(check())
        (self.root / 'case.cov').unlink()
        self.assertTrue(check())

    def test_safety_rests_on_definitions_not_files(self):
        (self.root / 'game').mkdir()
        source = self.root / 'game' / 'guard.c'
        source.write_text('static bool bounded(int v) { return v < 7; }\n'
                          'bool operation_runs(void) { return bounded(1); }\n'
                          'void operation(void) { operation_runs(); }\n'
                          'void unrelated(void) {}\n')
        registry = self.root / 'game' / 'registry.c'
        registry.write_text('const NativeEntry entries[] = {\n'
                            '    { .addr = 0xC01000, .name = "operation", .fn = operation, .cost_milli = 10,\n'
                            '      .guard = operation_runs },\n};\n')
        specs = u.safety_dependencies(self.root, self.row, ['game/guard.c:bounded'])
        self.assertEqual(specs, ['game/guard.c:bounded', 'game/registry.c:operation',
                                 'game/guard.c:operation', 'game/guard.c:operation_runs'])
        records = self.records[:-1] + [['safe', 'C01000', 'C01000-C01002', '3',
                                       ','.join(f'{s}={u.dependency_hash(self.root, s)}' for s in specs), 'bounded']]
        self.assertEqual(self.check(records=records), [])
        # Recalibrated time, an edited comment or an unrelated function: the argument stands.
        registry.write_text(registry.read_text().replace('= 10', '= 12'))
        source.write_text(source.read_text().replace('void unrelated(void) {}', '/* note */ void unrelated(void) { x(); }'))
        self.assertEqual(self.check(records=records), [])
        # The bound it cites, a caller's use of it, or the entry's guard: review again.
        for before, after in (('v < 7', 'v < 8'), ('bounded(1)', 'bounded(2)')):
            text = source.read_text()
            source.write_text(text.replace(before, after))
            self.assertTrue(self.check(records=records))
            source.write_text(text)
        registry.write_text(registry.read_text().replace('= operation_runs', '= always'))
        self.assertTrue(self.check(records=records))

    def test_newline_conversion_is_not_a_source_change(self):
        path = self.root / 'source.txt'
        path.write_bytes(b'line\r\n')
        first = u.file_hash(path)
        path.write_bytes(b'line\n')
        self.assertEqual(first, u.file_hash(path))

    def test_recipe_overrides_and_ties(self):
        base = ['--fill', '7E0040-7E0041=0000', '--range', 'X=0000-0010', '--keep', '7E0000-7E0010']
        extra = ['--fill', '7E0040-7E0041=FFFF', '--range', 'X=0002-0002', '--fill', '7E0050-7E0051=@7E0040']
        merged = f.merge_args(base, extra)
        self.assertNotIn('7E0040-7E0041=0000', merged)
        self.assertNotIn('X=0000-0010', merged)
        self.assertIn('7E0050-7E0051=@7E0040', merged)
        self.assertIn('7E0000-7E0010', merged)

    def test_an_unrelated_callout_is_not_evidence_for_a_port(self):
        self.assertFalse(f.covers({'name': 'caller'}, 'callee'))
        self.assertTrue(f.covers({'name': 'caller', 'covers': ['callee']}, 'callee'))

    def test_restart_only_stop_does_not_hide_screen_dispatch(self):
        # Synthetic instructions at the real regression's addresses. The
        # original dispatch serves curtain, ripple and restart; only restart
        # hands back. Burndown's stop/call-out estimate drops the other arms.
        code = {0xC03FAF: bytes.fromhex('4C0940'),
                0xC04009: bytes.fromhex('FC004260'),
                0xC04200: bytes.fromhex('4541C8418141'),
                0xC04145: bytes.fromhex('EA60'),
                0xC041C8: bytes.fromhex('EA60'),
                0xC04181: bytes.fromhex('4C00FF')}
        hints = Hints()
        for pc in (0xC03FAF, 0xC04145, 0xC041C8, 0xC04181):
            hints.entries.append(SimpleNamespace(addr=pc, m=True, x=True, kind='test'))
        an = Analyzer(FakeRom(code), hints)
        an.run()
        an.tables[0xC04009] = SimpleNamespace(table=0xC04200, count=3,
                                            kind='short', dispatcher=0xC04009, extra=set(), call=True)
        port = u.burndown.Port(0xC03FAF, 'screen_command')
        port.stops = {0xC04009}
        port.unreadable = True
        owners = {port.addr, 0xC0FF00}
        old, old_inline = u.burndown.walk(an, port.addr, owners, port)
        self.assertNotIn(0xC04145, old | old_inline)
        own, inline, unresolved = u.walk(an, port.addr, owners)
        self.assertEqual(unresolved, [])
        self.assertTrue({0xC04009, 0xC04145, 0xC041C8, 0xC04181} <= own | inline)
        self.assertNotIn(0xC0FF00, own | inline)

    def test_unresolved_walks_are_explicit(self):
        hints = Hints()
        hints.entries.append(SimpleNamespace(addr=0xC08000, m=True, x=True, kind='test'))
        an = Analyzer(FakeRom({0xC08000: bytes.fromhex('6C0090')}), hints)
        an.run()
        _, _, problems = u.walk(an, 0xC08000, {0xC08000})
        self.assertIn('C08000: unresolved JMP successor', problems)
        _, _, problems = u.walk(an, 0xC09000, {0xC09000})
        self.assertIn('C09000: no decoded instruction', problems)
        hints = Hints()
        hints.entries.append(SimpleNamespace(addr=0xC08000, m=True, x=True, kind='test'))
        an = Analyzer(FakeRom({0xC08000: bytes.fromhex('FC009060')}), hints)
        an.run()
        _, _, problems = u.walk(an, 0xC08000, {0xC08000})
        self.assertIn('C08000: unresolved call', problems)

    def test_fuzz_evidence_tracks_implementation_changes(self):
        (self.root / 'game').mkdir()
        implementation = self.root / 'game' / 'screen.c'
        implementation.write_text('void curtain(void) {}\n')
        (self.root / 'movie.ctm').write_text('0 1 NONE\n')
        recipe, states = {'state': 'field'}, {'field': {'movie': 'movie.ctm'}}
        before, recorded = f.implementation_hash(self.root), f.fingerprint(recipe, states, self.root)
        implementation.write_bytes(b'void curtain(void) {}\r\n')
        self.assertEqual(before, f.implementation_hash(self.root))
        implementation.write_text('void curtain(void) { wait(); }\n')
        self.assertNotEqual(before, f.implementation_hash(self.root))
        # The committed fingerprint names the recipe: a change to the C
        # stales the runs, not the ledger.
        self.assertEqual(recorded, f.fingerprint(recipe, states, self.root))


if __name__ == '__main__':
    unittest.main()
