"""Constant register values at instructions the analysis found (MD4 derivations).

`register_values(an, pc, reg, flow)` walks back from `pc` over every path into
it (fall-through from the previous instruction, and branches and `JMP abs` to
it) and returns the set of values the nearest instruction setting `reg` on each
path gives it, when each such instruction is an immediate load (or a transfer
from a register known the same way). It returns None when a path reaches an
address something else transfers to (a context entry, a call's return, a table
target) or an instruction that sets the register otherwise, before finding a
load. That is a sound under-approximation for argument enumeration: a None is
reported, never guessed.
"""

from dataclasses import dataclass, field

from .cpu import Mode

# Instructions that change a register without a known value (besides loads and
# transfers handled below). Calls clobber everything.
_CLOBBER = {
    "A": {"PLA", "TSC", "TDC", "XBA", "INC", "DEC", "ASL", "LSR", "ROL", "ROR", "ADC", "SBC", "AND", "ORA", "EOR",
          "JSR", "JSL", "MVN", "MVP"},
    "X": {"PLX", "TSX", "INX", "DEX", "JSR", "JSL", "MVN", "MVP"},
    "Y": {"PLY", "INY", "DEY", "JSR", "JSL", "MVN", "MVP"},
}
_TRANSFER = {"TAX": ("X", "A"), "TAY": ("Y", "A"), "TXA": ("A", "X"), "TYA": ("A", "Y"), "TXY": ("Y", "X"),
             "TYX": ("X", "Y")}
_NO_FALLTHROUGH = {"RTS", "RTL", "RTI", "JMP", "JML", "BRA", "BRL", "STP"}


@dataclass
class Flow:
    pred: dict = field(default_factory=dict)       # next_pc -> instruction that falls through to it
    branches: dict = field(default_factory=dict)   # target -> {branch or JMP abs instructions}
    joins: set = field(default_factory=set)        # targets of JMP (abs,X) tables


def flow(an) -> Flow:
    """Fall-through and branch predecessors of every instruction the analysis decoded
    (addresses decoded with several lengths are left out)."""
    f = Flow()
    for pc, variants in an.code.items():
        sizes = {i.size for i in variants.values()}
        if len(sizes) != 1:
            continue
        insn = next(iter(variants.values()))
        if insn.name not in _NO_FALLTHROUGH:
            f.pred[pc + insn.size] = pc
        if insn.mode in (Mode.REL8, Mode.REL16) and insn.name != "PER":
            f.branches.setdefault(insn.branch_target(), set()).add(pc)
        elif insn.name == "JMP" and insn.mode is Mode.ABS:
            f.branches.setdefault((pc & 0xFF0000) | insn.operand, set()).add(pc)
    for jt in an.tables.values():
        if not jt.call and jt.count:
            f.joins.update(an._table_targets(jt, jt.count))
    return f


predecessors = flow


def register_values(an, pc: int, reg: str, fl: Flow, limit: int = 64) -> set | None:
    """Values `reg` can hold before the instruction at pc (see the module doc)."""
    out = set()
    seen = set()
    work = [(pc, reg)]
    steps = 0
    while work:
        cur, r = work.pop()
        if (cur, r) in seen:
            continue
        seen.add((cur, r))
        steps += 1
        if steps > limit:
            return None
        sources = []
        prev = fl.pred.get(cur)
        if prev is not None:
            sources.append(prev)
        branch_in = fl.branches.get(cur, set())
        sources += sorted(branch_in)
        kinds = an.labels.get(cur, set())
        # something besides branches reaches cur (an entry, a table target), or nothing does
        if not sources or cur in fl.joins or "sub" in kinds or "jt" in kinds or ("loc" in kinds and not branch_in):
            return None
        for src in sources:
            insn = next(iter(an.code[src].values()))
            n, mode = insn.name, insn.mode
            if n == "LD" + r and mode in (Mode.IMM_M, Mode.IMM_X):
                out.add(insn.operand)
            elif n in _TRANSFER and _TRANSFER[n][0] == r:
                work.append((src, _TRANSFER[n][1]))
            elif n.startswith("LD") and n[2] == r or n in _CLOBBER[r] or n.startswith("PL") and n[2] == r:
                return None
            else:
                work.append((src, r))
    return out


def register_value(an, pc: int, reg: str, fl: Flow, limit: int = 64):
    """The single value of register_values, or None."""
    vals = register_values(an, pc, reg, fl, limit)
    return next(iter(vals)) if vals and len(vals) == 1 else None


def call_sites(an, target: int) -> list:
    """Every instruction that calls or jumps to `target` directly (JSR/JSL/JMP/JML)."""
    out = []
    for pc, variants in an.code.items():
        for insn in variants.values():
            if insn.name in ("JSR", "JMP") and insn.mode is Mode.ABS and (pc & 0xFF0000 | insn.operand) == target:
                out.append(pc)
                break
            if insn.name in ("JSL", "JML") and insn.mode is Mode.LONG and insn.operand == target:
                out.append(pc)
                break
    return sorted(out)


def callees(an, entries) -> set:
    """Entry addresses reachable from `entries` through direct calls and jumps
    (JSR/JSL/JML/JMP abs and branches stay inside a context's instructions)."""
    by_entry = {}
    for (entry, _), c in an.contexts.items():
        by_entry.setdefault(entry, []).append(c)
    todo, done = list(entries), set()
    while todo:
        e = todo.pop()
        if e in done:
            continue
        done.add(e)
        for c in by_entry.get(e, []):
            for pc in c.insns:
                for insn in an.code[pc].values():
                    if insn.name in ("JSR", "JSL", "JML") and insn.mode in (Mode.ABS, Mode.LONG):
                        todo.append(insn.operand if insn.mode is Mode.LONG else (pc & 0xFF0000) | insn.operand)
    return done


__all__ = ["Flow", "call_sites", "callees", "flow", "predecessors", "register_value", "register_values"]
