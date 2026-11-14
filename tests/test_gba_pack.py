"""The GBA cartridge's packing (targets/gba/host/pack.py), with no cartridge: the
pack's head and directory as targets/gba/system/main.c reads them, and the header's
complement check."""
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("gba_pack", ROOT / "targets/gba/host/pack.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


class PackTest(unittest.TestCase):
    def test_layout(self):
        resources = [b"\x01\x02\x03", b"", b"\x04" * 9]
        saved = pack.resources
        pack.resources = lambda cartridge: resources
        try:
            program = bytes(0x1F0)
            image, base = pack.pack(program, b"")
        finally:
            pack.resources = saved
        self.assertEqual(base, 0x200)                    # the program's end rounded up to 256
        magic, version, count = pack.HEAD.unpack_from(image, base)
        self.assertEqual((magic, version, count), (b"CTGB", pack.catalogue_version(), 3))
        for i, data in enumerate(resources):
            at, length = pack.ENTRY.unpack_from(image, base + pack.HEAD.size + i * pack.ENTRY.size)
            self.assertEqual(at % 4, 0)
            self.assertEqual(image[base + at:base + at + length], data)
        self.assertEqual(image[0xA0:0xA9], pack.TITLE)
        self.assertEqual((sum(image[0xA0:0xBD]) + 0x19 + image[0xBD]) & 0xFF, 0)


if __name__ == "__main__":
    unittest.main()
