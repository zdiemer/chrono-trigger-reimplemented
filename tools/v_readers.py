"""Whether the overflow flag (V) is read after a point, for a claim liveness can't settle alone.

    python3 tools/v_readers.py [--rom roms/ct-usa.sfc] [--list] [--after BB:AAAA]... [BB:AAAA...]

The 65816 reads V in two ways: a BVS or BVC branches on it, and a PHP pushes
it with the rest of P, where it is read if the byte is taken back as data
(PLA, a stack-relative load) rather than restored with PLP (which puts V back
as it was, for whatever comes after to read or not). ADC, SBC, BIT, CLV, PLP,
REP/SEP #$40 and RTI set it.

A port that hands the original's code a call with V declared don't-care
(docs/PORTING.md, "Declarations liveness can't prove") needs V dead from the
callee's entry on. The liveness analysis (tools/disasm/liveness.py) proves
that where it can follow every transfer; its strict solve, which
tools/dontcare_check.py holds call-outs to, keeps everything live at a jump
table whose targets come from data. For the tables in BOUNDED this check
follows instead every target the table's index can reach, argued from the
dispatch alone (what the table can hold, not what the game's data picks), and
solves liveness strictly otherwise: V is dead into a routine when that solve
has it so. For each routine named it says whether V is dead into it (and for
each --after, whether V is dead after the routine returns, to any of its
callers), and with
--list every V reader in the code it reaches (following calls, jumps and the
tables, as bounded), which is the reason when it isn't.

tools/dontcare_check.py runs this where a call-out's dc-evidence note names
it. Exit status 0 when V is dead into every routine named.
"""

import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.disasm.liveness import V, Liveness  # noqa: E402

# Jump tables whose targets the analysis takes from data (a `derive` hint in
# re/hints.toml), bounded by their dispatch alone: dispatcher -> (table,
# entries). Each dispatch doubles a byte with an 8-bit ASL and moves A to a
# 16-bit X with its high byte clear, as the battle's code keeps it (a TDC
# before the byte is loaded), so the index is 0-$FE: 128 words, whatever the
# byte. The words past a table's own entries are the next tables' entries.
BOUNDED = {
    # the battle's animation scripts: a command's operand picks a direction
    # ($C1:75CC) or a place ($C1:762E); the action's showing ($C1:BFA4) runs them
    0xC175D1: (0xC179A1, 128),
    0xC17633: (0xC179D3, 128),
    # the enemies' scripts: a block's condition byte, read from the script in
    # ROM, which nothing checks (the turn that follows an enemy's action,
    # $C1:8461, goes on to the next enemy's conditions)
    0xC1874E: (0xC1B80D, 128),
    0xC18769: (0xC1B80D, 128),
    0xC1B006: (0xC1B80D, 128),
    0xC1B021: (0xC1B80D, 128),
}

V_BRANCHES = ("BVS", "BVC")


def bounded_targets(an) -> tuple:
    """(dispatcher -> targets, findings): every word each BOUNDED table's index
    can reach, as a routine in the table's bank. A word that isn't code the
    analysis traced is a finding: liveness would have everything live there."""
    out, findings = {}, []
    for dispatcher, (table, entries) in BOUNDED.items():
        bank = dispatcher & 0xFF0000
        targets = []
        for i in range(entries):
            word = an.u16(table + 2 * i)
            if word is None:
                findings.append(f"${dispatcher >> 16:02X}:{dispatcher & 0xFFFF:04X}: entry {i} isn't in the ROM")
                continue
            t = bank | word
            if not any(k[0] == t for k in an.contexts):
                findings.append(f"${dispatcher >> 16:02X}:{dispatcher & 0xFFFF:04X}: entry {i} "
                                f"(${t >> 16:02X}:{t & 0xFFFF:04X}) isn't code the analysis traced")
            targets.append(t)
        out[dispatcher] = list(dict.fromkeys(targets))
    return out, findings


def solve(an, dynamic_calls: dict) -> tuple:
    """(liveness solved strictly with the BOUNDED tables followed, findings)."""
    bounded, findings = bounded_targets(an)
    lv = Liveness(an, dynamic_calls, strict=True, bounded=bounded)
    lv.solve()
    return lv, findings


def v_dead_into(lv, target: int) -> bool:
    """Whether a call to target can't read V: the routine doesn't, and it
    leaves V alone for no caller to read (callee_reads leaves the caller out;
    the port calling it doesn't read the machine's flags)."""
    return not lv.callee_reads(target) & V


def v_dead_after(lv, routine: int) -> bool:
    """Whether V is dead after the routine returns, to any caller."""
    live = lv.live_after_return(routine)
    return live is not None and not live & V


def readers(lv, roots) -> list:
    """(pc, why) for every V reader in the code the roots reach."""
    an = lv.an
    out, seen, work = {}, set(), list(roots)
    while work:
        entry = work.pop()
        if entry in seen:
            continue
        seen.add(entry)
        for key in [k for k in an.contexts if k[0] == entry]:
            for pc in an.contexts[key].insns:
                for _, insn in lv._code(key, pc):
                    if insn.name in V_BRANCHES:
                        out.setdefault(pc, insn.name)
                    elif insn.name == "PHP":
                        pulls = lv.push_pairs.get((key, pc))
                        if pulls is None:
                            out.setdefault(pc, f"PHP not paired ({lv.push_cases.get((key, pc), '?')})")
                        elif any(p != "PLP" for _r, p, _w in pulls):
                            out.setdefault(pc, "PHP pulled other than by PLP (liveness follows the byte)")
                    work += [t for t in lv._callees(pc, insn) if t is not None]
                    if insn.name == "JML" and insn.operand is not None and insn.mode.name == "LONG":
                        work.append(insn.operand)
                    elif insn.name == "JMP" and insn.mode.name == "ABS":
                        work.append((pc & 0xFF0000) | insn.operand)
                    elif insn.mode.name in ("ABSIND", "ABSLIND"):
                        work += lv.indirect_targets(pc)
                    elif insn.mode.name == "ABSXIND":
                        jt = an.tables.get(pc)
                        if jt is not None and jt.count is not None:
                            work += lv._table_targets(jt)
    return sorted(out.items())


def parse_addr(text: str) -> int:
    bank, _, addr = text.replace("$", "").partition(":")
    return int(bank, 16) << 16 | int(addr, 16)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python3 tools/v_readers.py", description=__doc__.split("\n")[0])
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms" / "ct-usa.sfc")
    ap.add_argument("--hints", type=pathlib.Path, default=ROOT / "re" / "hints.toml")
    ap.add_argument("--list", action="store_true", help="list the V readers each routine reaches")
    ap.add_argument("--after", action="append", default=[], help="BB:AAAA: V dead after it returns")
    ap.add_argument("routines", nargs="*", help="BB:AAAA: V dead into it")
    args = ap.parse_args(argv)

    from tools.dontcare_check import DEFAULT_CALLS, DEFAULT_COVERAGE, expand
    from tools.disasm import coverage as coverage_mod
    from tools.disasm import hints as hints_mod
    from tools.disasm.analyze import Analyzer
    from tools.disasm.memmap import Rom

    if not args.rom.exists():
        print(f"{args.rom}: not found (tools/rom.py stage <path>)", file=sys.stderr)
        return 2
    coverage = expand(DEFAULT_COVERAGE, ROOT)
    calls = expand(DEFAULT_CALLS, ROOT)
    an = Analyzer(Rom(args.rom.read_bytes()), hints_mod.load(args.hints),
                  coverage=coverage_mod.load(coverage) if coverage else None)
    an.run()
    lv, findings = solve(an, coverage_mod.load_calls(calls) if calls else {})
    for f in findings:
        print(f)
    bad = 0
    for text in args.routines:
        target = parse_addr(text)
        dead = v_dead_into(lv, target)
        bad += not dead
        print(f"${target >> 16:02X}:{target & 0xFFFF:04X}: V {'dead' if dead else 'may be read'} "
              f"into the routine (the tables at {', '.join(f'${d >> 16:02X}:{d & 0xFFFF:04X}' for d in BOUNDED)} "
              "bounded by their dispatch)")
        if args.list or not dead:
            for pc, why in readers(lv, [target]):
                print(f"  ${pc >> 16:02X}:{pc & 0xFFFF:04X} {why}")
    for text in args.after:
        routine = parse_addr(text)
        dead = v_dead_after(lv, routine)
        bad += not dead
        print(f"${routine >> 16:02X}:{routine & 0xFFFF:04X}: V {'dead' if dead else 'may be read'} after it returns")
    return 1 if bad or findings else 0


if __name__ == "__main__":
    sys.exit(main())
