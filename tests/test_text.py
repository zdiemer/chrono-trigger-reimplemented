"""The string encodings (tools/disasm/text.py), on synthetic data: no ROM bytes.

    python3 -m unittest discover tests
"""

import tempfile
import unittest
from pathlib import Path

from tools.disasm import text as T
from tools.disasm.analyze import Analyzer
from tools.disasm.dataformats import check_format
from tools.disasm.hints import DataHint, Entry, Hints
from tools.disasm.memmap import ROM_SIZE
from tools.disasm.resources import Resource, expand
from tools.matching.emit import Emitter
from tests.test_disasm import FakeRom
from tests.test_matching_asm import assemble

BASE = 0xFA00


def entry(i: int) -> bytes:
    """128 distinct glyph strings, some of three glyphs, one with a 16-bit code."""
    body = bytes([0xA0 + i % 26, 0xBA + i // 26])
    if i % 5 == 0:
        body += bytes([0xEF])
    if i == 7:
        body += bytes([0x01, 0x42])
    return body


def dictionary(order=None) -> T.Dictionary:
    entries = tuple(entry(i) for i in range(T.DICTIONARY_ENTRIES))
    return T.Dictionary(entries, tuple(order if order is not None else reversed(range(len(entries)))))


def glyphs(s: str) -> bytes:
    return bytes(T.CODES[c] for c in s)


class Glyphs(unittest.TestCase):
    def test_charset_round_trips_and_is_one_to_one(self):
        every = bytes(sorted(T.GLYPHS))
        self.assertEqual(T.encode_glyphs(T.decode_glyphs(every)), every)
        self.assertEqual(T.decode_glyphs(bytes([0xA0, 0xBA, 0xD4, 0xEF, 0xFF])), "Aa0 _")

    def test_bytes_without_a_character_and_16_bit_codes(self):
        blob = bytes([0xF4, 0x05, 0x02, 0x10, 0xA1])
        text = T.decode_glyphs(blob)
        self.assertEqual(text, "[$F4][$05][$0210]B")
        self.assertEqual(T.encode_glyphs(text), blob)

    def test_rejects(self):
        for bad in ("~", "[$01]", "[$0310]", "{A}", "[line]"):
            with self.assertRaises(T.TextError, msg=bad):
                T.encode_glyphs(bad)
        with self.assertRaises(T.TextError):
            T.decode_glyphs(bytes([0x01]))


class DictionaryFormat(unittest.TestCase):
    def test_round_trip_in_storage_order(self):
        d = dictionary()
        blob = T.encode_dictionary(d, BASE)
        first = int.from_bytes(blob[0:2], "little")
        last = int.from_bytes(blob[2 * 127:2 * 128], "little")
        self.assertEqual(last, BASE + 256)                     # stored first
        self.assertGreater(first, last)
        self.assertEqual(T.decode_dictionary(blob, BASE), d)
        text = T.dictionary_text(d)
        self.assertTrue(text.startswith("7F {"))
        self.assertEqual(T.parse_dictionary_text(text), d)
        self.assertEqual(T.encode_dictionary(T.parse_dictionary_text(text), BASE), blob)

    def test_rejects_a_gap_a_repeat_or_a_short_region(self):
        blob = bytearray(T.encode_dictionary(dictionary(), BASE))
        with self.assertRaises(T.TextError):
            T.decode_dictionary(bytes(blob) + b"\0", BASE)       # doesn't consume the region
        with self.assertRaises(T.TextError):
            T.decode_dictionary(bytes(blob[:-1]), BASE)          # the last entry runs out
        moved = bytearray(blob)
        moved[0] += 1                                           # entry 0 no longer where one ends
        with self.assertRaises(T.TextError):
            T.decode_dictionary(bytes(moved), BASE)
        same = list(dictionary().entries)
        same[3] = same[4]
        with self.assertRaises(T.TextError):
            T.decode_dictionary(T.encode_dictionary(T.Dictionary(tuple(same), dictionary().order), BASE), BASE)
        with self.assertRaises(T.TextError):
            T.parse_dictionary_text("00 {A}\n")


class Dialog(unittest.TestCase):
    d = dictionary()

    def round_trip(self, blob, text=None, **kw):
        got = T.decode_dialog(blob, self.d, **kw)
        if text is not None:
            self.assertEqual(got, text)
        self.assertEqual(T.encode_dialog(got, self.d, **kw), blob)
        return got

    def test_every_control_code(self):
        blob = bytearray()
        for b in range(0x01, 0x21):
            blob.append(b)
            if b in (0x01, 0x02, 0x03, 0x12):
                blob.append(0x0F)
        blob.append(0)
        text = self.round_trip(bytes(blob))
        for name in ("[delay $0F]", "[ext $0F]", "[$010F]", "[$020F]", "[$04]", "[$10]", "[line]\n",
                     "[line indent]\n", "[line key]\n", "[line key indent]\n", "[page]\n", "[page indent]\n",
                     "[page key]\n", "[page key indent]\n", "[num8]", "[num16]", "[num24]", "[pcname]",
                     "[crono]", "[magus]", "[crono2]", "[member1]", "[member3]", "[nadia]", "[item]",
                     "[epoch]"):
            self.assertIn(name, text)

    def test_dictionary_bytes_are_their_expansion_in_braces(self):
        blob = bytes([0x21, 0xA0, 0x21 + 7, 0x9F, 0])
        text = self.round_trip(blob)
        self.assertEqual(text, "{" + T.decode_glyphs(entry(0)) + "}A{" + T.decode_glyphs(entry(7)) + "}{"
                         + T.decode_glyphs(entry(0x7E)) + "}")
        self.assertIn("[$0142]", text)
        # Entry $7F is only reachable through $12, so braces can't name it.
        with self.assertRaises(T.TextError):
            T.encode_dialog("{" + T.decode_glyphs(entry(0x7F)) + "}", self.d)
        with self.assertRaises(T.TextError):
            T.encode_dialog("{ZZZ}", self.d)

    def test_glyphs_without_a_character(self):
        self.round_trip(bytes([0xF4, 0xFE, 0xA0, 0]), "[$F4][$FE]A")

    def test_end_is_a_string_end_for_the_field_dialog(self):
        blob = bytes([0xA0, 0x03, 0x00])
        self.assertEqual(T.dialog_length(blob + b"\xA1\x00", 0), 3)
        self.round_trip(blob, "A[end]")
        mid = bytes([0xA0, 0x03, 0x00, 0xA1, 0])
        self.round_trip(mid, "A[end]B")
        self.assertEqual(T.encode_dialog("A[delay $00]", self.d), blob)
        with self.assertRaises(T.TextError):
            T.decode_dialog(blob + b"\0", self.d)              # two strings in one region

    def test_operands_are_skipped_when_measuring(self):
        blob = bytes([0x12, 0x00, 0x01, 0x00, 0x03, 0x05, 0xA0, 0x00, 0xFF])
        self.assertEqual(T.dialog_length(blob, 0), 8)
        self.assertEqual(T.dialog_length(blob, 0, limit=7), None)

    def test_fragments_have_no_end(self):
        self.round_trip(bytes([0xEF, 0xA0]), " A", terminated=False)
        with self.assertRaises(T.TextError):
            T.decode_dialog(bytes([0xA0, 0x00]), self.d, terminated=False)

    def test_newlines_are_layout_only(self):
        self.assertEqual(T.encode_dialog("A[line]\nB\r\n", self.d), bytes([0xA0, 0x05, 0xA1, 0]))

    def test_rejects(self):
        for bad in ("~", "[nope]", "[delay]", "[delay $100]", "[$00]", "[$03]", "[$01]", "[$0310]", "[line $01]",
                    "[crono"):
            with self.assertRaises(T.TextError, msg=bad):
                T.encode_dialog(bad, self.d)
        for blob in (bytes([0xA0]), bytes([0x00, 0x00]), bytes([0x03])):
            with self.assertRaises(T.TextError):
                T.decode_dialog(blob, self.d)


class Menu(unittest.TestCase):
    def test_every_control_code(self):
        blob = bytearray()
        for b in range(0x01, 0x10):
            blob.append(b)
            c = T.MENU_CODES.get(b)
            for w in c.operands if c else ():
                blob += bytes(range(1, w + 1))
        blob += bytes([0x20, 0xA0, 0xFF, 0])
        text = T.decode_menu(bytes(blob))
        for name in ("[line]\n", "[at $0201]", "[number $0201 $0201]", "[string $030201]", "[name $0201]",
                     "[palette $0201]", "[tile $0201]", "[blanks $01]", "[mode $01]", "[attr $01]",
                     "[number2 $0201 $0201]", "[limited $01 $0201]", "[$0D]", "[$0F]", "[$20]A_"):
            self.assertIn(name, text)
        self.assertEqual(T.encode_menu(text), bytes(blob))

    def test_records_end_at_the_limit_or_a_terminator(self):
        blob = bytes([0xFF, 0xA0, 0xA1, 0xA2, 0xA3, 0xA0, 0xA1, 0x00])
        text = T.decode_menu(blob, width=4)
        self.assertEqual(text, "_ABC\nDAB[$00]\n")
        self.assertEqual(T.encode_menu(text, width=4), blob)
        with self.assertRaises(T.TextError):
            T.decode_menu(bytes([0xA0, 0x00, 0xA0, 0x00]), width=4)   # bytes after the terminator
        with self.assertRaises(T.TextError):
            T.encode_menu("ABCDE\n", width=4)
        with self.assertRaises(T.TextError):
            T.decode_menu(bytes([0xA0, 0x02, 0x01]))                  # operand cut off
        with self.assertRaises(T.TextError):
            T.encode_menu("{A}")


class Names(unittest.TestCase):
    def test_padding_and_icons(self):
        blob = bytes([0x20, 0xA0, 0xFF, 0xA1]) + bytes([0xEF]) * 4 + bytes([0xEF]) * 8
        text = T.decode_names(blob, 8)
        self.assertEqual(text, "[$20]A_B\n\n")
        self.assertEqual(T.encode_names(text, 8), blob)
        with self.assertRaises(T.TextError):
            T.encode_names("ABCDEFGHI\n", 8)
        with self.assertRaises(T.TextError):
            T.decode_names(blob[:-1], 8)


class Formats(unittest.TestCase):
    def test_check_format_decodes_and_reencodes(self):
        d = dictionary()
        ctx = {"dictionary": d}
        region = DataHint(0xDEFA00, 0xDEFA00, "text", None, {"format": "dialog"})
        check_format(bytes([0x21, 0xA0, 0]), region, ctx)
        with self.assertRaises(ValueError):
            check_format(bytes([0xA0]), region, ctx)
        with self.assertRaises(ValueError):
            check_format(bytes([0xA0, 0]), region, {})        # no dictionary
        blob = T.encode_dictionary(d, 0x1000)
        check_format(blob, DataHint(0xDE1000, 0xDE1000 + len(blob) - 1, "text", None,
                                    {"format": "dialog-dictionary"}), {})

    def test_resource_text_tables_carry_the_format_and_its_end(self):
        data = bytearray(ROM_SIZE)
        # A ptr16 table at C0:0000 of two strings: the first ends with $03 $00.
        data[0:4] = bytes([0x04, 0x00, 0x07, 0x00])
        data[4:7] = bytes([0xA0, 0x03, 0x00])
        data[7:10] = bytes([0xA1, 0x03, 0x00])
        data[10] = 0
        h = Hints()
        h.resources = [Resource(0xC00000, 2, None, "text", kind="ptr16", bank=0xC0, format="dialog")]
        rep = expand(h, bytes(data))
        self.assertEqual(rep.problems, [])
        strings = [(r.start, r.end, r.params) for r in h.data_regions if r.type == "text"]
        self.assertEqual(strings, [(0xC00004, 0xC00006, {"format": "dialog"}),
                                   (0xC00007, 0xC00009, {"format": "dialog"})])
        # An operand of zero doesn't end a dialog string; a plain zero scan stops there.
        data[4:8] = bytes([0x12, 0x00, 0xA0, 0x00])
        for form, end in (("dialog", 0xC00007), (None, 0xC00005)):
            h = Hints()
            h.resources = [Resource(0xC00000, 1, None, "text", kind="ptr16", bank=0xC0, format=form)]
            expand(h, bytes(data))
            self.assertEqual([(r.start, r.end) for r in h.data_regions if r.type == "text"], [(0xC00004, end)])


class Emission(unittest.TestCase):
    """The matching build writes a text region decoded, and assembles the bytes
    that text encodes to."""

    def test_regions_are_written_as_text_and_assembled_from_it(self):
        d = dictionary()
        dict_blob = T.encode_dictionary(d, 0x9000)
        string = bytes([0x21, 0xEF, 0xA0, 0x05, 0xA1, 0x03, 0x00])
        rom = FakeRom({0xC08000: bytes([0xDB]), 0xC19000: dict_blob, 0xC1A000: string})
        h = Hints()
        h.entries.append(Entry(0xC08000, True, True, "test"))
        for start, blob, form in ((0xC19000, dict_blob, "dialog-dictionary"), (0xC1A000, string, "dialog")):
            h.data.append((start, start + len(blob) - 1))
            h.data_regions.append(DataHint(start, start + len(blob) - 1, "text", None, {"format": form}))
        an = Analyzer(rom, h)
        an.run()
        em = Emitter(rom.data, an)
        files = em.build()
        self.assertFalse(em.stats.text_problems)
        self.assertEqual(dict(em.stats.text_regions), {"dialog-dictionary": 1, "dialog": 1})
        self.assertNotIn("data/text/text_C1A000.bin", files)
        text = files["data/text/text_C1A000.txt"]
        self.assertIn("[line]", text)
        self.assertIn('incbin "data/text/text_C1A000.bin"', files["bank_C1.asm"])
        self.assertFalse(em.check_data())
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp)
            for rel, content in files.items():
                (src / rel).parent.mkdir(parents=True, exist_ok=True)
                (src / rel).write_bytes(content.encode("utf-8") if isinstance(content, str) else content)
            encoded, problems = em.encode_text(src)
            self.assertEqual(problems, [])
            self.assertEqual(encoded["data/text/text_C1A000.bin"], string)
            self.assertEqual(encoded["data/text/text_C19000.bin"], dict_blob)
            res = assemble(files["rom.asm"], files={**{f"src/{k}": v for k, v in files.items()},
                                                    **{f"src/{k}": v for k, v in encoded.items()}})
            self.assertEqual(bytes(res.image), rom.data)
            # The bytes come from the text: an edit shows up in them.
            (src / "data/text/text_C1A000.txt").write_bytes(text.replace("[line]", "[line indent]").encode("utf-8"))
            self.assertNotEqual(em.encode_text(src)[0]["data/text/text_C1A000.bin"], string)
            (src / "data/text/text_C1A000.txt").write_bytes(b"~")
            self.assertTrue(em.encode_text(src)[1])


if __name__ == "__main__":
    unittest.main()
