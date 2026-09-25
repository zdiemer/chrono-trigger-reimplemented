"""The timing gate: bound each hand port's estimated time against the original's.

    python3 tools/timing_gate.py build/suite/shadow-*.log            (check, as tools/suite.py does)
    python3 tools/timing_gate.py build/suite/shadow-*.log --table    (every entry's numbers)
    python3 tools/timing_gate.py build/suite/shadow-*.log --calibrate game/registry.c

A hand port charges its time as an estimate: its DMA transfers' time as it is,
and `cost_milli`/1000 times the rest of its own bus clocks (docs/PORTING.md).
`ct_dev --native shadow --timing-report` measures that estimate against the
original, which runs on the interpreter in shadow mode, over every stretch of a
port no interrupt fell in (runtime/port/native.c, end_segment): the totals of
the original's time, the port's bus clocks and their DMA part, the total
absolute error, the largest, a histogram of the error by bit length, and the
same for the stretches that began in vblank. This reads those reports from the
movie library's shadow logs, sums them per entry over the movies, and checks
each entry against four limits, each tied to the budget an error could break
(docs/RECOMP.md, "The timing gate"):

- drift: the estimate's systematic error over the library, as a share of the
  entry's time (what one scalar fixes, and what --calibrate does fix);
- vblank-tail: of the stretches that began in vblank, the share off by three
  quarters of a scanline or more;
- vblank-worst: the largest error of a stretch that began in vblank;
- worst: the largest error of a stretch that began on a picture line.

An entry that exceeds a limit fails unless tools/timing_exceptions.txt lists
it for that limit with a reason. Exceptions that no longer exceed their limit
are reported, so the list can't go stale silently. Drift and vblank-tail need
MIN_STRETCHES stretches to judge; with fewer, only the worst cases count.

A port that runs other routines' work inline (a dispatcher's handlers as C
calls) attributes the bus clocks it spends on each to that routine
(native_time_as, runtime/port/native.h), and the report lists them as parts:
the estimate is then each part's clocks times its routine's own `cost_milli`,
plus the rest times the entry's. The routines are entries of their own, which
shadow mode measures and calibrates on their own calls. An attribution to an
address with no hand port fails the check.

--calibrate rewrites each measured entry's `cost_milli` in game/registry.c
to the value the totals imply: the original's time over the port's bus clocks,
DMA's time (the same in both) left out of both, and for an entry with parts,
the parts' estimates (at their routines' calibrated values) left out too.
"""

import argparse
import collections
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXCEPTIONS = ROOT / "tools/timing_exceptions.txt"

# The machine's budgets, in master clocks (runtime/snes/system.h).
CLOCKS_PER_LINE = 1364
LINES_PER_FRAME = 262
VBLANK_LINE = 225
VBLANK_CLOCKS = (LINES_PER_FRAME - VBLANK_LINE) * CLOCKS_PER_LINE   # 37 lines, 50468
CLOCKS_PER_FRAME = CLOCKS_PER_LINE * LINES_PER_FRAME

# The limits (docs/RECOMP.md, "The timing gate").
#
# Drift: a systematic error accumulates over every call in a frame, so it is
# bounded by what it does to a whole frame of work: if every clock of a frame
# were this entry's, a drift this large would move the end of the frame's work
# by one scanline.
DRIFT_LIMIT = CLOCKS_PER_LINE / CLOCKS_PER_FRAME        # 0.38%
# vblank-tail: in vblank the NMI handler does the frame's video work, and an
# error moves the handler's later writes. An error of a scanline moves them a
# line nearer the picture. The histogram counts by bit length, so "a
# scanline" is errors of 11 bits and more: 1024 clocks (three quarters of a
# line) and up. At most 1 in 100 of an entry's vblank stretches may reach it.
TAIL_BITS = CLOCKS_PER_LINE.bit_length() - 1             # errors >= 2^10
TAIL_LIMIT = 0.01
# vblank-worst: no stretch in vblank may be off by more than a quarter of it:
# a spike that size takes a quarter of the handler's time for video work.
VBLANK_WORST_LIMIT = VBLANK_CLOCKS // 4                  # 12617 clocks, 9.25 lines
# worst: on a picture line the error moves the main loop's work within its
# frame. Interrupts are taken on time whatever the error (native.c, paying
# debt), and the vblank check finds raster writes that moved, so what an
# error there can break is the frame: an error longer than vblank can push
# work that ended before the NMI past it, and the frame's update slips a frame.
WORST_LIMIT = VBLANK_CLOCKS                              # 50468 clocks, 37 lines
MIN_STRETCHES = 100

LIMITS = ("drift", "vblank-tail", "vblank-worst", "worst")

ENTRY = re.compile(r"^  ([0-9A-F]{6}) (\S+)\s+calls \d+")
STRETCHES = re.compile(r"^    timing: stretches (\d+)  estimate exact (\d+)  .*max \|error\| (\d+)")
TOTALS = re.compile(r"^    timing: totals original (\d+) native-bus (\d+) dma (\d+) \|error\| (\d+) cost_milli (\d+)"
                    r"  error bits ([\d ]+)$")
VBLANK = re.compile(r"^    timing: vblank stretches (\d+)  max \|error\| (\d+)  picture max \|error\| (\d+)"
                    r"  vblank error bits ([\d ]+)$")
PART = re.compile(r"^    timing: part ([0-9A-F]{6}) (\S+) native-bus (\d+) dma (\d+) cost_milli (\d+)$")
BAD_TAGS = re.compile(r"^    timing: attributions to no hand port (\d+) \(last ([0-9A-F]{6})\)"
                      r"  past the parts' room (\d+)$")


class Part:
    """A port's bus clocks attributed to another routine, and that routine's cost_milli."""

    def __init__(self, name, addr, bus, dma, cost_milli):
        self.name, self.addr, self.bus, self.dma, self.cost_milli = name, addr, bus, dma, cost_milli

    def estimate(self, cost_milli=None):
        return self.dma + (self.bus - self.dma) * (self.cost_milli if cost_milli is None else cost_milli) / 1000.0


def add_hist(a, b):
    if len(b) > len(a):
        a += [0] * (len(b) - len(a))
    for k, n in enumerate(b):
        a[k] += n


class Entry:
    """One entry's timing measurement, from one movie or summed over several."""

    def __init__(self, name, addr):
        self.name, self.addr = name, addr
        self.stretches = self.exact = self.max_error = 0
        self.original = self.bus = self.dma = self.error = 0
        self.vblank_stretches = self.vblank_max = self.picture_max = 0
        self.cost_milli = None
        self.hist = []
        self.vblank_hist = []
        self.movies = 0
        self.parts = {}                 # name: Part
        self.bad_tags = self.part_overflows = 0
        self.bad_tag = None

    def add(self, other):
        # A report whose every stretch of the entry was interrupted has no
        # totals line, so no cost_milli to compare.
        if self.cost_milli is not None and other.cost_milli is not None and other.cost_milli != self.cost_milli:
            raise ValueError("%s: cost_milli %d in one report and %d in another (mixed builds)"
                             % (self.name, self.cost_milli, other.cost_milli))
        if other.cost_milli is not None:
            self.cost_milli = other.cost_milli
        for f in ("stretches", "exact", "original", "bus", "dma", "error", "vblank_stretches", "movies"):
            setattr(self, f, getattr(self, f) + getattr(other, f))
        for f in ("max_error", "vblank_max", "picture_max"):
            setattr(self, f, max(getattr(self, f), getattr(other, f)))
        add_hist(self.hist, other.hist)
        add_hist(self.vblank_hist, other.vblank_hist)
        for name, q in other.parts.items():
            mine = self.parts.get(name)
            if mine is None:
                self.parts[name] = Part(name, q.addr, q.bus, q.dma, q.cost_milli)
                continue
            if mine.cost_milli != q.cost_milli:
                raise ValueError("%s: part %s at cost_milli %d in one report and %d in another (mixed builds)"
                                 % (self.name, name, mine.cost_milli, q.cost_milli))
            mine.bus += q.bus
            mine.dma += q.dma
        self.bad_tags += other.bad_tags
        self.part_overflows += other.part_overflows
        self.bad_tag = other.bad_tag or self.bad_tag

    @property
    def mean_time(self):
        return self.original / self.stretches if self.stretches else 0.0

    @property
    def mean_error(self):
        return self.error / self.stretches if self.stretches else 0.0

    @property
    def relative_error(self):
        """Mean absolute error as a share of the mean time."""
        return self.error / self.original if self.original else 0.0

    @property
    def own_bus(self):
        """The port's bus clocks not attributed to another routine, and their DMA part."""
        return self.bus - sum(q.bus for q in self.parts.values())

    @property
    def own_dma(self):
        return self.dma - sum(q.dma for q in self.parts.values())

    @property
    def estimate(self):
        """The estimate's total: DMA's time as it is, cost_milli times the rest of
        the port's own bus clocks, and each part's clocks times its routine's."""
        return (self.own_dma + (self.own_bus - self.own_dma) * self.cost_milli / 1000.0
                + sum(q.estimate() for q in self.parts.values()))

    @property
    def drift(self):
        """The estimate's total over the original's, minus one: its systematic error."""
        if not self.original:
            return 0.0
        return (self.estimate - self.original) / self.original

    @property
    def calibrated(self):
        """The cost_milli the totals imply: the original's time over the port's
        bus clocks, DMA's time (the same in both) left out; the parts' estimates
        at their routines' present cost_milli left out too."""
        return self.calibrated_with({})

    def calibrated_with(self, part_costs):
        """As calibrated, with the parts' routines at part_costs[name] where given."""
        own_bus, own_dma = self.own_bus, self.own_dma
        if own_bus <= own_dma:
            return self.cost_milli
        rest = self.original - own_dma - sum(q.estimate(part_costs.get(name)) for name, q in self.parts.items())
        return max(1, round(1000.0 * rest / (own_bus - own_dma)))

    @property
    def measured(self):
        """Whether the port has bus clocks of its own to calibrate."""
        return self.own_bus > self.own_dma

    def tail(self):
        """The share of all stretches off by 2^TAIL_BITS clocks or more."""
        return sum(self.hist[TAIL_BITS + 1:]) / self.stretches if self.stretches else 0.0

    def vblank_tail(self):
        """The share of the vblank stretches off by 2^TAIL_BITS clocks or more."""
        if not self.vblank_stretches:
            return 0.0
        return sum(self.vblank_hist[TAIL_BITS + 1:]) / self.vblank_stretches


def parse_report(text):
    """{name: Entry} from a ct_dev log with --timing-report output."""
    out = {}
    cur = None
    for line in text.splitlines():
        m = ENTRY.match(line)
        if m:
            cur = Entry(m.group(2), int(m.group(1), 16))
            continue
        if cur is None:
            continue
        m = STRETCHES.match(line)
        if m:
            cur.stretches, cur.exact, cur.max_error = (int(x) for x in m.groups())
            continue
        m = TOTALS.match(line)
        if m:
            cur.original, cur.bus, cur.dma, cur.error, cur.cost_milli = (int(x) for x in m.groups()[:5])
            cur.hist = [int(x) for x in m.group(6).split()]
            cur.movies = 1
            out[cur.name] = cur
            continue
        m = VBLANK.match(line)
        if m:
            cur.vblank_stretches, cur.vblank_max, cur.picture_max = (int(x) for x in m.groups()[:3])
            cur.vblank_hist = [int(x) for x in m.group(4).split()]
            continue
        m = PART.match(line)
        if m:
            addr, name, bus, dma, cost = m.groups()
            cur.parts[name] = Part(name, int(addr, 16), int(bus), int(dma), int(cost))
            continue
        m = BAD_TAGS.match(line)
        if m:
            cur.bad_tags, cur.part_overflows = int(m.group(1)), int(m.group(3))
            cur.bad_tag = int(m.group(2), 16)
            out.setdefault(cur.name, cur)
    return out


def aggregate(reports):
    """Sum per-movie {name: Entry} reports into one."""
    out = {}
    for rep in reports:
        for name, e in rep.items():
            if name not in out:
                out[name] = Entry(name, e.addr)
            out[name].add(e)
    # an entry no report measured a stretch of (all interrupted) has nothing
    # to judge
    return {name: e for name, e in out.items() if e.cost_milli is not None}


def load(paths):
    return aggregate(parse_report(pathlib.Path(p).read_text(errors="replace")) for p in paths)


def read_exceptions(path):
    """{(name, limit): reason} from the exceptions file."""
    out = {}
    if not path or not pathlib.Path(path).exists():
        return out
    for n, line in enumerate(pathlib.Path(path).read_text().splitlines(), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        f = line.split(None, 2)
        if len(f) < 3:
            raise SystemExit("%s:%d: needs an entry, a limit and a reason" % (path, n))
        if f[1] not in LIMITS:
            raise SystemExit("%s:%d: %s is not a limit (%s)" % (path, n, f[1], ", ".join(LIMITS)))
        out[(f[0], f[1])] = f[2]
    return out


def exceeded(e):
    """The limits entry e exceeds, as {limit: (value, bound)}."""
    out = {}
    if e.stretches >= MIN_STRETCHES and abs(e.drift) > DRIFT_LIMIT:
        out["drift"] = (e.drift, DRIFT_LIMIT)
    if e.vblank_stretches >= MIN_STRETCHES and e.vblank_tail() > TAIL_LIMIT:
        out["vblank-tail"] = (e.vblank_tail(), TAIL_LIMIT)
    if e.vblank_max > VBLANK_WORST_LIMIT:
        out["vblank-worst"] = (e.vblank_max, VBLANK_WORST_LIMIT)
    if e.picture_max > WORST_LIMIT:
        out["worst"] = (e.picture_max, WORST_LIMIT)
    return out


Result = collections.namedtuple("Result", "entries failures known stale")

# Not a limit but a failure all the same, which no exception covers: an entry
# attributed time to an address with no hand port, or to more routines than a
# stretch holds, so part of its estimate used its own cost_milli instead.
ATTRIBUTION = "attribution"


def check(agg, exceptions):
    """Judge every entry. failures and known are [(Entry, limit, value, bound)];
    stale is [(name, limit)]: exceptions for limits the entry no longer exceeds."""
    failures, known = [], []
    hit = set()
    for name in sorted(agg):
        e = agg[name]
        if e.bad_tags or e.part_overflows:
            failures.append((e, ATTRIBUTION, e.bad_tags + e.part_overflows, 0))
        for limit, (value, bound) in exceeded(e).items():
            if (name, limit) in exceptions:
                known.append((e, limit, value, bound))
                hit.add((name, limit))
            else:
                failures.append((e, limit, value, bound))
    stale = sorted(k for k in exceptions if k not in hit and k[0] in agg)
    return Result(agg, failures, known, stale)


def fmt_value(limit, value, bound=False):
    if limit == "drift":
        return ("+/-%.2f%%" if bound else "%+.2f%%") % (100 * abs(value) if bound else 100 * value)
    if limit == "vblank-tail":
        return "%.2f%%" % (100 * value)
    return "%d clocks" % value


def describe(e):
    return ("%d stretches in %d movie(s), mean time %.0f, mean |error| %.0f (%.1f%%), drift %+.2f%%, max %d; "
            "in vblank %d, off by >= %d clocks %.2f%%, max %d; cost_milli %d, calibrated %d"
            % (e.stretches, e.movies, e.mean_time, e.mean_error, 100 * e.relative_error, 100 * e.drift,
               e.picture_max, e.vblank_stretches, 1 << TAIL_BITS, 100 * e.vblank_tail(), e.vblank_max,
               e.cost_milli, e.calibrated))


def failure_lines(result, exceptions_name=EXCEPTIONS.name):
    """What tools/suite.py prints under the summary: every failure, then stale exceptions."""
    lines = []
    for e, limit, value, bound in result.failures:
        if limit == ATTRIBUTION:
            if e.bad_tags:
                lines.append("%s attributed time to $%02X:%04X %d time(s), where no hand port is registered "
                             "(native_time_as)" % (e.name, e.bad_tag >> 16, e.bad_tag & 0xFFFF, e.bad_tags))
            if e.part_overflows:
                lines.append("%s attributed time to more routines than a stretch holds, %d time(s) "
                             "(MAX_SEGMENT_PARTS, runtime/port/native.c)" % (e.name, e.part_overflows))
            continue
        lines.append("%s over the %s limit: %s (limit %s)"
                     % (e.name, limit, fmt_value(limit, value), fmt_value(limit, bound, bound=True)))
        lines.append("    " + describe(e))
    for name, limit in result.stale:
        lines.append("note: %s is listed for %s in %s but is within it now" % (name, limit, exceptions_name))
    return lines


def summary(result):
    """One line for tools/suite.py."""
    n = len(result.entries)
    stretches = sum(e.stretches for e in result.entries.values())
    if result.failures:
        return "%d of %d entries over the limits" % (len({f[0].name for f in result.failures}), n)
    return "%d entries, %d stretches within the limits (%d known exceptions)" % (n, stretches, len(result.known))


def table(agg, out=sys.stdout):
    total = sum(e.original for e in agg.values()) or 1
    print("%-44s %9s %7s %8s %7s %8s %6s %8s %6s %6s %6s %6s" % (
        "entry", "stretches", "time%", "mean", "|err|", "drift", "vbl%", "vbl-tail", "vbl-max", "pic-max",
        "cost", "calib"), file=out)
    for e in sorted(agg.values(), key=lambda e: -e.original):
        over = exceeded(e)
        print("%-44s %9d %6.2f%% %8.0f %6.1f%% %+7.2f%% %5.1f%% %7.2f%% %7d %7d %6d %6d%s" % (
            e.name, e.stretches, 100 * e.original / total, e.mean_time, 100 * e.relative_error, 100 * e.drift,
            100 * e.vblank_stretches / max(e.stretches, 1), 100 * e.vblank_tail(), e.vblank_max, e.picture_max,
            e.cost_milli, e.calibrated, "  " + ",".join(over) if over else ""), file=out)


def distribution(agg, out=sys.stdout):
    """The library-wide error, weighted by stretch and by time."""
    n = sum(e.stretches for e in agg.values())
    orig = sum(e.original for e in agg.values())
    err = sum(e.error for e in agg.values())
    exact = sum(e.exact for e in agg.values())
    signed = sum(e.estimate - e.original for e in agg.values())
    tail = sum(sum(e.hist[TAIL_BITS + 1:]) for e in agg.values())
    vbl = sum(e.vblank_stretches for e in agg.values())
    vtail = sum(sum(e.vblank_hist[TAIL_BITS + 1:]) for e in agg.values())
    over = collections.Counter(limit for e in agg.values() for limit in exceeded(e))
    print("%d entries, %d stretches: exact %.1f%%, mean |error| %.1f clocks (%.2f%% of time), net drift %+.3f%%, "
          "off by >= %d clocks %.3f%% (in vblank: %d stretches, %.3f%%); over %s"
          % (len(agg), n, 100.0 * exact / max(n, 1), err / max(n, 1), 100.0 * err / max(orig, 1),
             100.0 * signed / max(orig, 1), 1 << TAIL_BITS, 100.0 * tail / max(n, 1), vbl,
             100.0 * vtail / max(vbl, 1), ", ".join("%s %d" % (lim, over[lim]) for lim in LIMITS)), file=out)


def calibrated_values(agg):
    """{name: calibrated cost_milli} for every measured entry. An entry's parts
    (the routines it attributes time to) are estimated at their own calibrated
    values, so those are worked out first."""
    out = {}
    busy = set()

    def value(name):
        if name in out:
            return out[name]
        e = agg[name]
        busy.add(name)
        costs = {p: value(p) for p in e.parts if p in agg and agg[p].measured and p not in busy}
        busy.discard(name)
        out[name] = e.calibrated_with(costs)
        return out[name]

    for name in agg:
        if agg[name].measured:
            value(name)
    return out


def calibrate(agg, registry):
    """Rewrite each measured entry's cost_milli in registry to its calibrated
    value. Entries are either `{ .addr = 0x..., .name = "n", ..., .cost_milli
    = N, ... }` or a table macro's `MACRO(0x..., n, N)`, which may take more
    arguments after N (event's guard). Returns the changes,
    [(name, old, new)]; an entry measured but not found is an error."""
    text = pathlib.Path(registry).read_text()
    changes = []
    seen = set()
    values = calibrated_values(agg)

    def measured(name):
        e = agg.get(name)
        return e is not None and e.measured

    def new_value(name, old):
        seen.add(name)
        if not measured(name):
            return old
        new = values[name]
        if new != old:
            changes.append((name, old, new))
        return new

    def fix_struct(m):
        body = m.group(0)
        name = re.search(r'\.name = "([^"]+)"', body).group(1)
        cm = re.search(r"\.cost_milli = (\d+)", body)
        if cm is None:
            if measured(name):
                raise SystemExit("%s: measured but has no .cost_milli in %s" % (name, registry))
            return body
        return body[:cm.start(1)] + str(new_value(name, int(cm.group(1)))) + body[cm.end(1):]

    def fix_macro(m):
        return "%s%d%s)" % (m.group(1), new_value(m.group(2), int(m.group(3))), m.group(4) or "")

    text = re.sub(r"\{ \.addr = 0x[0-9A-F]+,.*?\}(?=,)", fix_struct, text, flags=re.S)
    text = re.sub(r"(^    [A-Z_]+\(0x[0-9A-F]+, (\w+), )(\d+)((?:, \w+)*)\)", fix_macro, text, flags=re.M)
    missing = sorted(n for n in agg if measured(n) and n not in seen)
    if missing:
        raise SystemExit("measured but not found in %s: %s" % (registry, ", ".join(missing)))
    pathlib.Path(registry).write_text(text, newline="\n")
    return changes


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 tools/timing_gate.py")
    ap.add_argument("logs", nargs="+", type=pathlib.Path, help="ct_dev --native shadow --timing-report logs")
    ap.add_argument("--exceptions", type=pathlib.Path, default=EXCEPTIONS)
    ap.add_argument("--table", action="store_true", help="print every entry's numbers")
    ap.add_argument("--calibrate", type=pathlib.Path, metavar="REGISTRY",
                    help="rewrite cost_milli in this registry from the measurement")
    args = ap.parse_args(argv)

    agg = load(args.logs)
    if not agg:
        sys.exit("no --timing-report output in the logs")
    if args.table:
        table(agg)
    distribution(agg)
    if args.calibrate:
        for name, old, new in calibrate(agg, args.calibrate):
            print("  %s: %d -> %d" % (name, old, new))
        return 0
    result = check(agg, read_exceptions(args.exceptions))
    for line in failure_lines(result, args.exceptions.name):
        print("  " + line)
    print("timing: " + summary(result))
    return 1 if result.failures else 0


if __name__ == "__main__":
    sys.exit(main())
