"""MD2 codecs, with synthetic inputs only."""

import unittest

from tools.disasm.dataformats import (
    Exit, check_format, decode_exits, decode_graphics, decode_sprites, encode_exits, encode_graphics,
)
from tools.disasm.hints import DataHint


class ExitLists(unittest.TestCase):
    def test_empty_lists_and_fields(self):
        record = Exit(3, 4, 0x85, 12, 0xA1, 6, 7)
        blob = bytes((8, 0, 8, 0, 15, 0, 15, 0, 3, 4, 0x85, 12, 0xA1, 6, 7))
        parsed = decode_exits(blob, 3)
        self.assertEqual(parsed, ((), (record,), ()))
        self.assertEqual(encode_exits(parsed), blob)
        # Editing a list relocates every following offset, including empties.
        changed = encode_exits(((record,), (record,), ()))
        self.assertEqual(changed[:8], bytes((8, 0, 15, 0, 22, 0, 22, 0)))

    def test_malformed_offsets(self):
        valid = bytes((8, 0, 8, 0, 15, 0, 15, 0)) + bytes(7)
        for index, value in ((0, 7), (2, 16), (2, 9), (4, 7), (6, 14), (6, 255)):
            with self.subTest(index=index, value=value):
                blob = bytearray(valid)
                blob[index] = value
                with self.assertRaises(ValueError):
                    decode_exits(blob, 3)
        for blob in (b'', valid[:7], valid + b'\0'):
            with self.assertRaises(ValueError):
                decode_exits(blob, 3)

    def test_requires_bank_relative_origin(self):
        region = DataHint(0xE50001, 0xE50004, 'struct', None,
                          {'format': 'location-exits', 'count': 1})
        with self.assertRaisesRegex(ValueError, 'bank offset zero'):
            check_format(bytes((4, 0, 4, 0)), region)


class Graphics(unittest.TestCase):
    def test_snes_plane_order_and_pixel_direction(self):
        blob = bytes((0x80, 0x40)) + bytes(14)
        tiles = decode_graphics(blob, 2)
        self.assertEqual(tiles[0][0], (1, 2, 0, 0, 0, 0, 0, 0))
        self.assertEqual(tiles[0][1:], ((0,) * 8,) * 7)
        self.assertEqual(encode_graphics(tiles, 2), blob)

    def test_storage_pool_units(self):
        blob = bytes(range(256))
        for planes in (1, 2):
            self.assertEqual(encode_graphics(decode_graphics(blob, planes), planes), blob)
            with self.assertRaises(ValueError):
                decode_graphics(blob[:-1], planes)

    def test_bad_pixels_and_shape(self):
        for tile in (((4,) * 8,) * 8, ((0,) * 7,) * 8, ((0,) * 8,) * 7):
            with self.assertRaises(ValueError):
                encode_graphics((tile,), 2)


class Sprites(unittest.TestCase):
    def test_field_and_battle_records(self):
        for size in (5, 10):
            blob = bytes(range(size * 3))
            records = decode_sprites(blob, size)
            self.assertEqual(len(records), 3)
            self.assertEqual(records[1].graphics, size)
            self.assertEqual(records[1].animation, size + 3)
            self.assertEqual(records[1].battle_fields,
                             tuple(range(size + 5, size * 2)))
            self.assertEqual(b''.join(r.encode() for r in records), blob)
            with self.assertRaises(ValueError):
                decode_sprites(blob[:-1], size)

    def test_matching_rejects_invalid_format(self):
        # Exercise the actual matching validation hook, without a ROM.
        from types import SimpleNamespace
        from tools.matching.emit import Emitter
        region = DataHint(0xC00000, 0xC00002, 'struct', None,
                          {'format': 'location-exits', 'count': 1})
        emitter = SimpleNamespace(clean={}, clusters={}, data=bytes(3),
                                  an=SimpleNamespace(hints=SimpleNamespace(data_regions=[region])))
        self.assertTrue(any('truncated exit' in p for p in Emitter.check_data(emitter)))


if __name__ == '__main__':
    unittest.main()
