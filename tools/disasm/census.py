"""ROM byte census for the matching disassembly (docs/MATCHING.md).

    python3 -m tools.disasm.census [--rom roms/ct-usa.sfc] [--coverage traces/coverage.txt ...]

Runs the analysis (hints and coverage, as `python3 -m tools.disasm`), then
classifies every ROM byte: code the analysis found, jump tables, the cartridge
header, and, in what's left, runs of padding, streams in the format of the
decompressor at $C3:0557 (game/data/decompress.c), BRR-like sample data, and
pointer-table-like runs. What no rule claims is unknown. Prints aggregate counts
per bank only, never bytes, so its output can go into docs.

The data rules are heuristics for sizing the work, not classification: a
stream is a byte range the decompressor would consume exactly, a BRR run is a
sequence of plausible 9-byte blocks, and so on. Each rule only claims bytes no
earlier rule claimed, in the order of CLASSES. Typed [[data]] hints come first
after code and tables: padding, lz and brr hints count in those columns, the
other types as typed data.

What stays unknown is also scanned for code-shaped runs (code_shaped): a
linear decode, under some pair of widths, of at least six instructions without
BRK/COP/WDM/STP that ends in a return or an unconditional jump and calls or
jumps to at least two entries the analysis knows. On this ROM the rule finds
nothing in the 2.2 MiB of candidate streams and about 40% of known code in C1,
so a run it reports is worth a look; its absence is evidence, not proof.
"""

import argparse
import math
import sys
import zlib
from collections import Counter
from pathlib import Path

from . import coverage as coverage_mod
from . import hints as hints_mod
from .analyze import Analyzer, State
from .cpu import RETURNS, SUSPICIOUS, Mode, decode
from .memmap import ROM_SIZE, Rom, bus_to_offset

REPO_ROOT = Path(__file__).resolve().parents[2]
ROM_CRC32 = 0x2D206BF7

CLASSES = ["code", "jump tables", "padding", "compressed", "BRR-like", "24-bit ptr tables",
           "16-bit code-ptr tables", "16-bit offset tables", "typed data", "unknown"]
CODE, TABLE, PAD, STREAM, BRR, PTR24, CODEPTR, OFS16, TYPED, UNKNOWN = range(len(CLASSES))
HINT_CLASS = {"padding": PAD, "lz": STREAM, "brr": BRR, "ptr24": PTR24}
MIN_SHAPED_INSNS = 6
MIN_SHAPED_KNOWN = 2

MIN_PADDING = 16            # bytes of one value ($00 or $FF)
MIN_STREAM_OUTPUT = 64      # decompressed bytes
MIN_STREAM_INPUT = 16       # compressed bytes
MIN_BRR_BLOCKS = 24
MIN_PTR24_ENTRIES = 4
MIN_CODEPTR_ENTRIES = 3
MIN_OFS16_ENTRIES = 8


def parse_stream(data: bytes, off: int) -> tuple[int, int] | None:
    """If the decompressor at $C3:0557 would consume a well-formed stream at file
    offset `off`, return (stream bytes, output bytes); otherwise None.

    Well-formed: segment ends are met exactly, back-references only reach bytes
    already produced (distance 1..output so far), the output fits a bank, and
    the stream doesn't run past its bank (the source index is 16 bits)."""
    n = len(data)
    base = off & 0xFFFF
    if off + 2 > n:
        return None
    x = 2
    seg = 2 + (data[off] | data[off + 1] << 8)
    if base + seg > 0xFFFF or off + seg >= n:
        return None
    wide = data[off + seg] & 0xC0
    out = 0
    bits = 8
    while True:
        if base + x > 0xFFFF or off + x >= n:
            return None
        if x == seg:
            header = data[off + x] & 0x3F
            if header == 0:
                return x + 1, out
            if base + x + 2 > 0xFFFF or off + x + 3 > n:
                return None
            bits = header
            seg = data[off + x + 1] | data[off + x + 2] << 8
            x += 3
            if seg < x or base + seg > 0xFFFF:
                return None
            continue
        if x > seg:
            return None
        control = data[off + x]
        if control == 0:           # eight literals
            x += 9
            out += 8
            continue
        x += 1
        ref = control & 1
        control >>= 1
        while True:
            if not ref:
                x += 1
                out += 1
            else:
                if base + x + 1 > 0xFFFF or off + x + 2 > n:
                    return None
                lo, hi = data[off + x], data[off + x + 1]
                word = lo | hi << 8
                if wide:
                    length, distance = (hi >> 3) + 3, word & 0x07FF
                else:
                    length, distance = (hi >> 4) + 3, word & 0x0FFF
                if distance == 0 or distance > out:
                    return None
                out += length
                x += 2
            bits -= 1
            if bits == 0:
                bits = 8
                break
            ref = control & 1
            control >>= 1
        if out > 0x10000:
            return None


def _runs(cls: bytearray, start_ok, extend, min_len: int, label: int) -> list:
    """Generic scan: at each unclaimed offset where start_ok(i), extend(i) gives the
    run's end; runs of at least min_len bytes are claimed as label."""
    found = []
    i, n = 0, len(cls)
    while i < n:
        if cls[i] != UNKNOWN or not start_ok(i):
            i += 1
            continue
        j = extend(i)
        if j - i >= min_len:
            cls[i:j] = bytes([label]) * (j - i)
            found.append((i, j - i))
            i = j
        else:
            i += 1
    return found


class Census:
    def __init__(self, data: bytes, an: Analyzer):
        self.data = data
        self.an = an
        self.cls = bytearray([UNKNOWN]) * len(data)
        self.streams = []           # (offset, stream bytes, output bytes)
        self.found = {}

    def run(self):
        data, cls, an = self.data, self.cls, self.an
        for off in an.covered_offsets():
            cls[off] = CODE
        for jt in an.tables.values():
            o = bus_to_offset(jt.table)
            if o is None:                      # a table in a RAM section
                continue
            for i in range(o, o + jt.count * (3 if jt.kind == "long" else 2)):
                if cls[i] == UNKNOWN:
                    cls[i] = TABLE
        for i in range(0xFFB0, 0x10000):      # cartridge header and vectors
            if cls[i] == UNKNOWN:
                cls[i] = TABLE
        for r in an.hints.data_regions:        # typed [[data]] hints
            if r.type is None:
                continue
            o = bus_to_offset(r.start)
            for i in range(o, bus_to_offset(r.end) + 1):
                if cls[i] == UNKNOWN:
                    cls[i] = HINT_CLASS.get(r.type, TYPED)

        def same_value(i):
            j = i
            while j < len(data) and cls[j] == UNKNOWN and data[j] == data[i]:
                j += 1
            return j
        self.found["padding"] = _runs(cls, lambda i: data[i] in (0x00, 0xFF), same_value, MIN_PADDING, PAD)

        i = 0
        while i < len(data):
            if cls[i] == UNKNOWN:
                r = parse_stream(data, i)
                if (r and r[0] >= MIN_STREAM_INPUT and r[1] >= MIN_STREAM_OUTPUT
                        and all(c == UNKNOWN for c in cls[i:i + r[0]])):
                    self.streams.append((i, r[0], r[1]))
                    cls[i:i + r[0]] = bytes([STREAM]) * r[0]
                    i += r[0]
                    continue
            i += 1

        def brr_start(i):       # first block: shift <= 12, filter 0, not the end block
            return data[i] >> 4 <= 12 and not data[i] & 0x0D

        def brr_extend(i):
            j = i
            blocks = 0
            while j + 9 <= len(data) and cls[j] == UNKNOWN and cls[j + 8] == UNKNOWN and data[j] >> 4 <= 12:
                blocks += 1
                j += 9
                if data[j - 9] & 1:             # end flag
                    return j if blocks >= MIN_BRR_BLOCKS else i
            return i
        self.found["brr"] = _runs(cls, brr_start, brr_extend, 1, BRR)

        def ptr24_extend(i):
            bank = data[i + 2]
            j = i
            if bank >= 0xC0:
                while j + 3 <= len(data) and cls[j] == UNKNOWN and cls[j + 2] == UNKNOWN and data[j + 2] == bank:
                    j += 3
            return j
        self.found["ptr24"] = _runs(cls, lambda i: i + 3 <= len(data), ptr24_extend, 3 * MIN_PTR24_ENTRIES, PTR24)

        starts = set(an.code)

        def codeptr_extend(i):
            bank = 0xC00000 | (i & 0x3F0000)
            j = i
            while (j + 2 <= len(data) and cls[j] == UNKNOWN and cls[j + 1] == UNKNOWN
                   and (bank | data[j] | data[j + 1] << 8) in starts):
                j += 2
            return j
        self.found["codeptr"] = _runs(cls, lambda i: True, codeptr_extend, 2 * MIN_CODEPTR_ENTRIES, CODEPTR)

        def ofs16_extend(i):
            j = i + 2
            prev = data[i] | data[i + 1] << 8
            while j + 2 <= len(data) and cls[j] == UNKNOWN and cls[j + 1] == UNKNOWN:
                v = data[j] | data[j + 1] << 8
                if not 0 < v - prev < 0x1000:
                    break
                prev = v
                j += 2
            return j
        self.found["ofs16"] = _runs(cls, lambda i: i + 2 <= len(data), ofs16_extend, 2 * MIN_OFS16_ENTRIES, OFS16)
        return self

    def code_shaped(self, weak: bool = False) -> list:
        """(offset, bytes) of code-shaped runs in unknown bytes (module docstring);
        with weak=True also in [[data]] regions typed mixed or undetermined."""
        an, cls, data = self.an, self.cls, self.data
        scan = bytearray(c == UNKNOWN for c in cls)
        if weak:
            for r in an.hints.data_regions:
                if r.type in ("mixed", "undetermined"):
                    o = bus_to_offset(r.start)
                    scan[o:bus_to_offset(r.end) + 1] = bytes([1]) * (bus_to_offset(r.end) + 1 - o)
        entries = {e for e, _ in an.contexts}
        memo = {}
        found = []
        o = 0
        n = len(data)
        while o < n:
            if not scan[o]:
                o += 1
                continue
            best = 0
            for widths in ((True, False), (False, False), (True, True), (False, True)):
                r = self._shaped_run(o, State(*widths), entries, scan, memo)
                if r and r[1] >= MIN_SHAPED_INSNS and r[2] >= MIN_SHAPED_KNOWN:
                    best = max(best, r[0])
            if best:
                found.append((o, best))
                o += best
            else:
                o += 1
        return found

    def _shaped_run(self, off: int, st: State, entries: set, scan, memo: dict):
        """(bytes, instructions, known calls) from off to the first return or
        unconditional jump, or None; memoized on (offset, state)."""
        an = self.an
        chain = []
        result = None
        while True:
            key = (off, st)
            if key in memo:
                result = memo[key]
                break
            pc = 0xC00000 | off
            insn = decode(an.rom.read, pc, st.m, st.x)
            if insn is None or insn.name in SUSPICIOUS or not all(scan[off:off + insn.size]):
                result = None
                memo[key] = None
                break
            known = int((insn.name in ("JSR", "JMP") and insn.mode is Mode.ABS
                         and (pc & 0xFF0000 | insn.operand) in entries)
                        or (insn.name in ("JSL", "JML") and insn.mode is Mode.LONG and insn.operand in entries))
            if insn.name in RETURNS or insn.name in ("JMP", "JML", "BRA", "BRL"):
                result = (insn.size, 1, known)
                memo[key] = result
                break
            chain.append((key, insn.size, known))
            if len(chain) > 4096:
                result = None
                break
            st = an.step_state(insn, st)
            off = bus_to_offset(insn.next_pc)
            if off is None or insn.next_pc & 0xFFFF < insn.pc & 0xFFFF:
                result = None
                break
        for key, size, known in reversed(chain):
            if result is not None:
                result = (result[0] + size, result[1] + 1, result[2] + known)
            memo[key] = result
        return result

    # ---- reporting (aggregates only) ---------------------------------------------

    def per_bank(self) -> list[Counter]:
        rows = [Counter() for _ in range(len(self.data) >> 16)]
        for off, c in enumerate(self.cls):
            rows[off >> 16][c] += 1
        return rows

    def stream_pointer_count(self) -> int:
        """Streams whose first byte some 24-bit little-endian value in the ROM points
        at (bank $C0-$FF form, or $40-$7D)."""
        n = 0
        for off, _, _ in self.streams:
            pats = [bytes([off & 0xFF, off >> 8 & 0xFF, 0xC0 | off >> 16])]
            if off < 0x3E0000:
                pats.append(bytes([off & 0xFF, off >> 8 & 0xFF, 0x40 | off >> 16]))
            n += any(self.data.find(p) >= 0 for p in pats)
        return n

    def entropy_pages(self) -> Counter:
        """Unknown bytes per Shannon-entropy band, over 4 KiB pages with >= 512 unknown bytes."""
        bands = Counter()
        for p in range(0, len(self.data), 0x1000):
            vals = [self.data[k] for k in range(p, p + 0x1000) if self.cls[k] == UNKNOWN]
            if len(vals) < 512:
                bands["sparse pages"] += len(vals)
                continue
            h = -sum(v / len(vals) * math.log2(v / len(vals)) for v in Counter(vals).values())
            band = next(name for top, name in ((1, "<1"), (3, "1-3"), (5, "3-5"), (6.5, "5-6.5"),
                                               (7.5, "6.5-7.5"), (9, ">=7.5")) if h < top)
            bands[band] += len(vals)
        return bands

    def operand_counts(self) -> Counter:
        """Instruction operands by what symbolizing them takes."""
        ops = Counter()
        for pc, variants in self.an.code.items():
            insn = next(iter(variants.values()))
            m, name = insn.mode, insn.name
            if name in ("JSL", "JML") and m is Mode.LONG:
                ops["JSL/JML long"] += 1
            elif m in (Mode.LONG, Mode.LONGX):
                ops["long operand into ROM" if bus_to_offset(insn.operand) is not None else "long operand, RAM/IO"] += 1
            elif name in ("JSR", "JMP") and m is Mode.ABS:
                ops["JSR/JMP abs"] += 1
            elif m in (Mode.ABS, Mode.ABSX, Mode.ABSY):
                v = insn.operand
                ops["abs < $2000" if v < 0x2000 else "abs $2000-$7FFF" if v < 0x8000 else "abs >= $8000"] += 1
            elif m in (Mode.REL8, Mode.REL16) and name != "PER":
                ops["branch"] += 1
            elif m in (Mode.IMM_M, Mode.IMM_X) and insn.size == 3:
                ops["16-bit immediate"] += 1
            elif m in (Mode.ABSIND, Mode.ABSXIND, Mode.ABSLIND):
                ops["indirect jump/call"] += 1
            elif name in ("PEA", "PER", "MVN", "MVP"):
                ops[name] += 1
        return ops


def main():
    ap = argparse.ArgumentParser(prog="python3 -m tools.disasm.census")
    ap.add_argument("--rom", type=Path, default=REPO_ROOT / "roms" / "ct-usa.sfc")
    ap.add_argument("--hints", type=Path, default=REPO_ROOT / "re" / "hints.toml")
    ap.add_argument("--coverage", type=Path, action="append", default=[])
    ap.add_argument("--list-code-shaped", action="store_true",
                    help="print the address and size of every code-shaped run in unknown bytes")
    args = ap.parse_args()

    if not args.rom.exists():
        sys.exit(f"{args.rom} not found; stage the ROM with tools/rom.py first")
    data = args.rom.read_bytes()
    if len(data) != ROM_SIZE or zlib.crc32(data) != ROM_CRC32:
        sys.exit(f"{args.rom}: not Chrono Trigger (USA), CRC32 {ROM_CRC32:08X}")
    an = Analyzer(Rom(data), hints_mod.load(args.hints), coverage=coverage_mod.load(args.coverage))
    an.run()
    c = Census(data, an).run()

    rows = c.per_bank()
    print("| bank | " + " | ".join(CLASSES) + " |")
    print("|---" * (len(CLASSES) + 1) + "|")
    total = Counter()
    for b, row in enumerate(rows):
        total.update(row)
        print(f"| ${0xC0 + b:02X} | " + " | ".join(str(row[k]) for k in range(len(CLASSES))) + " |")
    print("| all | " + " | ".join(str(total[k]) for k in range(len(CLASSES))) + " |")
    print("| % | " + " | ".join(f"{total[k] / len(data):.1%}" for k in range(len(CLASSES))) + " |")
    print()
    s_in = sum(l for _, l, _ in c.streams)
    s_out = sum(y for *_, y in c.streams)
    print("resources:", an.resources.summary())
    for p in an.resources.problems:
        print("  !", p)
    print(f"streams the heuristic still claims: {len(c.streams)}, {s_in} bytes decompressing to {s_out}; "
          f"{c.stream_pointer_count()} have a 24-bit pointer to their start in the ROM")
    print(f"BRR-like runs: {len(c.found['brr'])}; 24-bit pointer runs: {len(c.found['ptr24'])}; "
          f"16-bit code-pointer runs: {len(c.found['codeptr'])}; 16-bit offset runs: {len(c.found['ofs16'])}")
    print("unknown bytes by page entropy (bits/byte):", dict(c.entropy_pages()))
    print("operands:", dict(c.operand_counts()))
    long_targets = Counter(CLASSES[c.cls[bus_to_offset(i.operand)]]
                           for v in an.code.values() for i in [next(iter(v.values()))]
                           if i.mode in (Mode.LONG, Mode.LONGX) and i.name not in ("JSL", "JML")
                           and bus_to_offset(i.operand) is not None)
    print("long data operands into ROM, by class of the target:", dict(long_targets))
    shaped = c.code_shaped()
    weak = [r for r in c.code_shaped(weak=True) if r not in shaped]
    per_bank = Counter()
    for off, size in shaped:
        per_bank[off >> 16] += size
    print(f"code-shaped runs in unknown bytes: {len(shaped)}, {sum(s for _, s in shaped)} bytes; per bank: "
          + (", ".join(f"${0xC0 + b:02X} {v}" for b, v in sorted(per_bank.items())) or "none"))
    print(f"code-shaped runs in data hints typed mixed or undetermined: {len(weak)}, {sum(s for _, s in weak)} bytes")
    if args.list_code_shaped:
        for off, size in shaped + weak:
            print(f"  {0xC0 + (off >> 16):02X}:{off & 0xFFFF:04X} {size} bytes")
    print(f"contexts {len(an.contexts)}, jump tables {len(an.tables)} "
          f"({sum(1 for t in an.tables.values() if t.source == 'heuristic')} heuristic), "
          f"unresolved indirect {len(an.unresolved)}, assumed returns {len(an.assumed)}, "
          f"width conflicts {len(an.width_conflicts())}, "
          f"executed RAM addresses {sum(1 for a in an.coverage if bus_to_offset(a) is None)}")


if __name__ == "__main__":
    main()
