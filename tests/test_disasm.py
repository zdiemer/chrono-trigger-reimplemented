"""Decoder and analyzer tests on synthetic code (no ROM needed).

    python3 -m unittest discover tests
"""

import unittest

from tools.disasm.analyze import Analyzer
from tools.disasm.cpu import OPCODES, Mode, decode
from tools.disasm.hints import Hints
from tools.disasm.memmap import ROM_SIZE, Rom, bus_to_offset, parse


class FakeRom(Rom):
    def __init__(self, code: dict[int, bytes]):
        data = bytearray(b"\xDB" * ROM_SIZE)   # STP everywhere else
        for addr, blob in code.items():
            off = bus_to_offset(addr)
            data[off:off + len(blob)] = blob
        super().__init__(bytes(data))


def reader(blob: bytes, base: int = 0xC08000):
    def read(addr, n):
        i = addr - base
        return blob[i:i + n] if 0 <= i and i + n <= len(blob) else None
    return read


class OpcodeTable(unittest.TestCase):
    GROUP1 = ["ORA", "AND", "EOR", "ADC", "STA", "LDA", "CMP", "SBC"]

    def test_group1_grid(self):
        # The eight accumulator ops share one addressing-mode layout per column.
        layout = {
            0x01: Mode.DPXIND, 0x03: Mode.SR, 0x05: Mode.DP, 0x07: Mode.DPLIND,
            0x09: Mode.IMM_M, 0x0D: Mode.ABS, 0x0F: Mode.LONG, 0x11: Mode.DPINDY,
            0x12: Mode.DPIND, 0x13: Mode.SRINDY, 0x15: Mode.DPX, 0x17: Mode.DPLINDY,
            0x19: Mode.ABSY, 0x1D: Mode.ABSX, 0x1F: Mode.LONGX,
        }
        for row, name in enumerate(self.GROUP1):
            for low, mode in layout.items():
                op = row * 0x20 + low
                if op == 0x89:   # BIT #imm sits where STA #imm would be
                    self.assertEqual(OPCODES[op], ("BIT", Mode.IMM_M))
                    continue
                self.assertEqual(OPCODES[op], (name, mode), f"opcode {op:02X}")

    def test_immediate_widths(self):
        imm_m = {op for op, (_, m) in enumerate(OPCODES) if m is Mode.IMM_M}
        imm_x = {op for op, (_, m) in enumerate(OPCODES) if m is Mode.IMM_X}
        self.assertEqual(imm_m, {0x09, 0x29, 0x49, 0x69, 0x89, 0xA9, 0xC9, 0xE9})
        self.assertEqual(imm_x, {0xA0, 0xA2, 0xC0, 0xE0})

    def test_lengths(self):
        cases = [
            (b"\xA9\x34\x12", True, True, 2), (b"\xA9\x34\x12", False, True, 3),
            (b"\xA2\x34\x12", True, False, 3), (b"\x22\x56\x34\x12", True, True, 4),
            (b"\x54\x7E\x7F", True, True, 3), (b"\xDC\x00\x05", True, True, 3),
            (b"\x62\x00\x10", True, True, 3), (b"\xC2\x30", True, True, 2),
        ]
        for blob, m, x, size in cases:
            self.assertEqual(decode(reader(blob), 0xC08000, m, x).size, size, blob.hex())
        self.assertIsNone(decode(reader(b"\xA9\x00\x00"), 0xC08000, None, True))

    def test_branch_and_move_text(self):
        bra = decode(reader(b"\x80\xFE"), 0xC08000, True, True)
        self.assertEqual(bra.branch_target(), 0xC08000)
        brl = decode(reader(b"\x82\x00\x80"), 0xC08000, True, True)
        self.assertEqual(brl.branch_target(), 0xC00003)
        mvn = decode(reader(b"\x54\x7E\x7F"), 0xC08000, True, True)
        self.assertEqual(mvn.text(), "MVN $7F,$7E")   # src,dst


class MemoryMap(unittest.TestCase):
    def test_hirom(self):
        self.assertEqual(bus_to_offset(0xC00000), 0)
        self.assertEqual(bus_to_offset(0xFDC000), 0x3DC000)
        self.assertEqual(bus_to_offset(0x00FF00), 0x00FF00)
        self.assertEqual(bus_to_offset(0x80FFFC), 0x00FFFC)
        self.assertEqual(bus_to_offset(0x400000), 0)
        self.assertIsNone(bus_to_offset(0x7E0000))
        self.assertIsNone(bus_to_offset(0x001FFF))
        self.assertEqual(parse("$C0:1234"), 0xC01234)
        self.assertEqual(parse("c01234"), 0xC01234)


class Analysis(unittest.TestCase):
    def run_an(self, code, entry, m=True, x=True, hints=None):
        h = hints or Hints()
        h.entries.append(type("E", (), {"addr": entry, "m": m, "x": x, "kind": "test"})())
        an = Analyzer(FakeRom(code), h)
        an.run()
        return an

    def test_rep_sep_and_call_return_widths(self):
        # C0:8000  JSR $8010 ; LDA #$1234 (16-bit because callee returns with REP #$20) ; RTS
        # C0:8010  REP #$20 ; RTS
        code = {
            0xC08000: bytes.fromhex("201080 A93412 60".replace(" ", "")),
            0xC08010: bytes.fromhex("C220 60".replace(" ", "")),
        }
        an = self.run_an(code, 0xC08000)
        lda = an.code[0xC08003]
        self.assertEqual(list(lda.values())[0].size, 3)
        self.assertFalse(an.suspicious)

    def test_php_plp_restores(self):
        # PHP ; REP #$30 ; PLP ; LDA #$12 ; RTS  -> LDA is 8-bit again
        an = self.run_an({0xC08000: bytes.fromhex("08C230 28 A912 60".replace(" ", ""))}, 0xC08000)
        self.assertEqual(list(an.code[0xC08004].values())[0].size, 2)
        self.assertIn(0xC08006, an.code)

    def test_coverage_seeds_unreachable_code_and_flags_conflicts(self):
        # C0:8000 JMP ($0000)            unresolved: static tracing stops here
        # C0:8010 LDA #$1234 ; RTS       only known from coverage, which says m16
        code = {0xC08000: bytes.fromhex("6C0000"), 0xC08010: bytes.fromhex("A9341260")}
        h = Hints()
        h.entries.append(type("E", (), {"addr": 0xC08000, "m": True, "x": True, "kind": "test"})())
        an = Analyzer(FakeRom(code), h, coverage={0xC08010: {(False, True)}})
        an.run()
        self.assertEqual(an.coverage_seeded, {0xC08010})
        self.assertEqual(list(an.code[0xC08010].values())[0].size, 3)
        self.assertIn(0xC08013, an.code)
        self.assertFalse(an.coverage_conflicts())

        # The same bytes reached statically with the wrong width are reported.
        h2 = Hints()
        h2.entries.append(type("E", (), {"addr": 0xC08010, "m": True, "x": True, "kind": "test"})())
        an2 = Analyzer(FakeRom(code), h2, coverage={0xC08010: {(False, True)}})
        an2.run()
        self.assertIn(0xC08010, an2.coverage_conflicts())

    def test_coverage_reports_or_prunes_return_widths_that_never_ran(self):
        # C0:8000 JSR $8010 ; LDA #$12 ; RTS
        # C0:8010 BEQ +2 ; RTS ; REP #$20 ; RTS    returns m8 or m16 statically
        code = {
            0xC08000: bytes.fromhex("201080A91260"),
            0xC08010: bytes.fromhex("F00160C22060"),
        }
        h = Hints()
        h.entries.append(type("E", (), {"addr": 0xC08000, "m": True, "x": True, "kind": "test"})())
        static = Analyzer(FakeRom(code), h)
        static.run()
        self.assertIn(0xC08003, static.width_conflicts())

        # By default coverage only reports the return the movies never ran.
        h = Hints()
        h.entries.append(type("E", (), {"addr": 0xC08000, "m": True, "x": True, "kind": "test"})())
        reported = Analyzer(FakeRom(code), h, coverage={0xC08003: {(True, True)}})
        reported.run()
        self.assertEqual(reported.coverage_unobserved, {(0xC08003, (False, True))})
        self.assertFalse(reported.coverage_pruned)
        self.assertIn(0xC08003, reported.width_conflicts())

        h = Hints()
        h.entries.append(type("E", (), {"addr": 0xC08000, "m": True, "x": True, "kind": "test"})())
        guided = Analyzer(FakeRom(code), h, coverage={0xC08003: {(True, True)}}, prune_returns=True)
        guided.run()
        self.assertEqual(guided.coverage_pruned, {(0xC08003, (False, True))})
        self.assertFalse(guided.width_conflicts())

    def test_liveness_return_sets(self):
        from tools.disasm.liveness import A, C, D, N, X, Z, Liveness
        # C0:8000 JSR $8010 ; BCC +2 ; LDX $10 ; RTS     caller reads C, then X is written
        # C0:8010 LDA #$01 ; CMP #$02 ; RTS               callee: sets A (8-bit, not a full def), N Z C
        code = {
            0xC08000: bytes.fromhex("20108090 02A61060".replace(" ", "")),
            0xC08010: bytes.fromhex("A901C90260"),
        }
        an = self.run_an(code, 0xC08000)
        lv = Liveness(an)
        lv.solve()
        ret = lv.ret_live[0xC08010]
        self.assertTrue(ret & C, "carry is read by the caller's BCC")
        self.assertTrue(ret & D, "direct page is read by LDX $10")
        self.assertFalse(ret & (X | N | Z), "X, N and Z are overwritten before any read")
        self.assertFalse(lv.func_live_in[0xC08010] & C)

        # Entry points whose callers are unknown (hints, vectors, coverage seeds)
        # keep everything live at return.
        h = Hints()
        h.entries.append(type("E", (), {"addr": 0xC08000, "m": True, "x": True, "kind": "hint"})())
        an2 = Analyzer(FakeRom(code), h)
        an2.run()
        lv2 = Liveness(an2)
        lv2.solve()
        self.assertIn(0xC08000, lv2.all_live_returns)

    def test_liveness_callee_inputs_flow_to_callers(self):
        from tools.disasm.liveness import A, X, Liveness
        # C0:8000 JSR $8010 ; JSR $8020 ; RTS
        # C0:8010 RTS
        # C0:8020 TXA ; RTS                    reads X
        code = {
            0xC08000: bytes.fromhex("20108020208060"),
            0xC08010: bytes.fromhex("60"),
            0xC08020: bytes.fromhex("8A60"),
        }
        an = self.run_an(code, 0xC08000)
        lv = Liveness(an)
        lv.solve()
        self.assertTrue(lv.ret_live[0xC08010] & X, "X is read by the next callee")
        self.assertTrue(lv.func_live_in[0xC08020] & X)

    def test_liveness_splits_the_accumulator(self):
        from tools.disasm.liveness import AH, A, AW, dc_mask, dc_text, dead_of_dc, DC_NAMES
        # an 8-bit STA after the call reads A's low byte alone
        lv = self.php("A91060", after_call="853060")          # LDA #$10 : RTS ; caller STA $30
        self.assertEqual(lv.ret_live[0xC08010] & AW, A)
        self.assertFalse(lv.func_live_in[0xC08010] & AW, "the 8-bit LDA defines the low byte")
        # XBA swaps the bytes' liveness: the STA after it reads what was the high byte
        lv = self.php("60", after_call="EB853060")            # RTS ; caller XBA : STA $30
        self.assertEqual(lv.ret_live[0xC08010] & AW, AH)
        # and sets N and Z from it
        lv = self.php("60", after_call="EBF00060")            # RTS ; caller XBA : BEQ +0
        self.assertEqual(lv.ret_live[0xC08010] & AW, AH)
        # a 16-bit store after it reads both
        lv = self.php("60", after_call="C220EB853060")        # RTS ; caller REP #$20 : XBA : STA $30
        self.assertEqual(lv.ret_live[0xC08010] & AW, AW)
        # a 16-bit AND #$00FF sets the high byte whatever it held: it reads the low byte alone
        lv = self.php("60", after_call="C22029FF00853060")    # RTS ; caller REP #$20 : AND #$00FF : STA $30
        self.assertEqual(lv.ret_live[0xC08010] & AW, A)
        lv = self.php("60", after_call="C2202900FF853060")    # AND #$FF00 keeps the high byte
        self.assertEqual(lv.ret_live[0xC08010] & AW, AW)
        # a 16-bit TAX reads both, an 8-bit one the low byte (m8 x16 then x8)
        lv = self.php("60", after_call="AA60")
        self.assertEqual(lv.ret_live[0xC08010] & AW, A)       # entered x8
        # an 8-bit write leaves the high byte live through it
        lv = self.php("A901EB8530 60".replace(" ", ""))       # LDA #$01 : XBA : STA $30 : RTS
        self.assertEqual(lv.func_live_in[0xC08010] & AW, AH)
        # the DC_* forms: DC_A is the whole of A, DC_AH its high byte alone
        self.assertEqual(dc_text(dc_mask(AW)), "DC_A")
        self.assertEqual(dc_text(dc_mask(AH)), "DC_AH")
        self.assertEqual(dc_mask(A), 0)
        self.assertEqual(dead_of_dc(dict(DC_NAMES)["DC_A"]), AW)

    def php(self, callee: str, after_call: str = "60", extra=None):
        """Liveness of C0:8010 (`callee`), called from C0:8000 with
        `after_call` after the JSR; `extra` adds code elsewhere."""
        from tools.disasm.liveness import Liveness
        code = {0xC08000: bytes.fromhex("201080" + after_call), 0xC08010: bytes.fromhex(callee)}
        code.update(extra or {})
        lv = Liveness(self.run_an(code, 0xC08000))
        lv.solve()
        return lv

    def test_php_reads_only_what_is_read_after_its_plp(self):
        from tools.disasm.liveness import C, FLAGS, N, PAIRED, V, Z
        # PHP ; LDA #$01 ; PLP ; BCC +0 ; RTS: the saved carry is read, nothing else
        lv = self.php("08 A901 28 9000 60".replace(" ", ""))
        self.assertEqual(lv.push_cases[((0xC08010, (True, True)), 0xC08010)], PAIRED)
        self.assertEqual(lv.func_live_in[0xC08010] & FLAGS, C)
        # PHP ; PLP ; RTS, and the caller branches on N after the call
        lv = self.php("082860", after_call="300060")
        self.assertEqual(lv.func_live_in[0xC08010] & FLAGS, N)
        self.assertFalse(lv.func_live_in[0xC08010] & (V | Z | C))
        # PHP ; PHA ; PLA ; PLP ; RTS: the pair is found under other pushes
        lv = self.php("08486828 60".replace(" ", ""))
        self.assertEqual(lv.func_live_in[0xC08010] & FLAGS, 0)

    def test_php_pairs_through_calls_and_branches(self):
        from tools.disasm.liveness import FLAGS, PAIRED, Z
        # PHP ; JSR $8020 ; BCS +1 ; SEC ; PLP ; BEQ +0 ; RTS: Z is read after the PLP
        lv = self.php("08 202080 B001 38 28 F000 60".replace(" ", ""), extra={0xC08020: bytes.fromhex("60")})
        self.assertEqual(lv.push_cases[((0xC08010, (True, True)), 0xC08010)], PAIRED)
        self.assertEqual(lv.func_live_in[0xC08010] & FLAGS & ~0x40, Z)   # C is read by the BCS itself

    def test_php_stays_conservative_where_the_pairing_is_unclear(self):
        from tools.disasm.liveness import CALLEE_REACHES, FLAGS, LEAVES, READ_OTHERWISE, S_UNKNOWN
        key = ((0xC08010, (True, True)), 0xC08010)
        cases = [
            ("08 C210 FA 60", READ_OTHERWISE, None),        # PHP ; REP #$10 ; PLX: pulled with another byte
            ("08 A301 28 60", READ_OTHERWISE, None),        # PHP ; LDA 1,S: read where it lies
            ("08 3B 28 60", READ_OTHERWISE, None),          # PHP ; TSC: S itself is read
            ("08 60", READ_OTHERWISE, None),                # PHP ; RTS: the return pulls it
            ("08 4848 60", LEAVES, None),                   # PHP ; PHA ; PHA ; RTS: returns with it pushed
            ("08 9A 28 60", S_UNKNOWN, None),               # PHP ; TXS: S is set to what X holds
            ("08 9002 48 68 28 60", None, None),            # both paths pull a byte: still paired
            ("08 9001 48 28 60", S_UNKNOWN, None),          # PHP ; BCC ; PHA ; PLP: depths differ
            ("08 5C3080C0", LEAVES, {0xC08030: bytes.fromhex("2860")}),   # PHP ; JML: the PLP is elsewhere
            # PHP ; JSR $8020, which reads the byte under its return address where it lies
            ("08 202080 28 60", CALLEE_REACHES, {0xC08020: bytes.fromhex("A30360")}),
            # PHP ; JSR $8020, which pulls its return address and the byte, and returns with the
            # byte pulled: back to the caller, with a byte of the caller's frame missing
            ("08 202080 28 60", CALLEE_REACHES, {0xC08020: bytes.fromhex("FA68684860")}),
        ]
        for code, case, extra in cases:
            lv = self.php(code.replace(" ", ""), extra=extra)
            if case is None:
                self.assertEqual(lv.push_cases[key], "paired", code)
                continue
            self.assertEqual(lv.push_cases[key], case, code)
            self.assertEqual(lv.func_live_in[0xC08010] & FLAGS, FLAGS, code)

    def test_pushes_pair_with_the_pulls_that_restore_them(self):
        from tools.disasm.liveness import AW, FLAGS, PAIRED, X, Y
        key = ((0xC08010, (True, True)), 0xC08010)
        # PHX ; LDX #$05 ; STX $30 ; PLX ; RTS: X is only carried to the return
        lv = self.php("DA A205 8630 FA 60".replace(" ", ""))
        self.assertEqual(lv.push_cases[key], PAIRED)
        self.assertFalse(lv.func_live_in[0xC08010] & X)
        # ...and is live at the push when the caller reads it after
        lv = self.php("DA A205 8630 FA 60".replace(" ", ""), after_call="8640 60".replace(" ", ""))
        self.assertTrue(lv.func_live_in[0xC08010] & X)
        # PHP ; PLA: P moves into A, so the flags are read if A is
        lv = self.php("0868 60".replace(" ", ""))
        self.assertEqual(lv.push_cases[key], PAIRED)
        self.assertFalse(lv.func_live_in[0xC08010] & FLAGS)
        lv = self.php("0868 8530 60".replace(" ", ""))
        self.assertEqual(lv.func_live_in[0xC08010] & FLAGS, FLAGS)
        # PHY ; PLY ; BEQ: N and Z come from the pulled value, so Y is read
        lv = self.php("5A7A F000 60".replace(" ", ""))
        self.assertTrue(lv.func_live_in[0xC08010] & Y)
        self.assertEqual(lv.pairing_stats(("PHY",))["paired"], (1, 1))

    def escape(self, after_call: str):
        """C0:8040 saves P around a call to C0:8020, which pulls its return
        address and the byte under it and returns: through C0:8040's frame, to
        C0:8000, which runs `after_call`."""
        from tools.disasm.liveness import Liveness
        code = {0xC08000: bytes.fromhex("204080" + after_call),
                0xC08040: bytes.fromhex("08 202080 28 60".replace(" ", "")),     # PHP ; JSR $8020 ; PLP ; RTS
                0xC08020: bytes.fromhex("FA686860")}                            # PLX ; PLA ; PLA ; RTS
        lv = Liveness(self.run_an(code, 0xC08000))
        lv.solve()
        return lv

    def test_a_callee_that_leaves_through_its_callers_frame(self):
        from tools.disasm.liveness import A, FLAGS, PAIRED
        key = ((0xC08040, (True, True)), 0xC08040)
        # the byte PHP pushed ends in A when C0:8020 returns past C0:8040; C0:8000 sets A again
        lv = self.escape("A900 8530 60".replace(" ", ""))              # LDA #$00 ; STA $30 ; RTS
        self.assertEqual(lv.push_cases[key], PAIRED)
        self.assertEqual(lv.push_foreign[key], {((0xC08020, (True, True)), 0xC08023, "PLA", (True, True))})
        self.assertFalse(lv.func_live_in[0xC08040] & FLAGS)
        self.assertEqual(lv.escaped_returns((0xC08020, (True, True))), {0xC08023})
        # ...or stores it: the flags are read
        lv = self.escape("8530 60".replace(" ", ""))                   # STA $30 ; RTS
        self.assertEqual(lv.func_live_in[0xC08040] & FLAGS, FLAGS)
        # what is read after the escape counts after C0:8020 returns, and at its RTS
        self.assertTrue(lv.after_return(0xC08020) & A)
        self.assertTrue(lv.live_after_return(0xC08023) & A)

    def test_rti_restores_the_flags(self):
        from tools.disasm.liveness import FLAGS, X, Liveness
        # C0:8000  LDX #$00 ; RTI: the interrupted code's P comes off the stack, its X doesn't
        lv = Liveness(self.run_an({0xC08000: bytes.fromhex("A20040")}, 0xC08000))
        lv.solve()
        self.assertFalse(lv.func_live_in[0xC08000] & (FLAGS | X))

    def test_each_context_runs_its_own_widths(self):
        from tools.disasm.liveness import Liveness
        # C0:8000  JSR $8020 ; REP #$10 ; JSR $8020 ; RTS: C0:8020 (PHX ; PLX ; RTS) is run with
        # 8-bit and 16-bit X, pushing one byte or two, and reaches only its return address in each
        code = {0xC08000: bytes.fromhex("202080 C210 202080 60".replace(" ", "")),
                0xC08020: bytes.fromhex("DAFA60")}
        lv = Liveness(self.run_an(code, 0xC08000))
        self.assertEqual(lv.reach[0xC08020], 2)

    def indirect(self, bound, per=False):
        """C0:8000 calls C0:8050, which jumps through ($9000) to C0:8060 (a
        tail jump; or, with `per`, a call returning to C0:8056); after it,
        C0:8000 sets A, X, Y and every flag, and reads D and DB."""
        from tools.disasm.liveness import Liveness
        h = Hints()
        site = 0xC08053 if per else 0xC08050
        h.indirect[site] = [0xC08060]
        if bound:
            h.bounds[site] = "guard"
        body = "620200 6C0090 8530 60" if per else "6C0090"     # PER ; JMP ($9000) ; STA $30 ; RTS
        # JSR $8050 ; LDA #$00 ; TAX ; TAY ; REP #$CF ; STA $30 ; LDA $1000 ; RTS
        code = {0xC08000: bytes.fromhex("205080 A900 AA A8 C2CF 8530 AD0010 60".replace(" ", "")),
                0xC08050: bytes.fromhex(body.replace(" ", "")),
                0xC08060: bytes.fromhex("A90560")}                               # LDA #$05 ; RTS
        lv = Liveness(self.run_an(code, 0xC08000, hints=h))
        lv.solve()
        return lv

    def test_liveness_through_an_indirect_tail_jump(self):
        from tools.disasm.liveness import ALL, DB, D
        lv = self.indirect(bound=True)
        self.assertIsNone(lv.indirect_entries[0xC08060])
        self.assertNotIn(0xC08060, lv.all_live_returns)
        # it returns for C0:8050, so what C0:8000 reads after that call is what is live
        self.assertEqual(lv.ret_live[0xC08060], lv.ret_live[0xC08050])
        self.assertEqual(lv.ret_live[0xC08060], DB | D)
        # targets only the movies showed: anything could jump there, so everything stays live
        lv = self.indirect(bound=False)
        self.assertIn(0xC08060, lv.all_live_returns)
        self.assertIn("aren't argued complete", lv.reasons[0xC08060])
        self.assertEqual(lv.after_return(0xC08060), ALL)

    def test_liveness_through_an_indirect_call(self):
        from tools.disasm.liveness import A, Liveness
        # PER then JMP ($9000): C0:8060 returns to the STA after the jump, which reads A
        lv = self.indirect(bound=True, per=True)
        self.assertEqual(lv.indirect_resumes(0xC08053), [0xC08056])
        self.assertTrue(lv.ret_live[0xC08060] & A)
        self.assertNotIn(0xC08060, lv.all_live_returns)

    def test_liveness_through_a_table_filled_at_run_time(self):
        from tools.disasm.hints import TableHint
        from tools.disasm.liveness import A, Liveness
        # C0:8000  JSR ($9100,X) ; STA $30 ; RTS, a table with no entries in the ROM whose
        # targets a hint lists (C0:8020: RTS): they are called, and return to the STA
        h = Hints()
        h.tables[0xC08000] = TableHint(0, "abs", None, [0xC08020])
        code = {0xC08000: bytes.fromhex("FC0091 8530 60".replace(" ", "")), 0xC08020: bytes.fromhex("60")}
        lv = Liveness(self.run_an(code, 0xC08000, hints=h))
        lv.solve()
        self.assertTrue(lv.ret_live[0xC08020] & A)

    def test_php_under_a_new_stack_is_never_read(self):
        from tools.disasm.liveness import FLAGS, PAIRED
        # PHP ; LDX #$FF ; TXS ; BRA *: the stack is abandoned, and the saved P with it
        lv = self.php("08 A2FF 9A 80FE".replace(" ", ""))
        self.assertEqual(lv.push_cases[((0xC08010, (True, True)), 0xC08010)], PAIRED)
        self.assertEqual(lv.func_live_in[0xC08010] & FLAGS, 0)
        # a callee that stays in its frame, or leaves it only for a new stack, reaches its return address
        lv = self.php("08 202080 28 60".replace(" ", ""), extra={0xC08020: bytes.fromhex("48 68 60".replace(" ", ""))})
        self.assertEqual(lv.reach[0xC08020], 2)
        self.assertEqual(lv.pairing_stats()["paired"], (1, 1))

    def test_jump_table_bounded_by_following_code(self):
        # C0:8000 ASL ; TAX ; JMP ($8005,X)
        # C0:8005 table: $8009, $800A
        # C0:8009 RTS ; C0:800A RTS
        code = {0xC08000: bytes.fromhex("0AAA7C0580 0980 0A80 60 60".replace(" ", ""))}
        an = self.run_an(code, 0xC08000)
        jt = an.tables[0xC08002]
        self.assertEqual(jt.count, 2)
        self.assertIn(0xC08009, an.code)
        self.assertIn(0xC0800A, an.code)


if __name__ == "__main__":
    unittest.main()
