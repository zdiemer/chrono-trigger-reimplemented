"""The evidence cache's keys: an input that changes makes the entry stale, one
that doesn't matter leaves it standing, and a failing result is never kept."""

import os
import pathlib
import tempfile
import unittest
from unittest import mock

from tools import dc_evidence, evidence_cache, uncompared_evidence


class Inputs:
    """A binary, a ROM and a movie in a scratch directory."""

    def __init__(self, root: pathlib.Path):
        self.root = root
        self.binary = root / "ct_dev.exe"
        self.rom = root / "ct.sfc"
        self.movie_path = root / "movie.ctm"
        self.binary.write_bytes(b"program one")
        self.rom.write_bytes(b"rom")
        self.movie_path.write_bytes(b"# a movie\n1 A\n2 B\n")

    def rewrite(self, path, data: bytes):
        path.write_bytes(data)
        evidence_cache._digests.clear()

    def movie(self, frames=100, name="movie"):
        return (name, self.movie_path, frames)


class Keys(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.inputs = Inputs(pathlib.Path(self.tmp.name))
        evidence_cache._digests.clear()

    def tearDown(self):
        self.tmp.cleanup()

    def key(self, frames=100, sites=(0xC20000,), dispatches=(0xC20002,), entries=(0xC20003,), name="movie"):
        i = self.inputs
        return dc_evidence.movie_key(i.binary, i.rom, i.movie(frames, name), list(sites), list(dispatches),
                                     list(entries))

    def test_each_input_makes_the_key_stale(self):
        base = self.key()
        self.assertEqual(base, self.key())
        self.assertNotEqual(base, self.key(frames=101))
        self.assertNotEqual(base, self.key(sites=(0xC20000, 0xC20010)))
        self.assertNotEqual(base, self.key(dispatches=()))
        self.assertNotEqual(base, self.key(entries=()))
        i = self.inputs
        for path, data in ((i.binary, b"program two"), (i.rom, b"rom 2"), (i.movie_path, b"# a movie\n1 A\n2 C\n")):
            old = path.read_bytes()
            i.rewrite(path, data)
            self.assertNotEqual(base, self.key(), path.name)
            i.rewrite(path, old)
            self.assertEqual(base, self.key(), path.name)

    def test_the_judging_python_is_in_the_key(self):
        base = self.key()
        with mock.patch.object(evidence_cache, "tools_digest", return_value={"tools/trace.py": "changed"}):
            self.assertNotEqual(base, self.key())

    def test_what_does_not_matter_leaves_the_key(self):
        base = self.key()
        i = self.inputs
        i.rewrite(i.movie_path, i.movie_path.read_bytes().replace(b"\n", b"\r\n"))
        self.assertEqual(base, self.key(), "a checkout's line endings")
        self.assertEqual(base, self.key(name="renamed"), "the movie's name")
        (i.root / "notes.md").write_text("a doc changed")
        self.assertEqual(base, self.key(), "a file nothing reads")
        # the binary's place, only its bytes
        moved = i.root / "elsewhere.exe"
        moved.write_bytes(i.binary.read_bytes())
        self.assertEqual(base, dc_evidence.movie_key(moved, i.rom, i.movie(), [0xC20000], [0xC20002], [0xC20003]))

    def test_a_relink_stamps_nothing_into_the_key(self):
        i = self.inputs
        pe = 0x80
        image = bytearray(0x200)
        image[:2] = b"MZ"
        image[0x3C:0x40] = pe.to_bytes(4, "little")
        image[pe:pe + 4] = b"PE" + bytes(2)
        image[0x180:0x190] = b"the program code"

        def link(stamp, checksum, code=b"the program code"):
            out = bytearray(image)
            out[pe + 8:pe + 12] = stamp.to_bytes(4, "little")
            out[pe + 88:pe + 92] = checksum.to_bytes(4, "little")
            out[0x180:0x190] = code
            i.rewrite(i.binary, bytes(out))
            return self.key()

        first = link(1000, 7)
        self.assertEqual(first, link(2000, 9), "linked again later")
        self.assertNotEqual(first, link(1000, 7, b"the program c0de"), "the code changed")

    def test_uncompared_case_key(self):
        i = self.inputs
        case = ("scene_first_frame", "title-start", 2000, ("scene_boot",), ())
        with mock.patch.object(uncompared_evidence, "movie_path", return_value=i.movie_path):
            base = uncompared_evidence.case_key(case, i.binary, i.rom)
            self.assertNotEqual(base, uncompared_evidence.case_key(case[:2] + (2001,) + case[3:], i.binary, i.rom))
            self.assertNotEqual(base, uncompared_evidence.case_key(case[:3] + ((),) + case[4:], i.binary, i.rom))
            i.rewrite(i.binary, b"program two")
            self.assertNotEqual(base, uncompared_evidence.case_key(case, i.binary, i.rom))

    def test_the_judging_python_is_its_code_not_its_prose(self):
        i = self.inputs
        tool = i.root / "judge.py"
        def digest(text):
            i.rewrite(tool, text.encode())
            return evidence_cache.code_digest(tool)
        base = digest('"""Judges."""\n\ndef same(a, b):\n    """Whether."""\n    return a == b  # equal\n')
        self.assertEqual(base, digest('"""Judges two runs."""\n\n# a note\n\ndef same(a, b):\n'
                                      '    """Whether the two are one."""\n    return a == b\n'))
        self.assertNotEqual(base, digest('"""Judges."""\n\ndef same(a, b):\n    return a != b\n'))
        self.assertNotEqual(base, digest('"""Judges."""\n\ndef same(a, b):\n    return "no divergence" == b\n'))

    def test_store_can_stay_in_a_writable_worktree(self):
        directory = self.inputs.root / "local-cache"
        with mock.patch.dict(os.environ, {"CT_EVIDENCE_CACHE_DIR": str(directory)}):
            with mock.patch.object(evidence_cache, "shared_root") as shared:
                self.assertEqual(evidence_cache.default_dir(self.inputs.binary, "dc"), directory.resolve() / "dc")
                self.assertEqual(evidence_cache.default_dir(self.inputs.binary, "uncompared"),
                                 directory.resolve() / "uncompared")
                shared.assert_not_called()

    def test_every_worktree_shares_one_store(self):
        with mock.patch.object(evidence_cache, "shared_root", return_value=pathlib.Path("/checkout")):
            self.assertEqual(evidence_cache.default_dir("/checkout/.claude/worktrees/a/build/main/ct_dev", "dc"),
                             evidence_cache.default_dir("/checkout/build/main/ct_dev", "dc"))
        with mock.patch.object(evidence_cache, "shared_root", return_value=None):
            self.assertEqual(evidence_cache.default_dir(self.inputs.binary, "dc"),
                             self.inputs.binary.resolve().parent / "evidence-cache" / "dc")


class Storing(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = pathlib.Path(self.tmp.name)
        self.inputs = Inputs(root)
        self.cache_dir = root / "cache"
        self.runs = 0
        self.result = ("movie", [], True, "no divergence", {0xC20000: 11}, {0xC20002: (7, 10, 0)},
                       {0xC20003: (12, 4)})
        evidence_cache._digests.clear()

    def tearDown(self):
        self.tmp.cleanup()

    def fake_run(self, *args):
        self.runs += 1
        return self.result

    def evidence(self, read=True):
        i = self.inputs
        cache = evidence_cache.Cache(self.cache_dir, read=read)
        with mock.patch.object(dc_evidence, "run_movie", self.fake_run):
            return dc_evidence.cached_movie(cache, i.binary, i.rom, i.root, i.movie(), [0xC20000], [0xC20002],
                                            [0xC20003])

    def test_a_passing_result_is_taken_back_whole(self):
        first = self.evidence()
        second = self.evidence()
        self.assertEqual(self.runs, 1)
        self.assertFalse(first[-1])
        self.assertTrue(second[-1])
        self.assertEqual(first[:-1], second[:-1])

    def test_a_stale_entry_runs_again(self):
        self.evidence()
        self.inputs.rewrite(self.inputs.binary, b"program two")
        self.evidence()
        self.assertEqual(self.runs, 2)

    def test_a_failing_result_is_never_kept(self):
        for failing in (("movie", ["index"], True) + self.result[3:],
                        ("movie", [], False, "differs at frame 3") + self.result[4:],
                        self.result[:5] + ({0xC20002: (7, 0x1FF, 2)},) + self.result[6:]):
            self.result = failing
            before = self.runs
            self.evidence()
            self.evidence()
            self.assertEqual(self.runs, before + 2)
        self.assertFalse(list(self.cache_dir.glob("*.json")) if self.cache_dir.exists() else [])

    def test_the_cache_can_be_ignored(self):
        self.evidence()
        self.evidence(read=False)
        self.assertEqual(self.runs, 2)
        with mock.patch.dict(os.environ, {evidence_cache.ENV: "off"}):
            self.evidence()
        self.assertEqual(self.runs, 3)
        self.evidence()
        self.assertEqual(self.runs, 3)

    def test_uncompared_keeps_only_cases_that_compared(self):
        i = self.inputs
        case = ("scene_first_frame", "title-start", 2000, ("scene_boot",), ())
        verdicts = [False, True]
        made = []

        def run(*args):
            made.append(1)
            return case[0], verdicts[len(made) - 1] if len(made) <= 2 else True

        cache = evidence_cache.Cache(self.cache_dir)
        with mock.patch.object(uncompared_evidence, "movie_path", return_value=i.movie_path), \
                mock.patch.object(uncompared_evidence, "run", run):
            self.assertEqual(uncompared_evidence.cached_run(case, i.binary, i.rom, i.root, cache), (case[0], False, False))
            self.assertEqual(uncompared_evidence.cached_run(case, i.binary, i.rom, i.root, cache), (case[0], True, False))
            self.assertEqual(uncompared_evidence.cached_run(case, i.binary, i.rom, i.root, cache), (case[0], True, True))
        self.assertEqual(len(made), 2)


if __name__ == "__main__":
    unittest.main()
