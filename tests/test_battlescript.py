"""The battle script formats (tools/disasm/battlescript.py), on synthetic data:
no ROM bytes.

    python3 -m unittest discover tests
"""

import unittest

from tools.disasm import battlescript as B
from tools.disasm.dataformats import check_format
from tools.disasm.hints import DataHint
from tools.disasm.memmap import ROM_SIZE, Rom, bus_to_offset


def anim_command(op: int) -> bytes:
    """Command `op` with operand bytes 1, 2, ... ($80: a count of 3)."""
    c = B.ANIM[op]
    if c.length is None:
        return bytes([op, 0x03, 0x11, 0x22])
    return bytes([op]) + bytes(range(1, c.length))


def rom_with(parts: dict) -> Rom:
    data = bytearray(b"\xFF" * ROM_SIZE)
    for addr, blob in parts.items():
        o = bus_to_offset(addr)
        data[o:o + len(blob)] = blob
    return Rom(bytes(data))


def words(*ws) -> bytes:
    return b"".join(w.to_bytes(2, "little") for w in ws)


class AnimationCommands(unittest.TestCase):
    def test_every_command_round_trips_with_its_length(self):
        ctx = B.AnimContext()
        for op in range(0x100):
            with self.subTest(op=f"{op:02X}"):
                blob = anim_command(op)
                text = B.decode_animation(blob, 0xCE1000, ctx)
                self.assertEqual(text.count("\n"), 1)
                self.assertNotIn("db", text)
                self.assertEqual(B.encode_animation(text, 0xCE1000), blob)

    def test_names_are_distinct_and_lengths_known(self):
        self.assertEqual(len(B.ANIM_BY_NAME), 0x100)
        for op, c in B.ANIM.items():
            self.assertTrue(c.length is None or c.length >= 1)
            self.assertTrue(all(0 < o < (c.length or 2) for o in c.places + c.directions))
        self.assertIsNone(B.ANIM[0x80].length)
        self.assertEqual(B.ANIM[0x1E].kind, "call")
        self.assertEqual(B.ANIM[0xDB].kind, "halt")          # not dispatched

    def test_a_sequence_and_the_variable_command(self):
        blob = anim_command(0x02) + bytes([0x80, 0x12, 0xAA]) + anim_command(0x43) + bytes([0x00])
        text = B.decode_animation(blob, 0xCE1000, B.AnimContext())
        self.assertIn("copy_53 $12 $AA", text)
        self.assertEqual(B.encode_animation(text, 0xCE1000), blob)

    def test_header_calls_and_labels(self):
        start = 0xCE2000
        # a call table of 2 words, a header with channels 0 and 15, two scripts
        blob = (words(0x2010, 0x2012) + bytes([0x80, 0x01, 0x00, 0x00]) + words(0x2010, 0x9999)
                + b"\x00" * 4 + bytes([0x1E, 0x01, 0x00]) + bytes([0x1F]))
        ctx = B.AnimContext(headers={start + 4}, tables={start: 2})
        text = B.decode_animation(blob, start, ctx)
        self.assertIn("calls L_CE2010 L_CE2012", text)
        self.assertIn("header $8001 $0000 L_CE2010 $9999", text)
        self.assertIn("L_CE2010:\n\tcall $01", text)
        self.assertEqual(B.encode_animation(text, start), blob)

    def test_bytes_where_a_command_would_cross_a_boundary(self):
        start = 0xCD3000
        # $43 is 4 bytes, but a header starts at +2
        blob = bytes([0x43, 0x01, 0x80, 0x00, 0x00, 0x00]) + words(0x3008) + bytes([0x00])
        ctx = B.AnimContext(headers={start + 2})
        text = B.decode_animation(blob, start, ctx)
        self.assertIn("\tdb $43 $01\n", text)
        self.assertIn("header $8000 $0000 L_CD3008", text)
        self.assertEqual(B.encode_animation(text, start), blob)
        with self.assertRaises(B.ScriptError):                   # the header's pointer is missing
            B.decode_animation(blob[:6], start, ctx)
        blob = bytes([0x43, 0x01, 0x02])                          # runs past the region
        self.assertEqual(B.decode_animation(blob, start, B.AnimContext()), "\tdb $43 $01 $02\n")

    def test_pointer_into_a_command_is_rejected(self):
        start = 0xCE4000
        blob = bytes([0x80, 0x00, 0x00, 0x00]) + words(0x4007) + bytes([0x02, 0x05])
        with self.assertRaises(B.ScriptError):
            B.decode_animation(blob, start, B.AnimContext(headers={start}))

    def test_encoder_errors(self):
        for text in ("\tanim\n", "\tanim $01 $02\n", "\tnope\n", "L_CE0001:\n\tend\n",
                     "\theader $8000 $0000\n", "\tcalls L_CE0100\n", "\tdb $100\n"):
            with self.subTest(text=text), self.assertRaises(B.ScriptError):
                B.encode_animation(text, 0xCE0000)


def anim_rom(script: bytes, sub: bytes = bytes([0x1F]), header=(0x8000, 0x0000)) -> Rom:
    """One header in $CD:5EC6's first entry naming `script`, and a call table
    whose every entry is `sub`."""
    return rom_with({
        0xCD5EC6: words(0x0380) + b"\xFF" * 40,
        0xCE0380: bytes([header[0] >> 8, header[0] & 0xFF, header[1] >> 8, header[1] & 0xFF]) + words(0x0390),
        0xCE0390: script,
        0xCE0000: words(*([0xD000] * B.CALL_COUNT)),
        0xCED000: sub,
    })


class AnimationParse(unittest.TestCase):
    rom = staticmethod(anim_rom)

    def test_roots_places_directions_and_calls(self):
        script = (bytes([0x12, 0x05, 0x72, 0x1F, 0x76, 0x03, 0x04, 0x1E, 0x02]) + bytes([0x47, 0x09])
                  + bytes([0x01, 0x00]))
        p = B.parse_animation(self.rom(script))
        self.assertEqual(p.problems, [])
        self.assertIn(0xCE0390, p.roots)
        self.assertEqual(p.places, {5, 3, 4, 9})
        self.assertEqual(p.directions, {0x1F})
        self.assertEqual(p.calls, {2})
        self.assertIn(0xCE039B, p.commands)           # past the call: the subroutine returns
        self.assertNotIn(0xCE039C, p.commands)        # after the halt
        self.assertEqual(B._index(0x80), 0)           # an 8-bit ASL
        self.assertEqual(B._index(0x1F), 0x3E)

    def test_a_call_that_never_returns_ends_the_flow(self):
        p = B.parse_animation(self.rom(bytes([0x1E, 0x00, 0x02, 0x01]), sub=bytes([0x00])))
        self.assertNotIn(0xCE0392, p.commands)
        p = B.parse_animation(self.rom(bytes([0x1E, 0x00, 0x02, 0x01]), sub=bytes([0x21, 0x00])))
        self.assertIn(0xCE0392, p.commands)           # $21 can return

    def test_second_mask_pointers_follow_the_first(self):
        rom = self.rom(bytes([0x00]), header=(0x8000, 0x4000))
        self.assertEqual(B.header_size(rom.read, 0xCE0380), (0x8000, 0x4000, 8))

    def test_problems(self):
        p = B.parse_animation(self.rom(bytes([0x1E, 0x60])))
        self.assertTrue(any("past the 78-entry table" in x for x in p.problems))
        p = B.parse_animation(self.rom(bytes([0x80, 0x10, 0x00])))
        self.assertTrue(any("256 bytes" in x for x in p.problems))


def section(*blocks) -> bytes:
    out = b""
    for conds, acts in blocks:
        out += b"".join(conds) + b"\xFE" + b"".join(acts) + b"\xFE"
    return out + b"\xFF"


class EnemyScripts(unittest.TestCase):
    def test_every_condition_and_action_round_trips(self):
        start = 0xCC1000
        acts = [bytes([a]) + bytes(range(1, n)) for a, n in sorted(B.ACTION_SIZES.items())]
        conds = [bytes([c, 1, 2, 3]) for c in range(B.CONDITION_COUNT)]
        body = section(*[([c], [acts[i % len(acts)]]) for i, c in enumerate(conds)]) + section((conds[:2], acts))
        blob = words(0x1004, 0x1004) + body
        text = B.decode_enemy(blob, start, 2)
        self.assertIn("scripts L_CC1004 L_CC1004", text)
        for c in range(B.CONDITION_COUNT):
            self.assertIn(f"\tif_{B.COND[c].name} $01 $02 $03\n", text)
        for a in B.ACTION_SIZES:
            self.assertIn(f"\t{B.ACTIONS[a].name}", text)
        self.assertEqual(B.encode_enemy(text, start), blob)

    def test_labels_inside_a_section_and_an_unterminated_block(self):
        start = 0xCC2000
        s1 = bytes([0x00, 0, 0, 0, 0xFE, 0x01, 0, 6, 1, 0xFE]) + bytes([0x05, 2, 0, 0, 0xFE, 0x0F, 9, 0xFF])
        blob = words(0x2004, 0x200E) + s1
        text = B.decode_enemy(blob, start, 2)
        self.assertIn("L_CC200E:\n\tif_enemies_at_most $02 $00 $00", text)
        self.assertIn("\tmessage $09\n\tdone", text)      # no `end` before the $FF
        self.assertEqual(B.encode_enemy(text, start), blob)
        with self.assertRaises(B.ScriptError):
            B.decode_enemy(words(0x2005) + s1, start, 1)  # a pointer into a condition

    def test_bad_bytes(self):
        for body in (bytes([0x29, 0, 0, 0, 0xFE, 0xFE, 0xFF]),     # no such condition
                     bytes([0x00, 0, 0, 0, 0xFE, 0x03, 0xFE, 0xFF]),  # action $03 has no length
                     bytes([0x00, 0, 0, 0, 0xFE, 0x02, 1])):         # runs past the region
            with self.subTest(body=body.hex()), self.assertRaises(B.ScriptError):
                B.decode_enemy(words(0) + body, 0xCC3000, 1)
        with self.assertRaises(B.ScriptError):
            B.encode_enemy("\tattack $01\n", 0xCC3000)

    def rom(self, scripts: bytes, pointers):
        lengths = bytes(B.ACTION_TABLE_BYTES.get(i, 0) for i in range(B.ACTION_COUNT))
        return rom_with({B.ACTION_LENGTHS: lengths, B.ENEMY_POINTERS: words(*pointers) + scripts})

    def test_parse_reads_scripts_as_the_evaluators_do(self):
        base = (B.ENEMY_POINTERS + 2 * B.ENEMY_COUNT) & 0xFFFF
        attack = section(([bytes([0x05, 1, 0, 0]), bytes([0x09, 0, 1, 2])], [bytes([0x0F, 3])]),
                         ([bytes([0x00, 0, 0, 0])], [bytes([0x01, 0, 6, 1])]),
                         ([bytes([0x07, 0, 0, 0])], [bytes([0x01, 0, 6, 1])]))   # after an `always`
        reaction = section(([bytes([0x12, 0, 0, 0])], [bytes([0x04]), *[bytes([0x01, 0, 6, 1])] * 4]))
        rom = self.rom(attack + reaction + b"\x00", [base] * B.ENEMY_COUNT)
        p = B.parse_enemy(rom)
        self.assertEqual(p.problems, [])
        self.assertEqual(len(p.sections), 2)
        self.assertEqual(set(p.conditions), {0x05, 0x09, 0x00, 0x12})
        self.assertEqual(p.actions, {0x0F, 0x01, 0x04})
        self.assertEqual(B.condition_targets(rom, {1}), {0xC1FFFF})

    def test_parse_checks_the_length_table(self):
        rom = rom_with({B.ACTION_LENGTHS: b"\x09" * B.ACTION_COUNT})
        self.assertTrue(B.parse_enemy(rom).problems)


class Derivations(unittest.TestCase):
    """The derivers' checks on the code they stand on, with a code set of
    single instructions."""

    def analyzer(self, rom, extra=()):
        from types import SimpleNamespace
        from tools.disasm.cpu import decode
        code = {}
        for pc, table in B.ROOT_LOADS.items():
            raw = bytes([0xBF]) + table.to_bytes(3, "little")        # LDA long,X
            code[pc] = {(True, False): decode(lambda a, n, r=raw, p=pc: r[a - p:a - p + n], pc, True, False)}
        for pc, raw in extra:
            code[pc] = {(True, False): decode(lambda a, n, r=raw, p=pc: r[a - p:a - p + n], pc, True, False)}
        return SimpleNamespace(rom=rom, code=code)

    def test_targets_and_checks(self):
        script = bytes([0x12, 0x05, 0x72, 0x01, 0x00])
        rom = anim_rom(script)
        d = B.derive_animation(self.analyzer(rom), None)
        self.assertEqual(d.problems, [])
        self.assertEqual(d.targets["places"], {0xC10000 | rom.u16(B.PLACES + 10)})
        self.assertEqual(d.targets["directions"], {0xC10000 | rom.u16(B.DIRECTIONS + 2)})
        # a store to channel 2's pointer nothing lists
        d = B.derive_animation(self.analyzer(rom, [(0xC1ABCD, bytes([0x8D, 0xC5, 0x5D]))]), None)
        self.assertTrue(any("C1:ABCD" in p for p in d.problems))
        an = self.analyzer(rom)
        del an.code[0xC1AFD6]
        self.assertTrue(any("C1:AFD6" in p for p in B.derive_enemy(an, None).problems))


class CheckFormat(unittest.TestCase):
    def test_check_format_decodes_and_reencodes(self):
        region = DataHint(0xCC1000, 0xCC1000 + 6, "script", None, {"format": "enemy-ai", "count": 1})
        blob = words(0x1002) + bytes([0x00, 0, 0, 0, 0xFE])
        with self.assertRaises(ValueError):          # the section doesn't end
            check_format(blob, region, {})
        region = DataHint(0xCE1000, 0xCE1001, "script", None, {"format": "battle-animation"})
        check_format(bytes([0x02, 0x05]), region, {"battle-animation": B.AnimContext()})
        with self.assertRaises(ValueError):
            check_format(bytes([0x02, 0x05]), region, {})


if __name__ == "__main__":
    unittest.main()
