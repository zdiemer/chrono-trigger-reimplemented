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
table whose targets come from data. For the tables with an `index_reach`
hint (re/hints.toml) this check follows instead every target the table's
index can reach, argued from the dispatch alone (what the table can hold, not
what the game's data picks), and solves liveness strictly otherwise. The
dispatch must be ASL A (8-bit), TAX, JSR or JMP (abs,X), with nothing jumping
past the ASL (dispatch_findings), and A's high byte known clear at the ASL
(high_byte_findings, tools/disasm/high_byte.py: every path to it clears it,
a TDC on the battle's direct page $0000 or a 16-bit load of a small number,
and nothing after sets it), which `ct_dev --index-check` also shows over the
library (tools/dc_evidence.py runs it): V is dead into a routine when that solve
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

V_BRANCHES = ("BVS", "BVC")

# The dispatch an `index_reach` hint (re/hints.toml) must sit in: ASL A with an
# 8-bit A, then TAX, then JSR or JMP (abs,X), with nothing jumping into the TAX
# or the dispatch, so X is the byte doubled over A's high byte. That byte is
# argued clear in the hint and checked over the library by ct_dev
# --index-check (X at most $FE at the dispatch): with it, 128 words.
DISPATCH_OPCODES = {0xFC: "JSR", 0x7C: "JMP"}
ASL_A, TAX = 0x0A, 0xAA
REACH_OF_A_DOUBLED_BYTE = 128


def _transfers(an):
    """Every address some instruction the analysis traced branches, jumps or
    calls to (fallthrough aside), with a table's entries."""
    from tools.disasm.cpu import BRANCHES, Mode
    out = set()
    for pc, variants in an.code.items():
        for insn in variants.values():
            if insn.name in BRANCHES:
                out.add(insn.branch_target())
            elif insn.name in ("JMP", "JSR") and insn.mode is Mode.ABS:
                out.add((pc & 0xFF0000) | insn.operand)
            elif insn.name in ("JML", "JSL") and insn.mode is Mode.LONG:
                out.add(insn.operand)
    for jt in an.tables.values():
        if jt.count is None:
            continue
        for i in range(jt.count):
            v = an.u16(jt.table + i * 2) if jt.kind != "long" else an.u24(jt.table + i * 3)
            if v is not None:
                out.add(v if jt.kind == "long" else (jt.dispatcher & 0xFF0000) | v)
    return out


def dispatch_findings(an) -> list:
    """What doesn't hold of the dispatch each `index_reach` hint names."""
    findings, transfers = [], None
    for pc, reach in sorted(an.hints.index_reach.items()):
        where = f"${pc >> 16:02X}:{pc & 0xFFFF:04X}"
        raw = an.read(pc - 2, 3)
        if raw is None:
            findings.append(f"{where}: not in the ROM")
            continue
        asl, tax, op = raw
        if op not in DISPATCH_OPCODES:
            findings.append(f"{where}: not a JSR or JMP (abs,X)")
            continue
        if tax != TAX or asl != ASL_A:
            findings.append(f"{where}: not ASL A, TAX before the dispatch")
            continue
        widths = [w for w in an.code.get(pc - 2, {})]
        if not widths or any(w[0] is not True for w in widths):
            findings.append(f"{where}: the ASL A isn't traced with an 8-bit A everywhere ({widths})")
        if reach != REACH_OF_A_DOUBLED_BYTE:
            findings.append(f"{where}: index_reach {reach}, where a doubled byte reaches {REACH_OF_A_DOUBLED_BYTE}")
        if transfers is None:
            transfers = _transfers(an)
        for inside in (pc - 1, pc):
            if inside in transfers:
                findings.append(f"{where}: ${inside >> 16:02X}:{inside & 0xFFFF:04X} is jumped to, past the ASL")
    return findings


def bounded_targets(an) -> tuple:
    """(dispatcher -> targets, findings): every word each `index_reach` table's
    index can reach, as a routine in the table's bank. A word that isn't code
    the analysis traced is a finding (liveness would have everything live
    there), as is a dispatch that isn't the shape the bound is argued from."""
    out, findings = {}, dispatch_findings(an)
    for dispatcher, entries in sorted(an.hints.index_reach.items()):
        jt = an.tables.get(dispatcher)
        if jt is None:
            findings.append(f"${dispatcher >> 16:02X}:{dispatcher & 0xFFFF:04X}: no table the analysis sized")
            continue
        bank = dispatcher & 0xFF0000
        targets = []
        for i in range(entries):
            word = an.u16(jt.table + 2 * i)
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


def high_byte_findings(an, lv) -> list:
    """Each `index_reach` dispatch whose A high byte the analysis can't show
    clear at the ASL (tools/disasm/high_byte.py), with why: the byte TAX
    carries into X, so the bound holds only where it is. A TDC is taken to
    clear it (D's high byte is zero there: the battle's direct page is $0000,
    which its ports' guards check), and `ct_dev --index-check` backs that over
    the library."""
    from tools.disasm.high_byte import HighByte, Z
    hb = HighByte(an, lv, d_high_zero=True)
    hb.solve()
    out = []
    for pc in sorted(an.hints.index_reach):
        v = hb.at(pc - 2)
        if v is None or v[0] != Z:
            why = hb.why_not(pc - 2) if v is not None else ["nothing the analysis traced reaches it"]
            out.append(f"${pc >> 16:02X}:{pc & 0xFFFF:04X}: A's high byte isn't known clear at the ASL: "
                       + "; ".join(why))
    return out


def solve(an, dynamic_calls: dict) -> tuple:
    """(liveness solved strictly with the `index_reach` tables followed, findings):
    the findings say where a bound isn't what its hint argues (the dispatch's
    shape, A's high byte, a word that isn't traced code)."""
    bounded, findings = bounded_targets(an)
    lv = Liveness(an, dynamic_calls, strict=True, bounded=bounded)
    lv.solve()
    return lv, findings + high_byte_findings(an, lv)


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


def analysis(rom_path: pathlib.Path, hints_path: pathlib.Path) -> tuple:
    """(the analysis of the ROM, the call logs), with the coverage and call logs
    tools/dontcare_check.py reads (traces/, where there are any)."""
    from tools.dontcare_check import DEFAULT_CALLS, DEFAULT_COVERAGE, expand
    from tools.disasm import coverage as coverage_mod
    from tools.disasm import hints as hints_mod
    from tools.disasm.analyze import Analyzer
    from tools.disasm.memmap import Rom

    coverage = expand(DEFAULT_COVERAGE, ROOT)
    calls = expand(DEFAULT_CALLS, ROOT)
    an = Analyzer(Rom(rom_path.read_bytes()), hints_mod.load(hints_path),
                  coverage=coverage_mod.load(coverage) if coverage else None)
    an.run()
    return an, coverage_mod.load_calls(calls) if calls else {}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python3 tools/v_readers.py", description=__doc__.split("\n")[0])
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms" / "ct-usa.sfc")
    ap.add_argument("--hints", type=pathlib.Path, default=ROOT / "re" / "hints.toml")
    ap.add_argument("--list", action="store_true", help="list the V readers each routine reaches")
    ap.add_argument("--after", action="append", default=[], help="BB:AAAA: V dead after it returns")
    ap.add_argument("routines", nargs="*", help="BB:AAAA: V dead into it")
    args = ap.parse_args(argv)

    if not args.rom.exists():
        print(f"{args.rom}: not found (tools/rom.py stage <path>)", file=sys.stderr)
        return 2
    an, dynamic_calls = analysis(args.rom, args.hints)
    lv, findings = solve(an, dynamic_calls)
    for f in findings:
        print(f)
    bad = 0
    for text in args.routines:
        target = parse_addr(text)
        dead = v_dead_into(lv, target)
        bad += not dead
        tables = ", ".join(f"${d >> 16:02X}:{d & 0xFFFF:04X}" for d in sorted(an.hints.index_reach))
        print(f"${target >> 16:02X}:{target & 0xFFFF:04X}: V {'dead' if dead else 'may be read'} "
              f"into the routine (the tables at {tables} bounded by their dispatch)")
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
