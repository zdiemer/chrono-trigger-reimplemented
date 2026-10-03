"""Closure checks C1-C7 of the matching disassembly (docs/MATCHING.md, MD4).

`closure(an)` takes an Analyzer that has run and returns one Check per closure
condition, with the counts the matching build reports and the problems that
make it fail:

- C1, indirect transfers: every `JMP (abs,X)`, `JSR (abs,X)`, `JMP (abs)`,
  `JML [abs]` and every call whose operand code writes has an argument: a
  derivation (`derive`), or a `bound` of "guard", "layout" or "constant" on its
  hint. `PER` pushes are followed into the dispatch they precede; stack switches
  (`TCS`, `TXS`) must be the scheduler's listed below; every store to the RAM
  vector stubs ($00:0500-0507) must lie in the code of a `writes` section or be a
  listed partial installer whose handler is an entry.
- C2, widths: no length conflict an `overlap` hint doesn't declare, no call
  continued on assumed return widths, no return pruned by coverage, no
  instruction decoded with an unknown width.
- C3, data-driven dispatch: the derivers report no problem and the derivation
  rounds reached a fixpoint.
- C5, disjointness: no traced transfer lands in a `[[data]]` region and no
  instruction byte lies in one.
- C6, reachability: every instruction belongs to a context reachable from the
  roots (vectors, hints other than `unreached`, patches) through call and jump
  edges, or to one only an `unreached` entry starts.
- C7, coverage: executed addresses static tracing didn't reach (seeds) are
  none. The miss report over the movies is checked outside the build.

C4 (RAM code) is the sections the matching build assembles and checks against
executed RAM addresses; it is reported there.
"""

from collections import Counter
from dataclasses import dataclass, field

from .codeargs import flow, register_values
from .cpu import Mode
from .memmap import bus_to_offset, fmt

# TCS/TXS instructions and why the stack they install holds only known returns.
STACK_SWITCHES = {
    0xC281FA: "the menu thread scheduler (C2:81CA) switching to a thread's saved stack",
    0xC28208: "the scheduler starting a thread on a fresh stack before JMP ($0000) at C2:8203",
}
# Stores to the vector stubs outside `writes` sections: X is the handler, an entry.
VECTOR_PARTIAL = {
    0xC308E2: "C3:08DF installs the IRQ handler C3:08C9",
    0xC30941: "C3:093E installs the IRQ handler C3:08B3",
    0xCD0420: "CD:041D installs the NMI handler D1:F4C0",
    0xCD041A: "CD:041A writes the stub's bank byte for CD:0420",
    0xC02E4D: "C0:2E4D writes RTI ($40) into the IRQ stub, disabling it",
    0xC02E52: "C0:2E52 writes RTI into the NMI stub",
}
ROOT_KINDS = {"vector", "hint", "coverage", "patch"}


@dataclass
class Check:
    name: str
    ok: bool
    counts: dict = field(default_factory=dict)
    problems: list = field(default_factory=list)
    info: list = field(default_factory=list)


def indirect_sites(an) -> dict:
    """pc -> instruction for every indirect transfer in the code set."""
    out = {}
    for pc, variants in an.code.items():
        insn = next(iter(variants.values()))
        if insn.mode in (Mode.ABSXIND, Mode.ABSIND, Mode.ABSLIND):
            out[pc] = insn
        elif insn.name in ("JSR", "JSL") and (pc in an.hints.indirect or pc in an.hints.derive):
            out[pc] = insn
    return out


def check_c1(an) -> Check:
    c = Check("C1 indirect transfers", True)
    sites = indirect_sites(an)
    kinds = Counter()
    for pc, insn in sorted(sites.items()):
        if pc in an.hints.derive:
            kind = "derived"
        elif pc in an.hints.bounds:
            kind = an.hints.bounds[pc]
        else:
            kind = None
            c.problems.append(f"{fmt(pc)} {insn.text()}: no argument for its targets")
        kinds[kind or "none"] += 1
        if pc in an.hints.nofollow:
            entries = {e for e, _ in an.contexts}
            for t in an.hints.indirect.get(pc, []):
                if t not in entries:
                    c.problems.append(f"{fmt(pc)}: target {fmt(t)} isn't an entry")
    c.counts = {"sites": len(sites), "with argument": len(sites) - kinds["none"], "without": kinds["none"],
                **{f"argued by {k}": v for k, v in sorted(kinds.items()) if k != "none"}}
    fl = flow(an)
    switches = sorted(pc for pc, v in an.code.items() if next(iter(v.values())).name in ("TCS", "TXS"))
    # S saved with TSX then STX to a cell
    saved = {}
    for pc, v in an.code.items():
        insn = next(iter(v.values()))
        nxt = an.code.get(pc + insn.size)
        if insn.name == "TSX" and nxt:
            i2 = next(iter(nxt.values()))
            if i2.name == "STX" and i2.mode in (Mode.ABS, Mode.DP):
                saved.setdefault(i2.operand, []).append(pc)
    stack_kinds = Counter()
    for pc in switches:
        insn = next(iter(an.code[pc].values()))
        reg = "A" if insn.name == "TCS" else "X"
        prev = fl.pred.get(pc)
        pinsn = next(iter(an.code[prev].values())) if prev is not None else None
        if pc in STACK_SWITCHES:
            stack_kinds["scheduler"] += 1
        elif register_values(an, pc, reg, fl):
            stack_kinds["reset to a constant"] += 1      # nothing below the new S is returned to
        elif (pinsn is not None and pinsn.name == "LD" + reg and pinsn.mode in (Mode.ABS, Mode.DP)
              and pinsn.operand in saved):
            stack_kinds["restore of a saved S"] += 1     # the S a TSX saved there
        else:
            c.problems.append(f"{fmt(pc)}: a stack switch without an argument")
    pers = sorted(pc for pc, v in an.code.items() if next(iter(v.values())).name == "PER")
    c.counts["stack switches"] = len(switches)
    c.counts.update({f"stack switches: {k}": v for k, v in sorted(stack_kinds.items())})
    c.counts["PER (followed as calls)"] = len(pers)
    writes = [(h.source, h.end) for h in an.hints.ramcode if h.format == "writes" and h.end is not None]
    vec = 0
    for pc, variants in sorted(an.code.items()):
        insn = next(iter(variants.values()))
        if insn.name not in ("STA", "STX", "STY", "STZ") or insn.mode not in (Mode.ABS, Mode.LONG, Mode.ABSX):
            continue
        a = insn.operand & 0xFFFF if insn.mode is not Mode.LONG or insn.operand >> 16 in (0x00, 0x7E) else None
        if a is None or not 0x0500 <= a <= 0x0507:
            continue
        vec += 1
        if any(lo <= pc <= hi for lo, hi in writes):
            continue
        if pc in VECTOR_PARTIAL:
            if insn.name == "STX":
                xs = register_values(an, pc, "X", fl) or set()
                for x in xs:
                    if not any(e & 0xFFFF == x for e, _ in an.contexts):
                        c.problems.append(f"{fmt(pc)}: installs {x:04X}, which isn't an entry")
            continue
        c.problems.append(f"{fmt(pc)} {insn.text()}: a store to the RAM vector stubs outside the known installers")
    c.counts["stores to the vector stubs"] = vec
    c.ok = not c.problems
    return c


def check_c2(an) -> Check:
    c = Check("C2 widths", True)
    conflicts = an.width_conflicts()
    unknown = sum(1 for v in an.code.values() for w, i in v.items()
                  if (i.mode is Mode.IMM_M and w[0] is None) or (i.mode is Mode.IMM_X and w[1] is None))
    multi = 0
    for ctx in an.contexts.values():
        seen = {}
        for pc, st in ctx.visited:
            seen.setdefault(pc, set()).add(st.widths)
        multi += sum(1 for ws in seen.values() if len(ws) > 1)
    c.counts = {"undeclared length conflicts": len(conflicts),
                "declared (overlap hints)": len(an.width_conflicts(declared=True)),
                "calls continued on assumed widths": len(an.assumed),
                "returns pruned by coverage": len(an.coverage_pruned),
                "instructions decoded with an unknown width": unknown,
                "instructions reached with several widths in one context": multi}
    c.problems += [f"{fmt(pc)}: decoded with lengths " + ", ".join(sorted({str(i.size) for i in v.values()}))
                   for pc, v in sorted(conflicts.items())]
    c.problems += [f"call at {fmt(site)} continued on assumed widths" for _, site in sorted(an.assumed)]
    c.problems += [f"{fmt(a)}: return pruned by coverage" for a, _ in sorted(an.coverage_pruned)]
    if unknown:
        c.problems.append(f"{unknown} instructions decoded with an unknown width")
    c.info.append("several widths per context are width variants of loops and joins, each decoded to one "
                  "length (no length conflict)")
    c.ok = not c.problems
    return c


def check_c3(an) -> Check:
    c = Check("C3 data-driven dispatch", True)
    for name, d in sorted(an.derivations.items()):
        c.info += [f"{name}: {n}" for n in d.notes]
        c.problems += [f"{name}: {p}" for p in d.problems]
        c.counts[name] = sum(len(t) for t in d.targets.values())
    c.counts["derivation rounds"] = getattr(an, "derive_rounds", 0)
    if getattr(an, "derive_unstable", False):
        c.problems.append("the derivation rounds didn't reach a fixpoint")
    c.ok = not c.problems
    return c


def check_c5(an) -> Check:
    c = Check("C5 disjointness", True)
    in_data = []
    for pc, variants in an.code.items():
        off = bus_to_offset(pc)
        if off is None:
            continue
        size = max(i.size for i in variants.values())
        if any(an.hints.is_data(pc + k) for k in range(size)):
            in_data.append(pc)
    table_into_data = []
    for jt in an.tables.values():
        for t in an._table_targets(jt, jt.count or 0) + list(jt.extra):
            if bus_to_offset(t) is not None and an.hints.is_data(t):
                table_into_data.append((jt.dispatcher, t))
    c.counts = {"transfers into data": len(an.forced_data), "instructions with bytes in data": len(in_data),
                "table entries into data": len(table_into_data)}
    c.problems += [f"{fmt(a)}: a transfer into a [[data]] region" for a in sorted(an.forced_data)]
    c.problems += [f"{fmt(a)}: an instruction with bytes in a [[data]] region" for a in sorted(in_data)]
    c.ok = not c.problems
    return c


def _callees(an):
    """Each context's callees: the contexts its instructions call or jump to."""
    by_pc = {}
    for key, ctx in an.contexts.items():
        for pc in ctx.insns:
            by_pc.setdefault(pc, []).append(key)
    callees = {}
    for key, ctx in an.contexts.items():
        for site in ctx.callers:
            for caller in by_pc.get(site, ()):
                callees.setdefault(caller, []).append(key)
    return callees


def _reach(work, callees):
    reached = set()
    work = list(work)
    while work:
        k = work.pop()
        if k in reached:
            continue
        reached.add(k)
        work += callees.get(k, [])
    return reached


def reached_code(an) -> set:
    """The instructions (their addresses) of the contexts reachable from the
    roots (vectors, hints other than `unreached`, coverage, patches): the code
    something in the game runs, which an `unreached` entry's alone isn't."""
    reached = _reach((k for k, ctx in an.contexts.items() if ctx.kinds & ROOT_KINDS), _callees(an))
    return {pc for k in reached for pc in an.contexts[k].insns}


def check_c6(an) -> Check:
    c = Check("C6 reachability", True)
    callees = _callees(an)
    reached = _reach((k for k, ctx in an.contexts.items() if ctx.kinds & ROOT_KINDS), callees)
    code_reached = {pc for k in reached for pc in an.contexts[k].insns}
    only_unreached = set(an.code) - code_reached
    # a context nothing reaches is fine only when an `unreached` entry starts it, or
    # when it is called from such code
    starts = {k for k, ctx in an.contexts.items() if "unreached" in ctx.kinds}
    closed = _reach(starts, callees)
    orphans = [k for k in an.contexts if k not in reached and k not in closed]
    c.counts = {"instructions": len(an.code), "reachable from the roots": len(code_reached & set(an.code)),
                "only from `unreached` entries": len(only_unreached), "`unreached` entries": len(starts),
                "contexts with neither": len(orphans)}
    c.problems += [f"{fmt(k[0])}: code nothing reaches without an `unreached` annotation" for k in sorted(orphans)]
    c.ok = not c.problems
    return c


def check_c7(an) -> Check:
    c = Check("C7 coverage", True)
    c.counts = {"executed addresses": len(an.coverage), "seeded (not found statically)": len(an.coverage_seeded)}
    c.problems += [f"{fmt(a)}: executed, but static tracing didn't reach it" for a in sorted(an.coverage_seeded)]
    c.ok = not c.problems
    return c


def closure(an) -> list:
    return [check_c1(an), check_c2(an), check_c3(an), check_c5(an), check_c6(an), check_c7(an)]


def code_set(an) -> set:
    """(address, M 8-bit, X 8-bit) of every instruction variant the analysis has,
    unknown widths expanded to both."""
    out = set()
    for pc, variants in an.code.items():
        for w in variants:
            for m in ([True, False] if w[0] is None else [w[0]]):
                for x in ([True, False] if w[1] is None else [w[1]]):
                    out.add((pc, m, x))
    return out


def report_lines(checks) -> list:
    lines = ["## Closure (MD4)", ""]
    for c in checks:
        lines.append(f"### {c.name}: {'pass' if c.ok else 'FAIL'}")
        lines.append("")
        lines += [f"- {k}: {v}" for k, v in c.counts.items()]
        lines += [f"- {i}" for i in c.info]
        lines += [f"- problem: {p}" for p in c.problems[:200]]
        if len(c.problems) > 200:
            lines.append(f"- ... {len(c.problems) - 200} more problems")
        lines.append("")
    return lines


__all__ = ["Check", "closure", "code_set", "indirect_sites", "report_lines"]
