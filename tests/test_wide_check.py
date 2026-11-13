"""The wide check names where two end states differ, by work RAM bank or save RAM."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools import wide_check
from tools.native_state import SRAM_BYTES, WRAM_BYTES


class DifferencesTest(unittest.TestCase):
    def test_same(self):
        state = bytes(WRAM_BYTES + SRAM_BYTES)
        self.assertEqual(wide_check.differences(state, state), [])

    def test_spans(self):
        narrow = bytearray(WRAM_BYTES + SRAM_BYTES)
        wide = bytearray(narrow)
        wide[0x1234:0x1236] = b'\1\1'
        wide[0x10000] = 1
        wide[WRAM_BYTES + 0x10] = 1
        self.assertEqual(wide_check.differences(bytes(narrow), bytes(wide)),
                         ['wram $7E:1234 (2 bytes)', 'wram $7F:0000 (1 bytes)', 'sram $0010 (1 bytes)'])

    def test_more_than_shown(self):
        narrow = bytes(16)
        wide = bytes([1, 0] * 8)
        self.assertEqual(wide_check.differences(narrow, wide, shown=2)[-1], 'and 6 more')


if __name__ == '__main__':
    unittest.main()
