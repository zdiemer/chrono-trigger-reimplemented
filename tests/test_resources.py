"""Resource pointer tables (tools/disasm/resources.py), on synthetic data."""

import unittest

from tools.disasm.hints import DataHint, Hints, _indices
from tools.disasm.memmap import ROM_SIZE
from tools.disasm.resources import Resource, expand

# The shortest stream tools/disasm/lz.py accepts: a segment length of 9, one
# control byte of 0 (eight literals), then a header byte of 0 to end it.
STREAM = bytes([9, 0, 0]) + b"abcdefgh" + bytes([0])
STREAM_OUT = 8


def rom(**at: bytes) -> bytes:
    data = bytearray(ROM_SIZE)
    for off, blob in at.items():
        o = int(off[1:], 16)
        data[o:o + len(blob)] = blob
    return bytes(data)


def ptr(bus: int) -> bytes:
    return bytes([bus & 0xFF, bus >> 8 & 0xFF, bus >> 16])


class ResourceTest(unittest.TestCase):
    def expand(self, data, *resources, regions=()):
        h = Hints()
        h.resources = list(resources)
        h.data_regions = list(regions)
        h.data = [(r.start, r.end) for r in h.data_regions]
        return h, expand(h, data)

    def test_lz_table(self):
        data = rom(x000000=ptr(0xC01000) + ptr(0xC02000), x001000=STREAM, x002000=STREAM)
        h, rep = self.expand(data, Resource(0xC00000, 2, "tbl", "lz"))
        self.assertEqual(rep.problems, [])
        self.assertEqual((rep.streams, rep.stream_bytes, rep.stream_output),
                         (2, 2 * len(STREAM), 2 * STREAM_OUT))
        self.assertEqual([(r.start, r.end, r.type, r.name) for r in h.data_regions],
                         [(0xC00000, 0xC00005, "ptr24", "tbl"),
                          (0xC01000, 0xC01000 + len(STREAM) - 1, "lz", None),
                          (0xC02000, 0xC02000 + len(STREAM) - 1, "lz", None)])
        self.assertEqual(h.data_regions[1].params["output"], STREAM_OUT)

    def test_repeated_entry_claims_one_region(self):
        data = rom(x000000=ptr(0xC01000) * 3, x001000=STREAM)
        h, rep = self.expand(data, Resource(0xC00000, 3, None, "lz"))
        self.assertEqual((rep.streams, rep.duplicate_entries), (1, 2))
        self.assertEqual(len(h.data_regions), 2)

    def test_entry_that_is_not_a_stream_is_a_problem(self):
        data = rom(x000000=ptr(0xC01000) + ptr(0xC02000), x001000=STREAM)
        _, rep = self.expand(data, Resource(0xC00000, 2, "tbl", "lz"))
        self.assertEqual(len(rep.problems), 1)
        self.assertIn("C0:2000", rep.problems[0])

    def test_skip_drops_an_entry(self):
        data = rom(x000000=ptr(0xC01000) + ptr(0xC02000), x001000=STREAM)
        h, rep = self.expand(data, Resource(0xC00000, 2, "tbl", "lz", skip=(1,)))
        self.assertEqual((rep.problems, rep.skipped, rep.streams), ([], 1, 1))
        self.assertEqual(len(h.data_regions), 2)

    def test_blob_with_a_fixed_size(self):
        data = rom(x000000=ptr(0xC01000) + ptr(0xC02000))
        h, rep = self.expand(data, Resource(0xC00000, 2, "tbl", "blob", type="gfx", size=0x800))
        self.assertEqual((rep.problems, rep.blobs, rep.blob_bytes), ([], 2, 0x1000))
        self.assertEqual([(r.start, r.end, r.type) for r in h.data_regions[1:]],
                         [(0xC01000, 0xC017FF, "gfx"), (0xC02000, 0xC027FF, "gfx")])

    def test_blob_extending_to_the_next_target(self):
        data = rom(x000000=ptr(0xC01000) + ptr(0xC02000))
        h, rep = self.expand(data, Resource(0xC00000, 2, "tbl", "blob", type="gfx",
                                            extent="next", ends=(0xC02FFF,)))
        self.assertEqual(rep.problems, [])
        self.assertEqual([(r.start, r.end) for r in h.data_regions[1:]],
                         [(0xC01000, 0xC01FFF), (0xC02000, 0xC02FFF)])

    def test_blob_without_an_end_is_a_problem(self):
        data = rom(x000000=ptr(0xC01000))
        _, rep = self.expand(data, Resource(0xC00000, 1, "tbl", "blob", extent="next"))
        self.assertEqual(len(rep.problems), 1)
        self.assertIn("no end", rep.problems[0])

    def test_a_mixed_region_is_split(self):
        data = rom(x000000=ptr(0xC01000), x001000=STREAM)
        mixed = DataHint(0xC00800, 0xC01FFF, "mixed", "blob")
        h, rep = self.expand(data, Resource(0xC00000, 1, "tbl", "lz"), regions=[mixed])
        self.assertEqual((rep.problems, rep.split), ([], 1))
        self.assertEqual([(r.start, r.end, r.type, r.name) for r in h.data_regions],
                         [(0xC00000, 0xC00002, "ptr24", "tbl"),
                          (0xC00800, 0xC00FFF, "mixed", "blob"),
                          (0xC01000, 0xC01000 + len(STREAM) - 1, "lz", None),
                          (0xC01000 + len(STREAM), 0xC01FFF, "mixed", None)])

    def test_a_region_equal_to_a_hand_hint_is_dropped(self):
        data = rom(x000000=ptr(0xC01000), x001000=STREAM)
        hand = DataHint(0xC01000, 0xC01000 + len(STREAM) - 1, "lz", "by_hand")
        h, rep = self.expand(data, Resource(0xC00000, 1, "tbl", "lz"), regions=[hand])
        self.assertEqual(rep.problems, [])
        self.assertEqual([r.name for r in h.data_regions], ["tbl", "by_hand"])

    def test_overlapping_a_typed_region_is_a_problem(self):
        data = rom(x000000=ptr(0xC01000), x001000=STREAM)
        hand = DataHint(0xC01004, 0xC01100, "bytes", "other")
        _, rep = self.expand(data, Resource(0xC00000, 1, "tbl", "lz"), regions=[hand])
        self.assertEqual(len(rep.problems), 1)
        self.assertIn("overlaps", rep.problems[0])

    def test_expansion_happens_once(self):
        data = rom(x000000=ptr(0xC01000), x001000=STREAM)
        h, rep = self.expand(data, Resource(0xC00000, 1, "tbl", "lz"))
        again = expand(h, data)
        self.assertIs(again, rep)
        self.assertEqual(len(h.data_regions), 2)

    def test_regions_become_data_and_index(self):
        data = rom(x000000=ptr(0xC01000), x001000=STREAM)
        h, _ = self.expand(data, Resource(0xC00000, 1, "tbl", "lz"))
        self.assertTrue(h.is_data(0xC01005))
        self.assertFalse(h.is_data(0xC01000 + len(STREAM)))

    def test_ptr16_entries_take_their_bank_from_the_hint(self):
        data = rom(x000000=bytes([0x00, 0x10, 0x00, 0x20]))
        h, rep = self.expand(data, Resource(0xC00000, 2, "tbl", "blob", type="gfx", kind="ptr16",
                                            bank=0xC0, extent="next", ends=(0xC02FFF,)))
        self.assertEqual(rep.problems, [])
        self.assertEqual([(r.start, r.end, r.type, r.name) for r in h.data_regions],
                         [(0xC00000, 0xC00003, "ptr16", "tbl"),
                          (0xC01000, 0xC01FFF, "gfx", None), (0xC02000, 0xC02FFF, "gfx", None)])

    def test_slots_cover_the_unused_tail_of_a_table(self):
        data = rom(x000000=ptr(0xC01000), x001000=STREAM)
        h, rep = self.expand(data, Resource(0xC00000, 1, "tbl", "lz", slots=4))
        self.assertEqual(rep.problems, [])
        self.assertEqual((h.data_regions[0].start, h.data_regions[0].end), (0xC00000, 0xC0000B))

    def test_text_entries_run_to_their_terminator(self):
        strings = b"one\x00two\x00three\x00"
        table = bytes([0x00, 0x10, 0x04, 0x10, 0x08, 0x10, 0x0E, 0x10])
        data = rom(x002000=table, x001000=strings)
        h, rep = self.expand(data, Resource(0xC02000, 3, "tbl", "text", kind="ptr16",
                                            bank=0xC0, slots=4))
        self.assertEqual((rep.problems, rep.strings, rep.string_bytes, rep.string_gaps),
                         ([], 3, len(strings), 0))
        self.assertEqual([(r.start, r.end, r.type) for r in h.data_regions],
                         [(0xC01000, 0xC01003, "text"), (0xC01004, 0xC01007, "text"),
                          (0xC01008, 0xC0100D, "text"), (0xC02000, 0xC02007, "ptr16")])

    def test_text_gaps_are_counted(self):
        data = rom(x002000=bytes([0x00, 0x10, 0x08, 0x10]), x001000=b"one\x00\x00\x00\x00\x00two\x00")
        _, rep = self.expand(data, Resource(0xC02000, 2, "tbl", "text", kind="ptr16", bank=0xC0))
        self.assertEqual((rep.problems, rep.strings, rep.string_gaps), ([], 2, 4))

    def test_text_without_a_terminator_is_a_problem(self):
        data = bytearray(rom(x002000=bytes([0x00, 0x10])))
        data[0x1000:0x10000] = b"\xEE" * (0x10000 - 0x1000)
        _, rep = self.expand(bytes(data), Resource(0xC02000, 1, "tbl", "text", kind="ptr16", bank=0xC0))
        self.assertEqual(len(rep.problems), 1)
        self.assertIn("terminator", rep.problems[0])

    def test_sized_entries_take_their_length_from_the_first_word(self):
        data = rom(x000000=ptr(0xC01000), x001000=bytes([4, 0]) + b"abcd")
        h, rep = self.expand(data, Resource(0xC00000, 1, "tbl", "sized", type="spc700"))
        self.assertEqual((rep.problems, rep.blobs, rep.blob_bytes), ([], 1, 6))
        self.assertEqual((h.data_regions[1].start, h.data_regions[1].end, h.data_regions[1].type),
                         (0xC01000, 0xC01005, "spc700"))

    def test_a_sized_entry_running_into_the_next_bank_is_split(self):
        data = rom(x00FFFE=bytes([0x10, 0x00]), x000000=ptr(0xC0FFFE))
        h, rep = self.expand(data, Resource(0xC00000, 1, "tbl", "sized", type="spc700"))
        self.assertEqual((rep.problems, rep.blobs, rep.blob_bytes), ([], 1, 0x12))
        self.assertEqual([(r.start, r.end) for r in h.data_regions[1:]],
                         [(0xC0FFFE, 0xC0FFFF), (0xC10000, 0xC1000F)])

    def test_fill_region_takes_what_no_other_region_claims(self):
        data = rom(x000000=ptr(0xC01000), x001000=STREAM)
        fill = DataHint(0xC00000, 0xC01FFF, "gfx", "tiles", {"fill": True})
        hand = DataHint(0xC00800, 0xC008FF, "bytes", "hand")
        h, rep = self.expand(data, Resource(0xC00000, 1, "tbl", "lz"), regions=[fill, hand])
        self.assertEqual(rep.problems, [])
        self.assertEqual([(r.start, r.end, r.type, r.name) for r in h.data_regions],
                         [(0xC00000, 0xC00002, "ptr24", "tbl"),
                          (0xC00003, 0xC007FF, "gfx", "tiles"),
                          (0xC00800, 0xC008FF, "bytes", "hand"),
                          (0xC00900, 0xC00FFF, "gfx", None),
                          (0xC01000, 0xC01000 + len(STREAM) - 1, "lz", None),
                          (0xC01000 + len(STREAM), 0xC01FFF, "gfx", None)])

    def test_a_region_crossing_a_fill_edge_is_a_problem(self):
        data = rom()
        fill = DataHint(0xC00000, 0xC00FFF, "gfx", None, {"fill": True})
        hand = DataHint(0xC00F00, 0xC01100, "bytes", "hand")
        _, rep = self.expand(data, regions=[fill, hand])
        self.assertEqual(len(rep.problems), 1)
        self.assertIn("crosses the edge", rep.problems[0])

    def test_entry_index_lists(self):
        self.assertEqual(_indices([1, "4-6", 9]), [1, 4, 5, 6, 9])
        with self.assertRaises(ValueError):
            _indices(["nope"])


if __name__ == "__main__":
    unittest.main()
