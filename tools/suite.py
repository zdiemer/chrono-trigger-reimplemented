"""Run the product gate over the movie sample (or --all-movies).

    python3 tools/suite.py --ct-dev build/main/ct_dev -j 2 --evidence

Shadow movies retain contract, page, accessor and asset diagnostics until a
checking ct_native replaces them. Strict and native movies protect recorded
outcomes; shipping, native-shape and native routes check gameplay. Sound,
source quality, unreached-path evidence and asset checks remain in the gate.
Timing and vblank checks run only with --all-movies: calibration needs the
whole library's mix of scenes. Branches run the sample without --long-routes;
main runs whole-library calibration and long routes in separate merge batches.

tools/gate_movies.txt names the sample with a reason per movie. Subset runs
never drop unseen baseline entries. Frame counts and cartridge saves come
from movie headers. Logs, coverage, calls and reference end states go under
--out (default build/suite); these derived files stay out of the repository.
Exit status is zero only on success; the final line is then `suite OK`.
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
from tools import outcomes
from tools.strict_check import SavedGame
from tools import timing_gate  # noqa: E402

MISMATCH = re.compile(rb"mismatched [1-9]")
# a guard's refusal by page, or a retired routine's broken contract (a guard
# that became an assertion when its subsystem left the gate)
REFUSED = re.compile(rb"^(?:page refused|contract broken): .*$", re.M)
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
    selected = [p for paths in groups for p in paths if not names or p.stem in names]
    rows = outcomes.require_movies(set(names or ()) | {p.stem for p in selected}, ROOT)
    return [rows[p.stem] for p in selected]


def select_movies(only, names, all_movies=False):
    """Preflight the recorded set and gate sample before narrowing the run."""
    sample, bad = gate_movies()
    if bad:
        raise ValueError("\n".join(bad))
    recorded = outcomes.read(outcomes.LEDGER)
    outcomes.require_movies({n for n, _kind in recorded} | set(sample) | set(names or ()), ROOT)
    if names:
        chosen, what = set(names), "the movies named"
    elif all_movies:
        chosen, what = set(), "the whole library"
    else:
        chosen, what = set(sample), "the gate's sample, %s; --all-movies runs the library" % GATE_MOVIES.name
    return collect(only, chosen), what


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


def run_movie(binary, rom, out, name, path, frames):
    """One shadow run. Returns (name, ok, detail)."""
    log = out / ("shadow-%s.log" % name)
    cmd = [str(binary), "--rom", str(rom), "--movie", str(path),
           "--frames", str(frames), "--native", "shadow", "--timing-report", "--page-report",
           "--coverage", str(out / ("cov-%s.txt" % name)),
           "--calls", str(out / ("calls-%s.txt" % name)),
           "--end-state", str(out / ("end-%s.bin" % name)), "--locations", "--outcomes"] + PACK_ARGS
    started = time.time()
    with open(log, "wb") as fh:
        rc = subprocess.call(cmd, stdout=fh, stderr=subprocess.STDOUT)
    text = log.read_bytes()
    outcomes.capture(out, name, path, frames, text.decode(errors="replace"), rc,
                     out / ("end-%s.bin" % name), SavedGame(rom), "reference")
    bad = len(MISMATCH.findall(text))
    refused = [r.decode(errors="replace").strip() for r in REFUSED.findall(text)]
    strays = [r.decode(errors="replace").strip() for r in OUT_OF_RANGE.findall(text) + ASSETS.findall(text)]
    ok = rc == 0 and bad == 0 and not refused and not strays
    detail = "rc=%d mismatching=%d %.0fs" % (rc, bad, time.time() - started)
    if refused or strays:
        detail += " " + "; ".join(refused + strays)
    (out / ("status-%s.txt" % name)).write_text("%s %s\n" % (name, detail))
    return name, ok, detail


# ---- the checks after the shadow runs -------------------------------------------
#
# Each takes the parsed arguments and the context of the shadow runs and
# returns (ok, label, one-line summary, detail lines to print under it). The gate
# runs them in order after the shadow runs. Adding a check is a function here
# and a row in CHECKS, with a --no-<name> switch to skip it while iterating.


def check_timing(args, ctx):
    """Whole-library calibration against the emulator's measured time."""
    agg = timing_gate.load(args.out / ("shadow-%s.log" % name) for name in ctx["movies"])
    if not agg:
        return False, "timing", "no --timing-report output in the shadow logs", []
    result = timing_gate.check(agg, timing_gate.read_exceptions(timing_gate.EXCEPTIONS))
    return not result.failures, "timing", timing_gate.summary(result), timing_gate.failure_lines(result)


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
# The preparation routes make cartridge saves through long prerequisites
# (oracle/saves/README.md, "routes/"), and the rest are long plays from the
# nearest stock save (the Cathedral, the Reptite Lair, the shell's trial, the
# Sun Stone planted, Crono's trial); none of their movies is in the library,
# and those kept are in oracle/movies/hand, run only when named. `heckran`'s
# movie is in the library, but its route loses the fight in the shipping mode
# with the save's supplies (docs/TODO.md, "Heckran's route in the shipping mode").
HAND_RUN_ROUTES = {"tech_sweep", "successor_of_guardia", "legendary_hero", "unknown_past",
                   "people_of_the_times", "the_oath", "dino_age", "what_the_prophet_seeks",
                   "memory_lane", "reunion", "prepare_luccas_mother", "sun_stone", "rainbow_shell",
                   "yakra", "nizbel", "rainbow_shell_trial", "blackbird_save", "sun_stone_save",
                   "heckran_prepare", "cronos_trial", "heckran"}


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
    # the shipping mode, with the sound driver that ships (the C one)
    cmd = [sys.executable, "-m", "tools.movies.route_check", "--drive", str(drive), "--rom", str(args.rom),
           "--mode", "on:on", "-j", str(max(1, min(ctx["jobs"], len(routes))))]
    # a scratch of the caller's keeps the routes' checkpoints, so a run cut
    # short carries on where it stopped (route_check --resume)
    if args.routes_scratch:
        cmd += ["--scratch", str(args.routes_scratch), "--resume"]
    cmd += routes
    rc, lines = run_tool(cmd)
    passed = sum(1 for line in lines if re.match(r"^\S+\s+pass", line))
    failed = sum(1 for line in lines if re.match(r"^\S+\s+FAIL", line))
    summary = "%d of %d route(s) reach their outcome in the shipping mode" % (passed, passed + failed)
    # the routes that failed and why, ahead of the table's tail, which may not reach them
    failures = [line for line in lines if line.startswith("FAIL ")]
    return rc == 0 and failed == 0, "routes", summary, [] if rc == 0 else failures + lines[-30:]


NATIVE_ROUTE_EXCEPTIONS = ROOT / "tools/native_route_exceptions.txt"


def native_route_exceptions(path=None):
    """Exact route/phase exceptions, each with the observed failure's reason.
    A full ending's exception must not hide its passing fast prefix."""
    known = set(all_routes()) | set(FAST_ROUTES)
    result = {}
    for line in (path or NATIVE_ROUTE_EXCEPTIONS).read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        route, separator, reason = line.partition("#")
        route, reason = route.strip(), reason.strip()
        if route not in known or route in result or not separator or not reason:
            raise ValueError("invalid native route exception: " + line)
        result[route] = reason
    return result


def native_route_selection(long_routes, exceptions):
    """A full-route exception leaves its fast prefix protected in either gate."""
    if not long_routes:
        return list(FAST_ROUTES)
    return all_routes() + [route for route in FAST_ROUTES
                           if route.split(":", 1)[0] in exceptions]


def check_native_routes(args, ctx):
    """The routes in the same native program as window play, without a machine."""
    drive = ctx["binary"].with_name("ct_native" + ctx["binary"].suffix)
    if not drive.exists():
        return False, "native-routes", "%s: not found (build it first)" % drive, []
    try:
        exceptions = native_route_exceptions()
    except (ValueError, OSError) as error:
        return False, "native-routes", str(error), []
    selected = native_route_selection(args.long_routes, exceptions)
    routes = [route for route in selected if route not in exceptions]
    details = ["excepted %s: %s" % (route, exceptions[route]) for route in selected if route in exceptions]
    if not routes:
        return True, "native-routes", "0 run, %d explicitly excepted" % len(details), details
    cmd = [sys.executable, "-m", "tools.movies.route_check", "--native-drive", str(drive), "--rom", str(args.rom),
           "--mode", "native", "-j", str(max(1, min(ctx["jobs"], len(routes))))]
    if args.routes_scratch:
        cmd += ["--scratch", str(args.routes_scratch), "--resume"]
    rc, lines = run_tool(cmd + routes)
    passed = sum(1 for line in lines if line.startswith("PASS "))
    details += [line for line in lines if line.startswith(("PASS ", "FAIL "))]
    if rc:
        details += lines[-30:]
    return rc == 0 and passed == len(routes), "native-routes", \
        "%d of %d route(s) reach their outcome in ct_native; %d excepted" % (passed, len(routes), len(selected) - len(routes)), details


# ---- the native-shape routes, retained until the native route audit ----------------

NATIVE_SHAPE_EXCEPTIONS = ROOT / "tools/native_shape_exceptions.txt"
# Bound compatibility-route hangs independently of route_check's six hours.
NATIVE_SHAPE_ROUTE_SECONDS = 3600


def native_shape_exceptions():
    """{("route", name): reason} from tools/native_shape_exceptions.txt,
    and its lines that have no reason."""
    out, bad = {}, []
    if NATIVE_SHAPE_EXCEPTIONS.exists():
        for number, line in enumerate(NATIVE_SHAPE_EXCEPTIONS.read_text().splitlines(), 1):
            body, _, reason = line.partition("#")
            words = body.split()
            if not words:
                continue
            if len(words) != 2 or words[0] != "route" or not reason.strip():
                bad.append("%s:%d: not `route NAME # reason`" % (NATIVE_SHAPE_EXCEPTIONS.name, number))
                continue
            out[(words[0], words[1])] = reason.strip()
    return out, bad


def check_native_shape(args, ctx):
    """Compatibility routes retained until ct_native covers the route set."""
    drive = ctx["binary"].with_name("ct_drive_native_shape" + ctx["binary"].suffix)
    if not drive.exists():
        return False, "native shape", "%s: not found (build it first)" % drive, []
    out = args.out / "native-shape"
    out.mkdir(parents=True, exist_ok=True)
    exceptions, bad = native_shape_exceptions()
    failing, excepted = [], []
    routes = all_routes() if args.long_routes else FAST_ROUTES
    cmd = [sys.executable, "-m", "tools.movies.route_check", "--drive", str(drive), "--rom", str(args.rom),
           "--mode", "on:on", "-j", str(max(1, min(ctx["jobs"], len(routes)))),
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
    summary = "%d of %d route(s) reach their outcome; %d failure(s) excepted" % (
        passed, passed + len(failed_routes), len(excepted))
    details = bad + failing + ["excepted: " + e for e in excepted]
    return not failing and not bad, "native shape", summary, details[:40]


# ct_sound_fuzz's part of the gate: a few runs of each kind, from fixed seeds, so
# a run is the same run every time (docs/SOUND.md, "How it is checked"): the C
# driver in the sound processor's place as ct_play plays it, with the game's
# songs and with made-up ones, heard through the S-DSP and the native mixer
# side by side, every frame of the two the same (--mixer check; docs/PLATFORM.md,
# "The native mixer, as built"); and each kind's sound the digest
# tools/sound_golden.txt keeps for it.
SOUND_FUZZ_RUNS = [
    ("game's songs", ["--mixer", "check"]),
    ("made-up songs", ["--made-up-songs", "1", "--mixer", "check"]),
]
SOUND_FUZZ_SIZE = ["--runs", "10", "--commands", "400", "--seed", "1"]
SOUND_GOLDEN = ROOT / "tools" / "sound_golden.txt"
SOUND_DIGEST = re.compile(r"^\d+ runs, \d+ failed; digest ([0-9a-f]{16})$")


def sound_golden():
    """The digests tools/sound_golden.txt keeps, by kind of run: `<digest>  #
    <kind>` lines."""
    kept = {}
    for line in SOUND_GOLDEN.read_text().splitlines():
        digest, _, kind = line.partition("#")
        if digest.strip():
            kept[kind.strip()] = digest.strip()
    return kept


def check_sound_fuzz(args, ctx):
    """ct_sound_fuzz, a short run of each kind: the C driver in the sound
    processor's place answering every command with no sample placed where the
    echo writes and no command dropped, the native mixer's every frame the
    S-DSP's, and what each kind of run heard the digest tools/sound_golden.txt
    keeps for it. A digest that moved is a change to what the driver plays:
    it fails until the file is given the new one on purpose, with the reason
    in the commit (the original isn't there to say which is right any more)."""
    fuzz = ctx["binary"].with_name("ct_sound_fuzz" + ctx["binary"].suffix)
    if not fuzz.exists():
        return False, "sound fuzz", "%s: not found (build it first)" % fuzz, []
    kept = sound_golden()

    def one(run):
        label, extra = run
        rc, lines = run_tool([str(fuzz), "--rom", str(args.rom)] + SOUND_FUZZ_SIZE + extra)
        return label, rc, lines

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(ctx["jobs"], 2))) as pool:
        results = list(pool.map(one, SOUND_FUZZ_RUNS))
    failed, moved = [], []
    for label, rc, lines in results:
        found = SOUND_DIGEST.match(lines[-1]) if lines else None
        if rc != 0 or not found:
            failed.append((label, lines))
        elif kept.get(label) != found.group(1):
            moved.append("%s: digest %s, %s keeps %s" % (label, found.group(1), SOUND_GOLDEN.name,
                                                         kept.get(label, "none")))
    summary = "%d of %d kinds of run clean (%s commands each), %d with the digest kept" % (
        len(results) - len(failed), len(results), " runs of ".join(SOUND_FUZZ_SIZE[1:4:2]),
        len(results) - len(failed) - len(moved))
    details = list(moved)
    for label, lines in failed:
        details += ["%s:" % label] + ["  " + line for line in lines[-20:]]
    return not failed and not moved, "sound fuzz", summary, details


def check_strict(args, ctx):
    """How far the hand ports alone carry each movie: every movie strict
    (ct_dev --native on --generated off --no-interpreter strict, the C sound
    driver playing), none stopping earlier than tools/strict_baseline.txt says it reaches
    (tools/strict_check.py --check), and each that reaches its end ending as
    the reference play does, the shadow runs' end states. It needs no shadow
    mode, so it is quick; over a subset of the library (--movies, --only) it
    checks those."""
    cmd = [sys.executable, str(ROOT / "tools/strict_check.py"), "--ct-dev", str(ctx["binary"]), "--rom", str(args.rom),
           "--check", "-j", str(max(1, min(ctx["jobs"], 2))), "--reference", str(args.out),
           "--outcomes", str(args.out / "outcomes" / "strict")]
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
           "--check", "-j", str(max(1, min(ctx["jobs"], 2))), "--reference", str(args.out),
           "--outcomes", str(args.out / "outcomes" / "native")] + PACK_ARGS
    if not ctx["complete"]:
        cmd += ["--movies"] + list(ctx["movies"])
    rc, lines = run_tool(cmd)
    summary = lines[-1] if lines else "no output"
    return rc == 0, "native", summary, [] if rc == 0 else [l for l in lines[:-1] if ": dark" in l or ": crashed" in l
                                                           or ": hung" in l or "<--" in l or "not in" in l][-40:]


def check_outcomes(args, ctx):
    """The recorded reference, checked without running it beside the ports."""
    summaries, details, ok = [], [], True
    for mode in ("strict", "native"):
        rc, lines = run_tool([sys.executable, str(ROOT / "tools/outcomes.py"), "--check",
                              "--runs", str(args.out / "outcomes" / mode), "--mode", mode,
                              "--movies"] + list(ctx["movies"]))
        ok &= rc == 0
        summaries.append(lines[-1] if lines else "no output")
        details.extend(l for l in lines[:-1] if "FAILED" in l)
    return ok, "outcomes", "; ".join(summaries), details


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


def check_unreached(args, ctx):
    """The audited subsystems' full-library inventory and path evidence.

    The sample cannot establish absence from the library: use the ledger's
    manifest and re-derived reach, never the sample's partial coverage union.
    """
    extra = []
    if args.evidence:
        out = args.out / "unreached"
        rc, lines = run_tool([sys.executable, str(ROOT / "tools/unreached_fuzz.py"),
                             "--rom", str(args.rom), "--ct-dev", str(ctx["binary"]),
                             "--ct-fuzz", str(ctx["binary"].with_name("ct_fuzz" + ctx["binary"].suffix)),
                             "--ledger", str(ROOT / "tools/unreached_baseline.txt"), "--out", str(out),
                             "-j", str(max(1, min(ctx["jobs"], 2)))])
        if rc:
            return False, "unreached", "fuzz evidence failed", lines[-40:]
        extra = ["--recheck-fuzz", "--fuzz-dir", str(out / "fuzz")]
    rc, lines = run_tool([sys.executable, str(ROOT / "tools/unreached.py"),
                          "--check", "--rom", str(args.rom)] + extra)
    summary = lines[-1] if lines else "no output"
    return rc == 0, "unreached", summary, [] if rc == 0 else lines[:-1][-40:]


def check_assets(args, ctx):
    """Every row of the catalogue of resources is still a region the matching
    build extracts, and the C table is what the catalogue generates
    (tools/assets.py check, static)."""
    rc, lines = run_tool([sys.executable, str(ROOT / "tools/assets.py"), "check", "--rom", str(args.rom)])
    summary = lines[-1] if lines else "no output"
    return rc == 0, "assets", summary, [] if rc == 0 else lines[:-1][-40:]


CHECKS = (
    # name, function, what --no-<name> skips
    ("timing", check_timing, "the timing gate over the shadow runs"),
    ("vblank", check_vblank, "the check for video writes on the picture in the shipping mode"),
    ("routes", check_routes, "the route scripts in the shipping mode"),
    ("native-routes", check_native_routes, "the route scripts in ct_native"),
    ("native-shape", check_native_shape, "the native-shape routes"),
    ("sound-fuzz", check_sound_fuzz, "the short ct_sound_fuzz runs"),
    ("strict", check_strict, "the ratchet on how far the hand ports alone carry each movie"),
    ("native", check_native, "the movies in the native program, none ending dark"),
    ("outcomes", check_outcomes, "recorded end states, checkpoint pictures and places"),
    ("portstyle", check_portstyle, "the ratchet on ports that transliterate the assembly"),
    ("portsmell", check_portsmell, "the ratchet on the review rubric's mechanical items"),
    ("unreached", check_unreached, "the full-library unreached inventory and its checked paths"),
    ("assets", check_assets, "the catalogue of resources against the matching build's regions"),
)


def selected_checks(args, complete):
    """Calibration checks need both an explicit batch and the whole library."""
    for name, fn, what in CHECKS:
        if name in ("timing", "vblank") and not (args.all_movies and complete):
            continue
        if not getattr(args, "no_" + name.replace("-", "_")):
            yield name, fn, what


def main():
    ap = argparse.ArgumentParser(prog="python3 tools/suite.py")
    ap.add_argument("--ct-dev", type=pathlib.Path, required=True, help="the ct_dev to run")
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "build/suite")
    ap.add_argument("--only", choices=("all", "core", "saves"), default="all")
    ap.add_argument("--movies", nargs="*", default=[], help="run only these, by name")
    ap.add_argument("--all-movies", action="store_true",
                    help="run the whole library, including timing and vblank checks, "
                         "for the calibration batch and baseline updates")
    ap.add_argument("-j", "--jobs", type=int, default=0,
                    help="concurrent movies (default: half the CPUs, at least 1)")
    ap.add_argument("--routes-scratch", type=pathlib.Path,
                    help="keep the routes' runs here and resume them from their checkpoints where the build is the "
                         "same (route_check --resume): a long run cut short carries on on the next")
    ap.add_argument("--long-routes", action="store_true",
                    help="run every route, the ending ones too (most of an hour), not only the fast one")
    ap.add_argument("--evidence", action="store_true",
                    help="re-run the unreached-path fuzz evidence")
    for name, _fn, what in CHECKS:
        ap.add_argument("--no-%s" % name, action="store_true", help="skip " + what)
    args = ap.parse_args()
    binary = args.ct_dev.resolve()
    args.out = args.out.resolve()
    if not binary.exists():
        sys.exit("%s: not found (build it first)" % args.ct_dev)
    if not args.rom.exists():
        sys.exit("%s: not found (tools/rom.py stage <path>)" % args.rom)

    try:
        movies, what = select_movies(args.only, args.movies, args.all_movies)
    except (OSError, ValueError) as error:
        print("suite: FAILED: %s" % error, file=sys.stderr)
        return 1
    if not movies:
        sys.exit("no movies to run")
    jobs = args.jobs or max(1, (os.cpu_count() or 2) // 2)
    args.out.mkdir(parents=True, exist_ok=True)
    for stale in (list(args.out.glob("status-*.txt")) + list(args.out.glob("end-*.bin")) +
                  list(args.out.glob("outcome-*.txt"))):
        stale.unlink()
    shutil.rmtree(args.out / "outcomes", ignore_errors=True)

    # Longest first: the three endings are more than half the suite, and one
    # started late sets the wall time while the other workers sit idle.
    movies.sort(key=lambda m: -m[2])
    import_assets(binary, args.rom, args.out / "ct-usa.assets")
    print("%d movies (%s), %d at a time" % (len(movies), what, jobs), flush=True)

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
    # Wall time per stage, so a gate that has grown slow says where.
    times = [("movies", time.time() - started)]

    library = len(collect("all", set()))
    ctx = {"binary": binary, "jobs": jobs, "movies": [m[0] for m in movies], "movie_rows": movies,
           "library": library, "complete": library == len(movies)}
    failed = ["movies: " + ", ".join(bad_movies)] if bad_movies else []
    for name, fn, _what in selected_checks(args, ctx["complete"]):
        started = time.time()
        ok, label, summary, details = fn(args, ctx)
        times.append((name, time.time() - started))
        print("%s: %s%s" % (label, summary, "" if ok else "  <-- FAILED"), flush=True)
        for line in details:
            print("  " + line)
        if not ok:
            failed.append(name)

    print("times: " + ", ".join("%s %dm" % (name, round(s / 60))
                                for name, s in sorted(times, key=lambda t: -t[1])), flush=True)
    if failed:
        print("FAILED: " + "; ".join(failed))
        return 1
    print("suite OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
