"""The matching source's assembler and emitter (tools/matching, docs/MATCHING.md), no ROM needed.

    python3 -m unittest discover tests
"""

import unittest
from pathlib import Path

from tools.disasm.analyze import Analyzer
from tools.disasm.cpu import OPCODES, OPERAND_SIZE, Mode, decode
from tools.disasm.hints import Entry, Hints
from tools.disasm.memmap import bus_to_offset
from tools.matching.asm import AsmError, Assembler
from tools.matching.emit import Emitter, insn_line, registry_names
from tests.test_disasm import FakeRom

PC = 0xC08000
IMM_MODES = (Mode.IMM_M, Mode.IMM_X)


def assemble(text: str, symbols: dict | None = None, files: dict | None = None):
    files = files or {}

    def loader(path: Path) -> bytes:
        key = path.as_posix()
        if key not in files:
            raise FileNotFoundError(2, "no such file", key)
        v = files[key]
        return v.encode() if isinstance(v, str) else v
    return Assembler(loader, symbols).assemble_text(text, name="main.asm", base=Path("src"))


def bytes_at(res, addr: int, n: int) -> bytes:
    off = bus_to_offset(addr)
    assert all(res.written[off:off + n]), "not assembled"
    return bytes(res.image[off:off + n])


def one(line: str, symbols: dict | None = None, widths: str = "") -> bytes:
    res = assemble(f"hirom\norg ${PC:06X}\n{widths}\n\t{line}\n", symbols)
    n = sum(span[1] for span in res.spans)
    return bytes_at(res, PC, n)


def operand_samples(n: int) -> list[int]:
    return {0: [0], 1: [0x00, 0x12, 0x7F, 0x80, 0xFF],
            2: [0x0000, 0x0012, 0x1234, 0x7FFF, 0x8000, 0xFFFF],
            3: [0x000000, 0x000012, 0x7E1234, 0xC0FFEE, 0xFFFFFF]}[n]


def forms():
    """Every opcode in every width it has: (opcode, m8, x8)."""
    for op, (name, mode) in enumerate(OPCODES):
        if mode is Mode.IMM_M:
            yield op, True, None
            yield op, False, None
        elif mode is Mode.IMM_X:
            yield op, None, True
            yield op, None, False
        else:
            yield op, None, None


def annotation(m, x) -> str:
    f = lambda v: "?" if v is None else 8 if v else 16   # noqa: E731
    return f";@ m{f(m)} x{f(x)}"


class EveryOpcode(unittest.TestCase):
    """The encoder against the decoder: each opcode in each width with a range of
    operands, written as the emitter writes it, assembles to the original bytes
    and decodes back to the same instruction."""

    def test_round_trip(self):
        seen = set()
        count = 0
        for op, m, x in forms():
            name, mode = OPCODES[op]
            n = OPERAND_SIZE[mode] if mode not in IMM_MODES else (1 if (m if mode is Mode.IMM_M else x) else 2)
            for v in operand_samples(n):
                raw = bytes([op]) + v.to_bytes(n, "little")
                insn = decode(lambda a, k: (raw + bytes(4))[a - PC:a - PC + k], PC, m, x)
                self.assertEqual(insn.size, 1 + n)
                symbols, ref = {}, None
                if mode in (Mode.REL8, Mode.REL16):
                    symbols["target"] = insn.branch_target()
                    ref = "target"
                line = insn_line(name, mode, insn.size, insn.operand, ref)
                with self.subTest(line=line, m=m, x=x):
                    got = one(line, symbols, annotation(m, x))
                    self.assertEqual(got, raw)
                    back = decode(lambda a, k: got[a - PC:a - PC + k], PC, m, x)
                    self.assertEqual((back.name, back.mode, back.operand), (name, mode, insn.operand))
                seen.add((op, m, x))
                count += 1
        self.assertEqual(len({op for op, _, _ in seen}), 256)
        self.assertEqual(len(seen), 256 + 12)    # 8 IMM_M and 4 IMM_X opcodes have two widths
        self.assertGreater(count, 1000)

    def test_immediates_follow_the_annotation(self):
        for op, m, x in forms():
            name, mode = OPCODES[op]
            if mode not in IMM_MODES:
                continue
            narrow = (m if mode is Mode.IMM_M else x)
            wrong = annotation(not m if m is not None else None, not x if x is not None else None)
            line = f"{name}.b #$12" if narrow else f"{name}.w #$1234"
            with self.subTest(line=line):
                self.assertRaisesRegex(AsmError, "annotated", one, line, None, wrong)
                unknown = ";@ m? x16" if mode is Mode.IMM_M else ";@ m16 x?"
                self.assertRaisesRegex(AsmError, "unknown", one, line, None, unknown)


class HandWritten(unittest.TestCase):
    """Expected bytes written by hand, independent of the emitter's formatter."""

    CASES = [
        ("CLC", "", "18"),
        ("ASL A", "", "0A"),
        ("inc a", "", "1A"),
        ("LDA.b #$12", ";@ m8 x16", "A9 12"),
        ("LDA.w #$1234", ";@ m16 x8", "A9 34 12"),
        ("lda.W #$1234", "", "A9 34 12"),
        ("LDX.b #$12", ";@ m16 x8", "A2 12"),
        ("CPY.w #$BEEF", ";@ m8 x16", "C0 EF BE"),
        ("REP.b #$30", ";@ m8 x8", "C2 30"),
        ("SEP.b #%00100000", "", "E2 20"),
        ("BRK.b #$00", "", "00 00"),
        ("COP.b #$01", "", "02 01"),
        ("WDM.b #$42", "", "42 42"),
        ("LDA.b $12", "", "A5 12"),
        ("LDA.w $0012", "", "AD 12 00"),
        ("LDA.l $000012", "", "AF 12 00 00"),
        ("LDA.b $12,X", "", "B5 12"),
        ("LDA.w $1234,X", "", "BD 34 12"),
        ("LDA.w $1234, y", "", "B9 34 12"),
        ("LDA.l $7E1234,X", "", "BF 34 12 7E"),
        ("LDX.b $12,Y", "", "B6 12"),
        ("LDA.b ($12)", "", "B2 12"),
        ("LDA.b ($12,X)", "", "A1 12"),
        ("LDA.b ($12),Y", "", "B1 12"),
        ("LDA.b [$12]", "", "A7 12"),
        ("LDA.b [$12],Y", "", "B7 12"),
        ("LDA.b $03,S", "", "A3 03"),
        ("LDA.b ($03,S),Y", "", "B3 03"),
        ("STZ.w $2100", "", "9C 00 21"),
        ("JMP.w $8000", "", "4C 00 80"),
        ("JMP.w ($0050)", "", "6C 50 00"),
        ("JMP.w ($8010,X)", "", "7C 10 80"),
        ("JSR.w ($8010,X)", "", "FC 10 80"),
        ("JML.w [$0000]", "", "DC 00 00"),
        ("JML.l $FDC000", "", "5C 00 C0 FD"),
        ("JSL.l $C00005", "", "22 05 00 C0"),
        ("PEA.w $2100", "", "F4 00 21"),
        ("PEI.b ($30)", "", "D4 30"),
        ("MVN $C3,$7E", "", "54 7E C3"),       # MVN src,dst encodes dst first
        ("MVP $7E,$7F", "", "44 7F 7E"),
        ("BIT.b #$80", ";@ m8 x16", "89 80"),
        ("PHP", "", "08"),
        ("XCE", "", "FB"),
        ("RTL", "", "6B"),
    ]

    def test_cases(self):
        for line, widths, expected in self.CASES:
            with self.subTest(line=line):
                self.assertEqual(one(line, None, widths).hex(" ").upper(), expected)

    def test_branches(self):
        self.assertEqual(one("BNE fwd", {"fwd": PC + 2 + 0x10}), bytes.fromhex("D010"))
        self.assertEqual(one("BRA back", {"back": PC + 2 - 0x80}), bytes.fromhex("8080"))
        self.assertEqual(one("BEQ self", {"self": PC}), bytes.fromhex("F0FE"))
        self.assertEqual(one("BRL far", {"far": PC + 3 + 0x1234}), bytes.fromhex("823412"))
        self.assertEqual(one("PER here+2", {"here": PC + 3}), bytes.fromhex("620200"))
        # A 16-bit displacement wraps within the bank, as the CPU adds it.
        res = assemble("hirom\norg $C000DB\n\tBRL wrap\n", {"wrap": 0xC0B271})
        self.assertEqual(bytes_at(res, 0xC000DB, 3), bytes.fromhex("8293B1"))

    def test_labels_in_bank_local_jumps(self):
        self.assertEqual(one("JSR.w sub", {"sub": 0xC09000}), bytes.fromhex("200090"))
        self.assertEqual(one("JMP.w (table,X)", {"table": 0xC0A000}), bytes.fromhex("7C00A0"))
        self.assertEqual(one("JSL.l sub", {"sub": 0xFDC2C1}), bytes.fromhex("22C1C2FD"))


class Directives(unittest.TestCase):
    def test_org_maps_hirom(self):
        res = assemble("hirom\norg $C12345\n\tdb $01\norg $412346\n\tdb $02\norg $818000\n\tdb $03\n"
                       "org $FFFFFF\n\tdb $04\n")
        self.assertEqual(res.image[0x012345:0x012347], b"\x01\x02")
        self.assertEqual(res.image[0x018000], 3)
        self.assertEqual(res.image[0x3FFFFF], 4)
        self.assertEqual(sum(res.written), 4)

    def test_data(self):
        res = assemble("hirom\norg $C00000\nstart:\n\tdb $01, 2, %11\n\tdw $1234,start-$C00000\n\tdl start+3\n",
                       None)
        self.assertEqual(bytes(res.image[:10]), bytes.fromhex("010203" "34120000" "0300C0"))

    def test_labels_forward_and_back(self):
        res = assemble("hirom\norg $C08000\nback:\n\tBRA fwd\n\tdw fwd-back\nfwd:\n\tJML.l back\n")
        self.assertEqual(bytes_at(res, 0xC08000, 8), bytes.fromhex("8002 0400 5C0080C0".replace(" ", "")))
        self.assertEqual(res.symbols, {"back": 0xC08000, "fwd": 0xC08004})

    def test_label_on_statement_line(self):
        res = assemble("hirom\norg $C08000\nhere: RTS\n")
        self.assertEqual(res.symbols["here"], 0xC08000)
        self.assertEqual(res.image[0x8000], 0x60)

    def test_incbin_and_incsrc_relative_to_the_file(self):
        files = {"src/sub/part.asm": 'label:\n\tincbin "blob.bin"\n', "src/sub/blob.bin": b"\xAA\xBB"}
        res = assemble('hirom\norg $C10000\nincsrc "sub/part.asm"\n\tdw label-$C10000\n', files=files)
        self.assertEqual(bytes_at(res, 0xC10000, 4), bytes.fromhex("AABB0000"))
        self.assertEqual(res.locate(0x010001)[2:], (str(Path("src/sub/part.asm")), 2))

    def test_warnpc(self):
        assemble("hirom\norg $C0FFFE\n\tdw $1234\nwarnpc $C10000\n")
        self.assertRaisesRegex(AsmError, "past warnpc", assemble, "hirom\norg $C0FFF0\n\tdl 1\nwarnpc $C0FFF2\n")

    def test_comments_and_blank_lines(self):
        res = assemble("; top\nhirom ; mapper\n\norg $C08000 ; there\n\tNOP ; C0:8000\n")
        self.assertEqual(res.image[0x8000], 0xEA)


class Errors(unittest.TestCase):
    def check(self, text: str, pattern: str, symbols=None, files=None):
        with self.assertRaisesRegex(AsmError, pattern):
            assemble(text, symbols, files)

    def insn(self, line: str, pattern: str, symbols=None, widths=""):
        self.check(f"hirom\norg $C08000\n{widths}\n\t{line}\n", pattern, symbols)

    def test_ambiguous_sizes(self):
        for line in ("LDA $12", "LDA #$12", "JMP $8000", "LDA $1234,X", "JML [$0000]", "PEI ($12)", "PEA $1234"):
            with self.subTest(line=line):
                self.insn(line, "must be explicit")

    def test_no_suffix_where_the_layout_is_fixed(self):
        self.insn("BNE.b t", "no size suffix", {"t": PC})
        self.insn("BRL.w t", "no size suffix", {"t": PC})
        self.insn("MVN.b $01,$02", "no size suffix")

    def test_modes_that_dont_exist(self):
        for line in ("LDA.l $123456,Y", "LDY.l $123456", "STZ.l $123456", "REP.w #$1234", "CLC.b $12",
                     "JSR.l ($1234,X)", "LDX.b $12,X", "STA.b #$12", "JMP.b $12", "PEA.l $123456"):
            with self.subTest(line=line):
                self.insn(line, "not an addressing mode")

    def test_values_must_fit(self):
        self.insn("LDA.b $123", "doesn't fit")
        self.insn("LDA.w #$12345", "doesn't fit", widths=";@ m16 x16")
        self.insn("MVN $100,$00", "doesn't fit")
        self.check("hirom\norg $C08000\n\tdb 256\n", "doesn't fit")
        self.insn("LDA.w label", "doesn't fit", {"label": 0xC09000})

    def test_branch_range_and_bank(self):
        self.insn("BNE t", "out of range", {"t": PC + 2 + 0x80})
        self.insn("BNE t", "out of range", {"t": PC + 2 - 0x81})
        self.insn("BRL t", "isn't in the bank", {"t": 0xC18000})
        self.insn("BNE $8000", "isn't in the bank")
        self.insn("JSR.w t", "isn't in the bank", {"t": 0xC19000})

    def test_labels(self):
        self.insn("JSL.l nowhere", "undefined label nowhere")
        self.check("hirom\norg $C08000\na1:\na1:\n", "defined twice")
        self.check("hirom\norg $C08000\nlda:\n", "reserved")
        self.check("hirom\nearly:\n", "before any org")

    def test_layout(self):
        self.check("hirom\norg $C08000\n\tdw 1\norg $C08001\n\tdb 2\n", "written twice")
        self.check("hirom\norg $C0FFFF\n\tLDA.w $1234\n", "across a bank boundary")
        self.check("org $C08000\n", "before hirom")
        self.check("hirom\n\tNOP\n", "before any org")
        self.check("hirom\norg $7E0000\n", "isn't ROM")

    def test_syntax(self):
        self.insn("FOO.b $12", "unknown mnemonic")
        self.insn("LDA.q $12", "bad size suffix")
        self.check("hirom\n;@ m8\n", "bad width annotation")
        self.check('hirom\norg $C08000\n\tincbin blob.bin\n', "quoted file name")
        self.check('hirom\norg $C08000\n\tincbin "missing.bin"\n', "can't read")
        self.check("hirom\norg $C08000\n\tdb 1,,2\n", "empty value")
        self.insn("LDA.b $12+", "bad expression")

    def test_errors_name_file_and_line(self):
        try:
            assemble("hirom\norg $C08000\n\tNOP\n\tLDA $12\n")
        except AsmError as e:
            self.assertEqual(e.where, (str(Path("src/main.asm")), 4))
        else:
            self.fail("no error")


# Synthetic program for the emitter:
#   C0:8000  SEP #$30
#   C0:8002  JSR $8030      A is 8-bit
#   C0:8005  REP #$20
#   C0:8007  JSR $8030      A is 16-bit
#   C0:800A  JSL $C09000
#   C0:800E  BRA $800E
#   C0:8030  LDA #imm; RTS  2 bytes with m8, 3 with m16: a length conflict
#   C0:9000  RTL
PROGRAM = {0xC08000: bytes.fromhex("E230203080C220203080220090C080FE"),
           0xC08030: bytes.fromhex("A9016060"), 0xC09000: bytes.fromhex("6B")}


class Emit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        hints = Hints()
        hints.entries.append(Entry(0xC08000, True, True, "test"))
        hints.names[0xC09000] = "far_routine"
        rom = FakeRom(PROGRAM)
        an = Analyzer(rom, hints)
        an.run()
        cls.rom = rom.data
        cls.em = Emitter(rom.data, an, {0xC08005: ["port_name"]}, {0xC0800A})
        cls.files = cls.em.build()
        cls.bank = cls.files["bank_C0.asm"]

    def test_reassembles_to_the_rom(self):
        files = {f"src/{k}": v for k, v in self.files.items()}
        res = assemble(self.files["rom.asm"], files=files)
        self.assertEqual(sum(res.written), len(self.rom))
        self.assertEqual(bytes(res.image), self.rom)

    def test_breakdown(self):
        st = self.em.stats
        self.assertEqual((st.insn_bytes, st.db_bytes, st.db_regions), (2 + 3 + 2 + 3 + 4 + 2 + 1 + 1, 3, 1))
        self.assertEqual(st.insn_bytes + st.db_bytes + st.incbin_bytes, len(self.rom))
        self.assertEqual(st.incbin_files, sum(1 for k in self.files if k.endswith(".bin")))

    def test_instructions_labels_and_widths(self):
        text = self.bank
        for expected in ("org $C00000", ";@ m8 x8", "SEP.b #$30", "JSR.w sub_C08030", "REP.b #$20",
                         ";@ m16 x8", "JSL.l far_routine", "loc_C0800E:\n\tBRA loc_C0800E", "far_routine:\n",
                         "port_name:\n", "sub_C0800A:\n", "warnpc $C10000"):
            self.assertIn(expected, text)

    def test_length_conflict_is_db_with_labels(self):
        text = self.bank
        self.assertIn("sub_C08030:\n; C0:8030-C0:8032: instructions that overlap", text)
        self.assertIn("\tdb $A9,$01\nloc_C08032:\n\tdb $60\n", text)
        self.assertIn("[m16 x8] LDA #$6001", text)

    def test_data_is_incbin_of_extracted_files(self):
        path = "data/raw/bank_C0/C08010.bin"
        self.assertIn(f'incbin "{path}"', self.bank)
        self.assertEqual(self.files[path], self.rom[0x8010:0x8030])
        self.assertEqual(self.files["data/raw/bank_C1/C10000.bin"], self.rom[0x10000:0x20000])
        self.assertEqual(self.files["rom.asm"].count("incsrc"), 64)


class Registry(unittest.TestCase):
    def test_names(self):
        text = ('{ .addr = 0xC10299, .name = "battle_draw_window", .fn = battle_draw_window },\n'
                '{ .addr = 0x7E0000, .name = "ram", .fn = ram },\n'
                '{ .addr = 0xC10299, .name = "battle_draw_window", .fn = x },\n')
        self.assertEqual(registry_names(text), {0xC10299: ["battle_draw_window"]})


if __name__ == "__main__":
    unittest.main()
