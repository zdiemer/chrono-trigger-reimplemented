"""Fuzz a rewritten port and the port it replaces with the same seed, and compare.

    python3 tools/fuzz_compare.py --before <old ct_fuzz> --after build/main/ct_fuzz \\
        --ct-dev build/main/ct_dev --subsystem scene [--routine NAME]... [--seed 1] \\
        [--iterations N] [-j 3] [--markdown]

A rewrite can reorder memory accesses in a way no movie exposes -- a read hoisted
out of a loop moves before a write that used to precede it, which differs only
when a buffer overlaps the cell being read. Shadow mode over the whole library
can't see it; comparing the fuzz counts of the rewrite and its predecessor can
(docs/PORTING.md, "Fuzzing"). An absolute count means little on its own, since
the predecessor may have residual mismatches of its own: what matters is whether
the rewrite's are the same ones. So this runs both `ct_fuzz` binaries on every
routine with the same state, seed and constraints and reports the two counts
side by side, and exits non-zero when any routine's mismatch count differs.

`--before` is a `ct_fuzz` built from the tree before the rewrite (a scratch build
of `main`, outside the repo); `--after` is this tree's. Both must register the
routine under the same name.

The routines and their constraints are in tools/fuzz_routines.toml, next to
this file, so each subsystem adds its entries once and every later rewrite of
it reuses them:

    [state.attract]                     # a base state, made on first use
    movie = "oracle/movies/new-game.ctm"
    frames = 300

    [[routine]]
    name = "scene_run_sequence"
    subsystem = "scene"
    state = "attract"
    args = ["--d", "0A00", "--db", "7E", "--keep", "7E3000-7EB5FF"]
    iterations = 20000                  # optional; --iterations caps it
    why = "the program kept; the script pointer in the program"

A rewrite that renames an entry gives its address, `at = "C0:6788"`, which
both builds are run on (ct_fuzz takes a routine by name or by address).

A routine can be listed again with `variant = "loose"` and fewer constraints:
the tight run says the port is right on what the game produces, and the loose
one is where a reordered access shows, since it lets buffers and cursors land on
each other (the accessors pilot's reordering showed only with the table pointer
left free). Both are compared; `--variant` picks one.

States are saved with `--ct-dev` (`--native off`) under `--states` (default
traces/fuzz/, which is gitignored) the first time a routine needs one; a save
movie finds its cartridge save by the CRC in its header, as ct_dev always does.
At most `-j` fuzz processes run at once (two per routine, one per binary), so
`-j 3` is three heavy processes.
"""
import argparse
import concurrent.futures
import fnmatch
import pathlib
import re
import subprocess
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parent.parent
ROUTINES = pathlib.Path(__file__).resolve().parent / "fuzz_routines.toml"

RESULT = re.compile(
    r"^(?P<name>\w+): (?P<iterations>\d+) iterations, matched (?P<matched>\d+), "
    r"mismatched (?P<mismatched>\d+) \(the port looping where the original returns (?P<looped>\d+)\), "
    r"skipped (?P<skipped>\d+) \(guard (?P<guard>\d+)", re.M)


def load(path=ROUTINES):
    with open(path, "rb") as fh:
        data = tomllib.load(fh)
    states = data.get("state", {})
    routines = data.get("routine", [])
    for r in routines:
        if r.get("state") not in states:
            raise SystemExit("%s: routine %s names state %r, which isn't defined"
                             % (path.name, r.get("name"), r.get("state")))
    return states, routines


def label(r):
    return r["name"] + (" [%s]" % r["variant"] if r.get("variant") else "")


def select(routines, subsystems, names, variant=None):
    out = []
    for r in routines:
        if variant is not None and r.get("variant", "") != variant:
            continue
        if subsystems and r.get("subsystem") not in subsystems:
            continue
        if names and not any(fnmatch.fnmatchcase(r["name"], n) for n in names):
            continue
        out.append(r)
    return out


def make_state(ct_dev, rom, states_dir, name, spec):
    path = states_dir / ("%s.state" % name)
    if path.exists():
        return path
    states_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(ct_dev), "--rom", str(rom), "--movie", str(ROOT / spec["movie"]),
           "--frames", str(spec["frames"]), "--native", "off", "--save-state", str(path)]
    print("making state %s: %s" % (name, " ".join(cmd)), file=sys.stderr)
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    return path


def fuzz(binary, rom, state, routine, seed, iterations):
    entry = routine["at"].replace(":", "") if "at" in routine else routine["name"]
    cmd = [str(binary), "--rom", str(rom), "--load-state", str(state), "--routine", entry,
           "--seed", str(seed), "--iterations", str(iterations), "--details", "0"]
    cmd += [str(a) for a in routine.get("args", [])]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    m = RESULT.search(p.stdout)
    if not m:
        return {"error": (p.stdout.strip().splitlines() or ["exit %d" % p.returncode])[-1]}
    return {k: (v if k == "name" else int(v)) for k, v in m.groupdict().items()}


def verdict(before, after):
    if "error" in before or "error" in after:
        return "ERROR"
    if before["mismatched"] != after["mismatched"]:
        return "DIFFER"
    if before["matched"] != after["matched"]:
        return "same mismatches, compared differs"
    return "same"


def cell(res):
    if "error" in res:
        return "error: " + res["error"]
    return "%d/%d mismatched (%d skipped, %d guard)" % (res["mismatched"], res["matched"] + res["mismatched"],
                                                       res["skipped"], res["guard"])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--before", required=True, help="ct_fuzz built from the tree before the rewrite")
    ap.add_argument("--after", required=True, help="ct_fuzz built from this tree")
    ap.add_argument("--ct-dev", help="ct_dev, to make base states that don't exist yet")
    ap.add_argument("--rom", default=str(ROOT / "roms" / "ct-usa.sfc"))
    ap.add_argument("--states", default=str(ROOT / "traces" / "fuzz"), help="where base states are kept")
    ap.add_argument("--routines", default=str(ROUTINES), help="the constraints file")
    ap.add_argument("--subsystem", action="append", help="only this subsystem's routines (repeatable)")
    ap.add_argument("--routine", action="append", help="only routines matching this name or glob (repeatable)")
    ap.add_argument("--variant", help="only this variant ('' for the plain entries, 'loose', ...)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--iterations", type=int, help="at most this many states per routine")
    ap.add_argument("-j", "--jobs", type=int, default=3, help="fuzz processes at once")
    ap.add_argument("--markdown", action="store_true", help="print the table as Markdown")
    args = ap.parse_args(argv)
    for k in ("before", "after", "ct_dev", "rom"):
        if getattr(args, k):
            setattr(args, k, str(pathlib.Path(getattr(args, k)).resolve()))

    states, routines = load(pathlib.Path(args.routines))
    chosen = select(routines, args.subsystem, args.routine, args.variant)
    if not chosen:
        print("no routines selected", file=sys.stderr)
        return 2
    states_dir = pathlib.Path(args.states)
    paths = {}
    for name in sorted({r["state"] for r in chosen}):
        path = states_dir / ("%s.state" % name)
        if not path.exists():
            if not args.ct_dev:
                print("state %s doesn't exist yet; pass --ct-dev to make it" % path, file=sys.stderr)
                return 2
            make_state(args.ct_dev, args.rom, states_dir, name, states[name])
        paths[name] = path

    jobs = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(args.jobs, 1)) as pool:
        for r in chosen:
            n = r.get("iterations", 20000)
            if args.iterations:
                n = min(n, args.iterations)
            b = pool.submit(fuzz, args.before, args.rom, paths[r["state"]], r, args.seed, n)
            a = pool.submit(fuzz, args.after, args.rom, paths[r["state"]], r, args.seed, n)
            jobs.append((r, n, b, a))
        rows = [(r, n, b.result(), a.result()) for r, n, b, a in jobs]

    bad = 0
    if args.markdown:
        print("| Routine | States | Before | After | |")
        print("|---|--:|---|---|---|")
    for r, n, before, after in rows:
        v = verdict(before, after)
        bad += v in ("DIFFER", "ERROR")
        if args.markdown:
            print("| `%s` | %d | %s | %s | %s |" % (label(r), n, cell(before), cell(after), v))
        else:
            print("%-52s %6d  before %-38s after %-38s %s" % (label(r), n, cell(before), cell(after), v))
    print("fuzz_compare: %d routine%s, %d differ%s (seed %d)"
          % (len(rows), "" if len(rows) == 1 else "s", bad, "" if bad == 1 else "", args.seed))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
