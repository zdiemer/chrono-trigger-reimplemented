"""Run routes as a test of a native-code mode, and report each outcome.

Movies can't test `--native on --recomp on`, the mode the player ships in: hand
ports charge estimated time there, so a movie's presses land on the wrong
frames. A route decides what to press from the game's memory, so it plays the
same scene whatever the timing; whether it still reaches its goal (an ending, a
battle won, a place) is the test (docs/MOVIES.md, "Routes as a test").

    python3 -m tools.movies.route_check                     # the default set, shipping mode
    python3 -m tools.movies.route_check --mode off --mode on:on beyond_time telepod_lavos:shell

A route is `name` (routes/<name>.py, to its end) or `name:phase` (stopped at that
phase's checkpoint). A mode is `off` (the interpreter) or `<native>:<recomp>`,
ct_drive's --native and --recomp. Every route runs in every mode, from its own
cartridge save and from scratch, in `--scratch` (checkpoints, logs and screens;
not the repo); the table gives each outcome with the frame it was reached at,
modes side by side. Frame counts differ between modes by design; the outcome is
what must match. Exits 0 only if every run reached its outcome.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# A field route (the fair to the telepod: the world map, walking, talking and
# dialogue) and a battle route (two Lavos fights, then the ending's walks).
DEFAULT_ROUTES = ["telepod_lavos:shell", "beyond_time"]
DEFAULT_MODES = ["on:on"]

OUTCOME = re.compile(r"^outcome (reached|failed) (.*)$")
CHECKPOINT = re.compile(r"^checkpoint (\S+) frame (\d+)")


def parse_mode(text):
    if text == "off":
        return "off", None
    native, _, recomp = text.partition(":")
    if native not in ("off", "shadow", "on") or recomp not in ("", "off", "on", "only"):
        raise argparse.ArgumentTypeError("a mode is off or <off|shadow|on>:<off|on|only>, not %r" % text)
    return native, recomp or "on"


def mode_name(mode):
    native, recomp = mode
    return "off" if native == "off" else "%s:%s" % (native, recomp)


def run_one(route, mode, scratch, rom, drive, timeout):
    """Run one route in one mode; returns a result dict."""
    name, _, until = route.partition(":")
    where = os.path.join(scratch, "%s-%s" % (route.replace(":", "-"), mode_name(mode).replace(":", "-")))
    shutil.rmtree(where, ignore_errors=True)
    os.makedirs(where)
    command = [sys.executable, "-m", "tools.movies.routes." + name, "--prefix", os.path.join(where, name),
               "--native", mode[0]]
    if mode[1]:
        command += ["--recomp", mode[1]]
    if until:
        command += ["--until", until]
    if rom:
        command += ["--rom", rom]
    if drive:
        command += ["--drive", drive]
    started = time.time()
    with open(os.path.join(where, "output.txt"), "w", encoding="utf-8") as out:
        try:
            status = subprocess.run(command, cwd=REPO, stdout=out, stderr=subprocess.STDOUT,
                                    timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            status = None
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
            "frame": int(frame.group(1)) if frame else None, "seconds": time.time() - started, "dir": where}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("routes", nargs="*", default=DEFAULT_ROUTES,
                        help="name or name:phase (default %s)" % " ".join(DEFAULT_ROUTES))
    parser.add_argument("--mode", action="append", type=parse_mode,
                        help="off or <native>:<recomp>, repeatable (default on:on, the shipping mode)")
    parser.add_argument("--scratch", help="where runs go (default a temporary directory, removed afterwards)")
    parser.add_argument("--rom", help="ROM (default roms/ct-usa.sfc, or $CT_ROM)")
    parser.add_argument("--drive", help="ct_drive binary (default build/main/ct_drive, or $CT_DRIVE)")
    parser.add_argument("-j", "--jobs", type=int, default=1, help="routes run at once (default 1)")
    parser.add_argument("--timeout", type=int, default=6 * 3600, help="seconds per run (default 6 hours)")
    args = parser.parse_args(argv)
    modes = args.mode or [parse_mode(mode) for mode in DEFAULT_MODES]

    scratch = args.scratch or tempfile.mkdtemp(prefix="route_check-")
    jobs = [(route, mode) for route in args.routes for mode in modes]
    try:
        with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            futures = [pool.submit(run_one, route, mode, scratch, args.rom, args.drive, args.timeout)
                       for route, mode in jobs]
            results = []
            for future in futures:
                result = future.result()
                results.append(result)
                print("%s %s in %s: %s (%.0fs)" % ("PASS" if result["passed"] else "FAIL", result["route"],
                                                   result["mode"], result["what"], result["seconds"]), flush=True)
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
