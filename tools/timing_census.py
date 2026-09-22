"""Every instruction in the ROM that touches the timing and beam registers.

    python3 tools/timing_census.py [--rom roms/ct-usa.sfc] [--coverage traces/coverage-*.txt ...]

The registers are the ones a native timing backend has to answer for
(docs/PLATFORM.md, "Timing without a beam"): `$4200` (which interrupts
are on, and the automatic controller read), `$4207-$420A` (the line
interrupt's dot and line), `$4210`/`$4211` (the vblank and line interrupt
flags), `$4212` (blanking and controller-read status), `$2137` (latch the
beam position), `$213C`/`$213D` (the latched position) and `$213F` (the
counters' read toggle, and the frame's field bit).

For each instruction: the register, read or write, how the operand reaches
it, the routine it is in (the context whose entry is nearest before it in the
same bank), whether a hand port does it (`port`: a source file under `game/`
names the instruction's address, as a service call's site or an idle entry's;
`credited`: only tools/burndown.py's walk reaches it, which over-counts where a
port stops early, so these are mostly still generated), whether a movie ran it
(with `--coverage`), and whether it is a poll: a read followed within three
instructions by a conditional branch back to it or before it.

Then every wait in the ROM, whatever it polls (a conditional branch back at
most 16 bytes over a body of nothing but reads, tests and branches, so that
only something outside the loop can end it): what it reads, whether that is a
timing register, an APU port or work RAM, and whether `game/registry.c` has an
idle entry for it. These are the game's waits -- for the beam, for the other
processor, for a flag an interrupt handler clears -- and each is a place a
native build yields to the frame or drops the wait. Loops that call a
subroutine on each pass (the attract's wait at `$7E:3023`) are not in it.

**What it can and cannot see.**

- *Long* operands (`$004212`) are certain.
- *Absolute* operands (`$4212`) reach the register only while the data bank
  is one the registers are mapped in (`$00-$3F`, `$80-$BF`). The analysis
  doesn't track the data bank, so these are listed as the register; with the
  bank at `$7E` the same instruction reads work RAM. `ct_dev --io-log` is
  what settles it for code a movie runs.
- *Direct page* operands reach the registers when D is `$2100` or `$4200`.
  The analysis doesn't track D either; a routine that loads one of those
  value right before a `TCD` (or pushes it right before a `PLD`) puts D on
  that page from there, followed along the routine's control flow, and the
  direct-page instructions it reaches are mapped through it. A routine that
  inherits such a D from its caller, or a D computed rather than loaded, is
  not seen.
- *16-bit* accesses reach the register after the operand as well (`high
  byte`), in each width the analysis decoded the instruction with.
- *Indexed* operands are not followed. The only ones with a base in the
  register block (`$2100,Y`, `$2120,X`, `$213A,X` and the like) are tables
  in work RAM at `$7E:2100-$213F`, reached with the data bank at `$7E`.
- Code the analysis doesn't reach, and code the game builds at run time that
  no `[[ramcode]]` hint describes, is not seen at all.

Prints addresses, mnemonics and names, never bytes, so its output may be
quoted in docs; the listing it summarizes is ROM-derived and stays out of the
repository.
"""

import argparse
import collections
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from tools import burndown
from tools.disasm import coverage as coverage_mod
from tools.disasm import hints as hints_mod
from tools.disasm.analyze import Analyzer
from tools.disasm.cpu import RETURNS, Mode
from tools.disasm.memmap import Rom, fmt

ROOT = pathlib.Path(__file__).resolve().parent.parent

REGISTERS = {
    0x4200: "NMITIMEN", 0x4207: "HTIMEL", 0x4208: "HTIMEH", 0x4209: "VTIMEL", 0x420A: "VTIMEH",
    0x4210: "RDNMI", 0x4211: "TIMEUP", 0x4212: "HVBJOY",
    0x2137: "SLHV", 0x213C: "OPHCT", 0x213D: "OPVCT", 0x213F: "STAT78",
}
PAGES = (0x2100, 0x4200)          # direct pages that put the registers in reach
WRITES = {"STA", "STZ", "STX", "STY"}
RMW = {"TSB", "TRB", "INC", "DEC", "ASL", "LSR", "ROL", "ROR"}
BRANCHES = {"BPL", "BMI", "BEQ", "BNE", "BCC", "BCS", "BVC", "BVS"}   # conditional
DP_MODES = {Mode.DP, Mode.DPX, Mode.DPY}


def register_bank(bank: int) -> bool:
    return bank <= 0x3F or 0x80 <= bank <= 0xBF


def direct_pages(an) -> dict:
    """Instruction address -> the register page D is on there, by a forward
    pass over each context from its entry: `LDA #page : TCD` and
    `PEA page : PLD` set D to the page, any other `TCD` or `PLD` makes it
    unknown, and a call is taken to leave it as it was."""
    out = {}
    for key, c in an.contexts.items():
        seen = set()
        work = [(key[0], None, None)]           # (pc, D if a register page, previous insn)
        while work:
            pc, page, prev = work.pop()
            if (pc, page, prev and prev.pc) in seen or pc not in c.insns:
                continue
            seen.add((pc, page, prev and prev.pc))
            i = next(iter(an.code[pc].values()))
            if page is not None:
                out.setdefault(pc, set()).add(page)
            if i.name == "TCD":
                page = prev.operand if prev and prev.name == "LDA" and prev.mode is Mode.IMM_M \
                    and prev.operand in PAGES else None
            elif i.name == "PLD":
                page = prev.operand if prev and prev.name == "PEA" and prev.operand in PAGES else None
            if i.name in RETURNS or i.name in ("JML", "STP") or i.mode in (Mode.ABSIND, Mode.ABSXIND,
                                                                          Mode.ABSLIND):
                continue
            if i.name in BRANCHES or i.name in ("BRA", "BRL"):
                work.append((i.branch_target(), page, i))
                if i.name in ("BRA", "BRL"):
                    continue
            if i.name == "JMP" and i.mode is Mode.ABS:
                work.append(((pc & 0xFF0000) | i.operand, page, i))
                continue
            work.append((i.next_pc, page, i))
    return {pc: min(p) for pc, p in out.items() if len(p) == 1}


def named_sites(tree: pathlib.Path) -> set:
    """Every 24-bit address a source file under game/ names (`0xC0ECDB`, a
    service call's site or an idle entry's address)."""
    out = set()
    for f in (tree / "game").rglob("*.c"):
        for m in re.finditer(r"0x([0-9A-Fa-f]{6})(?![0-9A-Fa-f])", f.read_text(encoding="utf-8")):
            out.add(int(m.group(1), 16))
    return out


def owner(an, pc: int, by_pc: dict):
    keys = by_pc.get(pc, [])
    if not keys:
        return None
    same = [k for k in keys if k[0] >> 16 == pc >> 16 and k[0] <= pc]
    return max(same or keys, key=lambda k: k[0])


def is_poll(an, insn) -> bool:
    pc = insn.next_pc
    for _ in range(3):
        v = an.code.get(pc)
        if not v:
            return False
        i = next(iter(v.values()))
        if i.name in BRANCHES and i.branch_target() <= insn.pc and i.branch_target() >> 16 == insn.pc >> 16:
            return True
        if i.name in ("JMP", "BRA", "BRL", "RTS", "RTL", "RTI", "JSR", "JSL"):
            return False
        pc = i.next_pc
    return False


READS = {"LDA", "LDX", "LDY", "BIT", "CMP", "CPX", "CPY", "AND", "ORA", "EOR"}
MEMORY_MODES = {Mode.DP, Mode.ABS, Mode.LONG, Mode.DPX, Mode.ABSX, Mode.ABSY, Mode.LONGX}
LOOP_BYTES = 16


def idle_entries(tree: pathlib.Path) -> set:
    """The addresses game/registry.c registers as idle entries (one pass of a
    wait loop)."""
    text = (tree / "game" / "registry.c").read_text(encoding="utf-8")
    out = set()
    for m in re.finditer(r"\{\s*\.addr\s*=\s*0x([0-9A-Fa-f]{6})(.*?)\}", text, re.S):
        if re.search(r"\.idle\s*=\s*true", m.group(2)):
            out.add(int(m.group(1), 16))
    return out


def wait_loops(an):
    """Waits: a conditional branch back at most LOOP_BYTES over a body of
    nothing but reads, tests and branches, so only something outside the loop
    (the hardware, another processor, an interrupt handler) can end it.
    (branch, loop head, the reads)."""
    out = []
    for pc in sorted(an.code):
        i = next(iter(an.code[pc].values()))
        if i.name not in BRANCHES:
            continue
        head = i.branch_target()
        if head > pc or pc - head > LOOP_BYTES or head >> 16 != pc >> 16:
            continue
        body, p, ok = [], head, True
        while p < pc and ok:
            v = an.code.get(p)
            if not v:
                ok = False
                break
            b = next(iter(v.values()))
            if b.name not in READS and b.name not in BRANCHES and b.name not in ("SEP", "REP", "NOP"):
                ok = False
            body.append(b)
            p = b.next_pc
        reads = [b for b in body if b.name in READS and b.mode in MEMORY_MODES]
        if ok and reads:
            out.append((pc, head, reads))
    return out


def loop_target(pages: dict, insn) -> str:
    """What a polling loop's read reaches: a timing register, an APU port,
    another I/O register, or work RAM (by its operand as written)."""
    lo = insn.operand & 0xFFFF
    bank = insn.operand >> 16 if insn.mode in (Mode.LONG, Mode.LONGX) else None
    if insn.mode in DP_MODES:
        page = pages.get(insn.pc)
        if page is None:
            return "ram (direct page)"
        lo = page + insn.operand
    elif bank is not None and not register_bank(bank):
        return "ram"
    if lo in REGISTERS:
        return "timing " + REGISTERS[lo]
    if 0x2140 <= lo <= 0x217F:
        return "apu port %d" % (lo & 3)
    if 0x2100 <= lo <= 0x21FF or 0x4000 <= lo <= 0x43FF:
        return "io $%04X" % lo
    return "ram"


X_OPS = {"LDX", "LDY", "STX", "STY", "CPX", "CPY"}


def census(an, pages: dict):
    """(pc, insn, register, how, access) for every access to a timing
    register, with each width the analysis decoded the instruction at: a
    16-bit access reaches the register after its operand too."""
    rows = []
    for pc in sorted(an.code):
        seen = set()
        for (m, x), insn in an.code[pc].items():
            wide = (x if insn.name in X_OPS else m) is False
            if insn.mode is Mode.LONG and register_bank(insn.operand >> 16):
                lo, how = insn.operand & 0xFFFF, "long"
            elif insn.mode is Mode.ABS and insn.name not in ("JMP", "JSR", "PEA"):
                lo, how = insn.operand, "abs"
            elif insn.mode is Mode.DP and pages.get(pc) is not None:
                lo, how = pages[pc] + insn.operand, "dp"
            else:
                continue
            hits = [(lo, how)] + ([(lo + 1, how + ", high byte")] if wide else [])
            for reg, via in hits:
                if reg in REGISTERS and (reg, via) not in seen:
                    seen.add((reg, via))
                    access = "write" if insn.name in WRITES else "rmw" if insn.name in RMW else "read"
                    rows.append((pc, insn, reg, via, access))
    return rows


def main():
    ap = argparse.ArgumentParser(prog="python3 tools/timing_census.py")
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--hints", type=pathlib.Path, default=ROOT / "re/hints.toml")
    ap.add_argument("--tree", type=pathlib.Path, default=ROOT)
    ap.add_argument("--coverage", type=pathlib.Path, action="append", default=[],
                    help="executed-instruction coverage (ct_dev --coverage): marks what a movie ran")
    args = ap.parse_args()

    if not args.rom.exists():
        sys.exit("%s: not found (tools/rom.py stage <path>)" % args.rom)
    data = args.rom.read_bytes()
    cov = coverage_mod.load(args.coverage)
    # the burndown's analysis (no coverage), so "credited" means what it means
    # there; the coverage only marks what ran
    an = Analyzer(Rom(data), hints_mod.load(args.hints))
    an.run()
    covered, _, _ = burndown.coverage(an, burndown.read_ports(args.tree, an))
    named = named_sites(args.tree)

    by_pc = collections.defaultdict(list)
    for key, c in an.contexts.items():
        for pc in c.insns:
            by_pc[pc].append(key)
    pages = direct_pages(an)
    rows = census(an, pages)

    print("| Site | Instruction | Register | Access | Via | Routine | Poll | Ported | Ran |")
    print("|---|---|---|---|---|---|---|---|---|")
    counts = collections.Counter()
    for pc, insn, reg, how, access in rows:
        key = owner(an, pc, by_pc)
        routine = fmt(key[0]) if key else "?"
        name = REGISTERS[reg]
        ported = "port" if pc in named else "credited" if pc in covered else "no"
        ran = "yes" if pc in cov else ("no" if cov else "")
        poll = "poll" if access == "read" and is_poll(an, insn) else ""
        print("| %s | `%s` | $%04X %s | %s | %s | %s | %s | %s | %s |" % (
            fmt(pc), insn.text(an.label), reg, name, access, how, routine, poll,
            ported, ran))
        counts[(name, ported == "port")] += 1
    print()
    print("| Register | In a port | Generated or credited only |")
    print("|---|---|---|")
    for name in sorted({n for n, _ in counts}):
        print("| %s | %d | %d |" % (name, counts[(name, True)], counts[(name, False)]))
    print()
    idle = idle_entries(args.tree)
    print("Waits (a branch back at most %d bytes over nothing but reads, tests and branches):" % LOOP_BYTES)
    print()
    print("| Loop head | Branch | Reads | Target | Idle entry | Ported | Ran |")
    print("|---|---|---|---|---|---|---|")
    kinds = collections.Counter()
    for pc, head, reads in wait_loops(an):
        targets = sorted({loop_target(pages, r) for r in reads})
        target = "; ".join(targets)
        is_idle = any(a in idle for a in range(head, pc + 2))
        ported = "port" if is_idle or head in named else "credited" if head in covered else "no"
        ran = "yes" if head in cov else ("no" if cov else "")
        kind = next((k for k in ("timing", "apu", "io") if any(t.startswith(k) for t in targets)), "ram")
        kinds[(kind, ported == "port")] += 1
        print("| %s | %s | %s | %s | %s | %s | %s |" % (
            fmt(head), fmt(pc), "; ".join("`%s`" % r.text(an.label) for r in reads), target,
            "yes" if is_idle else "", ported, ran))
    print()
    print("| Loops polling | In a port | Not |")
    print("|---|---|---|")
    for k in ("timing", "apu", "io", "ram"):
        print("| %s | %d | %d |" % (k, kinds[(k, True)], kinds[(k, False)]))
    print()
    on_page = sorted({owner(an, pc, by_pc) for pc in pages} - {None})
    print("Routines with D on a register page somewhere: " + ", ".join(fmt(k[0]) for k in on_page))
    return 0


if __name__ == "__main__":
    sys.exit(main())
