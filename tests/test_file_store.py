"""Run the portable storage fault probe, including abrupt process termination."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CANDIDATES = [ROOT / "build" / build / ("file_store_test" + suffix)
              for build in ("main", "ci") for suffix in ("", ".exe")]
BINARY = next((path for path in CANDIDATES if path.exists()), CANDIDATES[0])


@unittest.skipUnless(BINARY.exists(), "build file_store_test first")
class FileStoreTest(unittest.TestCase):
    def test_failed_and_interrupted_save(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "game.srm"
            subprocess.run([str(BINARY), str(path)], check=True)
            before = path.read_bytes()
            interrupted = subprocess.run([str(BINARY), "--interrupt", str(path)])
            self.assertEqual(interrupted.returncode, 77)
            self.assertEqual(path.read_bytes(), before)
            partials = list(path.parent.glob("game.srm.tmp.*"))
            self.assertEqual(len(partials), 1)
            self.assertEqual(partials[0].stat().st_size, 4096)
            # An abandoned temporary file must not prevent the next save.
            subprocess.run([str(BINARY), str(path)], check=True)
            self.assertEqual(path.read_bytes(), before)
