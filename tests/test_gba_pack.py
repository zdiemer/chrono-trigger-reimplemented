"""The GBA cartridge's packing (targets/gba/pack.py), with no cartridge: the
pack's head and directory as targets/gba/main.c reads them, and the header's
complement check."""
import importlib.util
import pathlib
import struct
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("gba_pack", ROOT / "targets/gba/pack.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


class PackTest(unittest.TestCase):
    def test_layout(self):
        resources = [b"\x01\x02\x03", b"", b"\x04" * 9]
        saved = pack.resources
        pack.resources = lambda cartridge: resources
        try:
            program = bytes(0x1F0)
            image, base = pack.pack(program, b"", b"# frames 2\n", bytes(pack.SRAM_BYTES))
        finally:
            pack.resources = saved
        self.assertEqual(base, 0x200)                    # the program's end rounded up to 256
        magic, version, count, movie_at, movie_length, sram_at = pack.HEAD.unpack_from(image, base)
        self.assertEqual((magic, version, count), (b"CTGB", pack.catalogue_version(), 3))
        for i, data in enumerate(resources):
            at, length = pack.ENTRY.unpack_from(image, base + pack.HEAD.size + i * pack.ENTRY.size)
            self.assertEqual(at % 4, 0)
            self.assertEqual(image[base + at:base + at + length], data)
        self.assertEqual(image[base + movie_at:base + movie_at + movie_length], b"# frames 2\n")
        self.assertEqual(image[base + sram_at:base + sram_at + pack.SRAM_BYTES], bytes(pack.SRAM_BYTES))
        self.assertEqual(image[0xA0:0xA9], pack.TITLE)
        self.assertEqual((sum(image[0xA0:0xBD]) + 0x19 + image[0xBD]) & 0xFF, 0)

    def test_no_save(self):
        saved = pack.resources
        pack.resources = lambda cartridge: [b"\xAA"]
        try:
            image, base = pack.pack(bytes(0xC0), b"", b"", None)
        finally:
            pack.resources = saved
        self.assertEqual(struct.unpack_from("<I", image, base + 20)[0], 0)


if __name__ == "__main__":
    unittest.main()
