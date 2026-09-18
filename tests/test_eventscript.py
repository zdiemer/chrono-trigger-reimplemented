"""Event scripts (tools/disasm/eventscript.py), on synthetic data: no ROM bytes.

    python3 -m unittest discover tests
"""

import tempfile
import unittest
from pathlib import Path

from tools.disasm import eventscript as ev
from tools.disasm.analyze import Analyzer
from tools.disasm.dataformats import check_format
from tools.disasm.hints import DataHint, Entry, Hints
from tools.disasm.lz import decompress
from tools.disasm.memmap import ROM_SIZE
from tools.disasm.resources import Resource, expand
from tools.matching.emit import Emitter
from tests.test_disasm import FakeRom
from tests.test_matching_asm import assemble

# Every command's length, read from its handler (the module doc says where):
# the fixed-length ones. $2E, $4E, $88, $F1 and $FF depend on their operands
# (VariableCommands below); the 32 table entries at $C0:5F6E are invalid.
LENGTHS = {
    0x00: 1, 0x02: 3, 0x03: 3, 0x04: 3, 0x05: 3, 0x06: 3, 0x07: 3, 0x08: 1, 0x09: 1, 0x0A: 2, 0x0B: 2,
    0x0C: 2, 0x0D: 2, 0x0E: 2, 0x0F: 1, 0x10: 2, 0x11: 2, 0x12: 5, 0x13: 6, 0x14: 5, 0x15: 5, 0x16: 5,
    0x17: 1, 0x18: 3, 0x19: 2, 0x1A: 3, 0x1B: 1, 0x1C: 3, 0x1D: 1, 0x1E: 2, 0x1F: 2, 0x20: 2, 0x21: 4,
    0x22: 4, 0x23: 3, 0x24: 3, 0x25: 2, 0x26: 2, 0x27: 3, 0x28: 3, 0x29: 2, 0x2A: 1, 0x2B: 1, 0x2C: 3,
    0x2D: 2, 0x2F: 3, 0x30: 2, 0x31: 2, 0x32: 1, 0x33: 2, 0x34: 2, 0x35: 2, 0x36: 2, 0x37: 2, 0x38: 2,
    0x39: 2, 0x3B: 2, 0x3C: 2, 0x3F: 2, 0x40: 2, 0x41: 2, 0x42: 2, 0x43: 2, 0x44: 2, 0x47: 2, 0x48: 5,
    0x49: 5, 0x4A: 5, 0x4B: 6, 0x4C: 5, 0x4D: 5, 0x4F: 3, 0x50: 4, 0x51: 3, 0x52: 3, 0x53: 4, 0x54: 4,
    0x55: 2, 0x56: 4, 0x57: 1, 0x58: 4, 0x59: 4, 0x5A: 2, 0x5B: 3, 0x5C: 1, 0x5D: 3, 0x5E: 3, 0x5F: 3,
    0x60: 4, 0x61: 3, 0x62: 1, 0x63: 3, 0x64: 3, 0x65: 3, 0x66: 3, 0x67: 3, 0x68: 1, 0x69: 3, 0x6A: 1,
    0x6B: 3, 0x6C: 1, 0x6D: 1, 0x6F: 3, 0x71: 2, 0x72: 2, 0x73: 2, 0x75: 2, 0x76: 2, 0x77: 2, 0x7A: 4,
    0x7B: 5, 0x7C: 2, 0x7D: 2, 0x7E: 1, 0x7F: 2, 0x80: 2, 0x81: 2, 0x82: 2, 0x83: 3, 0x84: 2, 0x87: 2,
    0x89: 2, 0x8A: 2, 0x8B: 3, 0x8C: 3, 0x8D: 5, 0x8E: 2, 0x8F: 2, 0x90: 1, 0x91: 1, 0x92: 3, 0x94: 2,
    0x95: 2, 0x96: 3, 0x97: 3, 0x98: 3, 0x99: 3, 0x9A: 4, 0x9C: 3, 0x9D: 3, 0x9E: 2, 0x9F: 2, 0xA0: 3,
    0xA1: 3, 0xA6: 2, 0xA7: 2, 0xA8: 2, 0xA9: 2, 0xAA: 2, 0xAB: 2, 0xAC: 2, 0xAD: 2, 0xAE: 1, 0xAF: 1,
    0xB0: 1, 0xB1: 1, 0xB2: 1, 0xB3: 1, 0xB4: 1, 0xB5: 2, 0xB6: 2, 0xB7: 3, 0xB8: 4, 0xB9: 1, 0xBA: 1,
    0xBB: 2, 0xBC: 1, 0xBD: 1, 0xC0: 3, 0xC1: 2, 0xC2: 2, 0xC3: 3, 0xC4: 3, 0xC7: 2, 0xC8: 2, 0xC9: 3,
    0xCA: 2, 0xCB: 2, 0xCC: 4, 0xCD: 3, 0xCE: 3, 0xCF: 3, 0xD0: 2, 0xD1: 2, 0xD2: 3, 0xD3: 2, 0xD4: 2,
    0xD5: 3, 0xD6: 2, 0xD7: 3, 0xD8: 3, 0xD9: 7, 0xDA: 1, 0xDC: 5, 0xDD: 5, 0xDE: 5, 0xDF: 5, 0xE0: 5,
    0xE1: 5, 0xE2: 5, 0xE3: 2, 0xE4: 8, 0xE5: 8, 0xE6: 5, 0xE7: 3, 0xE8: 2, 0xEA: 2, 0xEB: 3, 0xEC: 4,
    0xED: 1, 0xEE: 1, 0xF0: 2, 0xF2: 1, 0xF3: 1, 0xF4: 2, 0xF8: 1, 0xF9: 1, 0xFA: 1, 0xFE: 18,
}
INVALID = {0x01, 0x3A, 0x3D, 0x3E, 0x45, 0x46, 0x6E, 0x70, 0x74, 0x78, 0x79, 0x85, 0x86, 0x93, 0x9B, 0xA2,
           0xA3, 0xA4, 0xA5, 0xBE, 0xBF, 0xC5, 0xC6, 0xDB, 0xE9, 0xEF, 0xF5, 0xF6, 0xF7, 0xFB, 0xFC, 0xFD}
# Where the flow goes after a command other than "next": the jump byte's
# offset for the conditions, and the commands that never move on.
CONDITIONS = {0x12: 4, 0x13: 5, 0x14: 4, 0x15: 4, 0x16: 4, 0x18: 2, 0x1A: 2, 0x27: 2, 0x28: 2, 0x2D: 1,
              0x30: 1, 0x31: 1, 0x34: 1, 0x35: 1, 0x36: 1, 0x37: 1, 0x38: 1, 0x39: 1, 0x3B: 1, 0x3C: 1,
              0x3F: 1, 0x40: 1, 0x41: 1, 0x42: 1, 0x43: 1, 0x44: 1, 0xC9: 2, 0xCC: 3, 0xCF: 2, 0xD2: 2}
STOPS = {0xB0, 0xB2, 0xB5, 0xB6, 0xDF, 0xE0, 0xE1, 0xE2}
HALT = 0xB2


def script(*objects: list, body: bytes) -> bytes:
    """A buffer: the object count, 16 function words per object (offsets
    relative to the byte after the count, as the pointers are), then body.
    An object's functions are given as offsets into body; missing ones repeat
    the last."""
    head = ev.header_size(len(objects))
    out = bytearray([len(objects)])
    for fs in objects:
        fs = list(fs) + [fs[-1]] * (16 - len(fs))
        for f in fs:
            out += (head + f - 1).to_bytes(2, "little")
    return bytes(out) + body


def operands(n: int) -> bytes:
    """n operand bytes that name no command boundary trouble: 1, 2, 3, ..."""
    return bytes(range(1, n + 1))


def lz_literals(data: bytes) -> bytes:
    """A one-segment stream of literals only (groups of eight): the length of
    the body, each group as a zero control byte and eight bytes, end header 0."""
    assert len(data) % 8 == 0
    body = b"".join(b"\x00" + data[i:i + 8] for i in range(0, len(data), 8))
    return len(body).to_bytes(2, "little") + body + b"\x00"


class CommandTable(unittest.TestCase):
    def test_every_entry_is_a_command_or_invalid(self):
        self.assertEqual(set(ev.COMMANDS) | INVALID, set(range(256)))
        self.assertFalse(set(ev.COMMANDS) & INVALID)
        self.assertEqual(set(ev.COMMANDS) - ev.VARIABLE, set(LENGTHS))
        self.assertEqual(len({c.name for c in ev.COMMANDS.values()}), len(ev.COMMANDS))

    def test_every_fixed_command_has_its_length_and_round_trips(self):
        for op, n in LENGTHS.items():
            with self.subTest(op=f"{op:02X}"):
                body = bytes([op]) + operands(n - 1)
                self.assertEqual(ev.length(body, 0), n)
                # A command, then a halt; the command's jump bytes are
                # small, so they stay inside the buffer.
                buf = script([0], body=body + bytes([HALT]) * 8)
                sc = ev.parse(buf)
                head = ev.header_size(1)
                self.assertIn(head, sc.commands)
                self.assertEqual(sc.commands[head], n)
                self.assertEqual(ev.encode(ev.decode(buf)), buf)

    def test_flow(self):
        for op in LENGTHS:
            flow = ev.COMMANDS[op].flow
            want = ("cond" if op in CONDITIONS else "stop" if op in STOPS else "return" if op == 0 else
                    "goto" if op == 0x10 else "back" if op == 0x11 else "next")
            self.assertEqual(flow, want, f"{op:02X}")
        for op, at in CONDITIONS.items():
            keys = [k for k, kind in ev.COMMANDS[op].operands]
            kinds = [kind for k, kind in ev.COMMANDS[op].operands]
            self.assertEqual(kinds[-1], "jump", f"{op:02X}")
            self.assertEqual(1 + sum(ev.SIZES[k] for k in kinds[:-1]), at, f"{op:02X}")
            self.assertEqual(keys[-1], "else")

    def test_conditions_branch_from_the_jump_byte(self):
        for op, at in CONDITIONS.items():
            with self.subTest(op=f"{op:02X}"):
                n = LENGTHS[op]
                # cond, halt (fall-through); the jump byte skips a byte no path
                # reaches to a second halt: from the jump byte, (n - at) + 2.
                cmd = bytearray([op]) + bytes(n - 1)
                if op in (0x12, 0x13, 0x14, 0x15, 0x16):
                    cmd[at - 1] = 0                              # operator: eq
                cmd[at] = (n - at) + 2
                body = bytes(cmd) + bytes([HALT, 0x09, HALT])
                sc = ev.parse(script([0], body=body))
                head = ev.header_size(1)
                self.assertEqual(sorted(p - head for p in sc.commands), [0, n, n + 2])
                self.assertIn(head + n + 2, sc.targets)


class VariableCommands(unittest.TestCase):
    CASES = [
        # (bytes, length): the operand-dependent lengths.
        (bytes([0x2E, 0x40, 1, 2, 3, 4]), 6),              # $4x/$5x: four more bytes
        (bytes([0x2E, 0x53, 1, 2, 3, 4]), 6),
        (bytes([0x2E, 0x80, 7, 5, 0, 0xA, 0xB, 0xC]), 8),  # $8x: index, length word 5, 3 data bytes
        (bytes([0x2E, 0x10]), 2),                          # anything else: the mode alone
        (bytes([0x88, 0x00]), 2),
        (bytes([0x88, 0x20, 1, 2]), 4),
        (bytes([0x88, 0x30, 1, 2]), 4),
        (bytes([0x88, 0x41, 1, 2, 3]), 5),
        (bytes([0x88, 0x54, 1, 2, 3]), 5),
        (bytes([0x88, 0x81, 4, 0, 0xA, 0xB]), 6),          # length word 4, 2 data bytes
        (bytes([0x88, 0x21]), 2),
        (bytes([0x4E, 0x00, 0x20, 0x7E, 3, 0, 0xA]), 7),   # dest, bank, length word 3, 1 data byte
        (bytes([0x4E, 0x00, 0x20, 0x7F, 2, 0]), 6),        # no data
        (bytes([0xF1, 0x00]), 2),                          # a zero color takes no rate
        (bytes([0xF1, 0xE0, 0x04]), 3),
        (bytes([0xFF, 0x90, 1, 2, 3]), 5),
        (bytes([0xFF, 0x97, 1, 2, 3]), 5),
        (bytes([0xFF, 0x91]), 2),
        (bytes([0xFF, 0x9F]), 2),
    ]

    def test_lengths_and_round_trips(self):
        for blob, n in self.CASES:
            with self.subTest(blob=blob.hex()):
                self.assertEqual(ev.length(blob + bytes(8), 0), n)
                buf = script([0], body=blob + bytes([HALT]))
                self.assertEqual(ev.parse(buf).commands[ev.header_size(1)], n)
                text = ev.decode(buf)
                self.assertNotIn("length=", text)            # rebuilt from the data
                self.assertEqual(ev.encode(text), buf)

    def test_the_data_sets_the_length_word(self):
        buf = script([0], body=bytes([0x4E, 0x00, 0x20, 0x7E, 4, 0, 0xA, 0xB, HALT]))
        text = ev.decode(buf)
        self.assertIn("data=0A0B", text)
        grown = ev.encode(text.replace("data=0A0B", "data=0A0B0C"))
        self.assertEqual(grown[ev.header_size(1) + 4:ev.header_size(1) + 9], bytes([5, 0, 0xA, 0xB, 0xC]))

    def test_modes_that_leave_and_modes_outside_the_table(self):
        for mode in (0x00, 0x8F, 0x96):
            self.assertEqual(ev.shape(bytes([0xFF, mode]), 0)[1], "stop")
        for mode in (0xA0, 0xA3, 0xFF):
            with self.assertRaises(ev.ScriptError):
                ev.shape(bytes([0xFF, mode]), 0)
        with self.assertRaises(ev.ScriptError):             # a length word below 2
            ev.shape(bytes([0x4E, 0, 0, 0x7E, 1, 0]), 0)


class Parse(unittest.TestCase):
    def test_init_pass_resumes_after_its_return(self):
        # Function 0: return, then more; the frame loop continues after the $00.
        body = bytes([0x08, 0x00, 0x09, HALT])
        sc = ev.parse(script([0], body=body))
        head = ev.header_size(1)
        self.assertEqual(sorted(p - head for p in sc.commands), [0, 1, 2, 3])
        # Another function stops at its return.
        body = bytes([HALT, 0x08, 0x00, 0x09, HALT])
        sc = ev.parse(script([0, 1], body=body))
        self.assertEqual(sorted(p - head for p in sc.commands), [0, 1, 2])

    def test_jumps_and_loops(self):
        # 0: jump_forward over two halts to 4; 4: var_increment; 6: jump_back to 4.
        # The halts are reached by no path.
        body = bytes([0x10, 0x03, HALT, HALT, 0x71, 0x05, 0x11, 0x03])
        sc = ev.parse(script([0], body=body))
        head = ev.header_size(1)
        self.assertEqual(sorted(p - head for p in sc.commands), [0, 4, 6])
        self.assertEqual({p - head for p in sc.targets}, {0, 4})
        text = ev.decode(script([0], body=body))
        self.assertIn("; unreached", text)
        self.assertIn("jump_back to=L", text)
        self.assertEqual(ev.encode(text), script([0], body=body))

    def test_problems(self):
        head = ev.header_size(1)
        sc = ev.parse(script([0], body=bytes([0x08, 0x01])))       # an invalid command
        self.assertEqual(len(sc.problems), 1)
        sc = ev.parse(script([0], body=bytes([0x08, 0x09])))       # runs off the end
        self.assertEqual(len(sc.problems), 1)
        with self.assertRaises(ev.ScriptError):
            ev.parse(bytes([3, 0, 0]))                              # header past the end
        # A function naming the end of the buffer is unused, not a problem.
        buf = bytearray(script([0], body=bytes([HALT])))
        buf[1 + 2:1 + 4] = (len(buf) - 1).to_bytes(2, "little")
        sc = ev.parse(bytes(buf))
        self.assertEqual((sc.problems, sc.at_end), ([], 1))
        self.assertIn(" END ", ev.decode(bytes(buf)))
        self.assertEqual(ev.encode(ev.decode(bytes(buf))), bytes(buf))
        self.assertEqual(sc.commands, {head: 1})

    def test_a_jump_into_an_operand(self):
        # var_clear $1B at 2 and, from the jump at 0, face_2 ($1B) at its operand.
        body = bytes([0x10, 0x02, 0x77, 0x1B, HALT])
        buf = script([0, 2], body=body)
        sc = ev.parse(buf)
        head = ev.header_size(1)
        self.assertEqual(sorted(p - head for p in sc.commands), [0, 2, 4])
        self.assertEqual({p - head: n for p, n in sc.shared.items()}, {3: 1})
        self.assertEqual(sc.uses[0x1B], 1)
        self.assertEqual(ev.encode(ev.decode(buf)), buf)

    def test_operand_values(self):
        body = bytes([0x12, 0x10, 0x03, 0x04, 0x02, 0x16, 0x20, 0x01, 0x83, 0x01, HALT, 0xFF, 0x90, 1, 2, 3, HALT])
        sc = ev.parse(script([0], body=body))
        self.assertEqual(sc.operands[(0x12, "op")], {4})
        self.assertEqual(sc.operands[(0x16, "op")], {0x83})
        self.assertEqual(sc.operands[(0xFF, "mode")], {0x90})
        text = ev.decode(script([0], body=body))
        self.assertIn("op=ge", text)
        self.assertIn("op=lt.hi", text)


class ReadableForm(unittest.TestCase):
    BODY = bytes([0x12, 0x10, 0x03, 0x00, 0x03, 0xBB, 0x05, HALT, 0xE8, 0x40, 0x11, 0x0A])

    def test_labels_and_edits(self):
        buf = script([0, 5], [8], body=self.BODY)
        text = ev.decode(buf)
        self.assertEqual(ev.encode(text), buf)
        # An inserted command moves what follows; the labels move with it.
        edited = text.replace("\tdialog string=$05", "\twait_4\n\tdialog string=$05")
        out = ev.encode(edited)
        self.assertEqual(len(out), len(buf) + 1)
        sc = ev.parse(out)
        self.assertEqual(sc.problems, [])
        self.assertEqual(sc.uses[0xB9], 1)
        self.assertEqual(ev.encode(ev.decode(out)), out)

    def test_rejects(self):
        text = ev.decode(script([0], body=self.BODY))
        for bad in (text.replace("objects 1", "objects 2"),
                    text.replace("if_var_const", "if_var_konst"),
                    text.replace("value=$03", "value=$103"),
                    text.replace("value=$03", "value=3"),
                    text.replace("string=$05", "string=$05 extra=$01"),
                    text + "\tjump_forward to=L7777\n",
                    "wait_4\n"):
            with self.assertRaises(ev.ScriptError):
                ev.encode(bad)

    def test_out_of_reach(self):
        body = bytes([0x10, 0x00, HALT])
        text = ev.decode(script([0], body=body)).replace("jump_forward to=$00",
                                                         "jump_forward to=L1000")
        with self.assertRaises(ev.ScriptError):
            ev.encode(text + "\t.db " + " ".join(["00"] * 300) + "\nL1000:\n\thalt\n")


class Commands(unittest.TestCase):
    """Runs of commands with no header, as at $C0:5D37."""

    def test_entries_and_round_trip(self):
        blob = bytes([0x72, 0x21, 0x12, 0x21, 0x07, 0x03, 0x04, 0xBA, 0x08, 0x00, 0x11, 0x09,
                      0x8B, 0x04, 0x05, 0xFF, 0x91, 0x00])
        sc = ev.parse_commands(blob, [0, 12])
        self.assertEqual(sc.problems, [])
        self.assertEqual(sum(sc.commands.values()), len(blob))
        text = ev.decode_commands(blob, [0, 12])
        self.assertIn("entries L0000 L000C", text)
        self.assertEqual(ev.encode(text), blob)
        with self.assertRaises(ev.ScriptError):
            ev.decode_commands(bytes([0x01, 0x00]), [0])    # the entry is no command
        with self.assertRaises(ev.ScriptError):
            ev.encode(text.replace("entries L0000", "entries L0001"))


class Formats(unittest.TestCase):
    def test_check_format_on_both_region_types(self):
        buf = script([0], body=bytes([0x08, 0x00, HALT]) + bytes([HALT]) * (8 - (ev.header_size(1) + 3) % 8))
        stream = lz_literals(buf)
        self.assertEqual(decompress(stream, 0), (buf, len(stream)))
        region = DataHint(0xC10000, 0xC10000 + len(stream) - 1, "lz", None, {"format": "event"})
        check_format(stream, region)
        bad = bytearray(buf)
        bad[ev.header_size(1)] = 0x01                        # decodes to .db, still round-trips
        check_format(lz_literals(bytes(bad)), DataHint(0xC10000, 0xC10000 + len(stream) - 1, "lz", None,
                                                       {"format": "event"}))
        with self.assertRaises(ValueError):                  # the header doesn't fit
            short = lz_literals(bytes([9]) + bytes(7))
            check_format(short, DataHint(0xC10000, 0xC10000 + len(short) - 1, "lz", None, {"format": "event"}))
        blob = bytes([0x08, 0x00, 0x09, 0x00])
        region = DataHint(0xC05D00, 0xC05D03, "script", None,
                          {"format": "event-commands", "entries": ["C0:5D00", "C0:5D02"]})
        check_format(blob, region)
        with self.assertRaises(ValueError):
            check_format(bytes([0x08, 0x01, 0x09, 0x00]),
                         DataHint(0xC05D00, 0xC05D03, "script", None,
                                  {"format": "event-commands", "entries": ["C0:5D01"]}))

    def test_resource_streams_carry_the_format(self):
        buf = script([0], body=bytes([HALT]) * (8 - ev.header_size(1) % 8))
        stream = lz_literals(buf)
        data = bytearray(ROM_SIZE)
        data[0:3] = bytes([0x10, 0x00, 0xC0])                # one ptr24 entry -> C0:0010
        data[0x10:0x10 + len(stream)] = stream
        h = Hints()
        h.resources = [Resource(0xC00000, 1, None, "lz", format="event")]
        rep = expand(h, bytes(data))
        self.assertEqual(rep.problems, [])
        lz = [r for r in h.data_regions if r.type == "lz"]
        self.assertEqual([(r.start, r.params) for r in lz], [(0xC00010, {"output": len(buf), "format": "event"})])


class Derivation(unittest.TestCase):
    """The `eventscript` deriver: the scripts the location records name, and
    the operand dispatches their operators and modes bound."""

    def rom(self, body: bytes):
        from tools.disasm.derive import EVENT_BUILTIN, EVENT_LOCATIONS, EVENT_STREAMS
        buf = script([0], body=body)
        buf += bytes([HALT]) * (-len(buf) % 8)
        start, end, _ = EVENT_BUILTIN
        return FakeRom({
            EVENT_LOCATIONS: bytes(14 * 512),                     # every location names script 0
            EVENT_STREAMS: bytes([0x00, 0x00, 0xC1]),             # script 0 at C1:0000
            0xC10000: lz_literals(buf),
            start: bytes([HALT]) * (end - start + 1),
            0xC06467: bytes(range(0x40, 0x60)),                   # the operator tables, 16 words
            0xC0400E: bytes(range(0x80, 0xA8)),                   # $FF's modes, 20 words
        })

    def derive(self, rom):
        from types import SimpleNamespace
        from tools.disasm.derive import derive_eventscript
        return derive_eventscript(SimpleNamespace(rom=rom), None)

    def test_targets_are_the_entries_used(self):
        # Each condition's else branch goes on to the next test.
        body = bytes([0x12, 0x10, 0x01, 0x02, 0x02, HALT, 0x13, 0x10, 0x01, 0x00, 0x84, 0x02, HALT,
                      0xFF, 0x91, 0xFF, 0x90, 1, 2, 3, HALT])
        d = self.derive(self.rom(body))
        self.assertEqual(d.problems, [])
        word = lambda base, i: 0xC00000 | (base + 2 * i) | (base + 2 * i + 1) << 8
        self.assertEqual(d.targets["if_var_const"], {word(0x40, 2)})
        self.assertEqual(d.targets["if_var_const16"], {word(0x50, 4)})      # $84: bit 7 is shifted out
        self.assertEqual(d.targets["mode"], {word(0x80, 0), word(0x80, 1)})
        self.assertEqual(d.targets["if_var_var"], set())

    def test_an_index_past_the_table_is_a_problem(self):
        d = self.derive(self.rom(bytes([0x13, 0x10, 0x01, 0x00, 0x08, 0x01, HALT])))
        self.assertEqual(len(d.problems), 1)
        self.assertEqual(d.targets["if_var_const16"], set())


class Emission(unittest.TestCase):
    """The matching build assembles a run of commands from its readable form,
    and writes a compressed script's output decoded beside the stream."""

    def test_commands_and_streams(self):
        blob = bytes([0x08, 0x00, 0x09, 0x00])
        buf = script([0], body=bytes([0x08, 0x00, HALT]) + bytes([HALT]) * (8 - (ev.header_size(1) + 3) % 8))
        stream = lz_literals(buf)
        rom = FakeRom({0xC08000: bytes([0xDB]), 0xC19000: blob, 0xC1A000: stream})
        h = Hints()
        h.entries.append(Entry(0xC08000, True, True, "test"))
        regions = ((0xC19000, blob, "script", {"format": "event-commands", "entries": ["C1:9000", "C1:9002"]}),
                   (0xC1A000, stream, "lz", {"format": "event", "output": len(buf)}))
        for start, b, kind, params in regions:
            h.data.append((start, start + len(b) - 1))
            h.data_regions.append(DataHint(start, start + len(b) - 1, kind, None, params))
        an = Analyzer(rom, h)
        an.run()
        em = Emitter(rom.data, an)
        files = em.build()
        self.assertFalse(em.stats.text_problems)
        self.assertEqual(dict(em.stats.text_regions), {"event-commands": 1})
        self.assertEqual((em.stats.event_streams, em.stats.event_bytes), (1, len(buf)))
        self.assertNotIn("data/script/script_C19000.bin", files)
        self.assertIn("refuse_calls", files["data/script/script_C19000.txt"])
        self.assertEqual(files["data/lz/lz_C1A000.bin"], stream)
        self.assertIn("objects 1", files["data/lz/lz_C1A000.txt"])
        self.assertFalse(em.check_data())
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp)
            for rel, content in files.items():
                (src / rel).parent.mkdir(parents=True, exist_ok=True)
                (src / rel).write_bytes(content.encode("utf-8") if isinstance(content, str) else content)
            encoded, problems = em.encode_text(src)
            self.assertEqual(problems, [])
            self.assertEqual(encoded, {"data/script/script_C19000.bin": blob})
            res = assemble(files["rom.asm"], files={**{f"src/{k}": v for k, v in files.items()},
                                                    **{f"src/{k}": v for k, v in encoded.items()}})
            self.assertEqual(bytes(res.image), rom.data)
            # The decoded stream is checked against the stream's output.
            txt = src / "data/lz/lz_C1A000.txt"
            txt.write_bytes(txt.read_bytes().replace(b"refuse_calls", b"accept_calls"))
            self.assertEqual(len(em.encode_text(src)[1]), 1)


if __name__ == "__main__":
    unittest.main()
