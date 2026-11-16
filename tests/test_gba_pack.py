"""The GBA cartridge's packing (targets/gba/host/pack.py), with no cartridge: the
pack's head and directory as targets/gba/system/main.c reads them, and the header's
complement check; and the sound's samples converted from BRR (targets/gba/host/samples.py)."""
import importlib.util
import os
import pathlib
import unittest
import zlib

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
            image, base = pack.pack(program, b"")
        finally:
            pack.resources, pack.sound_samples = saved
        self.assertEqual(base, 0x200)                    # the program's end rounded up to 256
        magic, version, count, samples_at, colours_at, records_at = pack.HEAD.unpack_from(image, base)
        self.assertEqual(colours_at, 0)                  # the original's colours: no table
        self.assertEqual(records_at, 0)                  # no event records: every command from its bytes
        self.assertEqual((magic, version, count), (b"CTGB", pack.catalogue_version(), 3))
        for i, data in enumerate(resources):
            at, length = pack.ENTRY.unpack_from(image, base + pack.HEAD.size + i * pack.ENTRY.size)
            self.assertEqual(at % 4, 0)
            self.assertEqual(image[base + at:base + at + length], data)
        self.assertEqual(samples_at % 4, 0)
        self.assertEqual(image[base + samples_at:base + samples_at + 7], b"SAMPLES")
        self.assertEqual(image[0xA0:0xA9], pack.TITLE)
        self.assertEqual((sum(image[0xA0:0xBD]) + 0x19 + image[0xBD]) & 0xFF, 0)

    def test_fast_rom(self):
        # the options word after the header, which start.s reads for the cartridge's waits
        saved = pack.resources, pack.sound_samples
        pack.resources, pack.sound_samples = (lambda cartridge: []), (lambda cartridge: b"")
        try:
            plain, _ = pack.pack(bytes(0x1F0), b"")
            fast, _ = pack.pack(bytes(0x1F0), b"", fast_rom=True)
            with self.assertRaises(SystemExit):
                pack.pack(b"\xff" * 0x1F0, b"")     # no options word where start.s keeps it
        finally:
            pack.resources, pack.sound_samples = saved
        self.assertEqual(plain[pack.OPTIONS_AT:pack.OPTIONS_AT + 4], bytes(4))
        self.assertEqual(fast[pack.OPTIONS_AT:pack.OPTIONS_AT + 4], pack.FAST_ROM.to_bytes(4, "little"))
        self.assertEqual(plain[:pack.OPTIONS_AT], fast[:pack.OPTIONS_AT])

    def test_logo(self):
        # the logo goes at $04-$9F, from the player's BIOS; without one it stays blank
        saved = pack.resources, pack.sound_samples
        pack.resources, pack.sound_samples = (lambda cartridge: []), (lambda cartridge: b"")
        try:
            blank, _ = pack.pack(bytes(0x1F0), b"")
            logo = bytes(range(1, pack.LOGO_BYTES + 1))
            marked, _ = pack.pack(bytes(0x1F0), b"", logo=logo)
        finally:
            pack.resources, pack.sound_samples = saved
        self.assertEqual(blank[pack.LOGO_AT:pack.LOGO_AT + pack.LOGO_BYTES], bytes(pack.LOGO_BYTES))
        self.assertEqual(marked[pack.LOGO_AT:pack.LOGO_AT + pack.LOGO_BYTES], logo)
        self.assertEqual(marked[0xA0:], blank[0xA0:])    # the logo is outside the complement's bytes
        self.assertIsNone(pack.bios_logo(bytes(pack.BIOS_BYTES)))
        self.assertIsNone(pack.bios_logo(b""))

    @unittest.skipUnless(os.environ.get("CT_GBA_BIOS"), "CT_GBA_BIOS names no BIOS dump")
    def test_logo_from_bios(self):
        logo = pack.bios_logo(pathlib.Path(os.environ["CT_GBA_BIOS"]).read_bytes())
        self.assertIsNotNone(logo)
        self.assertEqual(zlib.crc32(logo), pack.LOGO_CRC32)


    def test_event_records(self):
        # the event records ct_native writes, after the rest, a word apart
        records = b"CTER" + bytes(8) + b"RECORDS"
        saved = pack.resources, pack.sound_samples
        pack.resources, pack.sound_samples = (lambda cartridge: [b"\x01"]), (lambda cartridge: b"SAMPLES")
        try:
            image, base = pack.pack(bytes(0x1F0), b"", records=records)
        finally:
            pack.resources, pack.sound_samples = saved
        records_at = pack.HEAD.unpack_from(image, base)[5]
        self.assertEqual(records_at % 4, 0)
        self.assertEqual(image[base + records_at:base + records_at + len(records)], records)

    def test_port_style(self):
        saved = pack.resources, pack.sound_samples
        pack.resources, pack.sound_samples = (lambda cartridge: [b""]), (lambda cartridge: b"SAMPLES")
        try:
            image, base = pack.pack(bytes(0x1F0), b"", profile="port-style")
        finally:
            pack.resources, pack.sound_samples = saved
        colours_at = pack.HEAD.unpack_from(image, base)[4]
        self.assertEqual(colours_at % 4, 0)
        table = image[base + colours_at:base + colours_at + 2 * pack.COLOURS]
        self.assertEqual(table, pack.colour_table("port-style"))
        shown = lambda c: int.from_bytes(table[2 * c:2 * c + 2], "little")
        self.assertEqual((shown(0), shown(0x7FFF)), (0, 0x7FFF))   # black and white stay
        grey = 10 | 10 << 5 | 10 << 10
        self.assertEqual(shown(grey), 14 | 14 << 5 | 14 << 10)     # a grey only brightens: 31 * (10/31)^0.7
        red = shown(20)                                             # a red, brighter and purer
        self.assertGreater(red & 31, 20)
        self.assertEqual(red >> 5, 0)


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
