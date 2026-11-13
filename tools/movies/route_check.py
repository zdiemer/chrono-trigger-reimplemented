"""Run native routes; default to the fast New Game+ and inventory checks.

Other routes run explicitly by name; --resume retains checkpoints for the same build.
"""
import argparse
import hashlib
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Fast gameplay outcomes: New Game+ and cartridge-loaded inventory item use.
DEFAULT_ROUTES = ["new_game_plus", "menu_sweep:items"]
OUTCOME = re.compile(r"^outcome (reached|failed) (.*)$")
CHECKPOINT = re.compile(r"^checkpoint (\S+) frame (\d+)")


def all_routes():
    """Public route modules; underscore-prefixed files are shared helpers."""
    from pathlib import Path
    return sorted(p.stem for p in (Path(REPO) / 'tools/movies/routes').glob('*.py')
                  if not p.stem.startswith('_'))


def file_digest(path):
    """The SHA-256 of a file, or of nothing where there is none."""
    digest = hashlib.sha256()
    if path and os.path.exists(path):
        with open(path, "rb") as f:
            for block in iter(lambda: f.read(1 << 20), b""):
                digest.update(block)
    return digest.hexdigest()


def run_stamp(route, rom, drive):
    """What a run's checkpoints are only good for: the route, the
    build and ROM that played them, and the route's script and runner that
    chose its inputs."""
    if __package__:
        from .driver import DEFAULT_DRIVE
    else:  # run as a file, tools/movies/route_check.py
        sys.path.insert(0, REPO)
        from tools.movies.driver import DEFAULT_DRIVE
    drive = drive or os.environ.get("CT_NATIVE") or DEFAULT_DRIVE
    rom = rom or os.environ.get("CT_ROM") or os.path.join(REPO, "roms/ct-usa.sfc")
    name = route.partition(":")[0]
    script = os.path.join(REPO, "tools", "movies", "routes", name + ".py")
    from pathlib import Path
    runner = hashlib.sha256()
    for source in sorted((Path(REPO) / "tools/movies").rglob("*.py")):
        runner.update(str(source.relative_to(REPO)).encode())
        runner.update(source.read_bytes())
    return "route %s\nct_native %s\nrom %s\nscript %s\nrunner %s\n" % (
        route, file_digest(drive), file_digest(rom), file_digest(script), runner.hexdigest())


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


def runtime_faults(lines):
    """The lines a run's ct_native printed that fail any run of it (tools/outcomes.py's
    faults, an overlay crossing among them), as the movie stages fail on them."""
    if __package__:
        from ..outcomes import FAULT
    else:  # run as a file, tools/movies/route_check.py
        sys.path.insert(0, REPO)
        from tools.outcomes import FAULT
    return [line for line in lines if FAULT.search(line)]


def carried_fight(name, lines):
    """For a route that carries a gate movie's fight (tools/fight_routes.txt),
    what its run's fight retries and facts get wrong (tools/movies/fight_facts.py);
    nothing for any other route."""
    if __package__:
        from . import fight_facts
        from .. import outcomes
    else:  # run as a file, tools/movies/route_check.py
        sys.path.insert(0, REPO)
        from tools.movies import fight_facts
        from tools import outcomes
    bounds = outcomes.fight_retries()
    if name not in bounds:
        return []
    return fight_facts.verdict(name, lines, bounds[name], fight_facts.read_expected().get(name, {}))


def run_one(route, scratch, rom, drive, timeout, resume=False, protocol_log=False):
    """Run one native route and report its asserted outcome."""
    name, _, until = route.partition(":")
    run_name = route.replace(":", "-")
    where = os.path.join(scratch, run_name)
    resumed = prepare(where, run_stamp(route, rom, drive), resume)
    env = None
    if protocol_log:
        env = dict(os.environ, CT_PROTOCOL_LOG=os.path.join(os.path.abspath(where), "protocol.txt"))
    command = [sys.executable, "-m", "tools.movies.routes." + name, "--prefix", os.path.join(where, name)]
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
        process = subprocess.Popen(command, cwd=REPO, stdout=out, stderr=subprocess.STDOUT,
                                   env=env, start_new_session=os.name != "nt")
        try:
            status = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            # A driver stuck in a frame cannot observe EOF on the command pipe.
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
            else:
                os.killpg(process.pid, signal.SIGKILL)
            process.wait()
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
    faults = runtime_faults(lines)
    passed = status == 0 and outcome is not None and outcome[0] == "reached" and not faults
    facts = carried_fight(name, lines) if passed and not until else []
    passed = passed and not facts
    if facts:
        what = "fight facts: " + "; ".join(facts)
    elif faults:
        what = "runtime fault: %s" % faults[0]
    elif passed:
        what = outcome[1]
    elif status is None:
        what = "timed out after %ds" % timeout
    elif outcome:
        what = outcome[1]
    else:
        what = "exit status %d: %s" % (status, lines[-1] if lines else "no output")
    frame = re.search(r"frame (\d+)", what) if passed else None
    return {"route": route, "passed": passed, "what": what, "phases": phases,
            "frame": int(frame.group(1)) if frame else None, "seconds": time.time() - started, "dir": where,
            "resumed": resumed}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("routes", nargs="*", default=DEFAULT_ROUTES)
    parser.add_argument("--scratch")
    parser.add_argument("--rom")
    parser.add_argument("--drive", help="ct_native executable (or CT_NATIVE)")
    parser.add_argument("-j", "--jobs", type=int, choices=(1, 2), default=2)
    parser.add_argument("--timeout", type=int, default=6 * 3600)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--protocol-log", action="store_true")
    args = parser.parse_args(argv)
    if args.resume and not args.scratch:
        parser.error("--resume needs --scratch")
    for name in ("drive", "rom"):
        if getattr(args, name):
            setattr(args, name, os.path.abspath(getattr(args, name)))
    scratch = args.scratch or tempfile.mkdtemp(prefix="route_check-")
    try:
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(run_one, route, scratch, args.rom, args.drive, args.timeout,
                                   args.resume, args.protocol_log) for route in args.routes]
            results = []
            for future in as_completed(futures):
                r = future.result()
                results.append(r)
                print("%s %s: %s (%.1fs)" % ("PASS" if r["passed"] else "FAIL", r["route"], r["what"], r["seconds"]), flush=True)
    finally:
        if not args.scratch:
            shutil.rmtree(scratch, ignore_errors=True)
    return int(any(not r["passed"] for r in results))


if __name__ == "__main__":
    raise SystemExit(main())
