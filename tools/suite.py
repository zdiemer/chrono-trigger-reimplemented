"""Run the movie suite: every movie in shadow mode, then the gate's checks.

    python3 tools/suite.py --ct-dev build/main/ct_dev [--rom roms/ct-usa.sfc]
    python3 tools/suite.py --ct-dev build/main/ct_dev --all-movies
    python3 tools/suite.py --ct-dev build/main/ct_dev --only saves
    python3 tools/suite.py --ct-dev build/main/ct_dev --movies gato-battle field
    python3 tools/suite.py --ct-dev build/main/ct_dev --no-trace --no-timing -j 6

This is the validation gate (CLAUDE.md, "Validation"). It runs the gate's
sample of the movie library, tools/gate_movies.txt, by default, and the whole
library with --all-movies (see "The sample" below). Every movie run with
`--native shadow` must exit 0 with no `mismatched [1-9]` line, no `page
refused` line (a guard that turned away a call whose direct page is no instance
re/ram.toml declares, which would otherwise show only as a drop in a shadow
count) and no `accessor out of range` line (a port that called an accessor with
an index past what re/ram.toml declares, reaching the next variable in work RAM,
which only the original's layout makes work; both in docs/DISASSEMBLY.md, "The
accessors") and no `asset outside the catalogue` line (a cartridge read that
lands in no resource of re/assets.tsv, which the asset pack wouldn't hold:
docs/PLATFORM.md, "The assets, as built"). The ports read their resources from
an asset pack the suite imports from the ROM first (`ct_dev --assets`), as
ct_play does, and a read whose pack byte isn't the cartridge's prints an
`asset differs from the cartridge` line, which fails the run too; then the checks
in CHECKS run, each printing one line: `--native on --generated only` must produce
a trace identical to `--native off`; every hand port's estimated time must
stay within the timing gate's limits over the shadow runs' `--timing-report`
(tools/timing_gate.py); no video write in the shipping mode may land on the
picture where the reference's doesn't (tools/vblank_check.py); and the route
scripts must reach their outcomes in the shipping mode, with the C sound driver
that ships (tools/movies/route_check.py; the fast route by default, all of them
with --long-routes); and the C sound driver must match the original's at every
tick of every movie, register for register and field for field (`ct_dev
--sound-lockstep --sound-lockstep-level state`, run inside the shadow runs, where
the original's timing is kept: docs/SOUND.md, "How it was checked"), and over a
short ct_sound_fuzz in both its modes, against the original and in the SPC700's
place, where the native mixer must make the S-DSP's every frame; and every
don't-care declaration in game/ must be one the liveness analysis agrees with
(tools/dontcare_check.py, static, with the call logs the shadow runs just
wrote); and no port may transliterate the assembly
where one didn't before (tools/portstyle.py, static, a ratchet against its
baseline); and no port's logic may read what some port writes only as
scaffolding where none did before (tools/effects_check.py, static and over the
vblank check's shipping-mode runs, a ratchet against its baseline); and no port
may carry one of the review rubric's mechanical smells where one didn't before
(tools/portsmell.py, static, a ratchet against its baseline); and, over every
movie's reference play (the shadow runs, `ct_dev --fallbacks`), no new place
where the original's code runs in place of a hand port, nor more of it at a
known one (tools/fallbacks.py --check, a ratchet against its baseline; the
shipping mode's count over the movies and the routes is reported beside it),
and no hand port's guard refusing a call real play makes -- in the shadow runs,
the movies in the shipping mode or the routes -- unless its allowlist says why
(tools/fallbacks.py --refusals); and the build with the ports' scaffolding
compiled out (ct_dev_native_shape, ct_drive_native_shape) running every movie
to its end in the shipping mode with nothing the default build's run doesn't
show -- a failed exit, a hang, a halted machine, an accessor out of range --
and its routes reaching their outcomes (the fast set; all of them with
--long-routes); and every row of the catalogue of resources still being a
region the matching build extracts (tools/assets.py check); and no movie stopping earlier than it did on the hand ports
alone, with no generated code and no interpreter, nor any reaching its end
other than as the reference play does (tools/strict_check.py, a ratchet
against tools/strict_baseline.txt); and every movie in the native program
(ct_native, the game with no emulator) neither crashing nor ending on a dark
screen, where each stops and whether it ends as the reference play does being
reported (tools/native_check.py). Exit status is 0 only when all of
that holds.

The sample. The library grows with long movies made to find code and the
places ct_native stops, and replaying all of it at every commit costs hours.
tools/gate_movies.txt names the movies the gate runs by default, one per line
with a `#` reason: a greedy cover of the library's shadow coverage, most code
bytes per second of play first, plus what the checks need (the trace
identity's movie, power-on and save movies, each subsystem, the strict
baseline's `end` movies that matter). Every check then runs over those
movies, and the ratchets judge only what the sample can show: a baseline line
for a movie or routine the sample doesn't reach is neither dropped nor
reported as gone, and the timing gate judges only its worst cases (maxima,
which a subset can only lower): its drift and vblank tail are means over the
library the costs are calibrated to, which a subset's mix of scenes moves, so
those and its stale exceptions are judged over the whole library only.
--all-movies is the gate as it was before the sample:
run it before merging a change to timing or to the runtime, and for every
recalibration and baseline update (timing_gate.py --calibrate, fallbacks.py
--update, strict_check.py --update, effects_check.py --prune), which take
their numbers from the whole library's runs. --movies names a set by hand.

Each movie's frame count comes from its own `# frames N` header, and a movie
that starts from a cartridge save finds it by CRC-32 among the committed ones
(oracle/saves), so nothing here carries a list that can go stale.

Output goes under the build directory (`--out`, default build/suite): a log per
movie, a one-line status per movie, and the coverage and call files the shadow
runs collect and their end states (end-<movie>.bin, the strict check's reference),
which are ROM-derived and stay out of the repository.
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
ASSETS = re.compile(rb"^asset (?:outside the catalogue|differs from the cartridge): .*$", re.M)
FRAMES = re.compile(r"^# frames (\d+)", re.M)

ROOT = pathlib.Path(__file__).resolve().parent.parent


def movie_frames(path):
    """The frame count a movie declares, or None."""
    m = FRAMES.search(path.read_text(errors="replace"))
    return int(m.group(1)) if m else None


GATE_MOVIES = ROOT / "tools/gate_movies.txt"


def gate_movies():
    """The names tools/gate_movies.txt lists (`name  # reason` a line), and its
    lines that are malformed or name no movie in the library."""
    names, bad = [], []
    library = {p.stem for p in ROOT.glob("oracle/movies/**/*.ctm")}
    for number, line in enumerate(GATE_MOVIES.read_text().splitlines(), 1):
        body, _, reason = line.partition("#")
        words = body.split()
        if not words:
            continue
        if len(words) != 1 or not reason.strip():
            bad.append("%s:%d: not `name  # reason`" % (GATE_MOVIES.name, number))
        elif words[0] not in library:
            bad.append("%s:%d: %s is no movie in the library" % (GATE_MOVIES.name, number, words[0]))
        else:
            names.append(words[0])
    return names, bad


def collect(only, names):
    """The movies to run, as (name, path, frames)."""
    groups = []
    if only in ("all", "core"):
        groups.append(sorted((ROOT / "oracle/movies").glob("*.ctm")))
    if only in ("all", "saves"):
        groups.append(sorted((ROOT / "oracle/movies/saves").glob("*.ctm")))
        # Long hand runs are opt-in, even when the full library is selected.
        if names:
            groups.append(sorted((ROOT / "oracle/movies/hand").glob("*.ctm")))
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


# The asset pack the ports read their resources from, imported by main().
PACK_ARGS = []


def import_assets(binary, rom, pack):
    """A fresh asset pack at `pack`, imported from the ROM by ct_dev, which the
    shadow and shipping-mode runs read from (ct_dev --assets)."""
    if pack.exists():
        pack.unlink()
    rc = subprocess.call([str(binary), "--rom", str(rom), "--assets", str(pack), "--frames", "1"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    if rc != 0 or not pack.exists():
        sys.exit("%s: ct_dev couldn't import the asset pack" % pack)
    PACK_ARGS[:] = ["--assets", str(pack)]


def run_movie(binary, rom, out, name, path, frames, lockstep):
    """One shadow run, with the sound lockstep beside it unless `lockstep` is
    false (it adds about a twentieth to the run). Returns (name, ok, detail)."""
    log = out / ("shadow-%s.log" % name)
    cmd = [str(binary), "--rom", str(rom), "--movie", str(path),
           "--frames", str(frames), "--native", "shadow", "--timing-report", "--page-report",
           "--coverage", str(out / ("cov-%s.txt" % name)),
           "--calls", str(out / ("calls-%s.txt" % name)),
           "--fallbacks", str(out / "fallbacks" / "shadow" / ("%s.txt" % name)),
           "--end-state", str(out / ("end-%s.bin" % name))] + PACK_ARGS
    if lockstep:
        cmd += ["--sound-lockstep", "--sound-lockstep-level", "state"]
    started = time.time()
    with open(log, "wb") as fh:
        rc = subprocess.call(cmd, stdout=fh, stderr=subprocess.STDOUT)
    text = log.read_bytes()
    bad = len(MISMATCH.findall(text))
    refused = [r.decode(errors="replace").strip() for r in REFUSED.findall(text)]
    strays = [r.decode(errors="replace").strip() for r in OUT_OF_RANGE.findall(text) + ASSETS.findall(text)]
    ok = rc == 0 and bad == 0 and not refused and not strays
    detail = "rc=%d mismatching=%d %.0fs" % (rc, bad, time.time() - started)
    if refused or strays:
        detail += " " + "; ".join(refused + strays)
    (out / ("status-%s.txt" % name)).write_text("%s %s\n" % (name, detail))
    return name, ok, detail


def trace_check(binary, rom, out, movie, frames, jobs=2):
    """`--generated only` against `--native off` on one movie. Returns (ok, detail).

    The two runs are given their own copy of the binary so they can run at once
    on hosts that lock a running image. With one job, finish each run before
    starting the next, just as the movie and route checks do.
    """
    movie = ROOT / "oracle/movies" / (movie + ".ctm")
    if not movie.exists():
        return True, "skipped (no %s)" % movie.name
    traces = {}
    for tag, extra in (("off", ["--native", "off"]),
                       ("only", ["--native", "on", "--generated", "only"])):
        exe = (out / ("ct_dev_%s%s" % (tag, binary.suffix))).resolve()
        shutil.copy2(binary, exe)
        traces[tag] = out / ("%s.cttr" % tag)
        cmd = [str(exe), "--rom", str(rom), "--movie", str(movie),
               "--frames", str(frames), "--trace", str(traces[tag])] + extra
        with open(out / ("trace-%s.log" % tag), "wb") as fh:
            procs.append(subprocess.Popen(cmd, stdout=fh, stderr=subprocess.STDOUT))
            if jobs == 1:
                procs[-1].wait()
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
    """`--generated only` against `--native off` on one movie."""
    ok, detail = trace_check(ctx["binary"], args.rom, args.out, args.trace_movie, args.trace_frames, ctx["jobs"])
    return ok, "trace identity (%s)" % args.trace_movie, detail, []


# The timing gate's limits that are means over the library, not judged over a subset of it.
SUBSET_UNJUDGED = ("drift", "vblank-tail")


def check_timing(args, ctx):
    """The hand ports' estimated time against the original's (tools/timing_gate.py)."""
    agg = timing_gate.load(args.out / ("shadow-%s.log" % name) for name in ctx["movies"])
    if not agg:
        return False, "timing", "no --timing-report output in the shadow logs", []
    result = timing_gate.check(agg, timing_gate.read_exceptions(timing_gate.EXCEPTIONS))
    means = []
    if not ctx["complete"]:
        # Drift and the vblank tail are shares over every stretch the movies
        # run, and the costs are calibrated to the whole library's: a subset's
        # mix of scenes moves them however right the estimates are (the sample
        # alone put 305 entries past them). The worst cases are maxima, which
        # a subset can only lower, so those are judged here. And an exception
        # the subset doesn't reach may still be needed by the rest of the
        # library, so staleness is judged over all of it too.
        means = [f for f in result.failures if f[1] in SUBSET_UNJUDGED]
        result = result._replace(failures=[f for f in result.failures if f[1] not in SUBSET_UNJUDGED], stale=[])
    lines = timing_gate.failure_lines(result)
    if not ctx["complete"]:
        lines.append("note: over %d of the library's %d movies, so only the worst cases are judged; %d entr%s past "
                     "the drift or vblank-tail limit over these alone, which --all-movies judges, with stale exceptions"
                     % (len(ctx["movies"]), ctx["library"], len({f[0].name for f in means}),
                        "y" if len({f[0].name for f in means}) == 1 else "ies"))
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
    if not ctx["complete"]:
        cmd += ["--movies"] + list(ctx["movies"])
    rc, lines = run_tool(cmd)
    # its verdict is the "N failing finding(s)" line; progress goes to stderr
    verdict = [line for line in lines if "failing finding" in line]
    summary = verdict[-1] if verdict else (lines[-1] if lines else "no output")
    return rc == 0, "vblank", summary, [] if rc == 0 else lines[-40:]


# The routes the gate runs by default, and all of them for --long-routes: the
# ending routes are most of an hour of CPU, so they run on request. The fast
# set is a field route and the ending's approach to the second form, which is
# where a race in the original's wait for vblank hung the shipping mode once
# its timing moved (REIMPLEMENTATION.md, "The timing gate").
FAST_ROUTES = ["telepod_lavos:shell", "beyond_time:form2"]


# Routes run by hand rather than in every --long-routes gate: the tech sweep
# fights for about forty minutes for under a KiB of code no movie runs
# (docs/movies-wanted.md, "Every tech").
# The other New Game+ endings take over 100,000 frames apiece. Their
# scratch movies and shipping routes are checked by hand (MOVIES.md).
# The preparation routes make cartridge saves through long prerequisites;
# their movies are not in the library.
HAND_RUN_ROUTES = {"tech_sweep", "successor_of_guardia", "legendary_hero", "unknown_past",
                   "people_of_the_times", "the_oath", "dino_age", "what_the_prophet_seeks",
                   "memory_lane", "reunion", "prepare_luccas_mother", "sun_stone", "rainbow_shell",
                   "yakra", "nizbel", "rainbow_shell_trial", "blackbird_save"}


def all_routes():
    """Every route in tools/movies/routes, whole rather than up to a phase,
    except those run by hand."""
    return sorted(p.stem for p in (ROOT / "tools/movies/routes").glob("*.py")
                  if not p.stem.startswith("_") and p.stem not in HAND_RUN_ROUTES)


def check_routes(args, ctx):
    """The shipping mode end to end: state-driven routes reach their outcomes
    (tools/movies/route_check.py), which movies can't test once timing drifts."""
    drive = ctx["binary"].with_name("ct_drive" + ctx["binary"].suffix)
    if not drive.exists():
        return False, "routes", "%s: not found (build it first)" % drive, []
    # --long-routes runs every route whole; route_check's own default is two of
    # them, so naming them here is what makes it more than the fast set.
    routes = all_routes() if args.long_routes else FAST_ROUTES
    # the shipping mode with the sound driver that ships (the C one); the
    # original driver's shipping-mode runs are the vblank check's
    cmd = [sys.executable, "-m", "tools.movies.route_check", "--drive", str(drive), "--rom", str(args.rom),
           "--mode", "on:on:c", "-j", str(max(1, min(ctx["jobs"], len(routes))))]
    # a scratch of the caller's keeps the routes' checkpoints, so a run cut
    # short carries on where it stopped (route_check --resume)
    if args.routes_scratch:
        cmd += ["--scratch", str(args.routes_scratch), "--resume"]
    # what the original's code runs in place of hand ports, for the fallbacks
    # check after this one
    counts = args.out / "fallbacks" / "routes"
    shutil.rmtree(counts, ignore_errors=True)
    counts.mkdir(parents=True)
    cmd += ["--fallbacks", str(counts)]
    cmd += routes
    rc, lines = run_tool(cmd)
    passed = sum(1 for line in lines if re.match(r"^\S+\s+pass", line))
    failed = sum(1 for line in lines if re.match(r"^\S+\s+FAIL", line))
    summary = "%d of %d route(s) reach their outcome in the shipping mode" % (passed, passed + failed)
    return rc == 0 and failed == 0, "routes", summary, [] if rc == 0 else lines[-30:]


# ---- the shipping mode's fallbacks and the native shape -----------------------------

SHIP = ["--native", "on", "--generated", "on", "--sound-driver", "c"]
HALTED = re.compile(rb"^halted: .*$", re.M)
SHIP_PROBLEMS = (OUT_OF_RANGE, HALTED, REFUSED, ASSETS)


def ship_seconds(frames):
    """How long a movie may take in the shipping mode before it counts as hung:
    far more than any takes (the slowest runs at well over 60 frames a second)."""
    return 300 + frames // 10


def ship_movie(binary, rom, log, path, frames, extra):
    """One movie in the shipping mode. Returns (exit status or None if it hung,
    the log's problem lines)."""
    cmd = [str(binary), "--rom", str(rom), "--movie", str(path), "--frames", str(frames)] + SHIP + PACK_ARGS + extra
    with open(log, "wb") as fh:
        try:
            rc = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT, timeout=ship_seconds(frames)).returncode
        except subprocess.TimeoutExpired:
            rc = None
    text = log.read_bytes()
    problems = [m.decode(errors="replace").strip() for pattern in SHIP_PROBLEMS for m in pattern.findall(text)]
    return rc, problems


def ship_all(ctx, binary, rom, out, tag, extra_of):
    """Every movie in the shipping mode with `binary`, logs as out/<name>-<tag>.log.
    Returns {name: (rc, problems)}."""
    movies = ctx["movie_rows"]

    def one(m):
        name, path, frames = m
        return name, ship_movie(binary, rom, out / ("%s-%s.log" % (name, tag)), path, frames, extra_of(name))

    with concurrent.futures.ThreadPoolExecutor(max_workers=ctx["jobs"]) as pool:
        return dict(pool.map(one, movies))


def shipping_inputs(args):
    """The shipping mode's fallback files: the movies' and, where the routes
    check wrote them, the routes'."""
    out = args.out / "fallbacks"
    paths = [out / "movies"]
    routes = out / "routes"
    if routes.is_dir() and any(routes.rglob("*.txt")):
        paths.append(routes)
    return paths


def check_fallbacks(args, ctx):
    """What the original's code runs in place of hand ports. The ratchet
    (tools/fallbacks.py --check against its baseline) is over the shadow runs,
    where each movie plays as it was recorded whatever the ports' timing, so it
    moves only when a port does; the shipping mode's count, over every movie
    (run here) and the routes (the routes check's runs), drifts with the
    timing and is reported beside it."""
    out = args.out / "fallbacks"
    movies = out / "movies"
    shutil.rmtree(movies, ignore_errors=True)
    movies.mkdir(parents=True)
    # the original's code's reads of scaffolding, for the native-shape check to trace through
    ctx["ship"] = ship_all(ctx, ctx["binary"], args.rom, out, "ship",
                           lambda name: ["--fallbacks", str(movies / ("%s.txt" % name)),
                                         "--scaffold-reads", "original"])
    shadow = out / "shadow"
    missing = sorted(name for name in ctx["movies"] if not (shadow / ("%s.txt" % name)).exists())
    missing += sorted("%s (shipping mode)" % name for name in ctx["ship"] if not (movies / ("%s.txt" % name)).exists())
    rc, lines = run_tool([sys.executable, str(ROOT / "tools/fallbacks.py"), "--check",
                          "--report", str(out / "report.txt"), str(shadow)]
                         + ([] if ctx["complete"] else ["--subset"]))
    summary = "reference play (the movies in shadow mode): " + (lines[-1] if lines else "no output")
    paths = shipping_inputs(args)
    _, totals = run_tool([sys.executable, str(ROOT / "tools/fallbacks.py"), "--totals",
                          "--report", str(out / "report-shipping.txt")] + [str(p) for p in paths])
    summary += "; shipping mode (the movies%s): %s" % (" and routes" if len(paths) > 1 else ", no routes",
                                                       totals[-1] if totals else "no output")
    details = [] if rc == 0 else lines[:-1][-40:]
    if missing:
        details.append("no counts from: " + ", ".join(missing))
    return rc == 0 and not missing, "fallbacks", summary, details


def check_refusals(args, ctx):
    """Hand ports' guards refusing calls real play makes, over the shadow runs,
    the movies in the shipping mode and the routes (tools/fallbacks.py
    --refusals): each fails unless its allowlist line says why."""
    paths = [args.out / "fallbacks" / "shadow"] + shipping_inputs(args)
    if not any(p.is_dir() and any(p.glob("*.txt")) for p in paths):
        return False, "refusals", "no fallback counts (the shadow runs and the fallbacks check make them)", []
    rc, lines = run_tool([sys.executable, str(ROOT / "tools/fallbacks.py"), "--refusals"] + [str(p) for p in paths])
    summary = lines[-1] if lines else "no output"
    return rc == 0, "refusals", summary, [] if rc == 0 else lines[:-1][-60:]


NATIVE_SHAPE_EXCEPTIONS = ROOT / "tools/native_shape_exceptions.txt"
# A native-shape route fails in an hour or not at all: the routes take a
# quarter of that, and one the missing scaffolding loops (a battle that can't
# end) would otherwise run to route_check's six hours.
NATIVE_SHAPE_ROUTE_SECONDS = 3600


def native_shape_exceptions():
    """{("movie"|"route", name): reason} from tools/native_shape_exceptions.txt,
    and its lines that have no reason."""
    out, bad = {}, []
    if NATIVE_SHAPE_EXCEPTIONS.exists():
        for number, line in enumerate(NATIVE_SHAPE_EXCEPTIONS.read_text().splitlines(), 1):
            body, _, reason = line.partition("#")
            words = body.split()
            if not words:
                continue
            if len(words) != 2 or words[0] not in ("movie", "route") or not reason.strip():
                bad.append("%s:%d: not `movie|route NAME # reason`" % (NATIVE_SHAPE_EXCEPTIONS.name, number))
                continue
            out[(words[0], words[1])] = reason.strip()
    return out, bad


def check_native_shape(args, ctx):
    """The build with the ports' scaffolding compiled out, in the shipping mode:
    every movie runs to its end with no problem the default build's run of it
    doesn't show (a failed exit, a hang, a halted machine, an accessor out of
    range, a refused page), and the routes reach their outcomes (the fast set,
    all of them with --long-routes). Nothing is compared byte for byte: the
    scaffolding is what it leaves out. A failure listed in
    tools/native_shape_exceptions.txt, with its reason, is reported and
    doesn't fail. The default build's shipping runs (the fallbacks check's)
    note the original's code's reads of scaffolding (ct_dev --scaffold-reads
    original), and tools/effects_check.py --originals lists them with the
    source of each write, in native-shape/originals.txt: what a failure here
    is traced through."""
    dev = ctx["binary"].with_name("ct_dev_native_shape" + ctx["binary"].suffix)
    drive = ctx["binary"].with_name("ct_drive_native_shape" + ctx["binary"].suffix)
    for exe in (dev, drive):
        if not exe.exists():
            return False, "native shape", "%s: not found (build it first)" % exe, []
    out = args.out / "native-shape"
    out.mkdir(parents=True, exist_ok=True)
    exceptions, bad = native_shape_exceptions()
    shape = ship_all(ctx, dev, args.rom, out, "shape", lambda name: [])
    default = ctx.get("ship") or ship_all(ctx, ctx["binary"], args.rom, out, "ship",
                                          lambda name: ["--scaffold-reads", "original"])
    frames = dict((m[0], m[2]) for m in ctx["movie_rows"])
    failing, excepted = [], []
    for name in sorted(shape):
        rc, problems = shape[name]
        base_rc, base_problems = default.get(name, (0, []))
        what = []
        if rc is None:
            what.append("hung (no end after %ds)" % ship_seconds(frames[name]))
        elif rc != 0 and base_rc == 0:
            what.append("exit status %d" % rc)
        what += [p for p in problems if p not in base_problems]
        if what:
            line = "movie %s: %s (log %s)" % (name, "; ".join(what), out / ("%s-shape.log" % name))
            (excepted if ("movie", name) in exceptions else failing).append(line)
    routes = all_routes() if args.long_routes else FAST_ROUTES
    cmd = [sys.executable, "-m", "tools.movies.route_check", "--drive", str(drive), "--rom", str(args.rom),
           "--mode", "on:on:c", "-j", str(max(1, min(ctx["jobs"], len(routes)))),
           "--timeout", str(NATIVE_SHAPE_ROUTE_SECONDS), "--scratch", str(out / "routes")] + routes
    shutil.rmtree(out / "routes", ignore_errors=True)
    rrc, lines = run_tool(cmd)
    passed = sum(1 for line in lines if re.match(r"^\S+\s+pass", line))
    failed_routes = [m.group(1) for m in (re.match(r"^FAIL (\S+) in \S+: (.*)$", line) for line in lines) if m]
    for line in lines:
        m = re.match(r"^FAIL (\S+) in \S+: (.*)$", line)
        if m:
            entry = "route %s: %s" % (m.group(1), m.group(2))
            (excepted if ("route", m.group(1)) in exceptions else failing).append(entry)
    if rrc != 0 and not failed_routes:
        failing += lines[-10:]
    originals = [str(p) for p in sorted((args.out / "fallbacks").glob("*-ship.log"))]
    traced = ""
    if originals:
        rc_o, olines = run_tool([sys.executable, str(ROOT / "tools/effects_check.py"), "--originals"] + originals)
        (out / "originals.txt").write_text("\n".join(olines) + "\n")
        traced = "; %d read(s) of scaffolding by the original's code in the default build's runs (%s)" % (
            max(0, len(olines) - 1), out / "originals.txt")
    summary = ("%d of %d movies run to their end and %d of %d route(s) reach their outcome with the scaffolding "
               "compiled out, %d failure(s) excepted%s"
               % (len(shape) - sum(1 for b in failing + excepted if b.startswith("movie ")), len(shape),
                  passed, passed + len(failed_routes), len(excepted), traced))
    details = bad + failing + ["excepted: " + e for e in excepted]
    return not failing and not bad, "native shape", summary, details[:40]


LOCKSTEP = re.compile(r"^sound lockstep \(.*?\): (\d+) ticks, (\d+) while loading, (\d+) commands, (\d+) loads(.*)$",
                      re.M)
LOCKSTEP_SAME = re.compile(r"^: identical(?:; the samples' places checked (\d+) times against the CPU half's: "
                           r"identical)?$")


def check_lockstep(args, ctx):
    """The C sound driver beside the original's at every tick of every movie
    (ct_dev --sound-lockstep --sound-lockstep-level state, in the shadow runs):
    status, keys, S-DSP registers and the drivers' state, and the samples'
    places against the CPU half's. Any difference, or a run without its
    verdict, fails."""
    totals, places, bad = [0, 0, 0, 0], 0, []
    for name in ctx["movies"]:
        log = args.out / ("shadow-%s.log" % name)
        text = log.read_text(errors="replace") if log.exists() else ""
        found = LOCKSTEP.findall(text)
        same = found and LOCKSTEP_SAME.match(found[-1][4])
        if not same:
            line = next((l for l in text.splitlines() if l.startswith("sound lockstep")), "no lockstep verdict")
            bad.append("%s: %s" % (name, line))
            continue
        totals = [t + int(n) for t, n in zip(totals, found[-1][:4])]
        places += int(same.group(1) or 0)
    summary = ("%d of %d movies identical: %d ticks (%d loading), %d commands, %d loads, the places checked %d times"
               % (len(ctx["movies"]) - len(bad), len(ctx["movies"]), *totals, places))
    return not bad, "sound lockstep", summary, bad[:40]


# ct_sound_fuzz's part of the gate: a few runs of each kind, from fixed seeds, so
# a run is the same run every time (docs/SOUND.md, "How it was checked"). Against
# the original at the state level, with the game's songs and with made-up ones;
# and the C driver in the SPC700's place as ct_play plays it, likewise, heard
# through the S-DSP and the native mixer side by side, every frame of the two
# the same (--mixer check; docs/PLATFORM.md, "The native mixer, as built").
SOUND_FUZZ_RUNS = [
    ("original", ["--level", "state"]),
    ("original, made-up songs", ["--level", "state", "--made-up-songs", "1"]),
    ("hosted", ["--sound-driver", "c", "--mixer", "check"]),
    ("hosted, made-up songs", ["--sound-driver", "c", "--made-up-songs", "1", "--mixer", "check"]),
]
SOUND_FUZZ_SIZE = ["--runs", "10", "--commands", "400", "--seed", "1"]


def check_sound_fuzz(args, ctx):
    """ct_sound_fuzz in both its modes (a short run of each): the C driver
    against the original on command streams no movie plays, and in the
    SPC700's place answering every command with no sample placed where the
    echo writes and no command dropped, and the native mixer's every frame
    the S-DSP's."""
    fuzz = ctx["binary"].with_name("ct_sound_fuzz" + ctx["binary"].suffix)
    if not fuzz.exists():
        return False, "sound fuzz", "%s: not found (build it first)" % fuzz, []

    def one(run):
        label, extra = run
        rc, lines = run_tool([str(fuzz), "--rom", str(args.rom)] + SOUND_FUZZ_SIZE + extra)
        return label, rc, lines

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(ctx["jobs"], 2))) as pool:
        results = list(pool.map(one, SOUND_FUZZ_RUNS))
    failed = [(label, lines) for label, rc, lines in results if rc != 0]
    summary = "%d of %d kinds of run clean (%s commands each)" % (len(results) - len(failed), len(results),
                                                                  " runs of ".join(SOUND_FUZZ_SIZE[1:4:2]))
    details = []
    for label, lines in failed:
        details += ["%s:" % label] + ["  " + line for line in lines[-20:]]
    return not failed, "sound fuzz", summary, details


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


def check_uncompared(args, ctx):
    """No hand port mostly goes uncompared in the shadow runs (interrupted, or
    abandoned inside another port's stretch) without a reason recorded
    (tools/uncompared.py, a ratchet against tools/uncompared_baseline.txt)."""
    logs = [str(args.out / ("shadow-%s.log" % name)) for name in ctx["movies"]]
    rc, lines = run_tool([sys.executable, str(ROOT / "tools/uncompared.py"), "--check"] + logs)
    summary = lines[-1] if lines else "no output"
    return rc == 0, "uncompared", summary, [] if rc == 0 else lines[:-1][-40:]


def check_strict(args, ctx):
    """How far the hand ports alone carry each movie: every movie strict
    (ct_dev --native on --generated off --no-interpreter strict --sound-driver
    c), none stopping earlier than tools/strict_baseline.txt says it reaches
    (tools/strict_check.py --check), and each that reaches its end ending as
    the reference play does, the shadow runs' end states. It needs no shadow
    mode, so it is quick; over a subset of the library (--movies, --only) it
    checks those."""
    cmd = [sys.executable, str(ROOT / "tools/strict_check.py"), "--ct-dev", str(ctx["binary"]), "--rom", str(args.rom),
           "--check", "-j", str(max(1, min(ctx["jobs"], 2))), "--reference", str(args.out)]
    if not ctx["complete"]:
        cmd += ["--movies"] + list(ctx["movies"])
    rc, lines = run_tool(cmd)
    summary = lines[-1] if lines else "no output"
    return rc == 0, "strict", summary, [] if rc == 0 else [l for l in lines[:-1] if "<--" in l or "not in" in l][-40:]


def check_native(args, ctx):
    """Every movie in the native program (ct_native, beside ct_dev), none ending
    on a dark screen or crashing (tools/native_check.py --check); where each
    stops, and whether it ends as the shadow runs' reference play does, are
    reported with it. Over a subset of the library (--movies, --only) it checks
    those."""
    native = ctx["binary"].with_name("ct_native" + ctx["binary"].suffix)
    if not native.exists():
        return False, "native", "%s isn't built" % native.name, []
    cmd = [sys.executable, str(ROOT / "tools/native_check.py"), "--ct-native", str(native), "--rom", str(args.rom),
           "--check", "-j", str(max(1, min(ctx["jobs"], 2))), "--reference", str(args.out)] + PACK_ARGS
    if not ctx["complete"]:
        cmd += ["--movies"] + list(ctx["movies"])
    rc, lines = run_tool(cmd)
    summary = lines[-1] if lines else "no output"
    return rc == 0, "native", summary, [] if rc == 0 else [l for l in lines[:-1] if ": dark" in l or ": crashed" in l
                                                           or ": hung" in l][-40:]


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


def check_assets(args, ctx):
    """Every row of the catalogue of resources is still a region the matching
    build extracts, and the C table is what the catalogue generates
    (tools/assets.py check, static)."""
    rc, lines = run_tool([sys.executable, str(ROOT / "tools/assets.py"), "check", "--rom", str(args.rom)])
    summary = lines[-1] if lines else "no output"
    return rc == 0, "assets", summary, [] if rc == 0 else lines[:-1][-40:]


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
    ("trace", check_trace, "the --generated only trace comparison"),
    ("timing", check_timing, "the timing gate over the shadow runs"),
    ("vblank", check_vblank, "the check for video writes on the picture in the shipping mode"),
    ("routes", check_routes, "the route scripts in the shipping mode"),
    ("fallbacks", check_fallbacks, "the ratchet on the original's code run in place of hand ports (the movies' "
                                   "shipping-mode runs)"),
    ("refusals", check_refusals, "the check of real play's refusals against their allowlist"),
    ("native-shape", check_native_shape, "the movies and routes with the ports' scaffolding compiled out"),
    ("lockstep", check_lockstep, "the C sound driver against the original's in the shadow runs (and the lockstep "
                                 "in them)"),
    ("sound-fuzz", check_sound_fuzz, "the short ct_sound_fuzz runs"),
    ("dontcare", check_dontcare, "the don't-care declarations against liveness"),
    ("uncompared", check_uncompared, "the ratchet on hand ports the shadow runs mostly leave uncompared"),
    ("strict", check_strict, "the ratchet on how far the hand ports alone carry each movie"),
    ("native", check_native, "the movies in the native program, none ending dark"),
    ("effects", check_effects, "the ratchet on logic that reads scaffolding"),
    ("portstyle", check_portstyle, "the ratchet on ports that transliterate the assembly"),
    ("portsmell", check_portsmell, "the ratchet on the review rubric's mechanical items"),
    ("assets", check_assets, "the catalogue of resources against the matching build's regions"),
    ("evidence", check_evidence, "the dc-evidence notes' library runs (only run with --evidence)"),
)


def main():
    ap = argparse.ArgumentParser(prog="python3 tools/suite.py")
    ap.add_argument("--ct-dev", type=pathlib.Path, required=True, help="the ct_dev to run")
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "build/suite")
    ap.add_argument("--only", choices=("all", "core", "saves"), default="all")
    ap.add_argument("--movies", nargs="*", default=[], help="run only these, by name")
    ap.add_argument("--all-movies", action="store_true",
                    help="run the whole library, not the gate's sample (tools/gate_movies.txt): before merging "
                         "a change to timing or the runtime, and for every recalibration or baseline update")
    ap.add_argument("-j", "--jobs", type=int, default=0,
                    help="concurrent movies (default: half the CPUs, at least 1)")
    ap.add_argument("--trace-movie", default="new-game")
    ap.add_argument("--trace-frames", type=int, default=7200)
    ap.add_argument("--routes-scratch", type=pathlib.Path,
                    help="keep the routes' runs here and resume them from their checkpoints where the build is the "
                         "same (route_check --resume): a long run cut short carries on on the next")
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
    for stale in list(args.out.glob("status-*.txt")) + list(args.out.glob("end-*.bin")):
        stale.unlink()
    shutil.rmtree(args.out / "fallbacks" / "shadow", ignore_errors=True)
    (args.out / "fallbacks" / "shadow").mkdir(parents=True)

    if args.movies:
        chosen, what = set(args.movies), "the movies named"
    elif args.all_movies:
        chosen, what = set(), "the whole library"
    else:
        sample, bad = gate_movies()
        if bad:
            sys.exit("\n".join(bad))
        chosen, what = set(sample), "the gate's sample, %s; --all-movies runs the library" % GATE_MOVIES.name
    movies = collect(args.only, chosen)
    if not movies:
        sys.exit("no movies to run")
    # Longest first: the three endings are more than half the suite, and one
    # started late sets the wall time while the other workers sit idle.
    movies.sort(key=lambda m: -m[2])
    import_assets(binary, args.rom, args.out / "ct-usa.assets")
    print("%d movies (%s), %d at a time" % (len(movies), what, jobs), flush=True)

    bad_movies = []
    started = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(run_movie, binary, args.rom, args.out, *m, not args.no_lockstep) for m in movies]
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
    ctx = {"binary": binary, "jobs": jobs, "movies": [m[0] for m in movies], "movie_rows": movies,
           "library": library, "complete": library == len(movies)}
    failed = ["movies: " + ", ".join(bad_movies)] if bad_movies else []
    for name, fn, _what in CHECKS:
        if getattr(args, "no_" + name.replace("-", "_")):
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
