"""MD1 pieces of the matching disassembly (docs/MATCHING.md), no ROM needed: the
decompressor, RAM sections, the hints they use, `base` in the assembler, and the
emitter's overlap, data and coverage checks.

    python3 -m unittest discover tests
"""

import tempfile
import unittest
from pathlib import Path

from tools.disasm import hints as hints_mod
from tools.disasm.analyze import Analyzer
from tools.disasm.hints import DataHint, Entry, Hints, RamCodeHint
from tools.disasm.lz import decompress

from tools.disasm.ram import canonical_ram, simulate_writes
from tools.matching.emit import Emitter
from tools.matching.ramsrc import build_section, check_ram_coverage, check_section
from tests.test_disasm import FakeRom
from tests.test_matching_asm import assemble, bytes_at


def stream(body: bytes) -> bytes:
    return len(body).to_bytes(2, "little") + body + b"\x00"


class Decompress(unittest.TestCase):
    def test_literals_and_overlapping_reference(self):
        # Control bits LSB first: two literals, then a reference copying 3 bytes
        # from distance 2 (so it reads bytes it writes), then five literals.
        body = bytes([0b00000100]) + b"AB" + b"\x02\x00" + b"CDEFG"
        out, used = decompress(stream(body) + b"\xEE" * 8, 0)
        self.assertEqual(out, b"ABABACDEFG")
        self.assertEqual(used, len(stream(body)))

    def test_reference_before_the_output_is_rejected_unless_lenient(self):
        body = bytes([0b00000001]) + b"\x01\x00" + b"ABCDEFG"
        self.assertIsNone(decompress(stream(body), 0))
        out, _ = decompress(stream(body), 0, strict=False)
        self.assertEqual(out[-7:], b"ABCDEFG")


class Hint(unittest.TestCase):
    def load(self, text: str) -> Hints:
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "h.toml"
            p.write_text(text, encoding="utf-8")
            return hints_mod.load(p)

    def test_new_kinds(self):
        h = self.load('[[noreturn]]\naddr = "C0:1234"\n'
                      '[[overlap]]\naddr = "C2:8C52"\nm = 8\nx = 8\n'
                      '[[data]]\nstart = "C0:9000"\nend = "C0:9003"\ntype = "ptr16"\nname = "tbl"\n'
                      '[[ramcode]]\nname = "s"\nbase = "00:0500"\nsource = "C0:8000"\nformat = "copy"\nsize = 4\n'
                      '[[jumptable]]\nat = "7E:3000"\ncount = 0\ntargets = ["7E:3100"]\n')
        self.assertIn(0xC01234, h.noreturn)
        self.assertEqual(h.overlaps[0xC28C52], (True, True))
        self.assertEqual(h.data_regions, [DataHint(0xC09000, 0xC09003, "ptr16", "tbl", {})])
        self.assertTrue(h.is_data(0xC09002) and not h.is_data(0xC09004))
        self.assertEqual(h.ramcode[0].size, 4)
        self.assertEqual(h.tables[0x7E3000].targets, [0x7E3100])

    def test_bad_data_type(self):
        with self.assertRaises(ValueError):
            self.load('[[data]]\nstart = "C0:9000"\nend = "C0:9003"\ntype = "sprites?"\n')


class RamSections(unittest.TestCase):
    # C0:8000 REP #$30 ; JSL $7E3000 ; STP
    # C0:9000 the code copied to $7E:3000: LDA #$1234 ; BNE +0 ; RTL
    # C0:A000 LDA #$5C ; STA $0500 ; LDX #$8000 ; STX $0501 ; LDA #$C0 ; STA $0503
    CODE = {0xC08000: bytes.fromhex("C230 2200307E DB".replace(" ", "")),
            0xC09000: bytes.fromhex("A93412 D000 6B".replace(" ", "")),
            0xC0A000: bytes.fromhex("A95C 8D0005 A20080 8E0105 A9C0 8D0305".replace(" ", ""))}

    def analyzer(self):
        h = Hints()
        h.entries.append(Entry(0xC08000, True, True, "test"))
        h.ramcode.append(RamCodeHint("prog", 0x7E3000, 0xC09000, "copy", 6))
        h.ramcode.append(RamCodeHint("stub", 0x000500, 0xC0A000, "writes", 4, 0xC0A00F, True, False))
        rom = FakeRom(self.CODE)
        an = Analyzer(rom, h)
        an.run()
        return rom, an

    def test_traced_and_returns(self):
        _, an = self.analyzer()
        self.assertIn(0x7E3003, an.code)
        self.assertEqual(len(next(iter(an.code[0x7E3000].values())).raw), 3)   # 16-bit A from the caller
        self.assertIn(0xC08006, an.code)                                       # the JSL returned
        self.assertFalse(an.assumed)

    def test_writes_image(self):
        rom, an = self.analyzer()
        stub = next(s for s in an.ram.sections if s.name == "stub")
        self.assertEqual(stub.image, bytes.fromhex("5C0080C0"))
        self.assertEqual(canonical_ram(0x000500), 0x7E0500)
        h = RamCodeHint("x", 0x000500, 0xC0A000, "writes", 8, 0xC0A00F, True, False)
        self.assertIsNone(simulate_writes(rom, h)[0])                          # bytes 4-7 never written

    def test_source_assembles_to_the_image(self):
        _, an = self.analyzer()
        for sec in an.ram.sections:
            files, st = build_section(sec, an)
            with tempfile.TemporaryDirectory() as d:
                for rel, content in files.items():
                    p = Path(d) / rel
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.write_bytes(content.encode() if isinstance(content, str) else content)
                check_section(sec, Path(d) / "ram" / f"{sec.name}.asm", st)
            self.assertFalse(st.problems, sec.name)
            if sec.name == "prog":
                self.assertIn("base $7E3000", files["ram/prog.asm"])
                self.assertEqual(st.insn_bytes, 6)
        sections = [(s, build_section(s, an)[1]) for s in an.ram.sections]
        total, inside, problems, outside = check_ram_coverage(
            sections, {0x7E3000: {(False, True)}, 0x7E3001: {(False, True)}, 0x7F0000: {(True, True)}})
        self.assertEqual((total, inside, len(problems), outside), (3, 2, 1, [0x7F0000]))


class Base(unittest.TestCase):
    def test_labels_and_branches_use_the_base(self):
        res = assemble("hirom\norg $C08000\nbase $7E3000\nstart:\n\tBRA start\n\tJSR.w start\nbase off\nafter:\n")
        self.assertEqual(res.symbols["start"], 0x7E3000)
        self.assertEqual(res.symbols["after"], 0xC08005)
        self.assertEqual(bytes_at(res, 0xC08000, 5), bytes.fromhex("80FE200030"))

    def test_org_ends_base(self):
        res = assemble("hirom\norg $C08000\nbase $7E3000\norg $C09000\nhere:\n")
        self.assertEqual(res.symbols["here"], 0xC09000)


class EmitterChecks(unittest.TestCase):
    # C0:8000 SEP #$30 ; JSR $8010 ; REP #$30 ; JSR $8010 ; STP
    # C0:8010 LDA #imm ; XBA ; RTS     2+1 bytes with m8, 3 with m16: an overlap
    # C0:8020 a 4-byte table (data hint)
    PROGRAM = {0xC08000: bytes.fromhex("E2302010 80C23020 1080DB".replace(" ", "")),
               0xC08010: bytes.fromhex("A900EB60"), 0xC08020: bytes.fromhex("01020304")}

    def build(self, overlap=True, data_type="bytes"):
        h = Hints()
        h.entries.append(Entry(0xC08000, True, True, "test"))
        if overlap:
            h.overlaps[0xC08010] = (True, True)
        h.data.append((0xC08020, 0xC08023))
        h.data_regions.append(DataHint(0xC08020, 0xC08023, data_type, "tbl"))
        rom = FakeRom(self.PROGRAM)
        an = Analyzer(rom, h)
        an.run()
        em = Emitter(rom.data, an)
        return rom, an, em, em.build()

    def test_overlap_writes_the_hinted_decode(self):
        rom, an, em, files = self.build()
        text = files["bank_C0.asm"]
        self.assertIn("LDA.b #$00", text)
        self.assertIn("also runs as [m16 x16] LDA #$EB00", text)
        self.assertFalse(em.stats.overlap_problems)
        self.assertFalse(an.width_conflicts())
        self.assertIn(0xC08010, an.width_conflicts(declared=True))
        res = assemble(files["rom.asm"], files={f"src/{k}": v for k, v in files.items()})
        self.assertEqual(bytes(res.image), rom.data)

    def test_coverage_check(self):
        _, _, em, _ = self.build()
        ok = em.check_coverage({0xC08010: {(True, True), (False, False)}, 0xC08012: {(True, True)}})
        self.assertEqual((ok.rom, ok.ok, ok.problems), (2, 2, []))
        bad = em.check_coverage({0xC08011: {(True, True)}, 0xC08013: {(True, False)}, 0xC08000: {(True, True)}})
        self.assertEqual([a for a, _ in bad.problems], [0xC08011, 0xC08013])   # inside; widths it never gets
        _, _, plain, _ = self.build(overlap=False)
        self.assertTrue(plain.check_coverage({0xC08010: {(True, True)}}).problems)   # a db cluster

    def test_data_regions_are_named_and_checked(self):
        _, _, em, files = self.build()
        self.assertIn("data/bytes/tbl.bin", files)
        self.assertIn("tbl:\n", files["bank_C0.asm"])
        self.assertFalse(em.check_data())
        _, _, em2, _ = self.build(data_type="padding")
        self.assertTrue(any("fill value" in p for p in em2.check_data()))
        _, _, em3, _ = self.build(data_type="ptr24")
        self.assertTrue(any("multiple of 3" in p for p in em3.check_data()))


if __name__ == "__main__":
    unittest.main()
