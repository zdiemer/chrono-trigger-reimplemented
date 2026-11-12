"""Inspect original-code handbacks while diagnosing emulator-build divergences.

Manual development tool, outside the gate: read ct_dev/ct_drive --fallbacks
files to locate the original instructions a hybrid run still executes.
Use DIR... --by routine --top 30, --totals, or --report out.txt.
There is no baseline or refusal allowlist: ct_native runs no original code.
"""

import argparse
import collections
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.burndown import SUBSYSTEM  # noqa: E402

KINDS = ("refused", "handback", "skipped", "unported")


# Routines filed under their bank (no call on record) reported separately, as (first address, last address): the window list's
# copy of an area ($C2:EF05, menu_window_copy_area) and its rows' block moves
# ($C2:EF2B), whose rise in chunk 11 (an ended comparison no longer covering
# them) the bank's falling line hid.
CUT_OUT = ((0xC2EF05, 0xC2EF47),)

INSN = re.compile(r"^insn (\w+) ([0-9A-F]{6}) (\d+) (\d+) ([0-9A-F]{2}(?:[0-9A-F]{4}|----)) (\S+)$")
EVENT = re.compile(r"^event (\w+) ([0-9A-F]{6}) ([0-9A-F]{6}) (\d+) (\S+)$")
REFUSAL = re.compile(r"^refusal ([0-9A-F]{6}) (\w+) (\d+) (\S+)$")
STATE = re.compile(r"^state ([0-9A-F]{6}) (\w+) (.*)$")
OPEN = re.compile(r"^open ([0-9A-F]{6}) (\d+) (\S+)$")
CLOSED = re.compile(r"^closed ([0-9A-F]{6}) (\d+) (\S+)$")
IDLE_CLOSED = re.compile(INSN.pattern.replace("^insn ", "^idle-closed "))


def canonical(addr):
    """A code address in one form: the bank a HiROM mirror stands for
    ($00-$3F and $80-$BF above $8000 are $C0-$FF), and low work RAM as $7E."""
    bank, low = addr >> 16, addr & 0xFFFF
    if bank < 0x40 or 0x80 <= bank < 0xC0:
        if low >= 0x8000:
            return (0xC0 | (bank & 0x3F)) << 16 | low
        if low < 0x2000:
            return 0x7E0000 | low
    return addr


def bank_name(bank):
    return SUBSYSTEM.get(bank, "bank $%02X" % bank)


def subsystem(addr):
    return bank_name(canonical(addr) >> 16)


def fmt(addr):
    return "%02X:%04X" % (addr >> 16, addr & 0xFFFF)


def routine_text(text):
    """A file's routine field (BBAAAA, or BB---- for a bank) as the baseline writes it."""
    if text.endswith("----"):
        return fmt(canonical(int(text[:2], 16) << 16 | 0x8000))[:3] + "----"
    return fmt(canonical(int(text, 16)))


def cut_out(routine, pc):
    """The line an instruction is filed on: its routine's, or for one filed
    under its bank, a CUT_OUT routine's where it is one."""
    if routine.endswith("----"):
        for first, last in CUT_OUT:
            if first <= pc <= last:
                return fmt(first)
    return routine


def routine_subsystem(routine):
    return bank_name(int(routine[:2], 16))


class Measure:
    """The files added up."""

    def __init__(self):
        self.insns = collections.Counter()      # (kind, pc, routine, port) -> instructions
        self.length = {}                        # pc -> instruction length
        self.events = collections.Counter()     # (what, port, where) -> count
        self.names = {}                         # port address -> name
        self.refusals = collections.Counter()   # (entry, name, why) -> count
        self.states = collections.defaultdict(list)
        self.open = collections.Counter()       # (port, name) -> instructions under its unfinished comparison
        self.completed = collections.Counter()  # cutoff comparisons matched in a bounded tail
        self.idle_completed = collections.Counter()  # exact cutoff idle prefixes completed in the tail
        self.bad = []
        self.files = 0

    def read(self, path):
        self.files += 1
        run = path.parent.name + "/" + path.stem
        opened, closed = collections.Counter(), collections.Counter()
        instructions, idle_closed = collections.Counter(), collections.Counter()
        for line in path.read_text(errors="replace").splitlines():
            m = INSN.match(line) or IDLE_CLOSED.match(line)
            if m:
                kind, pc, length, count, routine, port = m.groups()
                pc = canonical(int(pc, 16))
                port = "-" if port == "-" else fmt(int(port, 16))
                key = (kind, pc, cut_out(routine_text(routine), pc), port, int(length))
                if line.startswith("idle-closed "):
                    idle_closed[key] += int(count)
                else:
                    instructions[key] += int(count)
                continue
            m = EVENT.match(line)
            if m:
                what, port, where, count, name = m.groups()
                self.events[(what, fmt(int(port, 16)), fmt(int(where, 16)))] += int(count)
                self.names[fmt(int(port, 16))] = name
                continue
            m = REFUSAL.match(line)
            if m:
                entry, why, count, name = m.groups()
                self.refusals[(fmt(int(entry, 16)), name, why)] += int(count)
                self.names[fmt(int(entry, 16))] = name
                continue
            m = OPEN.match(line)
            if m:
                port, count, name = m.groups()
                opened[(fmt(int(port, 16)), name)] += int(count)
                continue
            m = CLOSED.match(line)
            if m:
                port, count, name = m.groups()
                closed[(fmt(int(port, 16)), name)] += int(count)
                continue
            m = STATE.match(line)
            if m:
                entry, why, state = m.groups()
                key = (fmt(int(entry, 16)), why)
                state = "%s (%s)" % (state, run)
                if state not in self.states[key]:
                    self.states[key].append(state)
        for key, count in idle_closed.items():
            if count > instructions[key]:
                self.bad.append("%s: idle completion exceeds this run's instruction count: %s" % (run, fmt(key[1])))
            else:
                instructions[key] -= count
                self.idle_completed[key[:-1]] += count
        for key, count in instructions.items():
            if count:
                self.insns[key[:-1]] += count
                self.length[key[1]] = max(self.length.get(key[1], 0), key[-1])
        for key, count in closed.items():
            if count > opened[key]:
                self.bad.append("%s: closed comparison exceeds this run's open count: %s" % (run, key[0]))
            else:
                opened[key] -= count
                self.completed[key] += count
        self.open.update({key: n for key, n in opened.items() if n})

    def routines(self):
        """routine -> [instructions, set of byte addresses, Counter of kinds, {kind: set of ports}]."""
        out = {}
        for (kind, pc, routine, port), n in self.insns.items():
            row = out.setdefault(routine, [0, set(), collections.Counter(), collections.defaultdict(set)])
            row[0] += n
            row[1].update(range(pc, pc + self.length[pc]))
            row[2][kind] += n
            if port != "-":
                row[3][kind].add(port)
        return out

    def totals(self, by=None):
        """(group, kind) -> [instructions, set of bytes]; group is None, the subsystem, or the routine."""
        out = collections.defaultdict(lambda: [0, set()])
        for (kind, pc, routine, port), n in self.insns.items():
            group = None if by is None else subsystem(pc) if by == "subsystem" else routine
            row = out[(group, kind)]
            row[0] += n
            row[1].update(range(pc, pc + self.length[pc]))
        return out


def gather(paths):
    m = Measure()
    for p in paths:
        p = pathlib.Path(p)
        files = sorted(p.rglob("*.txt")) if p.is_dir() else [p] if p.exists() else []
        for f in files:
            m.read(f)
    return m


def kib(nbytes):
    return "%.1fK" % (nbytes / 1024)


def totals_line(m):
    """The totals in one line: KiB of distinct code and instructions, all and by kind."""
    total = m.totals()
    nbytes = len(set().union(*(total[(None, k)][1] for k in KINDS)))
    ninsns = sum(total[(None, k)][0] for k in KINDS)
    parts = ", ".join("%s %s/%d" % (k, kib(len(total[(None, k)][1])), total[(None, k)][0]) for k in KINDS)
    summary = "%s of code, %d instructions in place of hand ports (%s); %d under comparisons still open" % (
        kib(nbytes), ninsns, parts, sum(m.open.values()))
    if m.completed:
        summary += "; %d cutoff instructions compared in bounded tails" % sum(m.completed.values())
    if m.idle_completed:
        summary += "; %d cutoff idle instructions certified in bounded tails" % sum(m.idle_completed.values())
    return summary


def describe(m, routine, row):
    """A routine's reason in words, for a new baseline line."""
    _n, _b, kinds, ports = row
    where = routine_subsystem(routine)
    if set(kinds) == {"unported"}:
        return "no port yet (%s)" % where
    parts = []
    for kind in KINDS:
        if not kinds[kind]:
            continue
        if kind == "unported":
            parts.append("unported")
            continue
        names = sorted({m.names.get(p, p) for p in ports[kind]})
        parts.append("%s by %s" % (kind, ", ".join(names[:3]) + (" and %d more" % (len(names) - 3)
                                                                if len(names) > 3 else "")))
    return "%s (%s)" % ("; ".join(parts), where)


def print_totals(m, by, top, out):
    total = m.totals()
    out.write("fallbacks over %d file(s): %s\n" % (m.files, totals_line(m)))
    for kind in KINDS:
        n, b = total[(None, kind)]
        out.write("  %-9s %8s  %13d instructions\n" % (kind, kib(len(b)), n))
    if by == "subsystem":
        rows = m.totals("subsystem")
        groups = sorted({g for g, _ in rows}, key=lambda g: -sum(rows[(g, k)][0] for k in KINDS))
        out.write("\n%-28s" % "subsystem" + "".join("%22s" % k for k in KINDS) + "\n")
        for g in groups:
            cells = "".join("%9s %12d" % (kib(len(rows[(g, k)][1])), rows[(g, k)][0]) for k in KINDS)
            out.write("%-28s%s\n" % (g, cells))
    elif by == "routine":
        rows = m.routines()
        out.write("\n%-8s %12s %8s  %s\n" % ("routine", "instructions", "code", "what"))
        for routine, row in sorted(rows.items(), key=lambda kv: -kv[1][0])[:top]:
            out.write("%-8s %12d %8s  %s\n" % (routine, row[0], kib(len(row[1])), describe(m, routine, row)))


def write_report(m, path):
    """Every address: kind, address, length, instructions, routine, port, subsystem."""
    rows = sorted(m.insns.items(), key=lambda kv: (-kv[1], kv[0]))
    with open(path, "w", newline="") as f:
        f.write("# kind address length instructions routine port subsystem (tools/fallbacks.py)\n")
        for (kind, pc, routine, port), n in rows:
            f.write("%s %s %d %d %s %s %s\n" % (kind, fmt(pc), m.length[pc], n, routine, port, subsystem(pc)))


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 tools/fallbacks.py", description=__doc__.splitlines()[0])
    ap.add_argument("paths", nargs="+", help="files ct_dev/ct_drive --fallbacks wrote, or directories of them")
    ap.add_argument("--by", choices=("subsystem", "routine"), default="subsystem")
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--report", type=pathlib.Path, help="write every address's counts here")
    ap.add_argument("--totals", action="store_true", help="one line: the totals by kind")
    args = ap.parse_args(argv)

    m = gather(args.paths)
    if not m.files:
        print("no fallback files under %s" % " ".join(args.paths))
        return 1
    if args.report:
        write_report(m, args.report)
    if args.totals:
        print(totals_line(m))
        return 0
    print_totals(m, args.by, args.top, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
