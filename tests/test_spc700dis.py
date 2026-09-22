"""SPC700 disassembler tests on synthetic bytes (no ROM needed).

    python3 -m unittest discover tests
"""

import unittest

from tools.spc700dis import OPCODES, BootBlock, boot_blocks, decode, dsp_register_name, linear, reachable


def reader(blob: bytes, base: int = 0x0200):
    def read(addr, n):
        i = addr - base
        return blob[i:i + n] if 0 <= i and i + n <= len(blob) else None
    return read


def text(blob: bytes, base: int = 0x0200) -> str:
    return decode(reader(blob, base), base).text


class OpcodeTable(unittest.TestCase):
    def test_every_opcode_has_a_size(self):
        # Operand byte counts, as runtime/apu/spc700.c fetches them.
        sizes = [op.size for op in OPCODES]
        self.assertEqual(len(sizes), 256)
        self.assertEqual({1, 2, 3}, set(sizes))
        three = {i for i, s in enumerate(sizes) if s == 3}
        for op in (0x05, 0x09, 0x0E, 0x18, 0x1F, 0x2E, 0x3F, 0x5F, 0x6E, 0x8F, 0xC5, 0xDE, 0xFA,
                   0x0A, 0xCA, 0xEA, 0x03, 0xF3, 0x1E, 0x5E, 0xE9, 0xEC, 0xC9, 0xCC):
            self.assertIn(op, three, f"{op:02X}")
        for op in (0x00, 0x01, 0x06, 0x19, 0x6F, 0x7F, 0x9E, 0xCF, 0xAF, 0xBF, 0xEF, 0xFF, 0x9F):
            self.assertEqual(sizes[op], 1, f"{op:02X}")
        for op in (0x02, 0x04, 0x10, 0x2F, 0x3A, 0x4F, 0x8D, 0xBA, 0xDA, 0xE8, 0xFE, 0xD9):
            self.assertEqual(sizes[op], 2, f"{op:02X}")

    def test_alu_grid(self):
        # The six ALU rows share one layout: even rows A,dp .. dp,dp; odd rows A,dp+X .. (X),(Y).
        for row, mn in zip(range(0, 0xC, 2), ["OR", "AND", "EOR", "CMP", "ADC", "SBC"]):
            self.assertEqual(text(bytes([row << 4 | 0x4, 0x12])), f"{mn:<5} A,$12")
            self.assertEqual(text(bytes([row << 4 | 0x5, 0x34, 0x12])), f"{mn:<5} A,!$1234")
            self.assertEqual(text(bytes([(row + 1) << 4 | 0x6, 0x34, 0x12])), f"{mn:<5} A,!$1234+Y")
            self.assertEqual(text(bytes([(row + 1) << 4 | 0x9])), f"{mn:<5} (X),(Y)")

    def test_operand_order(self):
        # dp,dp: source byte first; dp,#imm: immediate first.
        self.assertEqual(text(bytes([0xFA, 0x10, 0x20])), "MOV   $20,$10")
        self.assertEqual(text(bytes([0x8F, 0x55, 0x20])), "MOV   $20,#$55")
        self.assertEqual(text(bytes([0x09, 0x10, 0x20])), "OR    $20,$10")
        self.assertEqual(text(bytes([0x78, 0x07, 0x20])), "CMP   $20,#$07")

    def test_bit_ops(self):
        self.assertEqual(text(bytes([0xE2, 0x40])), "SET1  $40.7")
        self.assertEqual(text(bytes([0x12, 0x40])), "CLR1  $40.0")
        self.assertEqual(text(bytes([0xAA, 0x34, 0x52])), "MOV1  C,$1234.2")
        self.assertEqual(text(bytes([0x2A, 0x34, 0xF2])), "OR1   C,/$1234.7")

    def test_misc(self):
        self.assertEqual(text(bytes([0x1F, 0x00, 0x10])), "JMP   [!$1000+X]")
        self.assertEqual(text(bytes([0x4F, 0xC0])), "PCALL $C0")
        self.assertEqual(text(bytes([0x61])), "TCALL 6")
        self.assertEqual(text(bytes([0xDA, 0xF4])), "MOVW  $F4,YA")
        self.assertEqual(text(bytes([0x9E])), "DIV   YA,X")


class Decoding(unittest.TestCase):
    def test_branch_targets(self):
        # BNE back 5 from $0202, BBS forward, CBNE with a dp+X operand.
        blob = bytes([0x00, 0x00, 0xD0, 0xFB, 0x03, 0x40, 0x02, 0xDE, 0x30, 0x00])
        insns = list(linear(reader(blob), 0x0200, 0x0200 + len(blob)))
        self.assertEqual([i.addr for i in insns], [0x0200, 0x0201, 0x0202, 0x0204, 0x0207])
        self.assertEqual(insns[2].target, 0x01FF)
        self.assertEqual(insns[3].target, 0x0209)
        self.assertEqual(insns[3].text, "BBS   $40.0,$0209")
        self.assertEqual(insns[4].text, "CBNE  $30+X,$020A")

    def test_io_and_dsp_names(self):
        i = decode(reader(bytes([0x8F, 0x4C, 0xF2])), 0x0200)
        self.assertEqual(i.comment, "DSPADDR = KON")
        i = decode(reader(bytes([0xC4, 0xF4])), 0x0200)
        self.assertEqual(i.comment, "CPUIO0")
        self.assertEqual(dsp_register_name(0x32), "V3PITCHL")
        self.assertEqual(dsp_register_name(0x6D), "ESA")
        self.assertEqual(dsp_register_name(0x7F), "FIR7")

    def test_truncated(self):
        self.assertIsNone(decode(reader(bytes([0x3F, 0x00])), 0x0200))

    def test_reachable(self):
        # CALL $0206; BRA $0209 (skipping a data byte... two); $0206: RET; data; $0209: JMP [$1000+X]
        blob = bytes([0x3F, 0x06, 0x02, 0x2F, 0x04, 0xAA, 0x6F, 0xBB, 0xCC, 0x1F, 0x00, 0x10, 0xDD])
        found = reachable(reader(blob), [0x0200], 0x0200, 0x0200 + len(blob))
        self.assertEqual(sorted(found), [0x0200, 0x0203, 0x0206, 0x0209])


class BootTable(unittest.TestCase):
    def test_boot_blocks(self):
        # A synthetic bank $C7: six blocks, each a length and bytes, found by the two word tables.
        rom = bytearray(0x400000)
        base = 0x070000
        at = 0x0100
        for k in range(6):
            data = bytes([k] * (k + 2))
            rom[base + 0x10 + 2 * k:base + 0x12 + 2 * k] = at.to_bytes(2, "little")
            rom[base + 0x1C + 2 * k:base + 0x1E + 2 * k] = (0x0200 + 0x100 * k).to_bytes(2, "little")
            rom[base + at:base + at + 2] = len(data).to_bytes(2, "little")
            rom[base + at + 2:base + at + 2 + len(data)] = data
            at += 2 + len(data)
        blocks = boot_blocks(bytes(rom))
        self.assertEqual(len(blocks), 6)
        self.assertEqual(blocks[3], BootBlock(3, blocks[3].rom_addr, 0x0500, bytes([3] * 5)))
        self.assertEqual(blocks[0].reader()(0x0201, 1), bytes([0]))
        self.assertIsNone(blocks[0].reader()(0x0202, 1))


if __name__ == "__main__":
    unittest.main()
