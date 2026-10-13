"""The fallback measure: how much of real play still runs the original's code
in place of hand ports, and the gate's ratchet on it.

    python3 tools/fallbacks.py build/suite/fallbacks/shadow           # the totals, by subsystem
    python3 tools/fallbacks.py build/suite/fallbacks/shadow --by routine --top 30
    python3 tools/fallbacks.py DIR... --report out.txt                # every address
    python3 tools/fallbacks.py build/suite/fallbacks/shadow --check   # the ratchet (the gate)
    python3 tools/fallbacks.py DIR... --refusals                      # refusals against their allowlist
    python3 tools/fallbacks.py build/suite/fallbacks/shadow --update  # take today's numbers as the baseline

The project's goal is a program with no emulator in it, and every fallback is a
place a native build would have nothing to run: a guard that refuses so the
original runs, a port that stops and hands the rest back, a call-out to code
that is still the original's, code nobody has ported. `ct_dev --fallbacks` and
`ct_drive --fallbacks` (dev/fallbacks.h) count every instruction the original's
code executes (generated or interpreted) in place of hand ports, by its address
and why it ran, and this reads the files they write (any number of them, or
directories of them) and adds them up. The gate (tools/suite.py) writes them
from two kinds of run:

- **The reference play**: every movie in shadow mode, where the original runs
  everything at its own timing and the runtime says where a port stands for it
  (dev/fallbacks.h). A movie plays the same there whatever the ports' timing,
  so this count moves only when a port changes, and the ratchet is taken over it.
- **The shipping mode** (`--native on --generated on --sound-driver c`): every
  movie, and the routes the routes check plays. Hand ports estimate their time
  there, so a movie's presses land on other frames and it wanders off where it
  was recorded (the native-shape build's timing alone moved 99 of 1068 keys and
  doubled the wait loops' counts); the routes play to their goals but meet
  other battles. That count is reported, and its refusals checked, but it
  can't be ratcheted address by address.

**Kinds.** `refused`: a hand port is registered where the call began and every
hand entry there turned it away (its precondition on P, the CPU's mode, or its
guard), so the original ran the whole call. `handback`: code a hand port
handed back -- the callee of a call-out, or what runs after the place it
stopped (m_stop_at, a PC it set) until its routine returns. `skipped`: a hand
entry applied but the runtime couldn't run it (the nesting of suspended
routines was at its limit). `unported`: no port is involved. Each instruction
is filed under the routine it belongs to -- the target of the latest call on
record, the port's own entry for what it hands back after a stop, or `BB:----`,
its bank, where there is no call on record (the main loop, a task's own stack)
-- and, for the first three, the port. Totals are given as KiB of distinct
code (each instruction's bytes, once per kind; a bank's mirrors count as the
bank) and as instructions executed, by kind and by subsystem (the bank's, as
tools/burndown.py names them).

**The ratchet** (`--check`, tools/fallback_baseline.txt): a line per routine
with its instructions and distinct bytes over the reference play, all kinds
together, and a reason after `#`. It fails on a routine the baseline doesn't
have and on one whose distinct bytes rose (a new address run in place of a
port), or whose instructions rose by more than INSTRUCTION_SLACK. Kinds are
summed so that a port landing doesn't move its callees: what was `unported`
becomes `handback` in the same routine. Routines that fell, or went, are
reported for `--update` to take; `--update` keeps the reasons of the lines it
keeps and writes one for each new line from what the counts show (the kinds,
the subsystem, the ports that hand back), which a reader may replace.

**Refusals** (`--refusals`, tools/refusal_allowlist.txt): in a native build
there is no original to hand a refused call to (docs/PLAN.md, "Retiring the
gate, deliberately"), so a refusal in real play is a gap, and every one fails
unless the allowlist names its entry and condition with a concrete reason
(`BBAAAA name why # reason`). Each is printed with its count and the first
machine states it was refused in, with the run they came from; for code in
work RAM the state says whether its bytes are those the generated blocks were
made from (a guard that checks for its own code refuses another program's).
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

BASELINE = ROOT / "tools/fallback_baseline.txt"
ALLOWLIST = ROOT / "tools/refusal_allowlist.txt"
KINDS = ("refused", "handback", "skipped", "unported")

# How far past its baseline a routine's instructions may go before the ratchet
# fails. The reference play is the same every run, so this is room for the
# counting's own edges (a wait loop's last pass, which stays counted), not
# for drift; distinct bytes get none.
INSTRUCTION_SLACK = (0.05, 1000)     # 5% more, plus this many instructions
# A routine this far below its baseline is reported as a gain to take.
GAIN = 0.75

INSN = re.compile(r"^insn (\w+) ([0-9A-F]{6}) (\d+) (\d+) ([0-9A-F]{2}(?:[0-9A-F]{4}|----)) (\S+)$")
EVENT = re.compile(r"^event (\w+) ([0-9A-F]{6}) ([0-9A-F]{6}) (\d+) (\S+)$")
REFUSAL = re.compile(r"^refusal ([0-9A-F]{6}) (\w+) (\d+) (\S+)$")
STATE = re.compile(r"^state ([0-9A-F]{6}) (\w+) (.*)$")
OPEN = re.compile(r"^open ([0-9A-F]{6}) (\d+) (\S+)$")


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
        self.files = 0

    def read(self, path):
        self.files += 1
        run = path.parent.name + "/" + path.stem
        for line in path.read_text(errors="replace").splitlines():
            m = INSN.match(line)
            if m:
                kind, pc, length, count, routine, port = m.groups()
                pc = canonical(int(pc, 16))
                port = "-" if port == "-" else fmt(int(port, 16))
                self.insns[(kind, pc, routine_text(routine), port)] += int(count)
                self.length[pc] = max(self.length.get(pc, 0), int(length))
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
                self.open[(fmt(int(port, 16)), name)] += int(count)
                continue
            m = STATE.match(line)
            if m:
                entry, why, state = m.groups()
                key = (fmt(int(entry, 16)), why)
                state = "%s (%s)" % (state, run)
                if state not in self.states[key]:
                    self.states[key].append(state)

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
    return "%s of code, %d instructions in place of hand ports (%s); %d under comparisons still open" % (
        kib(nbytes), ninsns, parts, sum(m.open.values()))


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


# ---- the baseline ---------------------------------------------------------------------

LINE = re.compile(r"^([0-9A-F]{2}:(?:[0-9A-F]{4}|----)) (\d+) (\d+)\s*(?:#\s*(.*))?$")


def read_baseline(path):
    """routine -> (instructions, bytes, reason); and malformed lines."""
    rows, bad = {}, []
    if not path.exists():
        return rows, bad
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        m = LINE.match(line)
        if not m or not (m.group(4) or "").strip():
            bad.append("%s:%d: %s" % (path.name, number, line if m else "not `BB:AAAA instructions bytes # reason`"))
            continue
        routine, n, b, reason = m.groups()
        rows[routine] = (int(n), int(b), reason.strip())
    return rows, bad


def check(m, baseline_path, out):
    base, bad = read_baseline(baseline_path)
    now = m.routines()
    new, rose, gains = [], [], []
    for routine, row in sorted(now.items()):
        n, b = row[0], len(row[1])
        if routine not in base:
            new.append((routine, n, b, row))
            continue
        bn, bb, _ = base[routine]
        if b > bb or n > bn * (1 + INSTRUCTION_SLACK[0]) + INSTRUCTION_SLACK[1]:
            rose.append((routine, n, b, bn, bb, row))
        elif n < bn * GAIN or b < bb:
            gains.append(routine)
    gone = [r for r in base if r not in now]
    for line in bad:
        out.write("baseline: %s\n" % line)
    for routine, n, b, row in new:
        out.write("new: %s %d instructions, %d bytes: %s\n" % (routine, n, b, describe(m, routine, row)))
    for routine, n, b, bn, bb, row in rose:
        out.write("rose: %s %d instructions (baseline %d), %d bytes (baseline %d): %s\n"
                  % (routine, n, bn, b, bb, describe(m, routine, row)))
    for (port, name), n in sorted(m.open.items()):
        out.write("open: %s %s, %d instructions the original ran under its comparison, still open when a run ended\n"
                  % (port, name, n))
    summary = "%s; %d routine(s) new, %d rose, %d malformed" % (totals_line(m), len(new), len(rose), len(bad))
    if gains or gone:
        summary += "; %d fell and %d went (--update takes the gain)" % (len(gains), len(gone))
    out.write(summary + "\n")
    return not (new or rose or bad)


def update(m, baseline_path):
    base, _ = read_baseline(baseline_path)
    lines = []
    for routine, row in sorted(m.routines().items()):
        old = base.get(routine)
        reason = old[2] if old else describe(m, routine, row)
        lines.append("%s %d %d  # %s" % (routine, row[0], len(row[1]), reason))
    head = ("# The fallback measure's baseline (tools/fallbacks.py --check; docs/REIMPLEMENTATION.md, \"The fallback\n"
            "# measure\"): every routine whose code the reference play (the movies in shadow mode) still\n"
            "# runs as the original's in place of hand ports, as `routine instructions bytes # reason`.\n"
            "# A port that lands lowers these, and `--update` takes the gain; a new line, or more bytes\n"
            "# on one, is a new fallback, and needs its reason.\n")
    baseline_path.write_text(head + "\n".join(lines) + "\n", newline="")
    return len(lines)


# ---- refusals ---------------------------------------------------------------------------

ALLOW = re.compile(r"^([0-9A-F]{2}:?[0-9A-F]{4}) (\S+) (\w+)\s*(?:#\s*(.*))?$")


def read_allowlist(path):
    rows, bad = {}, []
    if not path.exists():
        return rows, bad
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        m = ALLOW.match(line)
        if not m or not (m.group(4) or "").strip():
            bad.append("%s:%d: %s" % (path.name, number, line if m else "not `BBAAAA name why # reason`"))
            continue
        addr = m.group(1).replace(":", "")
        rows[(fmt(int(addr, 16)), m.group(3))] = (m.group(2), m.group(4).strip())
    return rows, bad


def refusals(m, allow_path, out, verbose):
    allow, bad = read_allowlist(allow_path)
    failing = []
    by_sub = collections.Counter()
    for (entry, name, why), n in sorted(m.refusals.items(), key=lambda kv: -kv[1]):
        allowed = (entry, why) in allow
        by_sub[subsystem(int(entry.replace(":", ""), 16))] += 1
        if not allowed:
            failing.append((entry, name, why, n))
        if verbose or not allowed:
            out.write("%s %s %s %s: refused %d times%s\n"
                      % ("allowed" if allowed else "REFUSED", entry, name, why, n,
                         " (" + allow[(entry, why)][1] + ")" if allowed else ""))
            for state in m.states.get((entry, why), [])[:4]:
                out.write("    %s\n" % state)
    for line in bad:
        out.write("allowlist: %s\n" % line)
    seen = {(entry, why) for entry, _name, why in m.refusals}
    unused = [k for k in allow if k not in seen]
    total = sum(m.refusals.values())
    summary = ("%d refusal(s) in real play at %d entr%s, %d not in the allowlist"
               % (total, len(m.refusals), "y" if len(m.refusals) == 1 else "ies", len(failing)))
    if by_sub:
        summary += " (" + ", ".join("%s %d" % kv for kv in sorted(by_sub.items())) + ")"
    if unused:
        summary += "; %d allowlist line(s) not seen" % len(unused)
    out.write(summary + "\n")
    return not failing and not bad


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
    ap.add_argument("--check", action="store_true", help="the ratchet against the baseline")
    ap.add_argument("--refusals", action="store_true", help="refusals against the allowlist")
    ap.add_argument("--update", action="store_true", help="rewrite the baseline from these counts")
    ap.add_argument("--totals", action="store_true", help="one line: the totals by kind")
    ap.add_argument("--baseline", type=pathlib.Path, default=BASELINE)
    ap.add_argument("--allowlist", type=pathlib.Path, default=ALLOWLIST)
    ap.add_argument("-v", "--verbose", action="store_true", help="--refusals: the allowed ones too")
    args = ap.parse_args(argv)

    m = gather(args.paths)
    if not m.files:
        print("no fallback files under %s" % " ".join(args.paths))
        return 1
    if args.report:
        write_report(m, args.report)
    if args.update:
        print("%s: %d lines" % (args.baseline, update(m, args.baseline)))
        return 0
    if args.totals:
        print(totals_line(m))
        return 0
    if args.check or args.refusals:
        ok = True
        if args.check:
            ok &= check(m, args.baseline, sys.stdout)
        if args.refusals:
            ok &= refusals(m, args.allowlist, sys.stdout, args.verbose)
        return 0 if ok else 1
    print_totals(m, args.by, args.top, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
