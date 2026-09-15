"""MD4 pieces of the matching disassembly (docs/MATCHING.md), no ROM needed:
constant arguments, PER calls, derivations and their rounds, the sequence
script parser, stored operands, and the closure checks.

    python3 -m unittest discover tests
"""

import unittest

from tools.disasm import derive as derive_mod
from tools.disasm import seqscript
from tools.disasm.analyze import Analyzer
from tools.disasm.closure import check_c1, check_c5, check_c6, closure, code_set
from tools.disasm.codeargs import call_sites, flow, register_values
from tools.disasm.hints import Entry, Hints
from tests.test_disasm import FakeRom


def hex_code(s: str) -> bytes:
    return bytes.fromhex(s.replace(" ", ""))


def analyze(code: dict, entries, hints: Hints | None = None) -> Analyzer:
    h = hints or Hints()
    for addr, m, x in entries:
        h.entries.append(Entry(addr, m, x, "hint"))
    an = Analyzer(FakeRom(code), h)
    an.run()
    return an


class Arguments(unittest.TestCase):
    # C0:8000 LDX #$1234 ; BEQ +3 ; LDX #$5678 ; JSR $8020 ; RTS
    # C0:8020 RTS
    CODE = {0xC08000: hex_code("A23412 F003 A27856 202080 60"), 0xC08020: hex_code("60")}

    def test_values_over_every_path(self):
        an = analyze(self.CODE, [(0xC08000, True, False)])
        fl = flow(an)
        site = call_sites(an, 0xC08020)[0]
        self.assertEqual(site, 0xC08008)
        self.assertEqual(register_values(an, site, "X", fl), {0x1234, 0x5678})
        self.assertIsNone(register_values(an, site, "A", fl))

    def test_entry_stops_the_walk(self):
        # the JSR's own context entry is reached without a load: unknown
        an = analyze({0xC08000: hex_code("202080 60"), 0xC08020: hex_code("60")}, [(0xC08000, True, True)])
        self.assertIsNone(register_values(an, 0xC08000, "X", flow(an)))


class PerCalls(unittest.TestCase):
    def test_per_then_jmp_returns_after_the_per_address(self):
        # C0:8000 PER $8007 (pushes C0:8007) ; JMP ($0010) ; (C0:8006) NOP ; (C0:8008) LDA #$1234 ; STP
        # target C0:8030: REP #$20 ; RTS -> the continuation C0:8008 runs 16-bit
        h = Hints()
        h.indirect[0xC08003] = [0xC08030]
        h.bounds[0xC08003] = "constant"
        code = {0xC08000: hex_code("620400 6C1000 EA EA A93412 DB"), 0xC08030: hex_code("C220 60")}
        an = analyze(code, [(0xC08000, True, True)], h)
        self.assertIn(0xC08008, an.code)
        self.assertEqual(next(iter(an.code[0xC08008].values())).size, 3)
        self.assertIn(0xC08008, an.resumes)


class Derivations(unittest.TestCase):
    def setUp(self):
        self.saved = dict(derive_mod.DERIVERS)

    def tearDown(self):
        derive_mod.DERIVERS.clear()
        derive_mod.DERIVERS.update(self.saved)

    def test_targets_found_in_derived_code_are_followed(self):
        # C0:8000 LDX #$8010 ; JSR $8040 (the "starter") ; JMP ($0000) ; (C0:8040) RTS
        # C0:8010 LDX #$8020 ; JSR $8040 ; RTS    (derived: found only through the first load)
        # C0:8020 RTS
        code = {0xC08000: hex_code("A21080 204080 6C0000"), 0xC08010: hex_code("A22080 204080 60"),
                0xC08020: hex_code("60"), 0xC08040: hex_code("60")}

        def deriver(an, fl):
            d = derive_mod.Derivation()
            d.targets["x"] = set()
            for site in call_sites(an, 0xC08040):
                for v in register_values(an, site, "X", fl) or ():
                    d.targets["x"].add(0xC00000 | v)
            return d

        derive_mod.DERIVERS["test"] = deriver
        h = Hints()
        h.indirect[0xC08006] = []
        h.derive[0xC08006] = "test.x"
        an = analyze(code, [(0xC08000, True, False)], h)
        self.assertEqual(an.derived[0xC08006], {0xC08010, 0xC08020})
        self.assertIn(0xC08020, an.code)
        self.assertFalse(an.derive_unstable)
        c1 = check_c1(an)
        self.assertTrue(c1.ok, c1.problems)
        self.assertEqual(c1.counts["argued by derived"], 1)

    def test_rounds_drop_targets_a_later_round_no_longer_derives(self):
        # the deriver gives a bogus target while C0:8010 isn't traced yet, and only
        # the real one afterwards: the final analysis doesn't keep the bogus one
        code = {0xC08000: hex_code("6C0000"), 0xC08010: hex_code("60"), 0xC08020: hex_code("60")}

        def deriver(an, fl):
            d = derive_mod.Derivation()
            d.targets["x"] = {0xC08010} if 0xC08010 in an.code else {0xC08010, 0xC08020}
            return d

        derive_mod.DERIVERS["test"] = deriver
        h = Hints()
        h.indirect[0xC08000] = []
        h.derive[0xC08000] = "test.x"
        an = analyze(code, [(0xC08000, True, True)], h)
        self.assertEqual(an.derived[0xC08000], {0xC08010})
        self.assertNotIn(0xC08020, an.code)
        self.assertGreater(an.derive_rounds, 1)


class SequenceScripts(unittest.TestCase):
    def reader(self, image: bytes, base=0x7E4000):
        return lambda a, n: image[a - base:a - base + n] if 0 <= a - base and a - base + n <= len(image) else None

    def test_commands_and_targets(self):
        # 7E:4000 $01 store word (3) ; $A0 call 7E:5000 (3) ; $A8 add task 7E:5100 (3) ;
        #         $AA add task 7E:5200 (3) ; $8n counter loop to 7E:4000 (5) ; $AE goto 7E:4014 ;
        # 7E:4014 $AF stop
        img = hex_code("013412 A00050 A80051 AA0052 83050000 40 AE1440 AF")
        res = seqscript.parse(self.reader(img), [(0x7E4000, 0x0A20, "test")])
        self.assertEqual(set(res.calls), {0x7E5000})
        self.assertEqual(set(res.tasks_0940), {0x7E5100})
        self.assertEqual(set(res.tasks_0920), {0x7E5200})
        self.assertIn(0x7E4014, res.commands)
        self.assertFalse(res.problems)

    def test_pointer_stores_continue_two_bytes_on_the_running_channel(self):
        # $A2 stores $4010 to $0A20 (the running channel's pointer): continues at 7E:4012
        img = hex_code("A2200A1040 AF") + bytes(12) + hex_code("AF")
        res = seqscript.parse(self.reader(img), [(0x7E4000, 0x0A20, "test")])
        self.assertIn(0x7E4012, res.commands)
        self.assertNotIn(0x7E4005, res.commands)     # doesn't fall through
        res = seqscript.parse(self.reader(img), [(0x7E4000, 0x0B20, "test")])
        self.assertIn((0x7E4010, 0x0A20), res.roots)  # the other channel's: at the address itself
        self.assertIn(0x7E4005, res.commands)

    def test_guarded_stores_are_problems(self):
        img = hex_code("A1400901 AF")    # a byte store to $0940, the task list
        res = seqscript.parse(self.reader(img), [(0x7E4000, 0x0A20, "test")])
        self.assertTrue(res.problems)


class Closure(unittest.TestCase):
    def test_sites_without_an_argument_fail_c1(self):
        # C0:8000 JMP ($8003,X) with a hinted count but no bound
        h = Hints()
        from tools.disasm.hints import TableHint
        h.tables[0xC08000] = TableHint(1, "abs", None)
        an = analyze({0xC08000: hex_code("7C0380 0580"), 0xC08005: hex_code("60")}, [(0xC08000, True, True)], h)
        self.assertFalse(check_c1(an).ok)
        h.bounds[0xC08000] = "layout"
        an = analyze({0xC08000: hex_code("7C0380 0580"), 0xC08005: hex_code("60")}, [(0xC08000, True, True)], h)
        self.assertTrue(check_c1(an).ok)

    def test_disjointness_and_reachability(self):
        h = Hints()
        h.data.append((0xC08010, 0xC0801F))
        # C0:8000 JMP $8010, into data
        an = analyze({0xC08000: hex_code("4C1080")}, [(0xC08000, True, True)], h)
        self.assertFalse(check_c5(an).ok)
        h2 = Hints()
        h2.entries.append(Entry(0xC08020, True, True, "unreached"))
        an = analyze({0xC08000: hex_code("60"), 0xC08020: hex_code("60")}, [(0xC08000, True, True)], h2)
        c6 = check_c6(an)
        self.assertTrue(c6.ok)
        self.assertEqual(c6.counts["only from `unreached` entries"], 1)
        self.assertTrue(all(c.ok for c in closure(an)))
        self.assertEqual(code_set(an), {(0xC08000, True, True), (0xC08020, True, True)})


if __name__ == "__main__":
    unittest.main()
