"""Block formation tests on synthetic code (no ROM needed).

    python3 -m unittest discover tests

Whether generated blocks behave like the interpreter is checked by shadow mode
over the movies (docs/RECOMP.md); these tests cover how entries are chosen and
where blocks end.
"""

import unittest

from tools.disasm.analyze import Analyzer
from tools.disasm.hints import Hints
from tools.recomp.blocks import BlockBuilder
from tools.recomp.translate import Ctx
from tests.test_disasm import FakeRom

M8X8, M16X8 = Ctx(True, True), Ctx(False, True)

# C0:8000  LDX #$05
# C0:8002  DEX              loop head
# C0:8003  BNE $8002
# C0:8005  JSR $8010
# C0:8008  LDA $4212        returns here with 16-bit A; a polling loop
# C0:800B  BPL $8008
# C0:800D  RTS
# C0:8010  REP #$20
# C0:8012  RTS
PROGRAM = {0xC08000: bytes.fromhex("A205CAD0FD201080AD124210FB60"), 0xC08010: bytes.fromhex("C22060")}


def builder(registry=()):
    h = Hints()
    h.entries.append(type("E", (), {"addr": 0xC08000, "m": True, "x": True, "kind": "test"})())
    an = Analyzer(FakeRom(PROGRAM), h)
    an.run()
    b = BlockBuilder(an, set(registry))
    blocks = {(blk.entry, blk.ctx): blk for blk in b.build_all()}
    return b, blocks


class Entries(unittest.TestCase):
    def test_contexts_returns_and_loop_heads(self):
        b, blocks = builder()
        self.assertEqual(set(blocks), {(0xC08000, M8X8), (0xC08002, M8X8), (0xC08008, M16X8), (0xC08010, M8X8)})

    def test_registered_addresses_end_blocks(self):
        b, blocks = builder(registry={0xC08003})
        self.assertIn((0xC08003, M8X8), blocks)
        self.assertEqual(sorted(blocks[(0xC08002, M8X8)].steps), [0xC08002])


class Blocks(unittest.TestCase):
    def test_reaching_another_entry_ends_the_block(self):
        b, blocks = builder()
        blk = blocks[(0xC08000, M8X8)]
        self.assertEqual(sorted(blk.steps), [0xC08000])
        text = "\n".join(b.emit(blk))
        self.assertTrue(text.endswith("c->pc = 0x8002;\nreturn;\n}"), text)

    def test_loop_yields_at_its_back_edge(self):
        b, blocks = builder()
        blk = blocks[(0xC08002, M8X8)]
        self.assertEqual(sorted(blk.steps), [0xC08002, 0xC08003, 0xC08005])
        text = "\n".join(b.emit(blk))
        self.assertIn("L_C08002:", text)
        self.assertIn("if (RC_YIELD(c)) {\nc->pc = 0x8002;\nreturn;\n}\ngoto L_C08002;", text)
        self.assertIn("rc_push16(c, 0x8007);\nc->pc = 0x8010;\nreturn;", text)
        self.assertEqual(text.count("RC_INSN(c);"), 3)

    def test_polling_loop_returns_on_every_pass(self):
        b, blocks = builder()
        blk = blocks[(0xC08008, M16X8)]
        self.assertTrue(blk.polls)
        text = "\n".join(b.emit(blk))
        self.assertNotIn("RC_YIELD", text)
        self.assertNotIn("L_C08008", text)
        self.assertIn("rc_lda16(c, rc_rd16(c, rc_abs(c, 0x4212)));", text)

    def test_widths_follow_rep(self):
        b, blocks = builder()
        blk = blocks[(0xC08010, M8X8)]
        self.assertEqual([s.ctx for _, s in sorted(blk.steps.items())], [M8X8, M16X8])


if __name__ == "__main__":
    unittest.main()
