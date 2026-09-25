"""Check `[[keeps]]` hints against the code they describe.

A `[[keeps]]` hint tells liveness that a call returns with some registers as
its caller had them and reads only the ones it names: a task switch, which
saves the caller's registers on one stack and returns on another, where the
pairing of pushes can't follow (DISASSEMBLY.md). The analysis takes the hint
on trust, so a wrong one silently kills a register the program reads. This
checks each hint from the decoded code:

- the entry pushes exactly the registers the hint says it keeps, before
  anything else;
- the hint's `resume`, where a switched-out task takes its turn again, pulls
  them back in the reverse order and returns;
- every register the entry doesn't save is in `reads` if some path from the
  pushes to the next task (the resume, or a task started through an
  indirect jump) reads it before setting it, or doesn't set it at all: a
  value that reaches the next task is read by it.

Paths are walked over the decoded instructions with each register's "set on
this path" state; calls are stepped over as setting nothing, which only ever
asks for more in `reads`.
"""
from __future__ import annotations

from .cpu import CONDITIONAL_BRANCHES, RETURNS, Mode
from .liveness import AH, A, DB, D, FLAGS, KEEP_BITS, X, Y, use_def

PUSHES = {"PHA": "A", "PHX": "X", "PHY": "Y", "PHP": "P", "PHD": "D", "PHB": "DB"}
PULLS = {"PLA": "A", "PLX": "X", "PLY": "Y", "PLP": "P", "PLD": "D", "PLB": "DB"}
# What an entry that doesn't save a register can hand the next task.
UNSAVED = (("A", A), ("AH", AH), ("X", X), ("Y", Y), ("P", FLAGS), ("DB", DB), ("D", D))
MAX_STATES = 20000


def _insn(an, pc):
    """(widths, insn) variants the analysis decoded at pc."""
    return list(an.code.get(pc, {}).items())


def _one(an, pc):
    variants = _insn(an, pc)
    return variants[0][1] if variants else None


def _pushes(an, entry: int) -> tuple[list, int]:
    """The registers the entry pushes before anything else, in order, and the
    pc after them."""
    names, pc = [], entry
    while True:
        insn = _one(an, pc)
        if insn is None or insn.name not in PUSHES:
            return names, pc
        names.append(PUSHES[insn.name])
        pc = insn.next_pc


def _pulls_before_return(an, resume: int) -> list | None:
    """The registers pulled just before the return the resume path reaches,
    in order; None if the path branches before it returns."""
    pc, run, seen = resume, [], set()
    while pc not in seen:
        seen.add(pc)
        insn = _one(an, pc)
        if insn is None:
            return None
        if insn.name in ("RTS", "RTL"):
            return run
        if insn.name in PULLS:
            run.append(PULLS[insn.name])
        else:
            run = []
        if insn.name in ("BRA", "BRL"):
            pc = insn.branch_target()
        elif insn.name == "JMP" and insn.mode is Mode.ABS:
            pc = (pc & 0xFF0000) | insn.operand
        elif insn.name in CONDITIONAL_BRANCHES or insn.name in RETURNS:
            return None
        else:
            pc = insn.next_pc
    return None


def _flows(an, start: int, resume: int) -> int:
    """The registers some path from `start` to the next task reads before it
    sets them or leaves unset: the next task is reached at `resume`, at an
    indirect jump (a task started) or at a return."""
    required = 0
    todo = [(start, 0)]
    seen = set()
    while todo:
        pc, set_ = todo.pop()
        if (pc, set_) in seen:
            continue
        seen.add((pc, set_))
        if len(seen) > MAX_STATES:
            return ~0
        variants = _insn(an, pc)
        if pc == resume or not variants:
            required |= ~set_
            continue
        uses, defs = 0, ~0
        for widths, insn in variants:
            u, d = use_def(insn, widths[0], widths[1])
            uses |= u
            defs &= d
        insn = variants[0][1]
        required |= uses & ~set_
        now = set_ | (defs & ~0)
        name, mode = insn.name, insn.mode
        if name in RETURNS or name in ("JML",) or mode in (Mode.ABSIND, Mode.ABSLIND, Mode.ABSXIND):
            required |= ~now
            continue
        if name in CONDITIONAL_BRANCHES:
            nexts = [insn.next_pc, insn.branch_target()]
        elif name in ("BRA", "BRL"):
            nexts = [insn.branch_target()]
        elif name == "JMP" and mode is Mode.ABS:
            nexts = [(pc & 0xFF0000) | insn.operand]
        else:
            nexts = [insn.next_pc]
        for n in nexts:
            todo.append((n, now))
    return required


def check(an) -> list[str]:
    """Findings, one line each, for every `[[keeps]]` hint the code disagrees
    with; empty when all agree."""
    out = []
    for entry, (kept, reads) in sorted(an.hints.keeps.items()):
        where = "keeps $%02X:%04X" % (entry >> 16, entry & 0xFFFF)
        pushed, after = _pushes(an, entry)
        if sorted(pushed) != sorted(kept):
            out.append("%s: the entry pushes %s, the hint keeps %s" % (where, pushed or "nothing", list(kept)))
        resume = an.hints.keep_resumes.get(entry)
        if resume is None:
            out.append("%s: no resume address" % where)
            continue
        pulls = _pulls_before_return(an, resume)
        if pulls != list(reversed(pushed)):
            out.append("%s: the resume at $%02X:%04X pulls %s, not %s reversed"
                       % (where, resume >> 16, resume & 0xFFFF, pulls, pushed))
        flows = _flows(an, after, resume)
        named = 0
        for name in reads:
            named |= KEEP_BITS[name]
        for name, bit in UNSAVED:
            if name in kept:
                continue
            if flows & bit and not (named & bit) == bit:
                out.append("%s: %s reaches the next task unset or is read before it is set, "
                           "and is not in reads" % (where, name))
    return out
