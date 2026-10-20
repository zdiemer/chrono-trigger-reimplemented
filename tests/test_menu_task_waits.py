"""The menus' tasks' waits against the ROM (review item 78: a stack don't-care
a port declares is tied to the disassembly by a test).

game/menu/tasks.c leaves uncompared the index registers the wait ($C2:821E)
keeps on a task's stack, under the return of the loops' JSRs to it ($C2:826C
in the main task's loop, $C2:E91E in the cursor's, $C2:8C30 in the scroll
bar's), while the task waits, and in the scroll bar's the flags it keeps under
them. That holds only if the wait pushes X, then Y, then the flags just under
the return, and nothing the loops run after the wait reads them before setting
them; the scroll bar's loop sets its accumulator 8-bit at its head, which the
port does too. Skipped without the ROM."""
import unittest

from tools import v_readers
from tools.disasm.liveness import FLAGS, X, Y, Liveness, describe
from tools.disasm.memmap import Rom, bus_to_offset

ROM = v_readers.ROOT / "roms" / "ct-usa.sfc"

THE_WAIT = 0xC2821E
PHX, PHY, PHP, JSR, SEP = 0xDA, 0x5A, 0x08, 0x20, 0xE2
# The loop whose kept flags aren't compared, and its head.
SCROLL_BAR_WAIT, SCROLL_BAR_HEAD, A_8_BIT = 0xC28C30, 0xC28BB8, 0x20
# The loops' JSRs to the wait, and where each goes on after it: the main task
# into the screen's step's lookup, the cursor's back round its loop to its
# work's call ($C2:E921 BRA $E91B, JSR $E92D), the scroll bar's back to its
# head ($C2:8C33 JMP $8BB8).
LOOPS = {0xC2826C: (0xC2826F,), 0xC2E91E: (0xC2E921, 0xC2E91B, 0xC2E92D), 0xC28C30: (0xC28C33, 0xC28BB8)}


@unittest.skipUnless(ROM.exists(), "needs the ROM (tools/rom.py stage)")
class TheLoopsAfterTheWait(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom = Rom(ROM.read_bytes())
        an, dynamic_calls = v_readers.analysis(ROM, v_readers.ROOT / "re" / "hints.toml")
        cls.lv = Liveness(an, dynamic_calls)
        cls.lv.solve()

    def byte(self, addr):
        return self.rom.data[bus_to_offset(addr)]

    def test_the_wait_keeps_x_then_y_then_the_flags_under_the_return(self):
        self.assertEqual((self.byte(THE_WAIT), self.byte(THE_WAIT + 1), self.byte(THE_WAIT + 2)), (PHX, PHY, PHP))

    def test_the_scroll_bar_s_loop_sets_its_width_at_its_head(self):
        self.assertEqual((self.byte(SCROLL_BAR_HEAD), self.byte(SCROLL_BAR_HEAD + 1)), (SEP, A_8_BIT))

    def test_the_scroll_bar_s_loop_reads_no_flag_after_it(self):
        for pc in LOOPS[SCROLL_BAR_WAIT]:
            live = self.lv.live_at(pc)
            self.assertIsNotNone(live, f"{pc:06X} isn't traced")
            self.assertFalse(live & FLAGS, f"{pc:06X} reads {describe(live & FLAGS)}")

    def test_the_loops_call_the_wait(self):
        for site in LOOPS:
            self.assertEqual(self.byte(site), JSR)
            self.assertEqual(self.byte(site + 1) | self.byte(site + 2) << 8, THE_WAIT & 0xFFFF)

    def test_the_loops_read_neither_after_it(self):
        for site, after in LOOPS.items():
            for pc in after:
                live = self.lv.live_at(pc)
                self.assertIsNotNone(live, f"{pc:06X} isn't traced")
                self.assertFalse(live & (X | Y), f"{pc:06X} (after {site:06X}) reads {describe(live & (X | Y))}")


if __name__ == "__main__":
    unittest.main()
