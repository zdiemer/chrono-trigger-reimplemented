"""tools/dontcare_check.py on synthetic code and synthetic ports (no ROM needed).

    python3 -m unittest discover tests

The check reads every don't-care declaration in game/ (registry entries,
m_stop_at / m_stop_dont_care, m_jsr_dc / m_jsl_dc, au_driver_call) and
verifies it against tools/disasm/liveness.py at the point it applies. Here a
few made-up routines stand for the ROM and a few made-up files for game/.
"""

import unittest

from tests.test_disasm import FakeRom
from tools import dontcare_check as dc
from tools.disasm.analyze import Analyzer
from tools.disasm.hints import Hints
from tools.disasm.liveness import Liveness
from tools.disasm.memmap import bus_to_offset

# C0:8000  JSR $8010 : JSR $8020 : JSR $8030 : BCS +0 : RTS
# C0:8010  LDA #$01 : STA $20 : RTS           after it returns X and Y are read, A and C aren't
# C0:8020  STX $30 : RTS                      reads X, carries Y through
# C0:8030  TYA : STA $50 : SEC : RTS          reads Y
CODE = {
    0xC08000: bytes.fromhex("201080 202080 203080 B000 60".replace(" ", "")),
    0xC08010: bytes.fromhex("A901 8520 60".replace(" ", "")),
    0xC08020: bytes.fromhex("8630 60".replace(" ", "")),
    0xC08030: bytes.fromhex("98 8550 38 60".replace(" ", "")),
}

NATIVE_H = """
enum {
    DC_A = 1 << 0, DC_X = 1 << 1, DC_Y = 1 << 2,
    DC_N = 1 << 3, DC_V = 1 << 4, DC_Z = 1 << 5, DC_C = 1 << 6,
    DC_DB = 1 << 7, DC_D = 1 << 8, DC_AH = 1 << 9,
    DC_FLAGS = DC_N | DC_V | DC_Z | DC_C,
};
"""

REGISTRY = """
#define ENTRY(address, function, dc) { .addr = address, .name = #function, .fn = function, .dont_care = dc }
const NativeEntry game_natives[] = {
    ENTRY(0xC08010, load_one, DC_A | DC_C | DC_N | DC_Z),
    { .addr = 0xC08010, .name = "load_one_wrong", .fn = load_one_wrong, .dont_care = DC_X },
    { .addr = 0xC08000, .name = "caller", .fn = caller },
    { .addr = 0xC08000, .name = "caller_owning", .fn = caller_owning, .dont_care = DC_Y },
    { .addr = 0xC08000, .name = "caller_idle", .fn = caller_idle, .idle = true, .dont_care = DC_Y },
};
"""

PORT = """
/* a stop helper and a table of stops, as ports write them */
static const struct {
    uint16_t at;
    uint16_t dead;
} stops[] = {
    [0] = { 0x8030, DC_X },
    [1] = { 0x8030, DC_Y },
};

static void stop_here(Snes *s, int i) { m_stop_at(s, 0xC00000u | stops[i].at, stops[i].dead); }

static void call_driver(Snes *s, uint16_t target, bool takes_x)
{
    uint16_t untaken = (uint16_t)(DC_A | (takes_x ? DC_Y : DC_X));
    au_driver_call(m_platform(s), PF_CALL(0xC08003, 0xC00000u | target), untaken);
}

void caller(Snes *s)
{
    m_jsr_dc(s, 0xC08003, 0xC08020, DC_Y);
    m_jsr_dc(s, 0xC08003, 0xC08020, DC_X);
    call_driver(s, 0x8020, true);
    call_driver(s, 0x8020, false);
    stop_here(s, 0);
    stop_here(s, 1);
    m_jsl_dc(s, 0xC08003, s->cpu.a ? 0xC08020 : 0xC08030, DC_Y);
    m_jsr_dc(s, 0xC08003, m_jump_table(s, 0xC0, 0x9000, s->cpu.x), DC_X);
}

void caller_owning(Snes *s) { m_stop_at(s, 0xC08030, 0); }

void caller_idle(Snes *s) { m_stop_at(s, 0xC08030, 0); }

void load_one(Snes *s) { m_rts(s); }

void load_one_wrong(Snes *s) { m_rts(s); }

void unreadable(Snes *s) { m_stop_dont_care(s, m_read16(s, 0x7E0000), 0); }
"""


def liveness():
    h = Hints()
    h.entries.append(type("E", (), {"addr": 0xC08000, "m": True, "x": True, "kind": "test"})())
    rom = FakeRom(CODE)
    an = Analyzer(rom, h)
    an.run()
    lv = Liveness(an)
    lv.solve()
    return lv, lambda addr: rom.data[bus_to_offset(addr)]


def findings(port=PORT, registry=REGISTRY):
    lv, opcode_at = liveness()
    sources = [dc.Source("game/registry.c", registry), dc.Source("game/port.c", port)]
    found, rows, counts = dc.run_sources(sources, [dc.Source("runtime/port/native.h", NATIVE_H)], lv, opcode_at)
    return [f.text() for f in found], rows, counts


class Evaluation(unittest.TestCase):
    def test_constants_parameters_locals_and_tables(self):
        src = dc.Source("game/port.c", PORT)
        ev = dc.Evaluator([src], [dc.Source("runtime/port/native.h", NATIVE_H)])
        stops = sorted({v for v, _ in ev.values([dc.parse("0xC00000u | stops[i].at"),
                                                   dc.parse("stops[i].dead")], src, src.funcs["stop_here"])})
        # the two fields of a row stay together
        self.assertEqual(stops, [(0xC08030, 1 << 1), (0xC08030, 1 << 2)])
        untaken = sorted(v[0] for v, _ in ev.values([dc.parse("untaken")], src, src.funcs["call_driver"]))
        self.assertEqual(untaken, [1 | 1 << 1, 1 | 1 << 2])
        # a condition only known at run time: either branch
        either = sorted(v[0] for v, _ in ev.values([dc.parse("s->cpu.a ? 1 : 2")], src, src.funcs["caller"]))
        self.assertEqual(either, [1, 2])
        with self.assertRaises(dc.Unreadable):
            ev.values([dc.parse("m_read16(s, 0x7E0000)")], src, src.funcs["unreadable"])

    def test_registry_macros(self):
        src = dc.Source("game/registry.c", REGISTRY)
        ev = dc.Evaluator([src], [dc.Source("runtime/port/native.h", NATIVE_H)])
        entries, problems = dc.registry_entries(src, ev)
        self.assertFalse(problems)
        by_name = {e.name: e for e in entries}
        self.assertEqual(by_name["load_one"].dont_care, 1 | 1 << 6 | 1 << 3 | 1 << 5)
        self.assertEqual(by_name["load_one"].fn, "load_one")
        self.assertTrue(by_name["caller_idle"].idle)


class Check(unittest.TestCase):
    def test_findings(self):
        found, rows, counts = findings()
        text = "\n".join(found)
        # the entry's set at its return: X is read after C0:8010 returns
        self.assertIn("load_one_wrong's .dont_care (DC_X)", text)
        self.assertNotIn("load_one's", text)
        # a call-out's set: C0:8020 reads X, and not Y (which it only carries)
        self.assertIn("the call from $C0:8003 to $C0:8020 declares DC_X,", text)
        self.assertNotIn("declares DC_Y,", text.replace("$C0:8030 declares DC_Y", ""))
        # the same through au_driver_call, per value of the helper's parameter
        self.assertIn("declares DC_A | DC_X, where liveness has X read", text)
        # both targets of a call whose target is decided at run time
        self.assertIn("to $C0:8030 declares DC_Y, where liveness has Y read", text)
        # a stop, and a stop reached from an entry whose set applies there too
        self.assertIn("the stop declares DC_Y at $C0:8030, where liveness has Y read", text)
        self.assertIn("(with caller_owning's DC_Y) at $C0:8030", text)
        self.assertNotIn("caller_idle", text)
        # a target computed at run time: the original's call at the site says where it goes
        self.assertIn("game/port.c:28: the call from $C0:8003 to $C0:8020 declares DC_X,", text)
        # an argument that isn't a constant
        self.assertIn("game/port.c:39: can't read m_stop_dont_care", text)
        self.assertEqual(len(found), 8)
        self.assertEqual(counts["calls"], 7)   # a value per target and per helper call
        self.assertTrue(any(r.endswith(": ok") for r in rows))

    def test_a_high_byte(self):
        # DC_AH claims A's high byte alone, which C0:8010's LDA #$01 (8-bit) leaves
        # as it was: dead after the return here, as is all of A
        registry = REGISTRY.replace("DC_A | DC_C | DC_N | DC_Z", "DC_AH")
        found, _, _ = findings(registry=registry)
        self.assertFalse([f for f in found if "load_one's" in f])


if __name__ == "__main__":
    unittest.main()
