"""The places a run enters, by frame, against the reference play's: how long
a run without the original's time keeps to the original's.

    python3 tools/places.py REFERENCE.log RUN.log          # one movie, place by place
    python3 tools/places.py --ct-native build/main/ct_native --ct-dev build/main/ct_dev --movies gato-battle

A movie's presses are the reference play's, frame for frame, so a run whose
frames aren't the original's (the native program, the hand ports' strict
mode) keeps to the reference only as long as its time does: a load a few
frames short, and a press meant for a menu lands in the field. Where the
party is ($7E:0100, the location's number) is the coarse, robust record of
where a run has got to: `ct_dev --locations` and `ct_native --locations`
print each place entered with the frames run so far, and the suite's shadow
runs (the original's code) are the reference.

The measure is how many of the reference's places a run enters within
ON_TIME frames of the reference's frame, among those it enters in the
reference's order (up to the first that is another place: from there the run
has gone elsewhere). A place entered late or early by more needn't part the
run from the reference, since a movie's walks wait on its presses and the two
can come together again; another place does.
tools/native_check.py holds the native program to it (a ratchet,
tools/places_baseline.txt); tools/strict_check.py reports it for strict mode.
"""

import argparse
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PLACE = re.compile(r"^location: frame (\d+) \$([0-9A-Fa-f]+)", re.M)
# A place entered this near the reference's frame is entered on time: half a
# second, about what a load or a fade the time model gets wrong by moves it.
ON_TIME = 30


def parse(text):
    """[(frame, place)] in the order entered."""
    if isinstance(text, bytes):
        text = text.decode(errors="replace")
    return [(int(f), int(p, 16)) for f, p in PLACE.findall(text)]


class Kept:
    """How a run's places keep to the reference's: the places it enters in the
    reference's order, up to the first that is another place (or none); of
    those, the ones entered on time; and the latest and earliest of them."""

    def __init__(self, reference, run, on_time=ON_TIME):
        self.total = len(reference)
        self.same = 0           # places in the reference's order
        self.kept = 0           # ... of them on time
        self.worst = 0          # the run's frame minus the reference's, the largest either way
        self.parted = None      # (reference frame, reference place, run's (frame, place) or None)
        for i, (frame, place) in enumerate(reference):
            theirs = run[i] if i < len(run) else None
            if theirs is None or theirs[1] != place:
                self.parted = (frame, place, theirs)
                break
            self.same += 1
            off = theirs[0] - frame
            self.kept += abs(off) <= on_time
            if abs(off) > abs(self.worst):
                self.worst = off

    def summary(self):
        text = "%d of %d places on time, %d in the reference's order" % (self.kept, self.total, self.same)
        if self.worst:
            text += " (at worst %+d frames)" % self.worst
        if self.parted:
            frame, place, theirs = self.parted
            if theirs is None:
                text += "; no $%03X at %d" % (place, frame)
            else:
                text += "; $%03X at %d where the reference enters $%03X at %d" % (theirs[1], theirs[0], place, frame)
        return text


def reference_places(reference_dir, name):
    """The reference's places from a suite shadow run's log, or None."""
    if not reference_dir:
        return None
    log = reference_dir / ("shadow-%s.log" % name)
    if not log.exists():
        return None
    places = parse(log.read_bytes())
    return places or None


def show(reference, run):
    k = Kept(reference, run)
    for i, (frame, place) in enumerate(reference):
        theirs = run[i] if i < len(run) else None
        right = "$%03X %7d %+6d" % (theirs[1], theirs[0], theirs[0] - frame) if theirs else "-"
        print("$%03X %7d   %s%s" % (place, frame, right, "   <--" if i == k.same else ""))
    print(k.summary())


def run_text(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return proc.stderr.decode(errors="replace")


def main():
    ap = argparse.ArgumentParser(prog="python3 tools/places.py")
    ap.add_argument("logs", nargs="*", type=pathlib.Path, help="a reference's log and a run's, each with location lines")
    ap.add_argument("--ct-native", type=pathlib.Path)
    ap.add_argument("--ct-dev", type=pathlib.Path, help="the reference play (--native off), and --strict's runs")
    ap.add_argument("--strict", action="store_true", help="the run is ct_dev's strict mode rather than ct_native")
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--reference", type=pathlib.Path, help="the suite's shadow runs' directory, for the reference")
    ap.add_argument("--movies", nargs="*", default=[])
    args = ap.parse_args()

    if len(args.logs) == 2:
        show(parse(args.logs[0].read_bytes()), parse(args.logs[1].read_bytes()))
        return 0
    from native_check import saves_by_sum, save_for
    from strict_check import STRICT, movies
    if not args.ct_dev or not (args.ct_native or args.strict):
        sys.exit("give two logs, or --ct-dev and --ct-native (or --strict) with --movies")
    saves = saves_by_sum()
    for name, path, frames in movies(set(args.movies)):
        sram = save_for(path, saves)
        sram_args = ["--sram", str(sram)] if sram else []
        base = ["--rom", str(args.rom), "--movie", str(path), "--frames", str(frames), "--locations"] + sram_args
        reference = reference_places(args.reference, name)
        if reference is None:
            reference = parse(run_text([str(args.ct_dev), "--native", "off"] + base))
        if args.strict:
            run = parse(run_text([str(args.ct_dev)] + STRICT + base))
        else:
            run = parse(run_text([str(args.ct_native)] + base))
        print("%s: %s" % (name, Kept(reference, run).summary()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
