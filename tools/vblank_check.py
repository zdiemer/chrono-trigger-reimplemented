"""Find video register writes that the shipping mode lands on the picture.

    python3 tools/vblank_check.py --ct-dev build/main/ct_dev [--only core|saves] [--movies NAME...] [-j 2]
    python3 tools/vblank_check.py --no-run [--movies NAME...]      (compare the logs already in --out)
    python3 tools/vblank_check.py --compare REF.txt SHIP.txt [--blocks build/main/generated]

Runs each movie twice with `ct_dev --io-log`: under `--native on --generated only`
(the reference: generated code, which times like the original) and under
`--native on --generated on` (the shipping mode: hand ports where they exist).
The log counts every write to the video registers ($2100-$2133 and HDMAEN
$420C, transfer bytes included) by who made it and when it landed: on a
vblank line, on a picture line in forced blank, or on a picture line with the
display on (dev/io_log.c; docs/REIMPLEMENTATION.md, "Where video writes land").

The two runs desync after a few dozen frames, so this is not a frame-by-frame
diff. It compares, per routine and register group, *when* each run writes: a
finding is something the shipping mode does that the same routine never does
in the reference run of the same movie. Failing categories:

- `visible`: writes with the display on, on a picture line, where the
  reference routine makes none. VRAM and OAM data written then is lost (ppu.c
  drops it); anything else changes mid-picture.
- `unblank`: the write that ends forced blank (with a nonzero brightness)
  landing on a picture line the reference routine never ends it on: every line
  above it was black.
- `lines`: writes with the display on, on picture lines on which the reference
  routine never writes that group, where it does write it on some: a raster
  effect that moved.

Reported without failing:

- `late`: other writes in forced blank on a picture line, where the reference
  routine makes none: the routine ran late, and the screen was blank anyway
  (what that cost shows up as `unblank`, in whichever routine ends the blank).
- `unmatched`: picture-line writes by a routine the reference never ran (the
  shipping run went somewhere else).

A routine is a hand port (keyed by its entry address; its region of the ROM
runs from the entry to its last service call site, and generated code there is
the same routine) or, outside every port's region, the generated block
containing the address (`--blocks`, the build's generated C).

The shipping run also takes `--scaffold-reads`, so its log (`<movie>-ship.log`)
lists the ports' reads of work RAM whose latest write was scaffolding, which
tools/effects_check.py --runs checks; this tool ignores them.

Exit status is 0 only when every failing finding is listed in the exceptions
file (tools/vblank_check_exceptions.txt: `routine group category reason...`).
Logs go under --out (default build/vblank): they name ROM addresses and stay
out of the repository.
"""

import argparse
import bisect
import collections
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.suite import collect  # noqa: E402

EXCEPTIONS = ROOT / "tools/vblank_check_exceptions.txt"

PORT_KINDS = ("port-service", "port-direct", "idle-service", "idle")
FAILING = ("visible", "unblank", "lines")
CATEGORIES = FAILING + ("late", "unmatched")
FIELDS = 17   # columns of an --io-log row

# The data ports whose writes the PPU drops with the display on: OAM and VRAM.
DATA_PORTS = (0x2104, 0x2118, 0x2119)
# Lines the picture shows: line 0 is drawn but not displayed.
FIRST_SHOWN, LAST_SHOWN = 1, 224

# Past a hand port's last service call site the reference's generated code for
# it can still be running: a call stands for a run of instructions, and a
# generated block's address is where it started or last branched.
REGION_SLACK = 0x20

GROUPS = (
    (0x2100, 0x2100, "display"),     # INIDISP: forced blank and brightness
    (0x2101, 0x2104, "oam"),
    (0x2105, 0x2114, "bg"),          # mode, tile maps, scroll
    (0x2115, 0x2119, "vram"),
    (0x211A, 0x2120, "mode7"),
    (0x2121, 0x2122, "cgram"),
    (0x2123, 0x2133, "screens"),     # windows, layers, color math, SETINI
    (0x420C, 0x420C, "hdma-enable"),
)


def video_group(addr):
    for lo, hi, name in GROUPS:
        if lo <= addr <= hi:
            return name
    return None


def parse_addr(text):
    """'C0:1234' -> 0xC01234, or None for '-'."""
    if text == "-":
        return None
    bank, off = text.split(":")
    return int(bank, 16) << 16 | int(off, 16)


def fmt_addr(a):
    return "$%02X:%04X" % (a >> 16, a & 0xFFFF)


def parse_lines(text):
    out = collections.Counter()
    if text != "-":
        for item in text.split(","):
            ln, n = item.split(":")
            out[int(ln)] = int(n)
    return out


Row = collections.namedtuple("Row", "kind origin site service addr entry times line_min line_max visible unblank")


def split_row(line):
    """The fields of a table row; an origin name may contain spaces."""
    f = line.split()
    if len(f) < FIELDS:
        return None
    tail = FIELDS - 2
    return [f[0], " ".join(f[1:len(f) - tail])] + f[len(f) - tail:]


def read_rows(path):
    with open(path) as fh:
        for line in fh:
            if line.startswith("#"):
                continue
            f = split_row(line)
            if f is None:
                raise SystemExit("%s: not an --io-log table with times (rebuild ct_dev)" % path)
            yield f


def read_log(path):
    """The rows of a `ct_dev --io-log` table that write video registers (HDMA's excepted)."""
    rows = []
    for f in read_rows(path):
        kind, origin, site, service, reg, _group, access = f[:7]
        addr = int(reg[1:], 16)
        if access != "write" or kind == "hdma" or video_group(addr) is None:
            continue
        lo = hi = None
        if f[13] != "-":
            lo, hi = (int(x) for x in f[13].split("-"))
        rows.append(Row(kind, origin, parse_addr(site), service, addr, parse_addr(f[9]),
                        (int(f[10]), int(f[11]), int(f[12])), lo, hi, parse_lines(f[15]), parse_lines(f[16])))
    return rows


class Routines:
    """Maps a write to the routine that made it, the same way in both runs."""

    def __init__(self, ship_logs, block_starts=()):
        names = collections.defaultdict(set)
        end = {}
        for path in ship_logs:
            for f in read_rows(path):
                if f[0] not in PORT_KINDS or f[9] == "-":
                    continue
                entry, site = parse_addr(f[9]), parse_addr(f[2])
                names[entry].add(f[1])
                last = site if site is not None and site >> 16 == entry >> 16 and site > entry else entry
                end[entry] = max(end.get(entry, entry), last)
        self.names = {e: "/".join(sorted(n)) for e, n in names.items()}
        self.regions = sorted((e, end[e] + REGION_SLACK) for e in names)
        self.region_starts = [r[0] for r in self.regions]
        self.blocks = sorted(set(block_starts))

    def of_code(self, pc):
        """The routine a generated block or interpreted instruction at `pc` belongs to."""
        i = bisect.bisect_right(self.region_starts, pc)
        while i > 0:   # the nearest entry at or below whose region reaches pc
            start, stop = self.regions[i - 1]
            if start >> 16 != pc >> 16:
                break
            if pc <= stop:
                return ("port", start)
            i -= 1
        i = bisect.bisect_right(self.blocks, pc)
        if i and self.blocks[i - 1] >> 16 == pc >> 16:
            return ("block", self.blocks[i - 1])
        return ("block", pc)

    def of_row(self, row):
        if row.kind in PORT_KINDS:
            return ("port", row.entry)
        return self.of_code(row.site)

    def label(self, key):
        kind, addr = key
        if kind == "port":
            return "%s (%s)" % (self.names.get(addr, "?"), fmt_addr(addr))
        return "block %s" % fmt_addr(addr)

    def ids(self, key):
        """What an exception can name the routine by."""
        kind, addr = key
        out = {fmt_addr(addr), fmt_addr(addr)[1:]}
        if kind == "port":
            out.update(self.names.get(addr, "").split("/"))
        return out


def block_starts(generated_dir):
    """Entry addresses of the generated blocks, from the build's generated C."""
    starts = set()
    pat = re.compile(r"^void generated_([0-9A-F]{6})_", re.M)
    for path in sorted(pathlib.Path(generated_dir).glob("blocks_*.c")):
        starts.update(int(m.group(1), 16) for m in pat.finditer(path.read_text(errors="replace")))
    return starts


class Stat:
    """One routine's writes to one register group."""

    def __init__(self):
        self.times = [0, 0, 0]   # vblank, forced blank on a picture line, visible
        self.visible = collections.Counter()
        self.unblank = collections.Counter()
        self.line_min = self.line_max = None
        self.registers = set()
        self.services = set()

    def merge(self, other):
        for i in range(3):
            self.times[i] += other.times[i]
        self.visible.update(other.visible)
        self.unblank.update(other.unblank)
        if other.line_min is not None:
            self.line_min = other.line_min if self.line_min is None else min(self.line_min, other.line_min)
            self.line_max = other.line_max if self.line_max is None else max(self.line_max, other.line_max)
        self.registers |= other.registers
        self.services |= other.services

    def add(self, row):
        s = Stat()
        s.times = list(row.times)
        # The log counts by the first line a write can change. A data port
        # write with the display on is lost wherever it lands; any other
        # write is harmless if every shown line already sees it.
        if row.addr in DATA_PORTS:
            s.visible = row.visible
        else:
            s.visible = collections.Counter({ln: n for ln, n in row.visible.items()
                                             if FIRST_SHOWN < ln <= LAST_SHOWN})
        s.unblank = collections.Counter({ln: n for ln, n in row.unblank.items() if ln > FIRST_SHOWN})
        s.line_min, s.line_max = row.line_min, row.line_max
        s.registers = {row.addr}
        s.services = {row.service} if row.service != "-" else set()
        self.merge(s)

    @property
    def total(self):
        return sum(self.times)

    @property
    def picture(self):
        return self.times[1] + self.times[2]


def tally(rows, routines):
    out = collections.defaultdict(Stat)
    for row in rows:
        out[(routines.of_row(row), video_group(row.addr))].add(row)
    return out


Finding = collections.namedtuple("Finding", "category routine group count lines ship ref")


def elsewhere(ship_lines, ref_lines):
    """The shipping run's counts on lines the reference's never are."""
    return collections.Counter({ln: n for ln, n in ship_lines.items() if ln not in ref_lines})


def compare(ref_rows, ship_rows, routines):
    """The findings for one movie's pair of logs."""
    ref = tally(ref_rows, routines)
    ship = tally(ship_rows, routines)
    out = []
    for (routine, group), s in sorted(ship.items()):
        if not s.picture:
            continue
        r = ref.get((routine, group))
        if r is None or not r.total:
            out.append(Finding("unmatched", routine, group, s.picture, None, s, None))
            continue
        damage = sum(s.visible.values())
        if damage and not sum(r.visible.values()):
            out.append(Finding("visible", routine, group, damage, s.visible, s, r))
        elif damage:
            moved = elsewhere(s.visible, r.visible)
            if moved:
                out.append(Finding("lines", routine, group, sum(moved.values()), moved, s, r))
        moved = elsewhere(s.unblank, r.unblank)
        if moved:
            out.append(Finding("unblank", routine, group, sum(moved.values()), moved, s, r))
        if s.times[1] and not r.times[1]:
            out.append(Finding("late", routine, group, s.times[1], None, s, r))
    return out


def read_exceptions(path):
    """{(routine, group, category)}: a routine by port name or address, '*' for any group."""
    out = set()
    if not path or not pathlib.Path(path).exists():
        return out
    for line in pathlib.Path(path).read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        f = line.split(None, 3)
        if len(f) < 4:
            raise SystemExit("%s: '%s' needs a routine, group, category and reason" % (path, line))
        if f[2] not in FAILING:
            raise SystemExit("%s: %s is not a failing category (%s)" % (path, f[2], ", ".join(FAILING)))
        out.add((f[0], f[1], f[2]))
    return out


def excepted(category, routine, group, routines, exceptions):
    return any((rid, g, category) in exceptions for rid in routines.ids(routine) for g in (group, "*"))


def compact_lines(counter, limit=8):
    """'line: count' for the lines with the most writes, then how many other lines."""
    items = sorted(counter.items(), key=lambda kv: (-kv[1], kv[0]))
    text = ", ".join("%d: %d" % kv for kv in items[:limit])
    if len(items) > limit:
        text += " and %d on %d other lines" % (sum(n for _ln, n in items[limit:]), len(items) - limit)
    return text


def report(per_movie, routines, exceptions, out=sys.stdout, show_info=True):
    """Aggregates findings over movies and prints them. Returns the number that fail."""
    agg = {}
    for movie, findings in per_movie:
        for f in findings:
            a = agg.setdefault((f.category, f.routine, f.group),
                               {"count": 0, "lines": collections.Counter(), "movies": [],
                                "ship": Stat(), "ref": Stat()})
            a["count"] += f.count
            if f.lines:
                a["lines"].update(f.lines)
            a["movies"].append(movie)
            a["ship"].merge(f.ship)
            if f.ref:
                a["ref"].merge(f.ref)
    failing = 0
    order = {c: i for i, c in enumerate(CATEGORIES)}
    for (category, routine, group), a in sorted(agg.items(), key=lambda kv: (order[kv[0][0]], -kv[1]["count"])):
        if category not in FAILING:
            status = "info"
        elif excepted(category, routine, group, routines, exceptions):
            status = "known"
        else:
            status = "FAIL"
            failing += 1
        if status == "info" and not show_info:
            continue
        s, r = a["ship"], a["ref"]
        where = ("on lines " + compact_lines(a["lines"])) if a["lines"] else (
            "lines %d-%d" % (s.line_min, s.line_max) if s.line_min is not None else "")
        print("%-5s %-9s %s, %s: %d writes %s" % (status, category, routines.label(routine), group, a["count"], where),
              file=out)
        print("      registers %s%s" % (" ".join("$%04X" % x for x in sorted(s.registers)),
                                        (", through " + " ".join(sorted(s.services))) if s.services else ""), file=out)
        print("      shipping: vblank %d, forced blank %d, visible %d" % tuple(s.times), file=out)
        if r.total:
            extra = ""
            if category == "lines":
                extra = "; visible on lines " + compact_lines(r.visible)
            elif category == "unblank" and r.unblank:
                extra = "; ends forced blank on lines " + compact_lines(r.unblank)
            print("      reference: vblank %d, forced blank %d, visible %d%s" % (tuple(r.times) + (extra,)), file=out)
        print("      in %s" % ", ".join(a["movies"]), file=out)
    return failing


def run_one(binary, rom, out, name, path, frames, mode):
    # The shipping mode's run also notes the ports' reads of scaffolding, which
    # tools/effects_check.py --runs reads from its log.
    extra = {"ref": ["--generated", "only"], "ship": ["--generated", "on", "--scaffold-reads"]}[mode]
    cmd = [str(binary), "--rom", str(rom), "--movie", str(path), "--frames", str(frames),
           "--native", "on", "--io-log", str(out / ("%s-%s.txt" % (name, mode)))] + extra
    started = time.time()
    with open(out / ("%s-%s.log" % (name, mode)), "wb") as fh:
        rc = subprocess.call(cmd, stdout=fh, stderr=subprocess.STDOUT)
    return name, mode, rc, time.time() - started


def run_movies(binary, rom, out, movies, jobs):
    tasks = sorted(((m, mode) for m in movies for mode in ("ref", "ship")), key=lambda t: -t[0][2])
    bad = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        futures = [pool.submit(run_one, binary, rom, out, *m, mode) for m, mode in tasks]
        for done, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            name, mode, rc, secs = fut.result()
            print("[%d/%d] %s %s rc=%d %.0fs" % (done, len(tasks), name, mode, rc, secs), file=sys.stderr, flush=True)
            if rc != 0:
                bad.append("%s (%s)" % (name, mode))
    return bad


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 tools/vblank_check.py")
    ap.add_argument("--ct-dev", type=pathlib.Path, help="the ct_dev to run")
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "build/vblank")
    ap.add_argument("--only", choices=("all", "core", "saves"), default="all")
    ap.add_argument("--movies", nargs="*", default=[], help="run only these, by name")
    ap.add_argument("-j", "--jobs", type=int, default=2, help="concurrent ct_dev runs (default 2)")
    ap.add_argument("--no-run", action="store_true", help="compare the logs already in --out")
    ap.add_argument("--compare", nargs=2, metavar=("REF", "SHIP"), type=pathlib.Path,
                    help="compare two --io-log tables instead of running movies")
    ap.add_argument("--blocks", type=pathlib.Path, help="the build's generated C (default: generated/ beside --ct-dev)")
    ap.add_argument("--exceptions", type=pathlib.Path, default=EXCEPTIONS)
    ap.add_argument("--failing-only", action="store_true", help="leave out the late and unmatched findings")
    args = ap.parse_args(argv)

    if args.compare:
        pairs = [("(compare)", args.compare[0], args.compare[1])]
    else:
        if not args.ct_dev and not args.no_run:
            ap.error("--ct-dev is required (or --compare, or --no-run)")
        movies = collect(args.only, set(args.movies))
        if not movies:
            sys.exit("no movies to run")
        args.out.mkdir(parents=True, exist_ok=True)
        if not args.no_run:
            binary = args.ct_dev.resolve()
            if not binary.exists() and binary.with_suffix(".exe").exists():
                binary = binary.with_suffix(".exe")
            if not binary.exists():
                sys.exit("%s: not found (build it first)" % args.ct_dev)
            if not args.rom.exists():
                sys.exit("%s: not found (tools/rom.py stage <path>)" % args.rom)
            bad = run_movies(binary, args.rom, args.out, movies, args.jobs)
            if bad:
                sys.exit("ct_dev failed: " + ", ".join(bad))
        pairs = [(name, args.out / ("%s-ref.txt" % name), args.out / ("%s-ship.txt" % name))
                 for name, _path, _frames in movies]
        missing = [str(p) for _n, a, b in pairs for p in (a, b) if not p.exists()]
        if missing:
            sys.exit("missing logs: " + ", ".join(missing))

    blocks_dir = args.blocks
    if blocks_dir is None and args.ct_dev:
        blocks_dir = args.ct_dev.resolve().parent / "generated"
    starts = block_starts(blocks_dir) if blocks_dir and blocks_dir.is_dir() else ()
    if not starts:
        # Without the build's generated blocks, generated code is keyed by its own
        # address instead of its block's, so findings split differently and the
        # exceptions (keyed by block) stop matching: the verdict would silently
        # change. A ct_dev copied away from its build directory loses them.
        sys.exit("no generated blocks found at %s: pass --blocks <build>/generated (the build's "
                 "generated C), or run the ct_dev in its own build directory" % blocks_dir)
    routines = Routines([ship for _n, _r, ship in pairs], starts)
    per_movie = [(name, compare(read_log(ref), read_log(ship), routines)) for name, ref, ship in pairs]
    failing = report(per_movie, routines, read_exceptions(args.exceptions), show_info=not args.failing_only)
    print("%d failing finding(s) not in %s" % (failing, args.exceptions.name))
    return 1 if failing else 0


if __name__ == "__main__":
    sys.exit(main())
