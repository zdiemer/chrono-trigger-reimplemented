"""Hand ports shadow mode mostly doesn't compare, and the gate's ratchet on them.

    python3 tools/uncompared.py build/suite/shadow-*.log            (the entries, worst first)
    python3 tools/uncompared.py build/suite/shadow-*.log --check    (the ratchet, as tools/suite.py runs it)

A shadow run's report gives each registered entry's calls and what became of
them (runtime/port/native.c, native_report): matched, mismatched, and two kinds
that weren't compared -- `interrupted` (an interrupt fell in a stretch the port
stood for, whose handler ran on the original only) and `abandoned` (the entry
started inside another port's stretch and was still running at that port's
point, or its callee unwound past it). A port nothing compares is a gap the
gate can't see, however clean its mismatch count is. So this sums the counts
over the library's logs and lists every entry whose uncompared share is past
LIMIT over at least MIN_CALLS calls; --check fails on one that
tools/uncompared_baseline.txt doesn't list. A line there is the entry's name and,
after `#`, why its calls go uncompared and what checks them instead (an
enclosing port's comparison, fuzzing). Calls an idle entry or a guard skipped
are not the port's to compare and aren't counted.
"""

import argparse
import collections
import glob
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tools/uncompared_baseline.txt"
LIMIT = 0.5       # the share of an entry's calls left uncompared that needs a reason
MIN_CALLS = 100   # fewer calls than this over the library is too few to judge

LINE = re.compile(r"^\s+[0-9A-F]{6} (\w+)\s+calls (\d+)\s+matched (\d+)\s+mismatched (\d+)\s+interrupted (\d+)(.*)$")


def totals(paths):
    out = collections.defaultdict(collections.Counter)
    for pattern in paths:
        for path in glob.glob(str(pattern)):
            for line in open(path, encoding="utf-8", errors="replace"):
                mo = LINE.match(line)
                if not mo:
                    continue
                c = out[mo.group(1)]
                c["calls"] += int(mo.group(2))
                c["matched"] += int(mo.group(3))
                c["mismatched"] += int(mo.group(4))
                c["interrupted"] += int(mo.group(5))
                for kind in ("abandoned", "skipped"):
                    k = re.search(r"\b%s (\d+)" % kind, mo.group(6))
                    if k:
                        c[kind] += int(k.group(1))
    return out


def uncompared(c):
    return c["interrupted"] + c["abandoned"]


def flagged(tot):
    rows = []
    for name, c in tot.items():
        ran = c["calls"] - c["skipped"]
        if ran >= MIN_CALLS and uncompared(c) > LIMIT * ran:
            rows.append((uncompared(c) / ran, name, c, ran))
    return sorted(rows, reverse=True)


def read_baseline():
    if not BASELINE.exists():
        return {}
    out = {}
    for line in BASELINE.read_text(encoding="utf-8").splitlines():
        body, _, reason = line.partition("#")
        if body.strip() and not line.startswith("#"):
            out[body.strip()] = reason.strip()
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 tools/uncompared.py")
    ap.add_argument("logs", nargs="+", help="ct_dev --native shadow logs (globs)")
    ap.add_argument("--check", action="store_true", help="fail on an entry the baseline doesn't list")
    args = ap.parse_args(argv)
    rows = flagged(totals(args.logs))
    base = read_baseline()
    new = []
    for share, name, c, ran in rows:
        known = name in base
        if not known:
            new.append(name)
        if not args.check or not known:
            print("  %-48s %5.1f%% of %d calls uncompared (%d interrupted, %d abandoned)%s" %
                  (name, 100 * share, ran, c["interrupted"], c["abandoned"], "" if known else "  <-- not listed"))
    print("uncompared: %d entr%s past %d%% of %d+ calls, %d not listed in %s" %
          (len(rows), "y" if len(rows) == 1 else "ies", int(LIMIT * 100), MIN_CALLS, len(new),
           BASELINE.relative_to(ROOT).as_posix()))
    return 1 if args.check and new else 0


if __name__ == "__main__":
    sys.exit(main())
