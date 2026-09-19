"""The M5 draft tool on synthetic code (no ROM needed).

    python3 -m unittest discover tests

tools/recomp/show.py turns a routine into a draft: a logic function on C
locals, a wrapper that moves the machine's registers in and out, and call-out
stubs. Whether drafts do what the original does is checked by registering them
in a scratch build and running shadow mode (docs/RECOMP.md, M5); these tests
cover the transformations one by one on small made-up routines: call
rewriting, helper mapping, dead flags, RAM and direct-page names, the wrapper
split, structure, wait loops, and the D/DB analysis.
"""

import unittest

from tools.disasm.analyze import Analyzer
from tools.disasm.hints import Hints
from tools.ram_map import RamMap
from tools.recomp import names, show
from tools.recomp.frame import Frames, single
from tools.recomp.ir import Bin, Branch, Cast, Const, Goto, Part, Un, Var, simplify, substitute
from tools.recomp.structure import If, Label, Loop, structure
from tests.test_disasm import FakeRom

RAM_TOML = '''
[[page]]
name = "test_page"
d = 0x0100
subsystem = "field"
meaning = "a page"
source = "test"

[[var]]
name = "test_source"
addr = "7E:0110"
page = "test_page"
subsystem = "field"
meaning = "where the value comes from"
source = "test"

[[page]]
name = "test_any"
subsystem = "field"
meaning = "cells on whichever page the caller set"
source = "test"
why = "the caller's"

[[var]]
name = "test_cell"
dp = 0x50
page = "test_any"
subsystem = "field"
meaning = "a cell by its offset"
source = "test"

[[var]]
name = "test_table"
addr = "7E:9100"
size = 1
count = 16
index = "slot"
subsystem = "field"
meaning = "a table"
source = "test"
'''

PROGRAMS = {
    # C0:8000  REP #$20 : LDA #$0100 : TCD : SEP #$20 : JSR $8010 : LDA #$01 : STA $30 : RTS
    # C0:8010  LDA $10 : STA $20 : RTS           (D = $0100 from the caller)
    0xC08000: bytes.fromhex("C220A900015BE220201080A9018530 60".replace(" ", "")),
    0xC08010: bytes.fromhex("A5108520 60".replace(" ", "")),
    # C0:8100  LDX #$00 : LDA $9000,X : STA $9100,X : INX : CPX #$10 : BNE $8102 : RTS
    0xC08100: bytes.fromhex("A200BD00909D0091E8E010D0F560"),
    # C0:8200  LDA $4212 : BPL $8200 : RTS        (a wait loop at the entry)
    0xC08200: bytes.fromhex("AD12421 0FB60".replace(" ", "")),
    # C0:8210  LDA #$01 : STA $00 : LDA $4212 : BPL $8214 : RTS
    0xC08210: bytes.fromhex("A9018500AD124210FB60"),
    # C0:8300  LDA #$05 : JSR $8310 : STA $40 : RTS
    # C0:8310  ASL A : RTS
    0xC08300: bytes.fromhex("A905201083854060"),
    0xC08310: bytes.fromhex("0A60"),
    # C0:8400  LDA $50 : CMP #$05 : BCC $840A : STZ $51 : RTS : STA $52 : RTS
    0xC08400: bytes.fromhex("A550C90590036451 60855260".replace(" ", "")),
    # C0:8500  PHD : PEA #$1D00 : PLD : LDA $10 : STA $7E0200 : PLD : RTS
    0xC08500: bytes.fromhex("0BF4001D2BA5108F00027E2B60"),
    # C0:62B5  INY : TYX : LDA $7F2001,X : RTS    (at the handler of event command $10)
    0xC062B5: bytes.fromhex("C8BBBF01207F60"),
    # C0:8600  JSR $8400 : JSR $8100 : REP #$FF : SEP #$30 : RTS
    #          (callers that set every flag again after the calls: the callees' flags are dead)
    0xC08600: bytes.fromhex("2000842000 81C2FFE23060".replace(" ", "")),
    # C0:8700  LDY #$1234 : JSR $8710 : STY $60 : RTS   (m8 x16)
    # C0:8710  SEP #$30 : REP #$10 : RTS                  (truncates Y without "writing" it)
    0xC08700: bytes.fromhex("A034122010878460 60".replace(" ", "")),
    0xC08710: bytes.fromhex("E230C21060"),
    # C0:8C00  PHP : JSR $8310 : PLP : RTS             (saves the caller's P around a call)
    0xC08C00: bytes.fromhex("08 201083 28 60".replace(" ", "")),
}
ENTRIES = [(0xC08000, True, True), (0xC08200, True, True), (0xC08210, True, True),
           (0xC08300, True, True), (0xC08500, True, True), (0xC062B5, True, False), (0xC08600, True, True),
           (0xC08700, True, False), (0xC08C00, True, True)]


def knowledge():
    h = Hints()
    for addr, m, x in ENTRIES:
        h.entries.append(type("E", (), {"addr": addr, "m": m, "x": x, "kind": "test"})())
    an = Analyzer(FakeRom(PROGRAMS), h)
    an.run()
    return show.Knowledge.of(an, ram=RamMap(text=RAM_TOML))


K = None


def draft(addr, m8=True, x8=True, **kw):
    global K
    if K is None:
        K = knowledge()
    d = show.Draft(K, addr, m8, x8, **kw)
    d.build()
    return d, d.render()


def logic_of(text):
    start = text.index("_logic(")
    return text[text.rfind("\n", 0, start):text.index("\n}\n", start)]


class Frames_(unittest.TestCase):
    def test_d_comes_from_the_caller(self):
        d, _ = draft(0xC08010)
        self.assertEqual(d.d, 0x0100)
        self.assertIn("1 call site", d.entry_why)

    def test_d_set_by_the_routine_itself(self):
        d, text = draft(0xC08500)
        # PEA #$1D00 : PLD puts D on $1D00 for the load; PHD/PLD brings it back
        self.assertIn("m_read8(s, 0x7E1D10)", text)
        self.assertIn("s->cpu.d = m_pull16(s);", text)

    def test_unknown_d_stays_an_offset(self):
        d, text = draft(0xC08400)
        self.assertIsNone(d.d)
        self.assertIn("m_dp(s, 0x50)", text)
        self.assertIn("D isn't known", text)

    def test_a_page_with_no_fixed_d(self):
        k = K or knowledge()
        d, text = draft(0xC08400, page=k.ram.pages["test_any"])
        self.assertIn("m_dp(s, RAM_TEST_CELL_DP)", text)
        self.assertIn("m_dp(s, 0x51)", text)

    def test_frames_of_the_caller(self):
        k = K or knowledge()
        f = Frames(k.an)
        fr = f.frames((0xC08000, (True, True)))
        at_call = [v for n, v in fr.items() if n[0] == 0xC08008][0]
        self.assertEqual(single(at_call.d), 0x0100)


class Drafts(unittest.TestCase):
    def test_no_emulated_cpu_in_the_logic(self):
        for addr in (0xC08010, 0xC08100, 0xC08300, 0xC08400):
            _, text = draft(addr)
            logic = logic_of(text)
            self.assertNotIn("c->", logic)
            self.assertNotRegex(logic, r"\brc_|RC_")
            self.assertNotRegex(logic, r"\bm_(lda|ldx|tax|rep|sep|set_nz)")

    def test_ram_and_direct_page_names(self):
        _, text = draft(0xC08010)
        self.assertIn("a = m_read8(s, RAM_TEST_SOURCE);", text)
        self.assertIn("m_write8(s, 0x7E0120, a);", text)

    def test_dead_flags_are_dropped(self):
        # the caller reloads the low byte of A and N, Z after the call: no flags
        # are left; A goes back (liveness keeps A as one register), and as the
        # routine never touches its high byte, only the low one
        _, text = draft(0xC08010)
        logic = logic_of(text)
        self.assertNotIn("zero", logic)
        self.assertNotIn("negative", logic)
        self.assertIn("static uint8_t sub_C08010_logic(Snes *s)", text)
        self.assertIn("m_set_a8(s, out);", text)

    def test_p_saved_at_the_entry_is_the_machines(self):
        # PHP before anything changes a flag pushes the caller's P as it is, so
        # the logic takes no flags for it
        _, text = draft(0xC08C00)
        self.assertIn("m_push8(s, s->cpu.p);", text)
        self.assertNotIn("draft_p(", logic_of(text))

    def test_flags_that_decide_a_branch_become_its_condition(self):
        _, text = draft(0xC08400)
        logic = logic_of(text)
        self.assertIn("if (a >= 5) {", logic)
        self.assertNotIn("carry", logic)
        self.assertNotIn("goto", logic)

    def test_loop_with_its_counter(self):
        _, text = draft(0xC08100)
        logic = logic_of(text)
        self.assertIn("do {", logic)
        self.assertIn("} while (x != 0x10);", logic)
        self.assertIn("m_write8(s, m_abs(s, 0x9100) + x, a);", logic)
        # the counter the loop condition reads is still updated in the loop
        self.assertIn("x = (uint8_t)(x + 1);", logic)

    def test_call_out_stub_and_wrapper(self):
        _, text = draft(0xC08300)
        # nothing calls C0:8300 here, so its callers may read every register and
        # flag: the call-out gives back A and the flags ASL sets
        self.assertIn("m_jsr_dc(s, site, 0xC08310, DC_X | DC_Y | DC_V);", text)
        self.assertIn("call_sub_C08310(s, 0xC08302, &a, &negative, &zero, &carry);", text)
        # the stub moves the registers in and out; the logic never touches the machine's
        stub = text[text.index("static void call_sub_C08310"):]
        self.assertIn("m_set_a8(s, *a);", stub)
        self.assertIn("*a = (uint8_t)s->cpu.a;", stub)
        self.assertIn("*carry = m_flag(s, CPU_FLAG_C);", stub)
        # the wrapper at the entry returns as the original does
        wrapper = text[text.index("// $C0:8300"):]
        self.assertIn("m_rts(s);", wrapper)
        self.assertIn("_logic(", wrapper)

    def test_a_callee_that_truncates_the_index_registers(self):
        # SEP #$10 in the callee cuts Y to 8 bits: the draft reads Y back after the call
        _, text = draft(0xC08700, True, False)
        logic = logic_of(text)
        self.assertRegex(logic, r"y = call_sub_C08710\(s, 0xC08703, y\);|&y")
        self.assertIn("m_write16(s, m_dp(s, 0x60), y);", logic)

    def test_wait_loops_hand_back(self):
        d, text = draft(0xC08210)
        self.assertIn("waits on $4212", text)
        self.assertIn("s->cpu.pc = 0x8214;", text)
        d, text = draft(0xC08200)
        self.assertIn("idle entry", text)

    def test_registration_line(self):
        d, text = draft(0xC08010)
        reg = d.registration()
        self.assertEqual(reg["guard"], ["s->cpu.d == 0x0100"])
        self.assertIn("NEEDS_M8_X8", text)

    def test_event_command_handler(self):
        d, text = draft(0xC062B5, True, False)
        self.assertIn("event command $10 `jump_forward`", text)
        self.assertEqual(d.name, "event_cmd_jump_forward")
        # the operand the handler reads, by the command's offset
        self.assertIn("0x7F2002 + cmd /* to */", text.replace("0x7F2001 + cmd + 1", "0x7F2002 + cmd"))


class Names(unittest.TestCase):
    def test_ram_names(self):
        n = names.RamNamer(RamMap(text=RAM_TOML), "field")
        self.assertEqual(n.name(0x7E0110), "RAM_TEST_SOURCE")
        self.assertEqual(n.name(0x7E9103), "RAM_TEST_TABLE + 3")
        self.assertIsNone(names.RamNamer(RamMap(text=RAM_TOML), "battle").name(0x7E0110))

    def test_io_names(self):
        self.assertEqual(names.io_name(0x002100), "INIDISP")
        self.assertEqual(names.io_name(0x004375), "DASL7")

    def test_script_commands(self):
        cmds = names.commands_at(0xC062B5)
        self.assertEqual(cmds[0][0], "event")
        self.assertIn("jump_forward", cmds[0][1])
        self.assertEqual(names.event_operand_at(cmds[0][2], 1), "to")


class Expressions(unittest.TestCase):
    def test_casts_and_constants(self):
        x = Var("x", 8)
        e = Cast(Bin("+", Cast(Bin("+", x, Const(1, 8), 8), 8), Const(1, 8), 8), 8)
        self.assertEqual(simplify(e), Cast(Bin("+", x, Const(2, 8), 8), 8))
        self.assertEqual(simplify(Un("!", Bin(">=", x, Const(5, 8), 1), 1)), Bin("<", x, Const(5, 8), 1))
        self.assertEqual(simplify(Bin(">=", x, Const(0, 8), 1)), Const(1, 1))

    def test_substitution_keeps_the_width(self):
        # t = x + 1 held in an 8-bit variable: reading it later is (uint8_t)(x + 1)
        e = substitute(Bin("==", Var("t", 8), Const(0, 8), 1), "t", Bin("+", Var("x", 8), Const(1, 8), 8))
        self.assertIsInstance(e.l, Cast)
        # the low byte of A replaced alone
        e = substitute(Bin("+", Part(False), Part(True), 8), "a", Const(3, 8), lo_only=True)
        self.assertEqual(e.l, Const(3, 8))
        self.assertEqual(e.r, Part(True))


class Structure(unittest.TestCase):
    def test_if_and_loop(self):
        c = Var("z", 1)
        items = [Label("A"), Branch(c, "B"), Goto("X"), Label("B"), Branch(c, "A"), Label("X")]
        out = structure(items)
        self.assertEqual(len(out), 1)
        self.assertIsInstance(out[0], Loop)

    def test_a_segment_entered_from_outside_keeps_its_goto(self):
        c = Var("z", 1)
        items = [Branch(c, "B"), Label("M"), Goto("E"), Label("B"), Branch(c, "M"), Label("E")]
        out = structure(items)
        text = repr(out)
        self.assertIn("Goto", text)       # M is entered from below: no if around it

    def test_else(self):
        c = Var("z", 1)
        items = [Branch(c, "E"), Goto("J"), Label("E"), Goto("K"), Label("J")]
        out = structure(items)
        self.assertIsInstance(out[0], If)


class Suite(unittest.TestCase):
    def test_every_program_drafts(self):
        for addr, m, x in ENTRIES + [(0xC08010, True, True), (0xC08100, True, True), (0xC08400, True, True)]:
            d, text = draft(addr, m, x)
            self.assertIn(f"// ${addr >> 16:02X}:{addr & 0xFFFF:04X}", text)
            self.assertEqual(text.count("{"), text.count("}"))


if __name__ == "__main__":
    unittest.main()
