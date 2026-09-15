"""Instruction translator tests on synthetic code (no ROM needed).

    python3 -m unittest discover tests

Semantics are checked by build/main/sst_recomp_runner (SingleStepTests) and
build/main/recomp_bus_diff (against the interpreter on the SNES bus); these tests
cover what the generator relies on: every opcode translates in every context,
constants fold, and control flow and widths are reported correctly.
"""

import re
import unittest

from tools.disasm.cpu import OPCODES, decode, operand_size
from tools.recomp import sst
from tools.recomp.linear import decode_run, emit_block
from tools.recomp.translate import Ctx, Operand, indent, translate
from tests.test_disasm import FakeRom

CONTEXTS = sst.CONTEXTS


class RecordingFlow:
    def __init__(self):
        self.jumps = []

    def jump(self, pc, pb=None):
        self.jumps.append((pc, pb))
        return [f"JUMP({pc}, {pb});"]

    def exit(self):
        return ["EXIT;"]


def baked(opcode, raw, pc=0x8000, pb=0xC0, ctx=Ctx(True, True)):
    flow = RecordingFlow()
    return translate(opcode, Operand(tuple(raw)), pc, pb, ctx, flow), flow


class EveryOpcode(unittest.TestCase):
    def test_all_contexts_translate_with_constants_and_run_time_values(self):
        for opcode in range(256):
            _, mode = OPCODES[opcode]
            for ctx in CONTEXTS:
                n = operand_size(mode, ctx.m8, ctx.x8)
                t, _ = baked(opcode, [0x80 + i for i in range(n)], ctx=ctx)
                text = "\n".join(t.lines)
                # Constants fold: nothing names a run-time operand or address.
                self.assertIsNone(re.search(r"(?<!->)\b(b[123]|pc|pb)\b", text), f"{opcode:02X} {ctx.name()}: {text}")
                self.assertTrue(t.lines[0].startswith("RC_FETCH(c, 0xC08000,"), f"{opcode:02X}")
                self.assertEqual(t.size, 1 + n)
                sym = translate(opcode, Operand(tuple(f"b{i + 1}" for i in range(n))), "pc", "pb", ctx,
                                RecordingFlow())
                self.assertEqual(sym.falls_through, t.falls_through, f"{opcode:02X}")

    def test_braces_balance(self):
        for opcode in range(256):
            for ctx in CONTEXTS:
                lines = sst.function(opcode, ctx)
                depth = 0
                for line in indent(lines, 0):
                    depth += line.count("{") - line.count("}")
                    self.assertGreaterEqual(depth, 0)
                self.assertEqual(depth, 0, f"{opcode:02X} {ctx.name()}")

    def test_operand_count_is_checked(self):
        with self.assertRaises(ValueError):
            baked(0xA9, [0x12], ctx=Ctx(False, True))   # LDA #imm is 16-bit here
        with self.assertRaises(ValueError):
            Ctx(False, True, e=True)


class Templates(unittest.TestCase):
    def test_direct_page_and_long_addressing(self):
        t, _ = baked(0xA5, [0x12])
        self.assertEqual(t.lines, ["RC_FETCH(c, 0xC08000, 2, 0x12);", "rc_lda8(c, rc_rd8(c, rc_dp(c, 0x12)));"])
        t, _ = baked(0x8F, [0x34, 0x12, 0x7E], ctx=Ctx(False, True))
        self.assertIn("rc_wr16(c, 0x7E1234, c->a);", t.lines)
        t, _ = baked(0x95, [0x12], ctx=Ctx(False, True))   # dp,X data wraps in bank 0
        self.assertIn("rc_wr16_b0(c, rc_dp_index(c, 0x12, c->x), c->a);", t.lines)
        t, _ = baked(0xB5, [0x12], ctx=Ctx(True, True, True))
        self.assertIn("rc_lda8(c, rc_rd8(c, rc_dp_index_emu(c, 0x12, c->x)));", t.lines)

    def test_indexed_write_always_takes_the_extra_cycle(self):
        t, _ = baked(0xBD, [0x00, 0x20])   # LDA abs,X with 8-bit X: only on page crossing
        self.assertIn("rc_absx(c, 0x2000, false)", t.lines[1])
        t, _ = baked(0x9D, [0x00, 0x20])   # STA abs,X
        self.assertIn("rc_absx(c, 0x2000, true)", t.lines[1])
        t, _ = baked(0xBD, [0x00, 0x20], ctx=Ctx(True, False))
        self.assertIn("rc_absx(c, 0x2000, true)", t.lines[1])

    def test_jsl_fetches_around_its_pushes(self):
        t, _ = baked(0x22, [0x56, 0x34, 0x12])
        self.assertEqual(t.lines[:5], ["RC_FETCH(c, 0xC08000, 3, 0x34);", "rc_push8(c, c->pb);", "RC_IO(c, 1);",
                                       "RC_FETCH(c, 0xC08003, 1, 0x12);", "rc_push16(c, 0x8003);"])
        self.assertEqual(t.calls, [0x123456])
        self.assertFalse(t.falls_through)


class ControlFlow(unittest.TestCase):
    def test_branch_targets_fold(self):
        t, flow = baked(0xD0, [0xFE])   # BNE to itself
        self.assertEqual(flow.jumps, [(0x8000, None)])
        self.assertTrue(t.falls_through)
        t, flow = baked(0x80, [0x7F], pc=0xFFF0)   # BRA wraps within the bank
        self.assertEqual(flow.jumps, [(0x0071, None)])
        self.assertFalse(t.falls_through)

    def test_emulation_branch_page_crossing(self):
        t, _ = baked(0xF0, [0x10], pc=0x80F0, ctx=Ctx(True, True, True))   # to $8102: crosses
        self.assertIn("RC_IO(c, 2);", t.lines)
        t, _ = baked(0xF0, [0x02], pc=0x8000, ctx=Ctx(True, True, True))
        self.assertIn("RC_IO(c, 1);", t.lines)

    def test_jumps_calls_and_returns(self):
        t, flow = baked(0x5C, [0x56, 0x34, 0x12])
        self.assertEqual(flow.jumps, [(0x3456, 0x12)])
        self.assertFalse(t.falls_through)
        t, _ = baked(0x20, [0x34, 0x12])
        self.assertEqual(t.calls, [0xC01234])
        self.assertIn("rc_push16(c, 0x8002);", t.lines)
        self.assertEqual(t.lines[-2:], ["c->pc = 0x1234;", "EXIT;"])
        for opcode in (0x60, 0x6B, 0x40, 0x6C, 0x7C, 0xDC, 0xFC, 0x00, 0x02, 0xCB, 0xDB):
            t, _ = baked(opcode, [0x00] * operand_size(OPCODES[opcode][1], True, True))
            self.assertFalse(t.falls_through, f"{opcode:02X}")
            self.assertEqual(t.lines[-1], "EXIT;")

    def test_block_move_repeats_itself(self):
        t, flow = baked(0x54, [0x7E, 0x7F], pc=0x9000, ctx=Ctx(False, False))
        self.assertEqual(flow.jumps, [(0x9000, None)])
        self.assertIn("rc_wr8(c, ((uint32_t)0x7E << 16 | c->y), rc_rd8(c, ((uint32_t)0x7F << 16 | c->x)));",
                      t.lines)
        self.assertTrue(t.falls_through)


class Widths(unittest.TestCase):
    def test_rep_sep_known_plp_xce_unknown(self):
        self.assertEqual(baked(0xC2, [0x30])[0].next_ctx, Ctx(False, False))
        self.assertEqual(baked(0xC2, [0x20], ctx=Ctx(True, True))[0].next_ctx, Ctx(False, True))
        self.assertEqual(baked(0xE2, [0x10], ctx=Ctx(False, False))[0].next_ctx, Ctx(False, True))
        self.assertEqual(baked(0xC2, [0x30], ctx=Ctx(True, True, True))[0].next_ctx, Ctx(True, True, True))
        self.assertIsNone(baked(0x28, [])[0].next_ctx)
        self.assertIsNone(baked(0xFB, [])[0].next_ctx)
        sym = translate(0xC2, Operand(("b1",)), "pc", "pb", Ctx(True, True), RecordingFlow())
        self.assertIsNone(sym.next_ctx)

    def test_linear_block_follows_widths_and_gotos(self):
        # C0:8000  REP #$20 ; LDA #$1234 ; DEX ; BNE $8002 ; PLP ; LDA #$12
        rom = FakeRom({0xC08000: bytes.fromhex("C220A93412CAD0FA28A912")})
        run = decode_run(rom, 0xC08000, Ctx(True, True), 10)
        self.assertEqual([i.pc for i, _ in run], [0xC08000, 0xC08002, 0xC08005, 0xC08006, 0xC08008])
        self.assertEqual(run[1][1], Ctx(False, True))
        text = "\n".join(emit_block(run))
        self.assertIn("L_C08002:", text)
        self.assertIn("goto L_C08002;", text)
        self.assertTrue(text.rstrip().endswith("c->pc = 0x8009;\n}"))
        self.assertEqual(decode(rom.read, 0xC08002, False, True).size, 3)


if __name__ == "__main__":
    unittest.main()
