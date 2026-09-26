"""What game/menu/sprites.c takes for granted about the ROM's sprite tables.

A record's load divides the span of a sprite's animation frames by the four
facings, and its animation adds up to three facings' worth to where the
frames start; the port writes both as plain arithmetic, where the original's
additions would take in a carry the division or the multiply left. Neither
carries for any sprite the ROM has: every entry of the table of animations'
places ($E4:FFE4, a word an entry, 256 entries for the assignment's byte)
spans a multiple of four frames, fewer than $4000 a facing. Needs the ROM."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
ROM = ROOT / "roms" / "ct-usa.sfc"

ANIMATIONS_TABLE = 0xE4FFE4
ENTRIES = 256
FACINGS = 4
MOST_FRAMES_A_FACING = 0x4000


def offset(address):
    """HiROM file offset of a bus address in banks $C0-$FF."""
    return ((address >> 16) & 0x3F) << 16 | (address & 0xFFFF)


@unittest.skipUnless(ROM.exists(), "needs the ROM (tools/rom.py stage)")
class TestAnimationSpans(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom = ROM.read_bytes()

    def word(self, address):
        o = offset(address)
        return self.rom[o] | self.rom[o + 1] << 8

    def spans(self):
        table = self.word(ANIMATIONS_TABLE)
        for entry in range(ENTRIES):
            at = 0xE40000 | (table + 2 * entry) & 0xFFFF
            first = self.word(at)
            following = self.word(0xE40000 | (at + 2) & 0xFFFF)
            yield entry, (following - first) & 0xFFFF

    def test_every_span_is_whole_facings(self):
        for entry, span in self.spans():
            with self.subTest(entry=entry):
                self.assertEqual(span % FACINGS, 0)

    def test_a_facing_has_fewer_than_the_bound(self):
        for entry, span in self.spans():
            with self.subTest(entry=entry):
                self.assertLess(span // FACINGS, MOST_FRAMES_A_FACING)


if __name__ == "__main__":
    unittest.main()
