"""An observer never changes play: ct_native's reporting options only look.

A gate movie (title-start) is played with no reporting option, and again with
each of --timing-report, --locations, --lag-log, --outcomes, --end-state,
--screenshot and --shot-every. The run's end state and last picture must not
change with any of them, and the time the game owed, which any read that
counted as the game's own would move (the timing report, per routine), must
be the same with every other option as with the timing report alone.

Run by ctest with ct_native's path; without a staged ROM (roms/ct-usa.sfc)
it says so and passes.

    python3 tests/frontend/observers.py <ct_native>
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools import native_slots  # noqa: E402

ROM = ROOT / "roms" / "ct-usa.sfc"
MOVIE = ROOT / "movies" / "title-start.ctm"
FRAMES = 1800


def play(binary, pack, *options):
    """The run's report (stderr) with the given options."""
    command = [str(Path(binary).resolve()), "--rom", str(ROM), "--assets", str(pack),
               "--movie", str(MOVIE), "--frames", str(FRAMES), *options]
    with native_slots.slot():
        p = subprocess.run(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=300)
    if p.returncode:
        raise SystemExit("observers: %s exited %d" % (" ".join(options) or "a plain run", p.returncode))
    return p.stderr.decode(errors="replace")


def owed(report):
    return [line for line in report.splitlines() if line.startswith("native time:")]


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if not ROM.exists():
        print("observers: no ROM staged, no movie played")
        return 0
    binary = argv[0]
    failures = []
    with tempfile.TemporaryDirectory(prefix="ct-observers-") as scratch:
        work = Path(scratch)
        pack = work / "pack.assets"

        def ends(tag, *options):
            """The end state and last picture of a run with `options`, and its report."""
            state, picture = work / (tag + ".bin"), work / (tag + ".png")
            report = play(binary, pack, "--end-state", str(state), "--screenshot", str(picture), *options)
            return state.read_bytes() + picture.read_bytes(), report

        plain, _ = ends("plain")
        timed, report = ends("timed", "--timing-report")
        if timed != plain:
            failures.append("--timing-report changes the end state or the picture")
        timing = owed(report)
        shots = work / "shots"
        shots.mkdir()
        for option in (["--locations"], ["--lag-log"], ["--outcomes"], ["--shot-every", "300", "--shot-dir", str(shots)]):
            seen, report = ends(option[0][2:], "--timing-report", *option)
            if seen != plain:
                failures.append("%s changes the end state or the picture" % option[0])
            if owed(report) != timing:
                failures.append("%s changes the time the game owes" % option[0])
        if owed(play(binary, pack, "--timing-report")) != timing:
            failures.append("--end-state or --screenshot changes the time the game owes")
    for failure in failures:
        print("observers: " + failure)
    print("observers: %d failures" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
