"""Run the movie suite: every movie in shadow mode, then the gate's checks.

    python3 tools/suite.py --ct-dev build/main/ct_dev [--rom roms/ct-usa.sfc]
    python3 tools/suite.py --ct-dev build/main/ct_dev --only saves
    python3 tools/suite.py --ct-dev build/main/ct_dev --movies gato-battle field
    python3 tools/suite.py --ct-dev build/main/ct_dev --no-trace --no-timing -j 6

This is the validation gate (CLAUDE.md, "Validation"): every movie run with
`--native shadow` must exit 0 with no `mismatched [1-9]` line, no `page
refused` line (a guard that turned away a call whose direct page is no instance
re/ram.toml declares, which would otherwise show only as a drop in a shadow
count) and no `accessor out of range` line (a port that called an accessor with
an index past what re/ram.toml declares, reaching the next variable in work RAM,
which only the original's layout makes work; both in docs/DISASSEMBLY.md, "The
accessors"); then the checks
in CHECKS run, each printing one line: `--native on --recomp only` must produce
a trace identical to `--native off`; every hand port's estimated time must
stay within the timing gate's limits over the shadow runs' `--timing-report`
(tools/timing_gate.py); no video write in the shipping mode may land on the
picture where the reference's doesn't (tools/vblank_check.py); and the route
scripts must reach their outcomes in the shipping mode
(tools/movies/route_check.py; the fast route by default, all of them with
--long-routes); and every don't-care declaration in game/ must be one the
liveness analysis agrees with (tools/dontcare_check.py, static, with the call
logs the shadow runs just wrote); and no port may transliterate the assembly
where one didn't before (tools/portstyle.py, static, a ratchet against its
baseline); and no port's logic may read what some port writes only as
scaffolding where none did before (tools/effects_check.py, static and over the
vblank check's shipping-mode runs, a ratchet against its baseline); and no port
may carry one of the review rubric's mechanical smells where one didn't before
(tools/portsmell.py, static, a ratchet against its baseline). Exit status
is 0 only when all of that holds.

Each movie's frame count comes from its own `# frames N` header, and a movie
that starts from a cartridge save finds it by CRC-32 among the committed ones
(oracle/saves), so nothing here carries a list that can go stale.

Output goes under the build directory (`--out`, default build/suite): a log per
movie, a one-line status per movie, and the coverage and call files the shadow
runs collect, which are ROM-derived and stay out of the repository.
"""

import argparse
import concurrent.futures
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

if __package__ in (None, ""):
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from tools import timing_gate  # noqa: E402

MISMATCH = re.compile(rb"mismatched [1-9]")
REFUSED = re.compile(rb"^page refused: .*$", re.M)
OUT_OF_RANGE = re.compile(rb"^accessor out of range: .*$", re.M)
FRAMES = re.compile(r"^# frames (\d+)", re.M)

ROOT = pathlib.Path(__file__).resolve().parent.parent


def movie_frames(path):
    """The frame count a movie declares, or None."""
    m = FRAMES.search(path.read_text(errors="replace"))
    return int(m.group(1)) if m else None


def collect(only, names):
    """The movies to run, as (name, path, frames)."""
    groups = []
    if only in ("all", "core"):
        groups.append(sorted((ROOT / "oracle/movies").glob("*.ctm")))
    if only in ("all", "saves"):
        groups.append(sorted((ROOT / "oracle/movies/saves").glob("*.ctm")))
    out = []
    for paths in groups:
        for p in paths:
            if names and p.stem not in names:
                continue
            frames = movie_frames(p)
            if frames is None:
                print("%s: no '# frames N' header, skipped" % p.name, file=sys.stderr)
                continue
            out.append((p.stem, p, frames))
    return out


def run_movie(binary, rom, out, name, path, frames):
    """One shadow run. Returns (name, ok, detail)."""
    log = out / ("shadow-%s.log" % name)
    cmd = [str(binary), "--rom", str(rom), "--movie", str(path),
           "--frames", str(frames), "--native", "shadow", "--timing-report", "--page-report",
           "--coverage", str(out / ("cov-%s.txt" % name)),
           "--calls", str(out / ("calls-%s.txt" % name))]
    started = time.time()
    with open(log, "wb") as fh:
        rc = subprocess.call(cmd, stdout=fh, stderr=subprocess.STDOUT)
    text = log.read_bytes()
    bad = len(MISMATCH.findall(text))
    refused = [r.decode(errors="replace").strip() for r in REFUSED.findall(text)]
    strays = [r.decode(errors="replace").strip() for r in OUT_OF_RANGE.findall(text)]
    ok = rc == 0 and bad == 0 and not refused and not strays
    detail = "rc=%d mismatching=%d %.0fs" % (rc, bad, time.time() - started)
    if refused or strays:
        detail += " " + "; ".join(refused + strays)
    (out / ("status-%s.txt" % name)).write_text("%s %s\n" % (name, detail))
    return name, ok, detail


def trace_check(binary, rom, out, movie, frames):
    """`--recomp only` against `--native off` on one movie. Returns (ok, detail).

    The two runs are given their own copy of the binary so they can run at once
    on hosts that lock a running image.
    """
    movie = ROOT / "oracle/movies" / (movie + ".ctm")
    if not movie.exists():
        return True, "skipped (no %s)" % movie.name
    traces = {}
    procs = []
    for tag, extra in (("off", ["--native", "off"]),
                       ("only", ["--native", "on", "--recomp", "only"])):
        exe = out / ("ct_dev_%s%s" % (tag, binary.suffix))
        shutil.copy2(binary, exe)
        traces[tag] = out / ("%s.cttr" % tag)
        cmd = [str(exe), "--rom", str(rom), "--movie", str(movie),
               "--frames", str(frames), "--trace", str(traces[tag])] + extra
        with open(out / ("trace-%s.log" % tag), "wb") as fh:
            procs.append(subprocess.Popen(cmd, stdout=fh, stderr=subprocess.STDOUT))
    if any(p.wait() != 0 for p in procs):
        return False, "a trace run failed"
    diff = subprocess.run([sys.executable, "-m", "tools.trace", "diff",
                           str(traces["off"]), str(traces["only"])],
                          cwd=ROOT, capture_output=True, text=True)
    tail = (diff.stdout or diff.stderr).strip().splitlines()
    tail = tail[-1] if tail else "(no output)"
    return diff.returncode == 0 and "no divergence" in tail, tail


# ---- the checks after the shadow runs -------------------------------------------
#
# Each takes the parsed arguments and the context of the shadow runs and
# returns (ok, label, one-line summary, detail lines to print under it). The gate
# runs them in order after the shadow runs. Adding a check is a function here
# and a row in CHECKS, with a --no-<name> switch to skip it while iterating.

def check_trace(args, ctx):
    """`--recomp only` against `--native off` on one movie."""
    ok, detail = trace_check(ctx["binary"], args.rom, args.out, args.trace_movie, args.trace_frames)
    return ok, "trace identity (%s)" % args.trace_movie, detail, []


def check_timing(args, ctx):
    """The hand ports' estimated time against the original's (tools/timing_gate.py)."""
    agg = timing_gate.load(args.out / ("shadow-%s.log" % name) for name in ctx["movies"])
    if not agg:
        return False, "timing", "no --timing-report output in the shadow logs", []
    result = timing_gate.check(agg, timing_gate.read_exceptions(timing_gate.EXCEPTIONS))
    lines = timing_gate.failure_lines(result)
    if not ctx["complete"]:
        lines.append("note: measured over %d of the library's %d movies" % (len(ctx["movies"]), ctx["library"]))
    return not result.failures, "timing", timing_gate.summary(result), lines



def run_tool(cmd):
    """(exit status, output lines) of a check that is its own program."""
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return proc.returncode, (proc.stdout + proc.stderr).rstrip().splitlines()


def check_vblank(args, ctx):
    """Video writes in the shipping mode that land on the picture where the
    reference's don't (tools/vblank_check.py), over the same movies."""
    cmd = [sys.executable, str(ROOT / "tools/vblank_check.py"), "--ct-dev", str(ctx["binary"]),
           "--rom", str(args.rom), "--out", str(args.out / "vblank"), "--failing-only",
           "-j", str(ctx["jobs"]), "--only", args.only]
    if args.movies:
        cmd += ["--movies"] + args.movies
    rc, lines = run_tool(cmd)
    # its verdict is the "N failing finding(s)" line; progress goes to stderr
    verdict = [line for line in lines if "failing finding" in line]
    summary = verdict[-1] if verdict else (lines[-1] if lines else "no output")
    return rc == 0, "vblank", summary, [] if rc == 0 else lines[-40:]


# The routes the gate runs by default, and all of them for --long-routes: the
# ending routes are most of an hour of CPU, so they run on request. The fast
# set is a field route and the ending's approach to the second form, which is
# where a race in the original's wait for vblank hung the shipping mode once
# its timing moved (RECOMP.md, "The timing gate").
FAST_ROUTES = ["telepod_lavos:shell", "beyond_time:form2"]


def all_routes():
    """Every route in tools/movies/routes, whole rather than up to a phase."""
    return sorted(p.stem for p in (ROOT / "tools/movies/routes").glob("*.py")
                  if not p.stem.startswith("_"))


def check_routes(args, ctx):
    """The shipping mode end to end: state-driven routes reach their outcomes
    (tools/movies/route_check.py), which movies can't test once timing drifts."""
    drive = ctx["binary"].with_name("ct_drive" + ctx["binary"].suffix)
    if not drive.exists():
        return False, "routes", "%s: not found (build it first)" % drive, []
    cmd = [sys.executable, "-m", "tools.movies.route_check", "--drive", str(drive), "--rom", str(args.rom),
           "--mode", "on:on", "-j", str(min(ctx["jobs"], 2))]
    # --long-routes runs every route whole; route_check's own default is two of
    # them, so naming them here is what makes it more than the fast set.
    cmd += all_routes() if args.long_routes else FAST_ROUTES
    rc, lines = run_tool(cmd)
    passed = sum(1 for line in lines if re.match(r"^\S+\s+pass", line))
    failed = sum(1 for line in lines if re.match(r"^\S+\s+FAIL", line))
    summary = "%d of %d route(s) reach their outcome in the shipping mode" % (passed, passed + failed)
    return rc == 0 and failed == 0, "routes", summary, [] if rc == 0 else lines[-30:]


def check_dontcare(args, ctx):
    """Every don't-care declaration in game/ against liveness (tools/dontcare_check.py),
    the analysis fed the call and coverage logs the shadow runs wrote as well as
    the committed tree's defaults: more call sites only make it more careful."""
    cmd = [sys.executable, str(ROOT / "tools/dontcare_check.py"), "--rom", str(args.rom)]
    for name in ctx["movies"]:
        for kind, flag in (("calls", "--calls"), ("cov", "--coverage")):
            path = args.out / ("%s-%s.txt" % (kind, name))
            if path.exists():
                cmd += [flag, str(path)]
    rc, lines = run_tool(cmd)
    summary = lines[-1] if lines else "no output"
    return rc == 0, "dontcare", summary, [] if rc == 0 else lines[:-1][-40:]


def check_effects(args, ctx):
    """No port reads, as logic, what some port writes only as scaffolding
    (tools/effects_check.py, a ratchet against its baseline): in the source,
    and in the shipping-mode runs the vblank check just made, whose logs list
    the reads it caught (ct_dev --scaffold-reads)."""
    cmd = [sys.executable, str(ROOT / "tools/effects_check.py"), "--check"]
    logs = [args.out / "vblank" / ("%s-ship.log" % name) for name in ctx["movies"]]
    logs = [str(p) for p in logs if p.exists()]
    if logs:
        cmd += ["--runs"] + logs
    rc, lines = run_tool(cmd)
    summary = lines[-1] if lines else "no output"
    if not logs:
        summary += " (no shipping-mode logs: the source only)"
    return rc == 0, "effects", summary, [] if rc == 0 else lines[:-1][-40:]


def check_portstyle(args, ctx):
    """No port transliterates the assembly where one didn't before
    (tools/portstyle.py, static, a ratchet against its baseline)."""
    rc, lines = run_tool([sys.executable, str(ROOT / "tools/portstyle.py"), "--check"])
    summary = lines[-1] if lines else "no output"
    return rc == 0, "portstyle", summary, [] if rc == 0 else lines[:-1][-40:]


def check_portsmell(args, ctx):
    """No port carries one of the review rubric's mechanical smells where one
    didn't before (tools/portsmell.py, static, a ratchet against its baseline)."""
    rc, lines = run_tool([sys.executable, str(ROOT / "tools/portsmell.py"), "--check"])
    summary = lines[-1] if lines else "no output"
    return rc == 0, "portsmell", summary, [] if rc == 0 else lines[:-1][-40:]


def check_evidence(args, ctx):
    """With --evidence: the library runs the tools/v_readers.py dc-evidence
    notes cite, made again (tools/dc_evidence.py), so a note can't go stale.
    Three more runs of every movie, so it is asked for rather than run always."""
    if not args.evidence:
        return True, "evidence", "not run (--evidence runs it)", []
    cmd = [sys.executable, str(ROOT / "tools/dc_evidence.py"), "--ct-dev", str(ctx["binary"]),
           "--rom", str(args.rom), "-j", str(max(1, min(ctx["jobs"], 2)))]
    rc, lines = run_tool(cmd)
    summary = lines[-1] if lines else "no output"
    return rc == 0, "evidence", summary, [] if rc == 0 else [l for l in lines if "same:" not in l][-40:]


CHECKS = (
    # name, function, what --no-<name> skips
    ("trace", check_trace, "the --recomp only trace comparison"),
    ("timing", check_timing, "the timing gate over the shadow runs"),
    ("vblank", check_vblank, "the check for video writes on the picture in the shipping mode"),
    ("routes", check_routes, "the route scripts in the shipping mode"),
    ("dontcare", check_dontcare, "the don't-care declarations against liveness"),
    ("effects", check_effects, "the ratchet on logic that reads scaffolding"),
    ("portstyle", check_portstyle, "the ratchet on ports that transliterate the assembly"),
    ("portsmell", check_portsmell, "the ratchet on the review rubric's mechanical items"),
    ("evidence", check_evidence, "the dc-evidence notes' library runs (only run with --evidence)"),
)


def main():
    ap = argparse.ArgumentParser(prog="python3 tools/suite.py")
    ap.add_argument("--ct-dev", type=pathlib.Path, required=True, help="the ct_dev to run")
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "build/suite")
    ap.add_argument("--only", choices=("all", "core", "saves"), default="all")
    ap.add_argument("--movies", nargs="*", default=[], help="run only these, by name")
    ap.add_argument("-j", "--jobs", type=int, default=0,
                    help="concurrent movies (default: half the CPUs, at least 1)")
    ap.add_argument("--trace-movie", default="new-game")
    ap.add_argument("--trace-frames", type=int, default=7200)
    ap.add_argument("--long-routes", action="store_true",
                    help="run every route, the ending ones too (most of an hour), not only the fast one")
    ap.add_argument("--evidence", action="store_true",
                    help="make the library runs the dc-evidence notes cite again (tools/dc_evidence.py)")
    for name, _fn, what in CHECKS:
        ap.add_argument("--no-%s" % name, action="store_true", help="skip " + what)
    ap.add_argument("--no-trace-check", action="store_true", help=argparse.SUPPRESS)   # the old spelling
    args = ap.parse_args()

    binary = args.ct_dev.resolve()
    if not binary.exists():
        sys.exit("%s: not found (build it first)" % args.ct_dev)
    if not args.rom.exists():
        sys.exit("%s: not found (tools/rom.py stage <path>)" % args.rom)

    jobs = args.jobs or max(1, (os.cpu_count() or 2) // 2)
    args.out.mkdir(parents=True, exist_ok=True)
    for stale in args.out.glob("status-*.txt"):
        stale.unlink()

    movies = collect(args.only, set(args.movies))
    if not movies:
        sys.exit("no movies to run")
    # Longest first: the three endings are more than half the suite, and one
    # started late sets the wall time while the other workers sit idle.
    movies.sort(key=lambda m: -m[2])
    print("%d movies, %d at a time" % (len(movies), jobs), flush=True)

    bad_movies = []
    started = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(run_movie, binary, args.rom, args.out, *m) for m in movies]
        for done, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            name, ok, detail = fut.result()
            print("[%d/%d] %s %s%s" % (done, len(movies), name, detail,
                                       "" if ok else "  <-- FAILED"), flush=True)
            if not ok:
                bad_movies.append(name)

    print("%d of %d movies clean in %.0fs"
          % (len(movies) - len(bad_movies), len(movies), time.time() - started))

    args.no_trace |= args.no_trace_check
    library = len(collect("all", set()))
    ctx = {"binary": binary, "jobs": jobs, "movies": [m[0] for m in movies], "library": library,
           "complete": library == len(movies)}
    failed = ["movies: " + ", ".join(bad_movies)] if bad_movies else []
    for name, fn, _what in CHECKS:
        if getattr(args, "no_" + name):
            continue
        ok, label, summary, details = fn(args, ctx)
        print("%s: %s%s" % (label, summary, "" if ok else "  <-- FAILED"), flush=True)
        for line in details:
            print("  " + line)
        if not ok:
            failed.append(name)

    if failed:
        print("FAILED: " + "; ".join(failed))
        return 1
    print("suite OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
