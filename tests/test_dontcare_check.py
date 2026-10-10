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
#
# and, for A's two bytes (8-bit A):
# C0:8040  JSR $8050 : XBA : STA $40 : RTS    after $8050 returns A's high byte is read, its low byte isn't
# C0:8050  LDA #$02 : RTS                     reads nothing
# C0:8058  LDA #$03 : RTS
# C0:8060  STA $60 : RTS                      reads A's low byte (8-bit), not its high byte
# C0:8070  JSR $8058 : STA $70 : RTS          after $8058 returns A's low byte is read
CODE = {
    0xC08000: bytes.fromhex("201080 202080 203080 B000 60".replace(" ", "")),
    0xC08010: bytes.fromhex("A901 8520 60".replace(" ", "")),
    0xC08020: bytes.fromhex("8630 60".replace(" ", "")),
    0xC08030: bytes.fromhex("98 8550 38 60".replace(" ", "")),
    0xC08040: bytes.fromhex("205080 EB 8540 60".replace(" ", "")),
    0xC08050: bytes.fromhex("A902 60".replace(" ", "")),
    0xC08058: bytes.fromhex("A903 60".replace(" ", "")),
    0xC08060: bytes.fromhex("8560 60".replace(" ", "")),
    0xC08070: bytes.fromhex("205880 8570 60".replace(" ", "")),
    # the accumulator's width: after $8130 returns its caller sets it again
    # (REP #$20) before reading A; after $8138 returns its caller reads A first
    0xC08100: bytes.fromhex("203081 C220 A510 60".replace(" ", "")),
    0xC08110: bytes.fromhex("203881 A510 60".replace(" ", "")),
    0xC08130: bytes.fromhex("A901 60".replace(" ", "")),
    0xC08138: bytes.fromhex("A901 60".replace(" ", "")),
}

NATIVE_H = """
enum {
    DC_A = 1 << 0, DC_X = 1 << 1, DC_Y = 1 << 2,
    DC_N = 1 << 3, DC_V = 1 << 4, DC_Z = 1 << 5, DC_C = 1 << 6,
    DC_DB = 1 << 7, DC_D = 1 << 8, DC_AH = 1 << 9, DC_AL = 1 << 10,
    DC_WIDTH = 1 << 11,
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
    for addr in (0xC08000, 0xC08040, 0xC08060, 0xC08070, 0xC08100, 0xC08110):
        h.entries.append(type("E", (), {"addr": addr, "m": True, "x": True, "kind": "test"})())
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

    def test_fields_declared_together(self):
        # `uint16_t at, pad;` declares two fields: the columns after it stay in place
        src = dc.Source("game/port.c", """
static const struct { uint16_t at, pad; uint16_t dead; } rows[] = {
    { 0x8030, 0x9999, DC_X },
    { 0x8031, 0x8888, DC_Y },
};
static void stop_row(Snes *s, int i) { m_stop_at(s, 0xC00000u | rows[i].at, rows[i].dead); }
void caller(Snes *s) { stop_row(s, 0); stop_row(s, 1); }
""")
        self.assertEqual(src.tables["rows"][0], ["at", "pad", "dead"])
        self.assertEqual(dc.struct_fields(" uint8_t *p, n[4]; unsigned bits : 3; "), ["p", "n", "bits"])
        ev = dc.Evaluator([src], [dc.Source("runtime/port/native.h", NATIVE_H)])
        stops = sorted({v for v, _ in ev.values([dc.parse("0xC00000u | rows[i].at"), dc.parse("rows[i].dead")],
                                                 src, src.funcs["stop_row"])})
        self.assertEqual(stops, [(0xC08030, 1 << 1), (0xC08031, 1 << 2)])

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
        self.assertTrue(any(": ok" in r for r in rows))

    def test_bridge_calls(self):
        """The bridges' vocabulary declares its call where it is called, and its
        own bodies, whose arguments are parameters, declare nothing."""
        lv, opcode_at = liveness()
        bridge = dc.Source(dc.BRIDGE_FILE, """
bool bridge_back(Snes *s, uint32_t returns_to, BridgeMachine m) { m_stop_at_own(s, returns_to, 0); return true; }
bool bridge_call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, BridgeMachine m)
{ m_jsr_dc(s, site, routine, dont_care); return bridge_back(s, site + JSR_SIZE, m); }
""")
        port = dc.Source("game/port.c", """
void calls(Snes *s) { bridge_call(s, 0xC08003, 0xC08020, DC_X, bridge_machine_now(s)); }
void carries(Snes *s) { bridge_call(s, 0xC08003, 0xC08020, DC_Y, bridge_machine_now(s)); }
""")
        registry = dc.Source("game/registry.c", REGISTRY)
        found, _, counts = dc.run_sources([registry, bridge, port], [dc.Source("runtime/port/native.h", NATIVE_H)],
                                          lv, opcode_at)
        text = "\n".join(f.text() for f in found)
        self.assertIn("game/port.c:2: the call from $C0:8003 to $C0:8020 declares DC_X,", text)
        self.assertNotIn("declares DC_Y", text)
        self.assertNotIn(dc.BRIDGE_FILE, text)
        self.assertEqual(counts["calls"], 2)

    def test_a_high_byte(self):
        # DC_AH claims A's high byte alone, which C0:8010's LDA #$01 (8-bit) leaves
        # as it was: dead after the return here, as is all of A
        registry = REGISTRY.replace("DC_A | DC_C | DC_N | DC_Z", "DC_AH")
        found, _, _ = findings(registry=registry)
        self.assertFalse([f for f in found if "load_one's" in f])



# Entries and ports for A's low byte alone and a stop's own set.
REGISTRY_SPLIT = """
const NativeEntry game_natives[] = {
    { .addr = 0xC08050, .name = "load_two", .fn = load_two, .dont_care = DC_AL },
    { .addr = 0xC08050, .name = "load_two_high", .fn = load_two_high, .dont_care = DC_AH },
    { .addr = 0xC08050, .name = "load_two_whole", .fn = load_two_whole, .dont_care = DC_A },
    { .addr = 0xC08058, .name = "load_three", .fn = load_three, .dont_care = DC_AL },
    { .addr = 0xC08030, .name = "store_y", .fn = store_y, .dont_care = DC_X | DC_Y },
    { .addr = 0xC08030, .name = "store_y_shared", .fn = store_y_shared, .dont_care = DC_X | DC_Y },
};
"""

PORT_SPLIT = """
void load_two(Snes *s) { m_rts(s); }
void load_two_high(Snes *s) { m_rts(s); }
void load_two_whole(Snes *s) { m_rts(s); }
void load_three(Snes *s) { m_rts(s); }

/* returns (X and Y dead after), or leaves early for code that reads Y */
void store_y(Snes *s)
{
    if (s->cpu.y) {
        m_stop_at_own(s, 0xC08030, DC_X);
        return;
    }
    m_rts(s);
}

/* the same stop, but the entry's set is added there */
void store_y_shared(Snes *s) { m_stop_at(s, 0xC08030, DC_X); }

/* an own set that is wrong by itself */
void store_y_wrong(Snes *s) { m_stop_at_own(s, 0xC08030, DC_Y); }

void call_low(Snes *s)
{
    m_jsr_dc(s, 0xC08040, 0xC08050, DC_AL | DC_Y);
    m_jsr_dc(s, 0xC08070, 0xC08060, DC_AH);
    m_jsr_dc(s, 0xC08070, 0xC08060, DC_AL);
}
"""


class Width(unittest.TestCase):
    """DC_WIDTH: the accumulator's width an entry leaves, dead where every
    caller sets it again before anything depends on it; an entry's own set
    alone may carry it."""

    def test_width(self):
        registry = """
const NativeEntry game_natives[] = {
    { .addr = 0xC08130, .name = "width_dead", .fn = width_dead, .dont_care = DC_WIDTH },
    { .addr = 0xC08138, .name = "width_read", .fn = width_read, .dont_care = DC_WIDTH },
};
"""
        port = """
void width_dead(Snes *s) { m_rts(s); }
void width_read(Snes *s) { m_rts(s); }
void stops_wide(Snes *s) { m_stop_at_own(s, 0xC08110, DC_WIDTH); }
"""
        found, rows, _ = findings(port=port, registry=registry)
        text = "\n".join(found)
        self.assertNotIn("width_dead", text)
        self.assertIn("width_read's .dont_care has DC_WIDTH", text)
        self.assertIn("DC_WIDTH is for an entry's own set", text)
        self.assertTrue(any("width_dead DC_WIDTH" in r for r in rows))


class SplitSets(unittest.TestCase):
    """DC_AL (A's low byte alone) and m_stop_at_own, each accepted where
    liveness agrees and rejected where it doesn't."""

    def setUp(self):
        self.found, self.rows, _ = findings(port=PORT_SPLIT, registry=REGISTRY_SPLIT)
        self.text = "\n".join(self.found)

    def test_a_low_byte_at_a_return(self):
        # C0:8040 reads A's high byte after C0:8050 returns (XBA), not its low byte
        self.assertNotIn("load_two's", self.text)
        self.assertIn("load_two_high's .dont_care (DC_AH)", self.text)
        self.assertIn("load_two_whole's .dont_care (DC_A)", self.text)
        # C0:8070 reads A's low byte after C0:8058 returns: DC_AL is wrong there
        self.assertIn("load_three's .dont_care (DC_AL) after $C0:8058's routine returns, where liveness has A read",
                      self.text)

    def test_a_low_byte_at_a_call(self):
        # C0:8050 reads nothing; C0:8060 reads the low byte and not the high one
        self.assertNotIn("to $C0:8050", self.text)
        self.assertNotIn("declares DC_AH,", self.text)
        self.assertIn("the call from $C0:8070 to $C0:8060 declares DC_AL, where liveness has A read", self.text)

    def test_own_stop(self):
        # alone, X is dead at C0:8030; with the entry's set added, Y (which TYA reads) isn't
        self.assertFalse([f for f in self.found if "(its own)" in f and "DC_X" in f])
        self.assertIn("the stop declares DC_X (with store_y_shared's DC_X | DC_Y) at $C0:8030, where liveness has Y read",
                      self.text)
        self.assertNotIn("with store_y's", self.text)
        # a wrong own set is still a finding
        self.assertIn("the stop declares DC_Y (its own) at $C0:8030, where liveness has Y read", self.text)
        # and the entry's set is still checked where it returns: X and Y are dead after C0:8030 returns
        self.assertTrue(any("store_y DC_X | DC_Y after $C0:8030's routine returns: ok" in r for r in self.rows))

    def test_list_suggests(self):
        # a declaration liveness allows more than: --list says what could be added
        self.assertTrue(any("load_two DC_AL" in r and "could add" in r for r in self.rows))
        self.assertEqual(dc.dc_text(dc.dc_of_dead(dc.dead_of_dc(dc.DC_AL))), "DC_AL")
        self.assertEqual(dc.dc_text(dc.DC_AL | 1), "DC_A | DC_AL")


# A dispatcher whose target is read from work RAM at run time: the analysis
# follows the one target a hint names, which reads nothing, but a caller that
# stores the pointer itself can send it anywhere.
# C0:8080  JSR $8088 : RTS
# C0:8088  LDX #$00 : JMP ($0100)             (hint: to C0:8058, LDA #$03 : RTS)
CODE_DYNAMIC = {
    **CODE,
    0xC08080: bytes.fromhex("208880 60".replace(" ", "")),
    0xC08088: bytes.fromhex("A200 6C0001".replace(" ", "")),
}

REGISTRY_DYNAMIC = """
const NativeEntry game_natives[] = {
    { .addr = 0xC08080, .name = "dispatch", .fn = dispatch },
    { .addr = 0xC08000, .name = "elsewhere", .fn = elsewhere },
};
"""

EVIDENCE = "dc-evidence: ct_fuzz --poison-dc dispatch, seed 1: 2000 states, 1500 call-outs scrambled, 0 mismatched"

PORT_DYNAMIC = """
void dispatch(Snes *s)
{
    m_jsr_dc(s, 0xC08080, 0xC08088, DC_A);

    /* The pointer is the game's own.
     * %(evidence)s */
    m_jsr_dc(s, 0xC08080, 0xC08088, DC_A | DC_Y);

    m_jsr_dc(s, 0xC08080, 0xC08088, DC_C);   /* dc-evidence: fuzzed it, all fine */

    /* %(evidence)s */
    m_jsr_dc(s, 0xC08080, 0xC08088, DC_X | DC_Z);

    /* %(elsewhere)s */
    m_jsr_dc(s, 0xC08080, 0xC08088, DC_C);

    /* %(few)s */
    m_jsr_dc(s, 0xC08080, 0xC08088, DC_V);

    m_jsr_dc(s, 0xC08080, 0xC08088,
             DC_D);   /* %(evidence)s */
}

void elsewhere(Snes *s) { m_rts(s); }
""" % {"evidence": EVIDENCE, "elsewhere": EVIDENCE.replace("dispatch,", "elsewhere,"),
       "few": EVIDENCE.replace("1500 call-outs", "12 call-outs")}


def dynamic_findings():
    h = Hints()
    for addr in (0xC08000, 0xC08080):
        h.entries.append(type("E", (), {"addr": addr, "m": True, "x": True, "kind": "test"})())
    h.indirect[0xC0808A] = [0xC08058]
    rom = FakeRom(CODE_DYNAMIC)
    an = Analyzer(rom, h)
    an.run()
    lv, strict = Liveness(an), Liveness(an, strict=True)
    lv.solve()
    strict.solve()
    sources = [dc.Source("game/registry.c", REGISTRY_DYNAMIC), dc.Source("game/port.c", PORT_DYNAMIC)]
    found, rows, _ = dc.run_sources(sources, [dc.Source("runtime/port/native.h", NATIVE_H)], lv,
                                    lambda addr: rom.data[bus_to_offset(addr)], strict=strict,
                                    fuzz_routines={("dispatch", ""), ("elsewhere", "")})
    return lv, [f.text() for f in found], rows


class DynamicTargets(unittest.TestCase):
    """A call-out into code that reaches a transfer whose target the machine
    decides at run time: the declaration must hold without trusting the target
    set, or carry a note naming a fuzz run that scrambled it."""

    @classmethod
    def setUpClass(cls):
        cls.lv, cls.found, cls.rows = dynamic_findings()
        cls.text = "\n".join(cls.found)

    def at(self, line):
        return [f for f in self.found if f.startswith("game/port.c:%d:" % line)]

    def test_the_transfer_is_dynamic(self):
        self.assertEqual(list(self.lv.dynamic_sites(0xC08088)), [0xC0808A])
        self.assertIn("pointer", self.lv.dynamic_sites(0xC08088)[0xC0808A])

    def test_unresolved_call_out_is_a_finding(self):
        # the trusting solve has A dead (the one target loads it); without the target set, A may be read
        self.assertFalse([r for r in self.rows if r.startswith("game/port.c:4:") and ": ok" in r])
        (f,) = self.at(4)
        self.assertIn("only by trusting a target set decided at run time", f)
        self.assertIn("$C0:808A: its pointer ($0100) is in RAM", f)
        self.assertIn("without that, A AH may be read", f)

    def test_annotated_call_out_passes(self):
        self.assertEqual(self.at(8), [])
        self.assertTrue(any(r.startswith("game/port.c:8:") and "ok by fuzz evidence" in r for r in self.rows))

    def test_malformed_note(self):
        (f,) = self.at(10)
        self.assertIn("isn't in the form", f)

    def test_note_where_liveness_proves_it(self):
        # X, N and Z are set by the LDX before the dispatch: no note needed
        (f,) = self.at(13)
        self.assertIn("remove the note", f)

    def test_note_must_name_a_routine_reaching_it(self):
        (f,) = self.at(16)
        self.assertIn("names elsewhere, which doesn't reach dispatch", f)

    def test_note_must_have_scrambled_enough(self):
        (f,) = self.at(19)
        self.assertIn("scrambled 12 call-outs", f)

    def test_note_on_the_statement_lines(self):
        self.assertEqual(self.at(21), [])

    def test_without_strict_nothing_changes(self):
        found, _, _ = findings()
        self.assertFalse([f for f in found if "trusting" in f])


def v_note(target, counts, set_="V", differing=0, static=False):
    return ("dc-evidence: tools/v_readers.py %s: V dead, its tables' indexes at most $FE (on the page $0000); "
            "ct_dev --poison-set %s at each site over the library, calls with V inverted: %s; %d differing%s"
            % (target, set_, ", ".join("%s %d" % sc for sc in counts), differing,
               "; a site under 100 rests on the static result" if static else ""))


# A table the analysis takes from data, and the dispatch its bound is argued from:
# C0:8100  TDC : LDA $10 : ASL A : TAX : JSR ($8200,X) : RTS    (the table's index a doubled byte)
# C0:8200  128 words: C0:8300 (what the data picks), then C0:8310 (and one more at entry 100)
# C0:8300  RTS    C0:8310  RTS    C0:8320  BVS +0 : RTS (reads V)    C0:8330  RTS (never traced)
# C0:8400  JSR $8100 : RTS    C0:8404  JSR $8100 : RTS         (the calls the declarations stand for)
# C0:8500  JMP $8104                                            (only with past_the_asl: into the TAX)
DISPATCH = 0xC08105


def v_code(entry_100=0x8310, shape="7BA5100AAAFC008260", past_the_asl=False):
    words = [0x8300] + [0x8310] * 127
    words[100] = entry_100
    code = {
        0xC08100: bytes.fromhex(shape),
        0xC08200: b"".join(w.to_bytes(2, "little") for w in words),
        0xC08300: bytes.fromhex("60"),
        0xC08310: bytes.fromhex("60"),
        0xC08320: bytes.fromhex("700060"),
        0xC08330: bytes.fromhex("60"),
        0xC08400: bytes.fromhex("20008160"),
        0xC08404: bytes.fromhex("20008160"),
    }
    if past_the_asl:
        code[0xC08500] = bytes.fromhex("4C0481")
    return code


def v_analysis(m8=True, reach=128, past_the_asl=False, **code):
    from tools.disasm import derive
    from tools.disasm.hints import TableHint
    h = Hints()
    entries = [0xC08100, 0xC08310, 0xC08320, 0xC08400, 0xC08404] + ([0xC08500] if past_the_asl else [])
    for addr in entries:
        h.entries.append(type("E", (), {"addr": addr, "m": m8, "x": False, "kind": "test"})())
    h.tables[DISPATCH] = TableHint(1, "abs", None, [])
    h.derive[DISPATCH] = "fakev.part"
    h.index_reach[DISPATCH] = reach
    saved = dict(derive.DERIVERS)
    derive.DERIVERS["fakev"] = lambda an, fl: derive.Derivation(targets={"part": {0xC08300}})
    try:
        rom = FakeRom(v_code(past_the_asl=past_the_asl, **code))
        an = Analyzer(rom, h)
        an.run()
    finally:
        derive.DERIVERS.clear()
        derive.DERIVERS.update(saved)
    return an, rom


REGISTRY_V = """
const NativeEntry game_natives[] = {
    { .addr = 0xC08400, .name = "calls_it", .fn = calls_it },
};
"""


def v_port(notes):
    """One declaration per note, each from the site the note's tuple names."""
    body = "".join("\n    /* %s */\n    m_jsr_dc(s, 0x%06X, 0xC08100, %s);\n" % (note, site, dc)
                   for note, dc, site in notes)
    return "void calls_it(Snes *s)\n{%s}\n" % body


def v_findings(notes, **code):
    an, rom = v_analysis(**code)
    lv, strict = Liveness(an), Liveness(an, strict=True)
    lv.solve()
    strict.solve()
    sources = [dc.Source("game/registry.c", REGISTRY_V),
               dc.Source("game/port.c", v_port([n if len(n) == 3 else n + (0xC08400,) for n in notes]))]
    found, rows, _ = dc.run_sources(sources, [dc.Source("runtime/port/native.h", NATIVE_H)], lv,
                                    lambda addr: rom.data[bus_to_offset(addr)], strict=strict)
    return [f.text() for f in found], rows


GOOD = v_note("C0:8100", [("C08400", 500)])


class OverflowEvidence(unittest.TestCase):
    """The other form of note: tools/v_readers.py's solve with the table bounded
    by its dispatch, and a library run that inverted V at each site. One
    passing case, and one failing case per thing the check asks of it."""

    @classmethod
    def setUpClass(cls):
        cls.found, cls.rows = v_findings([
            (GOOD, "DC_V"),                                                          # line 4
            (v_note("C0:8100", [("C08403", 500)]), "DC_V"),                          # 7: another site
            (v_note("C0:8100", [("C08400", 500)], set_="NZC"), "DC_V"),              # 10: V not poisoned
            (v_note("C0:8100", [("C08400", 99)]), "DC_V"),                           # 13: too few
            (v_note("C0:8100", [("C08400", 500)], differing=1), "DC_V"),             # 16: a trace differs
            (GOOD, "DC_Y | DC_V"),                                                   # 19: more than V
            (v_note("C0:8300", [("C08400", 500)]), "DC_V"),                          # 22: another target
            ("no evidence", "DC_V"),                                                 # 25: no note at all
            # a site the library rarely reaches, resting on the static result with another made
            (v_note("C0:8100", [("C08400", 3), ("C08404", 500)], static=True), "DC_V"),        # 28
            # the same without saying so
            (v_note("C0:8100", [("C08400", 3), ("C08404", 500)]), "DC_V"),                     # 31
            # resting on the static result, but no site made 100
            (v_note("C0:8100", [("C08400", 3), ("C08404", 99)], static=True), "DC_V"),         # 34
        ])

    def at(self, line):
        return [f for f in self.found if f.startswith("game/port.c:%d:" % line)]

    def test_without_the_bound_v_is_unproven(self):
        # with no note, the strict solve's giving up at the derived table is the finding
        (f,) = self.at(25)
        self.assertIn("only by trusting a target set decided at run time", f)
        self.assertIn("its table is filled out from data", f)
        self.assertIn("V may be read", f)

    def test_the_good_note_passes(self):
        self.assertEqual(self.at(4), [])
        self.assertTrue(any(r.startswith("game/port.c:4:") and "ok by tools/v_readers.py" in r for r in self.rows))

    def test_it_must_name_the_calls_own_site(self):
        (f,) = self.at(7)
        self.assertIn("didn't poison the call's own site $C0:8400", f)

    def test_it_must_have_poisoned_v(self):
        (f,) = self.at(10)
        self.assertIn("--poison-set leaves V alone", f)

    def test_it_must_have_inverted_enough(self):
        (f,) = self.at(13)
        self.assertIn("inverted V 99 times at $C0:8400, fewer than 100", f)

    def test_no_trace_may_differ(self):
        (f,) = self.at(16)
        self.assertIn("1 trace(s) differing", f)

    def test_it_speaks_for_v_alone(self):
        (f,) = self.at(19)
        self.assertIn("speaks for V alone, not Y", f)

    def test_it_must_name_the_target(self):
        (f,) = self.at(22)
        self.assertIn("names $C0:8300, not the call's target $C0:8100", f)

    def test_a_rare_site_on_the_static_result(self):
        self.assertEqual(self.at(28), [])
        (f,) = self.at(31)
        self.assertIn("doesn't rest that site on the static result", f)
        (f,) = self.at(34)
        self.assertIn("inverted V at no site of the note 100 times", f)

    def test_a_reader_the_bound_reaches(self):
        found, _ = v_findings([(GOOD, "DC_V")], entry_100=0x8320)
        (f,) = found
        self.assertIn("tools/v_readers.py has V read into $C0:8100", f)

    def test_a_word_that_is_not_traced_code(self):
        found, _ = v_findings([(GOOD, "DC_V")], entry_100=0x8330)
        (f,) = found
        self.assertIn("entry 100 ($C0:8330) isn't code the analysis traced", f)

    def test_a_dispatch_of_another_shape(self):
        found, _ = v_findings([(GOOD, "DC_V")], shape="7BA510EAAAFC008260")    # NOP for the ASL
        (f,) = found
        self.assertIn("not ASL A, TAX before the dispatch", f)

    def test_an_asl_on_a_16_bit_a(self):
        found, _ = v_findings([(GOOD, "DC_V")], m8=False)
        (f,) = found
        self.assertIn("the ASL A isn't traced with an 8-bit A everywhere", f)

    def test_a_jump_past_the_asl(self):
        found, _ = v_findings([(GOOD, "DC_V")], past_the_asl=True)
        (f,) = found
        self.assertIn("$C0:8104 is jumped to, past the ASL", f)

    def test_a_reach_other_than_a_doubled_byte(self):
        found, _ = v_findings([(GOOD, "DC_V")], reach=64)
        (f,) = found
        self.assertIn("index_reach 64, where a doubled byte reaches 128", f)

    def test_a_high_byte_nothing_clears(self):
        found, _ = v_findings([(GOOD, "DC_V")], shape="EAA5100AAAFC008260")    # NOP for the TDC
        (f,) = found
        self.assertIn("A's high byte isn't known clear at the ASL", f)
        self.assertIn("$C0:8100 is entered as test", f)

    def test_bounded_replaces_the_derived_targets(self):
        an, _ = v_analysis()
        jt = an.tables[DISPATCH]
        self.assertIn(0xC08300, Liveness(an)._table_targets(jt))
        self.assertEqual(Liveness(an, strict=True, bounded={DISPATCH: [0xC08310]})._table_targets(jt), [0xC08310])
        self.assertIsNone(Liveness(an, strict=True, bounded={DISPATCH: [0xC08310]}).dynamic_sites(0xC08100).get(DISPATCH))

    def test_the_bound_follows_every_word(self):
        from tools import v_readers
        an, _ = v_analysis(entry_100=0x8320)
        targets, problems = v_readers.bounded_targets(an)
        self.assertEqual(problems, [])
        self.assertEqual(sorted(targets[DISPATCH]), [0xC08300, 0xC08310, 0xC08320])

    def test_counts_are_read(self):
        m = dc.V_EVIDENCE.search(v_note("C0:8100", [("C08400", 12), ("C18400", 7)], static=True))
        self.assertEqual(dc.v_evidence_counts(m), {0xC08400: 12, 0xC18400: 7})
        self.assertTrue(m.group("static"))


class HighByteAnalysis(unittest.TestCase):
    """tools/disasm/high_byte.py on small routines: what clears A's high byte
    and what makes it unknown, and a call carrying it through its callee."""

    def run_code(self, code, at, entries):
        from tools.disasm.high_byte import HighByte
        h = Hints()
        for addr, m8 in entries:
            h.entries.append(type("E", (), {"addr": addr, "m": m8, "x": False, "kind": "test"})())
        an = Analyzer(FakeRom(code), h)
        an.run()
        lv = Liveness(an)
        lv.solve()
        hb = HighByte(an, lv, d_high_zero=True)
        hb.solve()
        return hb.at(at)

    def test_tdc_clears_it(self):
        # C0:8000 TDC : LDA $10 (8-bit) : NOP : RTS
        self.assertEqual(self.run_code({0xC08000: bytes.fromhex("7BA510EA60")}, 0xC08003, [(0xC08000, True)])[0], "Z")

    def test_xba_and_16_bit_loads(self):
        # C0:8000 TDC : XBA : NOP : RTS -- the swap brings A's low byte up
        self.assertEqual(self.run_code({0xC08000: bytes.fromhex("7BEBEA60")}, 0xC08002, [(0xC08000, True)])[0], "U")
        # C0:8000 (16-bit A) LDA #$0012 : NOP : LDA $10 : NOP : RTS
        code = {0xC08000: bytes.fromhex("A91200EAA510EA60")}
        self.assertEqual(self.run_code(code, 0xC08003, [(0xC08000, False)])[0], "Z")
        self.assertEqual(self.run_code(code, 0xC08006, [(0xC08000, False)])[0], "U")

    def test_a_call_carries_it_through(self):
        # C0:8000 TDC : JSR $8010 : NOP : RTS     C0:8010 NOP : RTS (leaves B alone)
        # C0:8020 TDC : JSR $8030 : NOP : RTS     C0:8030 XBA : RTS (doesn't)
        code = {0xC08000: bytes.fromhex("7B201080EA60"), 0xC08010: bytes.fromhex("EA60"),
                0xC08020: bytes.fromhex("7B203080EA60"), 0xC08030: bytes.fromhex("EB60")}
        entries = [(0xC08000, True), (0xC08020, True)]
        self.assertEqual(self.run_code(code, 0xC08004, entries)[0], "Z")
        self.assertEqual(self.run_code(code, 0xC08024, entries)[0], "U")


if __name__ == "__main__":
    unittest.main()
