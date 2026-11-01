"""The unreached ratchet fails closed; these fixtures need no cartridge."""
import copy
import json
import pathlib
import tempfile
import unittest
from unittest.mock import patch

from tools import unreached as u, unreached_fuzz as f


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
                return u.check_records([self.row], records, {'movie': 'movie-sha'}, {'case': {}}, {}, root=self.root)
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
        result = dict(exit=0, matched=20, mismatched=0, seed=1, iterations=20, fingerprint='hash')
        (self.root / 'case.json').write_text(json.dumps(result))
        (self.root / 'case.log').write_text('matched 20, mismatched 0')
        (self.root / 'case.cov').write_text('C0:1000 16 16\n')
        self.assertEqual(u.check_fuzz_evidence(rows, records, self.root), [])
        # A successful comparison in the narrow width did not execute the third byte.
        (self.root / 'case.cov').write_text('C0:1000 8 16\n')
        self.assertTrue(u.check_fuzz_evidence(rows, records, self.root))
        (self.root / 'case.cov').unlink()
        self.assertTrue(u.check_fuzz_evidence(rows, records, self.root))

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


if __name__ == '__main__':
    unittest.main()
