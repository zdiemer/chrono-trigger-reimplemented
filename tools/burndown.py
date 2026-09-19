"""How much of the ROM's code the hand ports cover, per bank and subsystem.

    python3 tools/burndown.py --rom roms/ct-usa.sfc
    python3 tools/burndown.py --rom roms/ct-usa.sfc --by subsystem
    python3 tools/burndown.py --rom roms/ct-usa.sfc --uncovered C1 --top 20

The recompilation runs the whole game from generated C ([RECOMP.md](../docs/RECOMP.md));
what is left to build is readable source, routine by routine (RECOMP.md's M5).
This says how far along that is, so the work can be sequenced and its progress
seen.

**What "covered" means here.** From each address in `game/registry.c`, the
instructions the port reaches without leaving the routine: branches and jumps
are followed, returns end a path, and a call is not followed because a port
calls out for what it hasn't inlined. A walk stops at another registry address,
which owns its own region. The total it is measured against is the analysis's
code, the same set the generator builds blocks from.

**Read it as an estimate.** It is wrong in both directions, and neither bias is
small enough to ignore:

- *Over-counts* where a port stops early. A port that ends at a call-out or at
  a loop head (the sound driver's wait loops, the battle main loop's entries)
  is credited with everything after it that the original reaches.
- *Under-counts* where a port inlines a callee. Ports routinely inline local
  routines with their stack bytes, and that code is credited to nobody.
- *Stops* at an indirect jump it can't resolve, so a routine that dispatches
  through a table is credited only up to the dispatch. A `JMP (abs,X)` whose
  table the analysis has sized (a hint's count, or its heuristic) is followed
  to every target, as a port that dispatches through it inlines them.

It is a burndown, not an accounting: useful for "which subsystem is barely
started", not for "this bank is 61.3% done".
"""

import argparse
import collections
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from tools.disasm import hints as hints_mod
from tools.disasm.analyze import Analyzer
from tools.disasm.cpu import Mode
from tools.disasm.memmap import Rom, bus_to_offset

ROOT = pathlib.Path(__file__).resolve().parent.parent

RETURNS = {"RTS", "RTL", "RTI", "STP"}
CALLS = {"JSR", "JSL"}
UNCONDITIONAL = {"JMP", "JML", "BRA", "BRL"}
# Bank -> what lives there, for the summary. From docs/PORTING.md's subsystems.
SUBSYSTEM = {
    0x7E: "scene program (RAM)",
    0xC0: "field engine, events",
    0xC1: "battle",
    0xC2: "menus, text",
    0xC3: "scene engine",
    0xC6: "scene data",
    0xC7: "sound driver",
    0xCC: "battle setup",
    0xCD: "battle frame work",
    0xCE: "battle",
    0xCF: "battle sprites",
    0xD1: "battle NMI, graphics",
    0xFD: "field graphics",
    0xFF: "misc",
}


def registry_addresses(path):
    """Every address named in the registry's entries."""
    src = path.read_text()
    return {int(m, 16) for m in re.findall(r"\.addr\s*=\s*0x([0-9A-Fa-f]{6})", src)}


def insn_at(an, pc):
    """The widest variant decoded at pc, or None."""
    variants = an.code.get(pc)
    if not variants:
        return None
    return max(variants.values(), key=lambda i: i.size)


def table_targets(an, insn):
    """The targets of a JMP (abs,X) whose table the analysis has sized, or []."""
    jt = an.tables.get(insn.pc)
    if jt is None or jt.call or jt.count is None:
        return []
    step = 3 if jt.kind == "long" else 2
    bank = jt.dispatcher & 0xFF0000
    out = []
    for i in range(jt.count):
        at = jt.table + i * step
        v = an.u24(at) if jt.kind == "long" else an.u16(at)
        if v is not None:
            out.append(v if jt.kind == "long" else bank | v)
    return out + list(jt.extra)


def successors(an, insn):
    """Where control can go next, not following calls."""
    name, mode = insn.name, insn.mode
    if name in RETURNS:
        return []
    if name in CALLS:
        return [insn.next_pc]           # the callee is a call-out
    if name == "JMP" and mode == Mode.ABSXIND:
        return table_targets(an, insn)
    if mode in (Mode.ABSIND, Mode.ABSXIND, Mode.ABSLIND):
        return []                        # indirect: not resolved here
    if name in ("JMP", "JML"):
        return [insn.operand if name == "JML"
                else (insn.pc & 0xFF0000) | (insn.operand & 0xFFFF)]
    if name in ("BRA", "BRL"):
        return [insn.branch_target()]
    if mode in (Mode.REL8, Mode.REL16):  # conditional branch
        return [insn.branch_target(), insn.next_pc]
    return [insn.next_pc]


def walk(an, entry, owners):
    """Bytes of code the port at `entry` reaches. Returns a set of bus addresses."""
    seen, reached = set(), set()
    queue = [entry]
    while queue:
        pc = queue.pop()
        if pc in seen:
            continue
        seen.add(pc)
        insn = insn_at(an, pc)
        if insn is None:
            continue
        reached.update(range(pc, pc + insn.size))
        for nxt in successors(an, insn):
            # another port owns its own region, and its own walk will count it
            if nxt != entry and nxt in owners:
                continue
            queue.append(nxt)
    return reached


def main():
    ap = argparse.ArgumentParser(prog="python3 tools/burndown.py")
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--hints", type=pathlib.Path, default=ROOT / "re/hints.toml")
    ap.add_argument("--registry", type=pathlib.Path, default=ROOT / "game/registry.c")
    ap.add_argument("--by", choices=("bank", "subsystem"), default="bank")
    ap.add_argument("--uncovered", metavar="BANK",
                    help="list the largest uncovered runs in this bank, e.g. C1")
    ap.add_argument("--top", type=int, default=15)
    args = ap.parse_args()

    if not args.rom.exists():
        sys.exit("%s: not found (tools/rom.py stage <path>)" % args.rom)
    data = args.rom.read_bytes()
    if len(data) % 0x8000 == 0x200:
        data = data[0x200:]

    an = Analyzer(Rom(data), hints_mod.load(args.hints))
    an.run()

    entries = registry_addresses(args.registry)
    covered = set()
    for entry in entries:
        covered |= walk(an, entry, entries)

    # The analysis's own code, which is what the generator builds blocks from.
    all_code = set()
    for pc, variants in an.code.items():
        all_code.update(range(pc, pc + max(i.size for i in variants.values())))
    covered &= all_code

    rows = collections.defaultdict(lambda: [0, 0, 0])   # key -> [code, covered, entries]
    for addr in all_code:
        rows[addr >> 16][0] += 1
    for addr in covered:
        rows[addr >> 16][1] += 1
    for entry in entries:
        rows[entry >> 16][2] += 1

    if args.by == "subsystem":
        merged = collections.defaultdict(lambda: [0, 0, 0])
        for bank, (code, cov, n) in rows.items():
            key = SUBSYSTEM.get(bank, "bank $%02X" % bank)
            for i, v in enumerate((code, cov, n)):
                merged[key][i] += v
        rows = merged
        head = "Subsystem"
    else:
        rows = {("$%02X %s" % (b, SUBSYSTEM.get(b, ""))).strip(): v for b, v in rows.items()}
        head = "Bank"

    total_code = sum(v[0] for v in rows.values())
    total_cov = sum(v[1] for v in rows.values())
    width = max(len(k) for k in rows)
    print("%-*s  %9s  %9s  %6s  %7s" % (width, head, "code", "ported", "left", "entries"))
    for key, (code, cov, n) in sorted(rows.items(), key=lambda kv: -kv[1][0]):
        print("%-*s  %8.1fK  %8.1fK  %5.0f%%  %7d"
              % (width, key, code / 1024, cov / 1024, 100.0 * (code - cov) / code if code else 0, n))
    print("%-*s  %8.1fK  %8.1fK  %5.0f%%  %7d"
          % (width, "total", total_code / 1024, total_cov / 1024,
             100.0 * (total_code - total_cov) / total_code if total_code else 0, len(entries)))

    if args.uncovered:
        bank = int(args.uncovered.lstrip("$"), 16)
        gap = sorted(a for a in all_code - covered if a >> 16 == bank)
        runs, start, prev = [], None, None
        for a in gap:
            if start is None:
                start = a
            elif a != prev + 1:
                runs.append((start, prev - start + 1))
                start = a
            prev = a
        if start is not None:
            runs.append((start, prev - start + 1))
        print("\nLargest uncovered runs in $%02X:" % bank)
        for addr, size in sorted(runs, key=lambda r: -r[1])[:args.top]:
            print("  $%02X:%04X  %5d bytes" % (addr >> 16, addr & 0xFFFF, size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
