"""The menus' tasks' waits against the ROM (review item 78: a stack don't-care
a port declares is tied to the disassembly by a test).

game/menu/tasks.c leaves uncompared the index registers the wait ($C2:821E)
keeps on a task's stack, under the return of the two loops' JSRs to it ($C2:826C
in the main task's loop, $C2:E91E in the cursor's), while the task waits. That
holds only if the wait pushes X and then Y just under the return, and nothing
the loops run after the wait reads either before setting it. Skipped without
the ROM."""
import unittest

from tools import v_readers
from tools.disasm.liveness import X, Y, Liveness, describe
from tools.disasm.memmap import Rom, bus_to_offset

ROM = v_readers.ROOT / "roms" / "ct-usa.sfc"

THE_WAIT = 0xC2821E
PHX, PHY, JSR = 0xDA, 0x5A, 0x20
# The loops' JSRs to the wait, and where each goes on after it: the main task
# into the screen's step's lookup, the cursor's back round its loop to its
# work's call ($C2:E921 BRA $E91B, JSR $E92D).
LOOPS = {0xC2826C: (0xC2826F,), 0xC2E91E: (0xC2E921, 0xC2E91B, 0xC2E92D)}


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

    def test_the_wait_keeps_x_then_y_under_the_return(self):
        self.assertEqual((self.byte(THE_WAIT), self.byte(THE_WAIT + 1)), (PHX, PHY))

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
