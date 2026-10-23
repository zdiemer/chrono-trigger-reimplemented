"""The census of how the game is entered, over the movie library.

    python3 tools/census.py --ct-dev build/main/ct_dev                 # run every movie, summarize
    python3 tools/census.py --ct-dev build/main/ct_dev --movies menu field -j 2
    python3 tools/census.py --files out/census-*.txt                   # summarize files already written
    python3 tools/census.py --ct-dev build/main/ct_dev --entries       # every entry, not only the top

A shipped build has a `main()` that boots the game and callers that call
ports; today the emulated CPU walks the ROM and a port runs because the
runtime finds its address in game/registry.c (docs/PLAN.md, "The native
program, and the registry taken down"). This plays each library movie in the
shipping mode (`ct_dev --native on --generated on --census`, dev/census.h)
and adds up, per registered entry, how often the machine entered it -- and
how -- and how often a port ran its work as C. The count entered by the
machine is the number that item drives to 0.

How the machine got to an entry (dev/census.h): `vector` (an interrupt or the
reset), `generated` or `interp` (the original's code), `call`, `stop`,
`yield` or `return` (a port's hand-off through the machine: a call-out, a
stop or jump, a task switch, a return into the original's frame), and `wait`
(a pass of a wait loop the machine still makes, an idle entry). The report
names the callers that hold the most entries to the registry: the ports
whose hand-offs make the machine find an entry by address.
"""

import argparse
import collections
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHIPPING = ["--native", "on", "--generated", "on"]
FRAMES = re.compile(r"^# frames (\d+)", re.M)
KINDS = ("vector", "generated", "interp", "call", "stop", "yield", "return", "wait")


def movies(names):
    out = []
    for group in ("oracle/movies", "oracle/movies/saves"):
        for p in sorted((ROOT / group).glob("*.ctm")):
            if names and p.stem not in names:
                continue
            m = FRAMES.search(p.read_text(errors="replace"))
            if m:
                out.append((p.stem, p, int(m.group(1))))
    return out


def run(binary, rom, path, frames, out):
    cmd = [str(binary), "--rom", str(rom), "--movie", str(path), "--frames", str(frames), "--census", str(out)]
    proc = subprocess.run(cmd + SHIPPING, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                          timeout=600 + frames // 20)
    if proc.returncode != 0 or not out.exists():
        tail = proc.stderr.decode(errors="replace").strip().splitlines()[-3:]
        raise RuntimeError("%s: exit %d\n  %s" % (path.stem, proc.returncode, "\n  ".join(tail)))


class Census:
    def __init__(self):
        self.names = {}
        self.machine = collections.Counter()          # entry -> count
        self.by_kind = collections.Counter()          # (entry, kind) -> count
        self.held = collections.Counter()             # (entry, kind, caller) -> count
        self.as_c = collections.Counter()             # entry -> count
        self.movies = collections.defaultdict(set)    # entry -> movies entering it by the machine

    def read(self, path, movie):
        for line in pathlib.Path(path).read_text().splitlines():
            if not line or line.startswith("#"):
                continue
            what, addr, name, kind, caller, caller_name, n = line.split()
            entry, n = (addr, name), int(n)
            self.names[addr] = name
            if what == "c":
                self.as_c[entry] += n
                continue
            self.machine[entry] += n
            self.by_kind[entry, kind] += n
            self.held[entry, kind, (caller, caller_name)] += n
            self.movies[entry].add(movie)

    def entered(self, waits):
        return {e: n for e, n in self.machine.items()
                if waits or sum(self.by_kind[e, k] for k in KINDS if k != "wait") > 0}

    def report(self, out, all_entries, top):
        machine = self.entered(waits=True)
        not_waits = self.entered(waits=False)
        total = sum(self.by_kind.values())
        kinds = collections.Counter()
        for (_, k), n in self.by_kind.items():
            kinds[k] += n
        only_c = [e for e in self.as_c if e not in machine]
        print("census over the movies: %d entries entered by the machine (%d of them only as wait-loop passes), "
              "%d times; %d entries only run as C" % (len(machine), len(machine) - len(not_waits), total,
                                                      len(only_c)), file=out)
        print("  by how: " + ", ".join("%s %d" % (k, kinds[k]) for k in KINDS if kinds[k]), file=out)

        # Callers: the ports (or addresses) whose hand-offs make the machine
        # find entries, by how many distinct entries (wait-loop passes aside).
        holds = collections.defaultdict(set)
        counts = collections.Counter()
        for (entry, kind, caller), n in self.held.items():
            if kind == "wait":
                continue
            key = caller if kind not in ("vector", "generated", "interp") else ("(%s)" % kind, caller[0])
            holds[key].add(entry)
            counts[key] += n
        print("\ncallers holding entries to the registry (wait-loop passes aside): entries held, times", file=out)
        for key in sorted(holds, key=lambda k: (-len(holds[k]), -counts[k]))[:top]:
            label = "%s %s" % (key[0], key[1]) if key[0].startswith("(") else "%s %s" % (key[1], key[0])
            print("  %-56s %5d %10d" % (label, len(holds[key]), counts[key]), file=out)

        waits = collections.Counter({e: self.by_kind[e, "wait"] for e in machine if self.by_kind[e, "wait"]})
        print("\nwait loops still made on the machine: %d, passes %d" % (len(waits), sum(waits.values())), file=out)
        for e, n in waits.most_common(top):
            print("  %s %-48s %10d" % (e[0], e[1], n), file=out)

        rows = sorted(not_waits, key=lambda e: -self.machine[e])
        print("\nentries entered by the machine, most first: count, as C, movies, how (top caller)", file=out)
        for e in rows if all_entries else rows[:top]:
            hows = ", ".join("%s %d" % (k, self.by_kind[e, k]) for k in KINDS if self.by_kind[e, k])
            callers = [(n, c) for (en, k, c), n in self.held.items() if en == e and k != "wait"]
            n, c = max(callers) if callers else (0, ("-", "-"))
            print("  %s %-44s %9d %8d %3d  %s  (%s %s)" % (e[0], e[1], self.machine[e], self.as_c.get(e, 0),
                                                           len(self.movies[e]), hows, c[1], c[0]), file=out)


def main():
    ap = argparse.ArgumentParser(prog="python3 tools/census.py")
    ap.add_argument("--ct-dev", type=pathlib.Path)
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--movies", nargs="*", default=[])
    ap.add_argument("--files", nargs="*", type=pathlib.Path, help="census files already written, one per movie")
    ap.add_argument("-j", "--jobs", type=int, default=2)
    ap.add_argument("--keep", type=pathlib.Path, help="keep each movie's census file here")
    ap.add_argument("--entries", action="store_true", help="list every entry the machine enters")
    ap.add_argument("--top", type=int, default=25)
    args = ap.parse_args()

    census = Census()
    if args.files:
        for f in args.files:
            census.read(f, f.stem)
    else:
        if not args.ct_dev:
            sys.exit("--ct-dev or --files")
        rows = movies(set(args.movies))
        if not rows:
            sys.exit("no movies to run")
        with tempfile.TemporaryDirectory(prefix="census-") as scratch:
            where = args.keep or pathlib.Path(scratch)
            where.mkdir(parents=True, exist_ok=True)

            def one(row):
                name, path, frames = row
                out = where / ("census-%s.txt" % name)
                run(args.ct_dev.resolve(), args.rom, path, frames, out)
                return name, out

            with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
                for name, out in pool.map(one, rows):
                    census.read(out, name)
    census.report(sys.stdout, args.entries, args.top)


if __name__ == "__main__":
    main()
