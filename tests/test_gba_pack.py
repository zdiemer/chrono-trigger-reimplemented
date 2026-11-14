"""The GBA cartridge's packing (targets/gba/host/pack.py), with no cartridge: the
pack's head and directory as targets/gba/system/main.c reads them, and the header's
complement check; and the sound's samples converted from BRR (targets/gba/host/samples.py)."""
import importlib.util
import pathlib
import struct
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("gba_pack", ROOT / "targets/gba/host/pack.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)
samples = pack.samples


class PackTest(unittest.TestCase):
    def test_layout(self):
        resources = [b"\x01\x02\x03", b"", b"\x04" * 9]
        saved = pack.resources, pack.sound_samples
        pack.resources, pack.sound_samples = (lambda cartridge: resources), (lambda cartridge: b"SAMPLES")
        try:
            program = bytes(0x1F0)
            image, base = pack.pack(program, b"", b"# frames 3\n1 1 A+P2:START\n", bytes(pack.SRAM_BYTES))
        finally:
            pack.resources, pack.sound_samples = saved
        self.assertEqual(base, 0x200)                    # the program's end rounded up to 256
        magic, version, count, frames, pads_at, sram_at, samples_at = pack.HEAD.unpack_from(image, base)
        self.assertEqual((magic, version, count), (b"CTGB", pack.catalogue_version(), 3))
        for i, data in enumerate(resources):
            at, length = pack.ENTRY.unpack_from(image, base + pack.HEAD.size + i * pack.ENTRY.size)
            self.assertEqual(at % 4, 0)
            self.assertEqual(image[base + at:base + at + length], data)
        self.assertEqual(frames, 3)
        self.assertEqual(struct.unpack_from("<3I", image, base + pads_at), (0, 0x10000080, 0))
        self.assertEqual(image[base + sram_at:base + sram_at + pack.SRAM_BYTES], bytes(pack.SRAM_BYTES))
        self.assertEqual(samples_at % 4, 0)
        self.assertEqual(image[base + samples_at:base + samples_at + 7], b"SAMPLES")
        self.assertEqual(image[0xA0:0xA9], pack.TITLE)
        self.assertEqual((sum(image[0xA0:0xBD]) + 0x19 + image[0xBD]) & 0xFF, 0)

    def test_no_save(self):
        saved = pack.resources, pack.sound_samples
        pack.resources, pack.sound_samples = (lambda cartridge: [b"\xAA"]), (lambda cartridge: b"")
        try:
            image, base = pack.pack(bytes(0xC0), b"", b"", None)
        finally:
            pack.resources, pack.sound_samples = saved
        self.assertEqual(struct.unpack_from("<I", image, base + 20)[0], 0)


def brr_block(header, nibbles):
    return bytes([header]) + bytes((nibbles[2 * i] & 15) << 4 | (nibbles[2 * i + 1] & 15) for i in range(8))


class SamplesTest(unittest.TestCase):
    def test_hash(self):
        self.assertEqual(samples.fnv1a(b""), 0x811C9DC5)
        self.assertEqual(samples.fnv1a(b"a"), 0xE40C292C)

    def test_loop(self):
        # range 12, no filter: a nibble n is n * 2048; the second block loops to itself
        data = brr_block(0xC0, [1] * 16) + brr_block(0xC3, [-1] * 16)
        out, loop = samples.decode(data, 0, 9)
        self.assertEqual((len(out), loop), (32, 16))
        self.assertEqual((out[0], out[16]), (2048, -2048))
        pcm, peak = samples.to_pcm8(out)
        self.assertEqual((peak, pcm[0], pcm[16]), (2048, 127, 0x81))

    def test_end_without_loop(self):
        # the block that ends without looping isn't played
        data = brr_block(0xC0, [2] * 16) + brr_block(0xC1, [7] * 16)
        out, loop = samples.decode(data, 0, 0)
        self.assertEqual((len(out), loop, out[0]), (16, None, 4096))

    def test_noise(self):
        sequence = samples.noise()
        self.assertEqual(len(sequence), 0x7FFF)
        self.assertEqual(sequence[0], 0x80)   # the generator starts at $4000: twice it is -32768


if __name__ == "__main__":
    unittest.main()
