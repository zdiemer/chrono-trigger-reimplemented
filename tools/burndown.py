"""How much of the ROM's code the hand ports cover, per bank and subsystem.

    python3 tools/burndown.py --rom roms/ct-usa.sfc
    python3 tools/burndown.py --rom roms/ct-usa.sfc --by subsystem
    python3 tools/burndown.py --rom roms/ct-usa.sfc --uncovered C1 --top 20

The recompilation runs the whole game from generated C ([RECOMP.md](../docs/RECOMP.md));
what is left to build is readable source, routine by routine (RECOMP.md's M5).
This says how far along that is, so the work can be sequenced and its progress
seen.

**What "covered" means here.** From each entry in `game/registry.c` (macro
rows such as the event commands' included), the instructions the port reaches
without leaving the routine: branches and jumps are followed, and returns end
a path. A walk stops at another registry address, which owns its own region.
A `JMP (abs,X)` whose table the analysis has sized, and a `JMP (abs)` or `JML
[abs]` whose targets it follows, go on to every target, as a port that
dispatches through one handles them or stops.

**Calls.** A call is followed into its callee when the port *inlines* it: the
port makes no call-out at that call (no `m_jsr`, `m_jsl`, their `_dc` forms or
`au_driver_call` in the functions the entry reaches whose site is that call or
whose target is its callee, read the way tools/dontcare_check.py reads them,
through the file's helpers, tables and parameters), and the callee isn't a
registered entry. The original makes that call during the port's call and the
port doesn't, so the port does the callee's work itself (shadow mode compares
the machine at every call-out and at the return, so a call it neither makes nor
does would show). Local helpers, `JSR (abs,X)` handlers and routines one bank
inlines from another are credited to the port that inlines them. A stop where a call returns, declaring nothing dead there, doesn't count as
a stop (below), being where a port gives up after a call-out that came back
with other widths. A port with a call-out,
or a PC it sets, that the check can't read credits no callee: any of them
might be the one it calls. A site that reads as something other than a call
is a misreading and counts as unreadable. The bytes credited this way alone
are the `inlined` column.

**Stops.** A place the port hands back at (an `m_stop_at` or
`m_stop_dont_care` it declares, or a PC it sets) is the original's: the walk
doesn't go on from it, so what the original runs from there -- a request the
port leaves it, down to a reset into the main loop -- isn't credited, unless
the port reaches the same code another way.

**Read it as an estimate.** It is still wrong in both directions:

- *Over-counts* where a port ends at a call-out whose return it doesn't
  declare a stop at, where a precondition or guard turns calls away, and a
  path the port gives up on by calling out to the original's loop from its
  own entry's call (the scene sequence interpreter does, for commands no
  script has); and code the port reaches another way than through its stop
  but only runs on paths that don't stop.
- *Under-counts* where a port inlines a callee it calls out to on some other
  path (the site or the target is named, so the callee isn't credited), a
  callee called after a stop the port only makes on some paths, all the
  callees of a port whose call-outs or stops the check can't read, and code a
  port covers by other means than a call the analysis sees.
- *Stops* at an indirect jump the analysis doesn't resolve.

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
from tools.disasm.memmap import Rom

ROOT = pathlib.Path(__file__).resolve().parent.parent

RETURNS = {"RTS", "RTL", "RTI", "STP"}
CALLS = {"JSR", "JSL"}
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


class Port:
    """What a registry entry's port does, read from its source: where it calls
    out and where it hands back."""

    def __init__(self, addr: int, name: str):
        self.addr = addr
        self.name = name
        self.sites: set = set()        # call-out sites (24-bit)
        self.targets: set = set()      # call-out targets
        self.unreadable = False        # a call-out, or a PC it sets, that can't be read
        self.stops: set = set()        # where it hands back
        self.idle = False              # an idle entry: a wait loop, whose calls aren't its own

    def calls_out(self, site: int, target: int | None) -> bool:
        return self.unreadable or site in self.sites or target in self.targets


def read_ports(root: pathlib.Path, an) -> list:
    """A Port for every registry entry, from the sources under root/game.
    Uses tools/dontcare_check.py's reading of the C (the same functions a
    declaration is followed through). A call-out whose site isn't a call the
    analysis knows was misread, and counts as unreadable."""
    from tools import dontcare_check as dc

    returns = {i.next_pc for variants in an.code.values() for i in variants.values() if i.name in CALLS}

    def returns_to(at):
        return at in returns

    def is_call(site):
        return any(i.name in CALLS or i.mode in (Mode.ABSIND, Mode.ABSXIND) for i in an.code.get(site, {}).values())

    sources, globals_ = dc.load_sources(root)
    ev = dc.Evaluator(sources, globals_)
    by_path = {s.path: s for s in sources}
    registry = by_path["game/registry.c"]
    entries, _ = dc.registry_entries(registry, ev)
    decls, _ = dc.declarations([s for s in sources if s.path.endswith(".c")], ev)
    calls = {src.path: src.calls(dc.CALL_OUTS) for src in sources}
    ports = []
    for e in entries:
        port = Port(e.addr, e.name)
        port.idle = e.idle
        ports.append(port)
        if e.generated:
            continue
        reach = dc.reaching(sources, e.fn)
        funcs = collections.defaultdict(set)
        for path, fname in reach:
            funcs[path].add(fname)
        for path, names in funcs.items():
            src = by_path[path]
            saved, ev.only_callers = ev.only_callers, set(names)
            for call in calls[path]:
                f = src.func_at(call.offset)
                if f is None or f.name not in names:
                    continue
                if f.name in dc.BRIDGES.get(path, ()):
                    continue                    # the bridge's own call, read at each delegate's call instead
                pairs = _call_values(dc, ev, src, f, call)
                if pairs is None or not all(is_call(site) for site, _ in pairs):
                    port.unreadable = True      # not read, or read as something that isn't a call
                    continue
                port.sites |= {site for site, _ in pairs}
                port.targets |= {target for _, target in pairs if target is not None}
            # a hand-back by setting PC (in the entry's bank, or one the
            # function sets PB to); where it can't be read, the port could stop
            # anywhere, and credits no callee
            for f in (src.funcs[n] for n in names if n in src.funcs):
                body = src.text[f.start:f.end]
                banks = {e.addr >> 16} | {int(b, 16) for b in re.findall(r"\bpb\s*=\s*0x([0-9A-Fa-f]{2})\b", body)}
                for m in re.finditer(r"(?:\.|->)pc\s*=(?!=)\s*([^;]*);", body):
                    try:
                        pcs = {v[0] & 0xFFFF for v, _ in ev.values([dc.parse(m.group(1))], src, f)}
                    except (dc.Unreadable, IndexError, KeyError, ValueError, TypeError, RecursionError):
                        pcs = None
                    if not pcs:
                        port.unreadable = True
                        continue
                    port.stops |= {b << 16 | v for b in banks for v in pcs}
            ev.only_callers = saved
        declared = set()
        for d in decls:
            if d.kind == "stop" and (d.path, d.func) in reach:
                port.stops.add(d.at)
                if d.dont_care:
                    declared.add(d.at)
        # a stop where a call returns, declaring nothing dead, is where the port
        # gives up after a call-out that came back other than expected (with
        # other widths): it goes on there otherwise, so the code after it is
        # still its own. One that declares what is dead there hands back.
        port.stops = {at for at in port.stops if at in declared or not returns_to(at)}
    return ports


def _call_values(dc, ev, src, f, call):
    """The (site, target) pairs a call-out can have, the target None where it
    is computed at run time (a jump table's); None if they can't be read."""
    try:
        if call.name == "au_driver_call":
            origin = dc.parse(call.args[1])
            if origin[0] != "call" or origin[1] != "PF_CALL":
                return None
            nodes = [origin[2][0], origin[2][1]]
        else:
            nodes = [dc.parse(call.args[1]), dc.parse(call.args[2])]
        try:
            got = [(v[0] & 0xFFFFFF, v[1] & 0xFFFFFF) for v, _ in ev.values(nodes, src, f)]
        except dc.Unreadable:
            got = [(v[0] & 0xFFFFFF, None) for v, _ in ev.values(nodes[:1], src, f)]
        return got or None
    except (dc.Unreadable, IndexError, KeyError, ValueError, TypeError, RecursionError):
        return None


def insn_at(an, pc):
    """The widest variant decoded at pc, or None."""
    variants = an.code.get(pc)
    if not variants:
        return None
    return max(variants.values(), key=lambda i: i.size)


def table_targets(an, insn):
    """The targets of an (abs,X) dispatch whose table the analysis has sized, or []."""
    jt = an.tables.get(insn.pc)
    if jt is None or jt.count is None:
        return []
    step = 3 if jt.kind == "long" else 2
    bank = jt.dispatcher & 0xFF0000
    out = []
    for i in range(jt.count):
        at = jt.table + i * step
        v = an.u24(at) if jt.kind == "long" else an.u16(at)
        if v is not None:
            out.append(v if jt.kind == "long" else bank | v)
    return out + list(jt.extra) + sorted(an.derived.get(insn.pc, ()))


def indirect_targets(an, pc):
    """Where a JMP (abs) or JML [abs] goes, as the analysis follows it."""
    if pc in an.hints.nofollow:
        return []
    return list(an.hints.indirect.get(pc, [])) + sorted(an.derived.get(pc, ()))


def call_targets(an, insn):
    """What a call instruction calls, as the analysis follows it; [] if unknown."""
    pc = insn.pc
    if insn.name in CALLS and insn.mode in (Mode.ABS, Mode.LONG):
        own = insn.operand if insn.name == "JSL" else (pc & 0xFF0000) | insn.operand
        if pc in an.hints.derive:
            return list(an.hints.indirect.get(pc, [])) + sorted(an.derived.get(pc, ()))
        return list(an.hints.indirect.get(pc, [own]))
    if insn.name == "JSR" and insn.mode is Mode.ABSXIND:
        jt = an.tables.get(pc)
        return table_targets(an, insn) if jt is not None and jt.call else []
    if insn.mode in (Mode.ABSIND, Mode.ABSXIND) and an.call_resumes.get(pc):
        return indirect_targets(an, pc) if insn.mode is Mode.ABSIND else table_targets(an, insn)
    return []


def successors(an, insn):
    """Where control can go next without a call: (next pcs, callees)."""
    name, mode = insn.name, insn.mode
    if name in RETURNS:
        return [], []
    callees = call_targets(an, insn)
    if callees or (name in CALLS and mode in (Mode.ABS, Mode.LONG)) or (name == "JSR" and mode is Mode.ABSXIND):
        resumes = sorted(an.call_resumes.get(insn.pc, ())) if name == "JMP" else [insn.next_pc]
        return resumes, callees
    if name == "JMP" and mode == Mode.ABSXIND:
        return table_targets(an, insn), []
    if mode in (Mode.ABSIND, Mode.ABSLIND):
        return indirect_targets(an, insn.pc), []
    if mode is Mode.ABSXIND:
        return [], []
    if name in ("JMP", "JML"):
        return [insn.operand if name == "JML"
                else (insn.pc & 0xFF0000) | (insn.operand & 0xFFFF)], []
    if name in ("BRA", "BRL"):
        return [insn.branch_target()], []
    if mode in (Mode.REL8, Mode.REL16):  # conditional branch
        return [insn.branch_target(), insn.next_pc], []
    return [insn.next_pc], []


def walk(an, entry, owners, port=None):
    """Bytes of code the port at `entry` covers: (the routine's own, the
    callees' it inlines). Without a port (nothing known about its source),
    calls aren't followed. A place where the port hands back is the
    original's: the walk doesn't go on from it, so the code reached only
    through it isn't credited (code the port also reaches another way still
    is)."""
    seen, reached, inlined = set(), set(), set()
    stops = port.stops if port is not None else set()
    queue = [(entry, False)]
    while queue:
        pc, inside = queue.pop()    # inside: in a callee the port inlines
        if (pc, inside) in seen:
            continue
        seen.add((pc, inside))
        insn = insn_at(an, pc)
        if insn is None:
            continue
        (inlined if inside else reached).update(range(pc, pc + insn.size))
        nexts, callees = successors(an, insn)
        for t in callees:
            # a callee the port stops at the entry of is the original's to run
            if port is None or port.idle or t in owners or t in stops or port.calls_out(pc, t):
                continue
            queue.append((t, True))
        for nxt in nexts:
            # another port owns its own region, and its own walk will count it
            if nxt != entry and nxt in owners:
                continue
            if nxt in stops:
                continue
            queue.append((nxt, inside))
    return reached, inlined


def coverage(an, ports):
    """(covered bytes, the part of them only an inlined callee covers, entry
    addresses)."""
    owners = {p.addr for p in ports}
    own, inlined = set(), set()
    for p in ports:
        r, i = walk(an, p.addr, owners, p)
        own |= r
        inlined |= i
    return own | inlined, inlined - own, owners


def main():
    ap = argparse.ArgumentParser(prog="python3 tools/burndown.py")
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--hints", type=pathlib.Path, default=ROOT / "re/hints.toml")
    ap.add_argument("--tree", type=pathlib.Path, default=ROOT,
                    help="the source tree whose game/ (registry and ports) to read")
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

    ports = read_ports(args.tree, an)
    covered, inlined, entries = coverage(an, ports)

    # The analysis's own code, which is what the generator builds blocks from.
    all_code = set()
    for pc, variants in an.code.items():
        all_code.update(range(pc, pc + max(i.size for i in variants.values())))
    covered &= all_code
    inlined &= all_code

    rows = collections.defaultdict(lambda: [0, 0, 0, 0])   # key -> [code, covered, inlined, entries]
    for addr in all_code:
        rows[addr >> 16][0] += 1
    for addr in covered:
        rows[addr >> 16][1] += 1
    for addr in inlined:
        rows[addr >> 16][2] += 1
    for entry in entries:
        rows[entry >> 16][3] += 1

    if args.by == "subsystem":
        merged = collections.defaultdict(lambda: [0, 0, 0, 0])
        for bank, vals in rows.items():
            key = SUBSYSTEM.get(bank, "bank $%02X" % bank)
            for i, v in enumerate(vals):
                merged[key][i] += v
        rows = merged
        head = "Subsystem"
    else:
        rows = {("$%02X %s" % (b, SUBSYSTEM.get(b, ""))).strip(): v for b, v in rows.items()}
        head = "Bank"

    totals = [sum(v[i] for v in rows.values()) for i in range(4)]
    width = max(len(k) for k in rows)
    print("%-*s  %9s  %9s  %9s  %6s  %7s" % (width, head, "code", "ported", "inlined", "left", "entries"))
    for key, (code, cov, inl, n) in sorted(rows.items(), key=lambda kv: -kv[1][0]):
        print("%-*s  %8.1fK  %8.1fK  %8.1fK  %5.0f%%  %7d"
              % (width, key, code / 1024, cov / 1024, inl / 1024, 100.0 * (code - cov) / code if code else 0, n))
    code, cov, inl, n = totals
    print("%-*s  %8.1fK  %8.1fK  %8.1fK  %5.0f%%  %7d"
          % (width, "total", code / 1024, cov / 1024, inl / 1024, 100.0 * (code - cov) / code if code else 0, n))

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
