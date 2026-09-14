"""Decoder and analyzer tests on synthetic code (no ROM needed).

    python3 -m unittest discover tests
"""

import unittest

from tools.disasm.analyze import Analyzer
from tools.disasm.cpu import OPCODES, Mode, decode
from tools.disasm.hints import Hints
from tools.disasm.memmap import ROM_SIZE, Rom, bus_to_offset, parse


class FakeRom(Rom):
    def __init__(self, code: dict[int, bytes]):
        data = bytearray(b"\xDB" * ROM_SIZE)   # STP everywhere else
        for addr, blob in code.items():
            off = bus_to_offset(addr)
            data[off:off + len(blob)] = blob
        super().__init__(bytes(data))


def reader(blob: bytes, base: int = 0xC08000):
    def read(addr, n):
        i = addr - base
        return blob[i:i + n] if 0 <= i and i + n <= len(blob) else None
    return read


class OpcodeTable(unittest.TestCase):
    GROUP1 = ["ORA", "AND", "EOR", "ADC", "STA", "LDA", "CMP", "SBC"]

    def test_group1_grid(self):
        # The eight accumulator ops share one addressing-mode layout per column.
        layout = {
            0x01: Mode.DPXIND, 0x03: Mode.SR, 0x05: Mode.DP, 0x07: Mode.DPLIND,
            0x09: Mode.IMM_M, 0x0D: Mode.ABS, 0x0F: Mode.LONG, 0x11: Mode.DPINDY,
            0x12: Mode.DPIND, 0x13: Mode.SRINDY, 0x15: Mode.DPX, 0x17: Mode.DPLINDY,
            0x19: Mode.ABSY, 0x1D: Mode.ABSX, 0x1F: Mode.LONGX,
        }
        for row, name in enumerate(self.GROUP1):
            for low, mode in layout.items():
                op = row * 0x20 + low
                if op == 0x89:   # BIT #imm sits where STA #imm would be
                    self.assertEqual(OPCODES[op], ("BIT", Mode.IMM_M))
                    continue
                self.assertEqual(OPCODES[op], (name, mode), f"opcode {op:02X}")

    def test_immediate_widths(self):
        imm_m = {op for op, (_, m) in enumerate(OPCODES) if m is Mode.IMM_M}
        imm_x = {op for op, (_, m) in enumerate(OPCODES) if m is Mode.IMM_X}
        self.assertEqual(imm_m, {0x09, 0x29, 0x49, 0x69, 0x89, 0xA9, 0xC9, 0xE9})
        self.assertEqual(imm_x, {0xA0, 0xA2, 0xC0, 0xE0})

    def test_lengths(self):
        cases = [
            (b"\xA9\x34\x12", True, True, 2), (b"\xA9\x34\x12", False, True, 3),
            (b"\xA2\x34\x12", True, False, 3), (b"\x22\x56\x34\x12", True, True, 4),
            (b"\x54\x7E\x7F", True, True, 3), (b"\xDC\x00\x05", True, True, 3),
            (b"\x62\x00\x10", True, True, 3), (b"\xC2\x30", True, True, 2),
        ]
        for blob, m, x, size in cases:
            self.assertEqual(decode(reader(blob), 0xC08000, m, x).size, size, blob.hex())
        self.assertIsNone(decode(reader(b"\xA9\x00\x00"), 0xC08000, None, True))

    def test_branch_and_move_text(self):
        bra = decode(reader(b"\x80\xFE"), 0xC08000, True, True)
        self.assertEqual(bra.branch_target(), 0xC08000)
        brl = decode(reader(b"\x82\x00\x80"), 0xC08000, True, True)
        self.assertEqual(brl.branch_target(), 0xC00003)
        mvn = decode(reader(b"\x54\x7E\x7F"), 0xC08000, True, True)
        self.assertEqual(mvn.text(), "MVN $7F,$7E")   # src,dst


class MemoryMap(unittest.TestCase):
    def test_hirom(self):
        self.assertEqual(bus_to_offset(0xC00000), 0)
        self.assertEqual(bus_to_offset(0xFDC000), 0x3DC000)
        self.assertEqual(bus_to_offset(0x00FF00), 0x00FF00)
        self.assertEqual(bus_to_offset(0x80FFFC), 0x00FFFC)
        self.assertEqual(bus_to_offset(0x400000), 0)
        self.assertIsNone(bus_to_offset(0x7E0000))
        self.assertIsNone(bus_to_offset(0x001FFF))
        self.assertEqual(parse("$C0:1234"), 0xC01234)
        self.assertEqual(parse("c01234"), 0xC01234)


class Analysis(unittest.TestCase):
    def run_an(self, code, entry, m=True, x=True, hints=None):
        h = hints or Hints()
        h.entries.append(type("E", (), {"addr": entry, "m": m, "x": x, "kind": "test"})())
        an = Analyzer(FakeRom(code), h)
        an.run()
        return an

    def test_rep_sep_and_call_return_widths(self):
        # C0:8000  JSR $8010 ; LDA #$1234 (16-bit because callee returns with REP #$20) ; RTS
        # C0:8010  REP #$20 ; RTS
        code = {
            0xC08000: bytes.fromhex("201080 A93412 60".replace(" ", "")),
            0xC08010: bytes.fromhex("C220 60".replace(" ", "")),
        }
        an = self.run_an(code, 0xC08000)
        lda = an.code[0xC08003]
        self.assertEqual(list(lda.values())[0].size, 3)
        self.assertFalse(an.suspicious)

    def test_php_plp_restores(self):
        # PHP ; REP #$30 ; PLP ; LDA #$12 ; RTS  -> LDA is 8-bit again
        an = self.run_an({0xC08000: bytes.fromhex("08C230 28 A912 60".replace(" ", ""))}, 0xC08000)
        self.assertEqual(list(an.code[0xC08004].values())[0].size, 2)
        self.assertIn(0xC08006, an.code)

    def test_jump_table_bounded_by_following_code(self):
        # C0:8000 ASL ; TAX ; JMP ($8005,X)
        # C0:8005 table: $8009, $800A
        # C0:8009 RTS ; C0:800A RTS
        code = {0xC08000: bytes.fromhex("0AAA7C0580 0980 0A80 60 60".replace(" ", ""))}
        an = self.run_an(code, 0xC08000)
        jt = an.tables[0xC08002]
        self.assertEqual(jt.count, 2)
        self.assertIn(0xC08009, an.code)
        self.assertIn(0xC0800A, an.code)


if __name__ == "__main__":
    unittest.main()
