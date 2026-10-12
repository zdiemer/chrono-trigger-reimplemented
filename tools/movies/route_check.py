"""Run routes as a test of a native-code mode, and report each outcome.

Movies can't test `--native on --generated on` with the C sound driver, the mode
the player ships in: hand ports charge estimated time there, and the driver's
commands take none, so a movie's presses land on the wrong frames. A route decides what to press from the game's memory, so it plays the
same scene whatever the timing; whether it still reaches its goal (an ending, a
battle won, a place) is the test (docs/MOVIES.md, "Routes as a test").

    python3 -m tools.movies.route_check                     # the default set, shipping mode
    python3 -m tools.movies.route_check --mode off --mode on:on beyond_time telepod_lavos:shell

A route is `name` (routes/<name>.py, to its end) or `name:phase` (stopped at that
phase's checkpoint). A mode is `off` (the interpreter) or
`<native>:<generated>[:<sound>]`, ct_drive's --native, --generated and
--sound-driver (`original`, the default, or `c`, which needs --native on); the
shipping mode is `on:on:c`, the default. Every route runs in every mode, from its own
cartridge save and from scratch, in `--scratch` (checkpoints, logs and screens;
not the repo); the table gives each outcome with the frame it was reached at,
modes side by side. Frame counts differ between modes by design; the outcome is
what must match. Exits 0 only if every run reached its outcome.

`--fallbacks DIR` has each run in `--native on` count what the original's
code runs in place of hand ports (ct_drive --fallbacks, through
$CT_DRIVE_FALLBACKS), kept with the run's checkpoints and copied to DIR as
`<route>-<mode>/` when it ends, for tools/fallbacks.py (the gate's fallback
measure; docs/REIMPLEMENTATION.md, "The fallback measure").

`--resume` (with `--scratch`) keeps each run's directory where the last run of
the same route, mode, ct_drive, ROM and route scripts left it, so a run cut short -- a time
limit, a restart -- carries on from its last checkpoint instead of replaying the
hour before it. A run whose build, ROM or scripts differ from the ones that wrote
the checkpoints starts over, so no outcome rests on phases another build played.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# A field route (the fair to the telepod: the world map, walking, talking and
# dialogue) and a battle route (two Lavos fights, then the ending's walks).
DEFAULT_ROUTES = ["telepod_lavos:shell", "beyond_time"]
DEFAULT_MODES = ["on:on:c"]

OUTCOME = re.compile(r"^outcome (reached|failed) (.*)$")
CHECKPOINT = re.compile(r"^checkpoint (\S+) frame (\d+)")


def parse_mode(text):
    if text == "off":
        return "off", None, "original"
    native, _, rest = text.partition(":")
    generated, _, sound = rest.partition(":")
    if native not in ("off", "shadow", "on") or generated not in ("", "off", "on", "only") or \
            sound not in ("", "original", "c"):
        raise argparse.ArgumentTypeError("a mode is off or <off|shadow|on>:<off|on|only>[:<original|c>], not %r"
                                         % text)
    if sound == "c" and (native != "on" or generated == "only"):
        raise argparse.ArgumentTypeError("the C sound driver needs --native on with the ported routines: %r" % text)
    return native, generated or "on", sound or "original"


def mode_name(mode):
    native, generated, sound = mode
    if native == "off":
        return "off"
    return "%s:%s%s" % (native, generated, "" if sound == "original" else ":" + sound)


def file_digest(path):
    """The SHA-256 of a file, or of nothing where there is none."""
    digest = hashlib.sha256()
    if path and os.path.exists(path):
        with open(path, "rb") as f:
            for block in iter(lambda: f.read(1 << 20), b""):
                digest.update(block)
    return digest.hexdigest()


def run_stamp(route, mode, rom, drive):
    """What a run's checkpoints are only good for: the route, the mode, the
    build and ROM that played them, and the route's script and runner that
    chose its inputs."""
    drive = drive or os.environ.get("CT_DRIVE") or os.path.join(REPO, "build/main/ct_drive")
    rom = rom or os.environ.get("CT_ROM") or os.path.join(REPO, "roms/ct-usa.sfc")
    name = route.partition(":")[0]
    script = os.path.join(REPO, "tools", "movies", "routes", name + ".py")
    runner = os.path.join(REPO, "tools", "movies", "campaign.py")
    return "route %s\nmode %s\nct_drive %s\nrom %s\nscript %s\nrunner %s\n" % (
        route, mode_name(mode), file_digest(drive), file_digest(rom), file_digest(script), file_digest(runner))


STAMP = "run-stamp.txt"


def prepare(where, stamp, resume):
    """The run's directory: kept where resuming and it holds this very run's
    checkpoints, otherwise emptied. True where it was kept."""
    kept = False
    if resume and os.path.isdir(where):
        try:
            with open(os.path.join(where, STAMP), encoding="utf-8") as f:
                kept = f.read() == stamp
        except OSError:
            kept = False
    if not kept:
        shutil.rmtree(where, ignore_errors=True)
        os.makedirs(where)
        with open(os.path.join(where, STAMP), "w", encoding="utf-8") as f:
            f.write(stamp)
    return kept


def run_one(route, mode, scratch, rom, drive, timeout, resume=False, fallbacks=None):
    """Run one route in one mode; returns a result dict."""
    name, _, until = route.partition(":")
    run_name = "%s-%s" % (route.replace(":", "-"), mode_name(mode).replace(":", "-"))
    where = os.path.join(scratch, run_name)
    resumed = prepare(where, run_stamp(route, mode, rom, drive), resume)
    env = None
    if fallbacks and mode[0] == "on":
        # kept with the checkpoints, so a resumed run's counts add to the
        # ones before it
        counts = os.path.join(where, "fallbacks")
        os.makedirs(counts, exist_ok=True)
        env = dict(os.environ, CT_DRIVE_FALLBACKS=counts)
    command = [sys.executable, "-m", "tools.movies.routes." + name, "--prefix", os.path.join(where, name),
               "--native", mode[0]]
    if mode[1]:
        command += ["--generated", mode[1]]
    if mode[2] != "original":
        command += ["--sound-driver", mode[2]]
    if until:
        command += ["--until", until]
    if rom:
        command += ["--rom", rom]
    if drive:
        command += ["--drive", drive]
    started = time.time()
    # a resumed run adds to the output of the runs before it, whose checkpoints
    # it carries on from
    with open(os.path.join(where, "output.txt"), "a" if resumed else "w", encoding="utf-8") as out:
        try:
            status = subprocess.run(command, cwd=REPO, stdout=out, stderr=subprocess.STDOUT,
                                    timeout=timeout, env=env).returncode
        except subprocess.TimeoutExpired:
            status = None
    if env:
        copy = os.path.join(fallbacks, run_name)
        shutil.rmtree(copy, ignore_errors=True)
        shutil.copytree(env["CT_DRIVE_FALLBACKS"], copy)
    with open(os.path.join(where, "output.txt"), encoding="utf-8", errors="replace") as out:
        lines = out.read().splitlines()
    outcome, phases = None, []
    for line in lines:
        match = OUTCOME.match(line)
        if match:
            outcome = match.groups()
        match = CHECKPOINT.match(line)
        if match:
            phases.append((match.group(1), int(match.group(2))))
    passed = status == 0 and outcome is not None and outcome[0] == "reached"
    if passed:
        what = outcome[1]
    elif status is None:
        what = "timed out after %ds" % timeout
    elif outcome:
        what = outcome[1]
    else:
        what = "exit status %d: %s" % (status, lines[-1] if lines else "no output")
    frame = re.search(r"frame (\d+)", what) if passed else None
    return {"route": route, "mode": mode_name(mode), "passed": passed, "what": what, "phases": phases,
            "frame": int(frame.group(1)) if frame else None, "seconds": time.time() - started, "dir": where,
            "resumed": resumed}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("routes", nargs="*", default=DEFAULT_ROUTES,
                        help="name or name:phase (default %s)" % " ".join(DEFAULT_ROUTES))
    parser.add_argument("--mode", action="append", type=parse_mode,
                        help="off or <native>:<generated>[:<sound driver>], repeatable (default on:on:c, the "
                             "shipping mode)")
    parser.add_argument("--scratch", help="where runs go (default a temporary directory, removed afterwards)")
    parser.add_argument("--rom", help="ROM (default roms/ct-usa.sfc, or $CT_ROM)")
    parser.add_argument("--drive", help="ct_drive binary (default build/main/ct_drive, or $CT_DRIVE)")
    parser.add_argument("-j", "--jobs", type=int, default=1, help="routes run at once (default 1)")
    parser.add_argument("--timeout", type=int, default=6 * 3600, help="seconds per run (default 6 hours)")
    parser.add_argument("--fallbacks", metavar="DIR",
                        help="count what the original's code runs in place of hand ports, a directory per run here "
                             "(tools/fallbacks.py)")
    parser.add_argument("--resume", action="store_true",
                        help="carry on from the checkpoints a run cut short left in --scratch, where the build and "
                             "ROM are the same")
    args = parser.parse_args(argv)
    if args.resume and not args.scratch:
        parser.error("--resume needs --scratch: the checkpoints are kept there")
    # The routes start ct_drive from their own working directory, so a relative
    # path given here would name nothing there: resolve it against ours.
    for name in ("drive", "rom", "fallbacks"):
        if getattr(args, name):
            setattr(args, name, os.path.abspath(getattr(args, name)))
    modes = args.mode or [parse_mode(mode) for mode in DEFAULT_MODES]

    scratch = args.scratch or tempfile.mkdtemp(prefix="route_check-")
    jobs = [(route, mode) for route in args.routes for mode in modes]
    try:
        with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            futures = [pool.submit(run_one, route, mode, scratch, args.rom, args.drive, args.timeout, args.resume,
                                   args.fallbacks)
                       for route, mode in jobs]
            # each outcome as it comes, so a run cut short still reports the
            # routes that finished
            results = []
            for future in as_completed(futures):
                result = future.result()
                results.append(result)
                print("%s %s in %s: %s (%.0fs%s)" % ("PASS" if result["passed"] else "FAIL", result["route"],
                                                     result["mode"], result["what"], result["seconds"],
                                                     ", resumed" if result["resumed"] else ""), flush=True)
    finally:
        if not args.scratch:
            shutil.rmtree(scratch, ignore_errors=True)

    print()
    names = [mode_name(mode) for mode in modes]
    print("%-24s %s" % ("route", "".join("%-26s" % name for name in names)))
    by_key = {(r["route"], r["mode"]): r for r in results}
    for route in args.routes:
        cells = []
        for name in names:
            r = by_key[(route, name)]
            cells.append("%-26s" % (("pass, frame %d" % r["frame"]) if r["passed"] and r["frame"] is not None
                                    else "pass" if r["passed"] else "FAIL"))
        print("%-24s %s" % (route, "".join(cells)))
        phases = sorted({phase for name in names for phase, _ in by_key[(route, name)]["phases"]},
                        key=lambda phase: min(frame for name in names for p, frame in by_key[(route, name)]["phases"]
                                              if p == phase))
        for phase in phases:
            cells = []
            for name in names:
                frames = [frame for p, frame in by_key[(route, name)]["phases"] if p == phase]
                cells.append("%-26s" % (frames[-1] if frames else "-"))
            print("  %-22s %s" % (phase, "".join(cells)))
    failed = [r for r in results if not r["passed"]]
    if failed and args.scratch:
        print("\nfailed runs' output: " + ", ".join(os.path.join(r["dir"], "output.txt") for r in failed))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
