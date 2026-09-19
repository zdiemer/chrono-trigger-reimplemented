import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

from . import coverage as coverage_mod
from . import hints as hints_mod
from .analyze import Analyzer, describe_widths
from .liveness import ALL, Liveness, dc_mask, dc_text, describe as describe_live
from .memmap import Rom, bus_to_offset, fmt, offset_to_bus

REPO_ROOT = Path(__file__).resolve().parents[2]


def write_listing(an: Analyzer, out: Path):
    """One file per ROM bank ($C0-$FF), code in ROM order, with data gaps summarized."""
    by_offset = defaultdict(list)
    for pc, variants in an.code.items():
        if bus_to_offset(pc) is None:          # RAM sections aren't in the bank listings
            continue
        for widths, insn in variants.items():
            by_offset[bus_to_offset(pc)].append((pc, widths, insn))
    lst = out / "listing"
    lst.mkdir(parents=True, exist_ok=True)
    for old in lst.glob("bank_*.asm"):
        old.unlink()

    offsets = sorted(by_offset)
    files = defaultdict(list)
    prev_end = None
    for off in offsets:
        bank = off >> 16
        lines = files[bank]
        if prev_end is not None and prev_end >> 16 == bank and off > prev_end:
            lines.append(f"\n; ---- {off - prev_end} bytes not code: "
                         f"{fmt(offset_to_bus(prev_end))}-{fmt(offset_to_bus(off - 1))}\n")
        elif prev_end is not None and off < prev_end:
            lines.append(f"; !!! overlaps previous instruction")
        for pc, widths, insn in by_offset[off]:
            lbl = an.label(pc)
            if lbl:
                kinds = an.labels.get(pc, set())
                if "sub" in kinds:
                    lines.append("")
                    notes = sorted({describe_widths(c.widths) for k, c in an.contexts.items()
                                    if k[0] == pc})
                    lines.append(f"; {', '.join(notes)}")
                lines.append(f"{lbl}:")
            hexbytes = " ".join(f"{b:02X}" for b in insn.raw)
            state = "" if len(by_offset[off]) == 1 else f"  ; [{describe_widths(widths)}]"
            lines.append(f"  {fmt(pc)}  {hexbytes:<12} {insn.text(an.label)}{state}")
            prev_end = max(prev_end or 0, off + insn.size)
    for bank, lines in files.items():
        (lst / f"bank_{0xC0 + bank:02X}.asm").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_functions(an: Analyzer, out: Path):
    funcs = []
    for (entry, widths), c in sorted(an.contexts.items(), key=lambda kv: (kv[0][0], str(kv[0][1]))):
        funcs.append({
            "addr": fmt(entry),
            "name": an.label(entry),
            "entry_widths": describe_widths(widths),
            "returns": sorted(describe_widths(r) for r in c.returns),
            "kinds": sorted(c.kinds),
            "callers": sorted(fmt(a) for a in c.callers),
            "instructions": len(c.insns),
        })
    (out / "functions.json").write_text(json.dumps(funcs, indent=1), encoding="utf-8")


def write_report(an: Analyzer, out: Path) -> str:
    cov = an.covered_offsets()
    lines = ["# Disassembly report", ""]
    entries = {e for e, _ in an.contexts}
    lines += [
        f"- functions (distinct entries): {len(entries)}",
        f"- contexts (entry x widths): {len(an.contexts)}",
        f"- instructions: {sum(len(v) for v in an.code.values())}",
        f"- code bytes: {len(cov)} ({len(cov) / 0x400000:.1%} of ROM)",
        f"- jump tables: {len(an.tables)} "
        f"({sum(1 for t in an.tables.values() if t.source == 'heuristic')} heuristic)",
        f"- unresolved indirect jumps: {len(an.unresolved)}",
        f"- stops on unknown width: {len(an.width_unknown)}",
        f"- suspicious opcodes: {len(an.suspicious)}",
        f"- calls continued on assumed widths: {len(an.assumed)}",
        f"- width conflicts: {len(an.width_conflicts())} ({len(an.width_conflicts(declared=True))} more declared "
        "by overlap hints)",
    ]
    if an.coverage:
        executed = len(an.coverage)
        lines += [
            f"- executed addresses (coverage): {executed}, "
            f"{executed - len(an.coverage_seeded)} found statically, {len(an.coverage_seeded)} seeded",
            f"- static decodes contradicted by coverage: {len(an.coverage_conflicts())}",
            f"- call returns with widths the coverage never ran there: {len(an.coverage_unobserved)}"
            + (f", {len(an.coverage_pruned)} pruned" if an.prune_returns else ""),
        ]
    lines.append("")
    summary = "\n".join(lines)

    lines += ["## Code bytes per bank", "", "| bank | code bytes | % |", "|---|---|---|"]
    per_bank = defaultdict(int)
    for off in cov:
        per_bank[off >> 16] += 1
    for b in sorted(per_bank):
        lines.append(f"| ${0xC0 + b:02X} | {per_bank[b]} | {per_bank[b] / 0x10000:.1%} |")

    def section(title, items):
        lines.extend(["", f"## {title}", ""])
        lines.extend(items or ["(none)"])

    section("Unresolved indirect jumps", [f"- {fmt(pc)} `{i.text()}`" for pc, i in sorted(an.unresolved.items())])
    section("Jump tables", [
        f"- {fmt(t.dispatcher)} table {fmt(t.table)} x{t.count} {t.kind}"
        f"{' (call)' if t.call else ''} [{t.source}]" for t in sorted(an.tables.values(), key=lambda t: t.dispatcher)])
    section("Stopped on unknown M/X width", [f"- {fmt(pc)}" for pc in sorted(an.width_unknown)])
    def origin(key):
        c = an.contexts[key]
        return f"{an.label(c.entry)} ({','.join(sorted(c.kinds))})"

    section("Suspicious opcodes (likely data reached as code)",
            [f"- {fmt(pc)} `{i.text()}` in {origin(k)}" for pc, (i, k) in sorted(an.suspicious.items())])
    section("Calls continued with assumed widths (callee never returned)", sorted(
        f"- call at {fmt(site)} -> {fmt(key[0])}" for key, site in an.assumed))
    section("Width conflicts (same address, different instruction length)",
            [f"- {fmt(pc)}: " + ", ".join(f"[{describe_widths(w)}] {i.text()}" for w, i in v.items())
             for pc, v in sorted(an.width_conflicts().items())])
    section("Control flow into non-ROM addresses", [f"- {fmt(pc)}" for pc in sorted(an.unmapped)])
    if an.coverage:
        section("Coverage seeds (executed, not reached statically)", [
            f"- {fmt(a)}" for a in sorted(an.coverage_seeded)])
        section("Call returns the coverage never ran with these widths"
                + (" (pruned)" if an.prune_returns else " (traced; hints decide)"), [
            f"- return to {fmt(a)} with {describe_widths(w)}" for a, w in sorted(an.coverage_unobserved)])
        section("Static decodes contradicted by coverage", [
            f"- {fmt(a)}: ran as " + ", ".join(describe_widths(w) for w in sorted(obs, key=str)) + "; decoded as "
            + ", ".join(f"[{describe_widths(w)}] {i.text()}" for w, i in wrong.items())
            for a, (obs, wrong) in sorted(an.coverage_conflicts().items())])
    (out / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return summary


def write_liveness(an: Analyzer, lv: Liveness, out: Path) -> str:
    entries = sorted({k[0] for k in an.contexts})
    rows = {}
    for e in entries:
        rows[fmt(e)] = {
            "name": an.label(e),
            "live_in": describe_live(lv.func_live_in[e]),
            "live_after_return": describe_live(lv.after_return(e)),
            "dead_after_return": describe_live(lv.dead_at_return(e)),
            "dont_care": dc_text(dc_mask(lv.dead_at_return(e))),
            "all_live_reason": lv.reasons.get(e),
        }
    (out / "liveness.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")
    relaxed = sum(1 for e in entries if lv.after_return(e) != ALL)
    return f"- liveness: {relaxed} of {len(entries)} functions have registers or flags dead after return"


def main():
    ap = argparse.ArgumentParser(prog="python3 -m tools.disasm")
    ap.add_argument("--rom", type=Path, default=REPO_ROOT / "roms" / "ct-usa.sfc")
    ap.add_argument("--hints", type=Path, default=REPO_ROOT / "re" / "hints.toml")
    ap.add_argument("--out", type=Path, default=REPO_ROOT / "disasm" / "out")
    ap.add_argument("--coverage", type=Path, action="append", default=[],
                    help="executed-instruction coverage from ct_dev --coverage (repeatable)")
    ap.add_argument("--calls", type=Path, action="append", default=[],
                    help="executed call edges from ct_dev --calls (repeatable); needed for sound liveness")
    ap.add_argument("--no-assume-returns", action="store_true",
                    help="don't continue after calls whose callee was never seen to return")
    ap.add_argument("--prune-returns", action="store_true",
                    help="drop call returns whose widths the coverage never ran at the return address "
                         "(exploration aid; the committed analysis traces them)")
    args = ap.parse_args()

    if not args.rom.exists():
        sys.exit(f"{args.rom} not found; stage the ROM with tools/rom.py first")
    rom = Rom(args.rom.read_bytes())
    an = Analyzer(rom, hints_mod.load(args.hints), assume_returns=not args.no_assume_returns,
                  coverage=coverage_mod.load(args.coverage), prune_returns=args.prune_returns)
    an.run()

    args.out.mkdir(parents=True, exist_ok=True)
    write_listing(an, args.out)
    write_functions(an, args.out)
    print(write_report(an, args.out))
    lv = Liveness(an, coverage_mod.load_calls(args.calls))
    lv.solve()
    print(write_liveness(an, lv, args.out))
    if not args.calls:
        print("  (no --calls given: functions called from code static analysis can't see may be misjudged)")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
