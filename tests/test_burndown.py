"""tools/burndown.py on synthetic code and a made-up game/ tree (no ROM needed).

    python3 -m unittest discover tests

The burndown credits a port with the code it covers: its routine up to where
it hands back, and the callees it inlines, which it reads from the port's
source as tools/dontcare_check.py reads declarations.
"""

import pathlib
import shutil
import tempfile
import unittest

from tools import burndown
from tools.disasm.analyze import Analyzer
from tools.disasm.hints import Hints
from tests.test_disasm import FakeRom

# C0:8000  JSR $8020 ; JSR $8030 ; JSR $8040 ; RTS
# C0:8020  INX ; RTS                 (a helper the port inlines)
# C0:8030  INY ; RTS                 (one it calls out to)
# C0:8040  DEX ; RTS                 (another port's entry)
# C0:8100  JSR $8020 ; NOP ; NOP ; JSR $8030 ; RTS   (a port that stops at the second NOP)
CODE = {
    0xC08000: bytes.fromhex("202080 203080 204080 60".replace(" ", "")),
    0xC08020: bytes.fromhex("E860"),
    0xC08030: bytes.fromhex("C860"),
    0xC08040: bytes.fromhex("CA60"),
    0xC08100: bytes.fromhex("202080 EA EA 203080 60".replace(" ", "")),
}

REGISTRY = """#include "registry.h"
const NativeEntry game_native_table[] = {
    { .addr = 0xC08000, .name = "outer", .fn = outer },
    { .addr = 0xC08040, .name = "other", .fn = other },
    { .addr = 0xC08100, .name = "stopper", .fn = stopper },
};
"""

PORT = """#include "port/machine.h"

static void call(Snes *s, uint32_t site, uint32_t target)
{
    m_jsr(s, site, target);
}

// $C0:8000
void outer(Snes *s)
{
    s->cpu.x++;
    call(s, 0xC08003, 0xC08030);
    other(s);
    m_rts(s);
}

// $C0:8040
void other(Snes *s)
{
    s->cpu.x--;
    m_rts(s);
}

// $C0:8100
void stopper(Snes *s)
{
    s->cpu.x++;
    m_stop_at(s, 0xC08104, 0);
}
"""


def tree(test: unittest.TestCase, port: str) -> pathlib.Path:
    root = pathlib.Path(tempfile.mkdtemp())
    test.addCleanup(shutil.rmtree, root, True)
    (root / "game").mkdir()
    (root / "game" / "registry.c").write_text(REGISTRY, encoding="utf-8")
    (root / "game" / "port.c").write_text(port, encoding="utf-8")
    return root


class Burndown(unittest.TestCase):
    def setUp(self):
        h = Hints()
        for addr in (0xC08000, 0xC08100):
            h.entries.append(type("E", (), {"addr": addr, "m": True, "x": True, "kind": "test"})())
        self.an = Analyzer(FakeRom(CODE), h)
        self.an.run()

    def cover(self, port: str):
        ports = burndown.read_ports(tree(self, port), self.an)
        covered, inlined, owners = burndown.coverage(self.an, ports)
        return {p.name: p for p in ports}, covered, inlined

    def outer(self, port: str):
        """The port at C0:8000: (its routine's bytes, the callees' it inlines)."""
        ports = burndown.read_ports(tree(self, port), self.an)
        p = next(p for p in ports if p.name == "outer")
        return (p,) + burndown.walk(self.an, p.addr, {q.addr for q in ports}, p)

    def test_inlined_callees_are_credited(self):
        ports, covered, inlined = self.cover(PORT)
        self.assertEqual(ports["outer"].sites, {0xC08003})
        # the helper it inlines is its; the one it calls out to isn't; another port's is that port's
        self.assertEqual(inlined, {0xC08020, 0xC08021})
        self.assertNotIn(0xC08030, covered)
        self.assertIn(0xC08040, covered)

    def test_no_call_is_inlined_past_a_hand_back(self):
        def stopper(port):
            ports = burndown.read_ports(tree(self, port), self.an)
            p = next(p for p in ports if p.name == "stopper")
            return (p,) + burndown.walk(self.an, p.addr, {q.addr for q in ports}, p)
        p, own, inlined = stopper(PORT)
        self.assertEqual(p.stops, {0xC08104})
        # the call before the stop is inlined; the one after it may be the original's
        self.assertEqual(inlined, {0xC08020, 0xC08021})
        # its own code after the stop still counts: a port often stops on some paths only
        self.assertIn(0xC08105, own)
        # a stop where a call returns is a port giving up after a call-out: it goes on there otherwise
        p, own, inlined = stopper(PORT.replace("0xC08104, 0", "0xC08103, 0"))
        self.assertEqual(p.stops, set())
        self.assertEqual(inlined, {0xC08020, 0xC08021, 0xC08030, 0xC08031})

    def test_a_call_out_that_cant_be_read_credits_nothing(self):
        # the site and target come from somewhere the check can't follow
        p, own, inlined = self.outer(PORT.replace("call(s, 0xC08003, 0xC08030);", "call(s, s->cpu.y, s->cpu.a);"))
        self.assertTrue(p.unreadable)
        self.assertEqual(inlined, set())
        self.assertIn(0xC08000, own)
        # a site read as something that isn't a call is a misreading, and the same
        p, _, inlined = self.outer(PORT.replace("call(s, 0xC08003, 0xC08030);", "call(s, 0xC08001, 0xC08030);"))
        self.assertTrue(p.unreadable)
        self.assertEqual(inlined, set())

    def test_a_call_out_by_its_target_alone(self):
        # a target the port names, from a site computed at run time: no call to it is inlined
        p, _, inlined = self.outer(PORT.replace("call(s, 0xC08003, 0xC08030);", "call(s, 0xC08003, 0xC08020);"))
        self.assertEqual(p.targets, {0xC08020})
        self.assertEqual(inlined, set())     # C0:8020 by its target, C0:8030 by its site


if __name__ == "__main__":
    unittest.main()
