"""M3 of the static recompiler (docs/RECOMP.md), no ROM needed: blocks for RAM
sections with guards and live operands, the reset vector's emulation-mode
block, the instruction index, and the RAM section formats M3 added (`run`,
`writes` from RAM, `patch`).

    python3 -m unittest discover tests

Whether such blocks behave like the interpreter is checked over the movies
(--native on --generated only traces, shadow mode, ct_dev --misses).
"""

import unittest

from tools.disasm.analyze import Analyzer
from tools.disasm.hints import Entry, Hints, RamCodeHint
from tools.disasm.ram import build, simulate_run
from tools.generate.__main__ import index_key, instruction_index
from tools.generate.blocks import EMULATION_RESET, BlockBuilder
from tools.generate.translate import Ctx
from tests.test_disasm import FakeRom

M16X16 = Ctx(False, False)

# C0:8000  REP #$30 ; JSL $7E3000 ; STP
# C0:9000  copied to $7E:3000:
#   7E:3000  LDA #$1234
#   7E:3003  STA $3007        patches the operand of the JSR below
#   7E:3006  JSR $3100
#   7E:3009  BNE $3000
#   7E:300B  RTL
# C0:9100  copied to $7E:3100: RTS
# C0:A000  generator: LDX #$0003 ; TXA ; STA $2180 ; DEX ; BNE -7 ; LDA #$60 ; STA $2180 ; RTL
CODE = {0xC08000: bytes.fromhex("C2302200307EDB"),
        0xC09000: bytes.fromhex("A934128D0730200031D0F56B"),
        0xC09100: bytes.fromhex("60"),
        0xC0A000: bytes.fromhex("A203008A8D8021CAD0F9A9608D80216B")}


def analyzed(extra=()):
    h = Hints()
    h.entries.append(Entry(0xC08000, True, True, "test"))
    h.ramcode.append(RamCodeHint("prog", 0x7E3000, 0xC09000, "copy", 12))
    h.ramcode.append(RamCodeHint("sub", 0x7E3100, 0xC09100, "copy", 1))
    h.ramcode.extend(extra)
    an = Analyzer(FakeRom(CODE), h)
    an.run()
    return an


def blocks_of(an):
    b = BlockBuilder(an)
    return b, {(blk.entry, blk.ctx, blk.space): blk for blk in b.build_all()}


class RamBlocks(unittest.TestCase):
    def test_ram_code_gets_guarded_blocks(self):
        an = analyzed()
        b, blocks = blocks_of(an)
        prog = next(i for i, s in enumerate(an.ram.sections) if s.name == "prog")
        blk = blocks[(0x7E3000, M16X16, prog)]
        self.assertEqual(blk.name, "generated_7E3000_m16x16_prog")
        self.assertEqual(sorted(blk.steps), [0x7E3000, 0x7E3003, 0x7E3006])
        text = "\n".join(b.emit(blk))
        self.assertIn("bool generated_7E3000_m16x16_prog_guard(Snes *s)", text)
        self.assertIn(f"rc_ram_matches(s, 0x03000, generated_ram_image_{prog} + 0, generated_ram_mask_{prog} + 0, 9)", text)
        self.assertIn("RC_CODE(generated_7E3000_m16x16_prog_code, 1);", text)
        self.assertTrue(text.endswith("RC_STOP_GUARDED(generated_7E3000_m16x16_prog, "
                                      "generated_7E3000_m16x16_prog_guard);\n}"), text)

    def test_operands_are_fetched_at_run_time_and_left_out_of_the_guard(self):
        an = analyzed()
        b, blocks = blocks_of(an)
        prog = next(i for i, s in enumerate(an.ram.sections) if s.name == "prog")
        text = "\n".join(b.emit(blocks[(0x7E3000, M16X16, prog)]))
        self.assertIn("RC_FETCH_LIVE(c, 0x7E3000, 3, 0);", text)
        self.assertIn("rc_lda16(c, (uint16_t)(rc_timing.op[1] | (uint16_t)rc_timing.op[2] << 8));", text)
        self.assertIn("c->pc = (uint16_t)(rc_timing.op[1] | (uint16_t)rc_timing.op[2] << 8);", text)
        mask = b.masks[prog]
        self.assertEqual(list(mask[:9]), [0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0, 0])

    def test_a_branch_whose_operand_is_written_keeps_its_image_target(self):
        an = analyzed()
        b, blocks = blocks_of(an)
        prog = next(i for i, s in enumerate(an.ram.sections) if s.name == "prog")
        b.written.add(0x7E300A)
        b._translations.clear()
        blk = b.build(0x7E3009, M16X16, prog)
        text = "\n".join(b.emit(blk))
        self.assertIn("if ((uint16_t)(0x300B + (int8_t)rc_timing.op[1]) == 0x3000) {", text)
        self.assertIn("c->pc = (uint16_t)(0x300B + (int8_t)rc_timing.op[1]);", text)

    def test_index_has_every_instruction(self):
        an = analyzed()
        b, blocks = blocks_of(an)
        lines = instruction_index(list(blocks.values()))
        keys = [int(line.split(",")[0].strip(" {"), 16) for line in lines if line.startswith("    { 0x")]
        self.assertEqual(keys, sorted(keys))
        self.assertIn(index_key(0x7E3003, M16X16), keys)
        self.assertIn('generated_7E3000_m16x16_prog_guard, "generated_7E3000_m16x16_prog" },', "\n".join(lines))


class Emulation(unittest.TestCase):
    def test_reset_vector_block_runs_in_emulation_mode(self):
        code = {0xC08000: bytes.fromhex("7818FBDB"), 0xC0FFFC: bytes.fromhex("0080")}
        h = Hints()
        h.entries.append(Entry(0x008000, True, True, "vector"))
        an = Analyzer(FakeRom(code), h)
        an.run()
        b = BlockBuilder(an)
        blocks = {(blk.entry, blk.ctx): blk for blk in b.build_all()}
        blk = blocks[(0x008000, EMULATION_RESET)]
        self.assertEqual(blk.name, "generated_008000_emu")
        self.assertEqual(sorted(blk.steps), [0x008000, 0x008001, 0x008002])
        self.assertEqual(index_key(0x008000, EMULATION_RESET), 0x70008000)


class Sections(unittest.TestCase):
    def test_run_collects_the_port_writes(self):
        h = RamCodeHint("gen", 0x7E2000, 0xC0A000, "run", 4, None, True, False, regs={"x": 0})
        image, why = simulate_run(FakeRom(CODE), h)
        self.assertIsNone(why)
        self.assertEqual(image, bytes([3, 2, 1, 0x60]))
        bad = RamCodeHint("gen", 0x7E2000, 0xC0A000, "run", 5, None, True, False)
        self.assertIn("not the hint's 5", simulate_run(FakeRom(CODE), bad)[1])

    def test_patch_written_by_ram_code_is_an_alternative(self):
        # the store at $7E:3003 writes the JSR's operand with A from the LDA at
        # $7E:3000: a `writes` section whose code runs from the section before it
        patch = RamCodeHint("p", 0x7E3007, 0x7E3000, "writes", 2, 0x7E3003, False, False, patch="prog")
        an = analyzed([patch])
        self.assertFalse(an.ram.problems)
        sec = next(s for s in an.ram.sections if s.name == "p")
        self.assertEqual(sec.image, bytes.fromhex("3412"))
        prog = next(s for s in an.ram.sections if s.name == "prog")
        self.assertIs(an.ram.section_at(0x7E3007), prog)        # the analysis reads the patched code
        self.assertTrue(an.ram.patched(prog, 0x7E3008))
        b, _ = blocks_of(an)
        self.assertIn(0x7E3009, b.entry_addrs)                    # the address after the patch is an entry

    def test_writes_from_ram_needs_an_earlier_section(self):
        h = Hints()
        h.ramcode.append(RamCodeHint("p", 0x7E3007, 0x7E3000, "writes", 2, 0x7E3003, False, False))
        images = build(FakeRom(CODE).data, h)
        self.assertEqual(images.sections, [])
        self.assertIn("isn't ROM or an earlier section", images.problems[0])


if __name__ == "__main__":
    unittest.main()
