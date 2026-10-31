"""How each library movie plays in the native program, and the gate's check
that none ends on a dark screen.

    python3 tools/native_check.py --ct-native build/main/ct_native            # every movie: how it ends
    python3 tools/native_check.py --ct-native build/main/ct_native --check    # the gate
    python3 tools/native_check.py --ct-native build/main/ct_native --movies battle-magus -j 2

`ct_native` is the game with no emulator under it (docs/PLAYING.md, "The
native program"); it says where its C goes no further, and getting to a
movie's last frame says no more than that: a run can get there with its
picture blanked, or somewhere the reference play never went. This runs every
movie in it (a save movie from the save its header names, found by its sum
among oracle/saves) and reports, per movie, one of:

- the frame and the routine where the program's C went no further;
- `dark`: the run got to its last frame, but its picture had been black for
  the last DARK_FRAMES frames or more (`ct_native` draws each frame with the
  native renderer and says how long it has been dark). Small differences in the
  picture are acceptable; a screen gone blank is not, whatever the game is
  doing under it, unless tools/native_dark_exceptions.txt names the movie: a
  run whose time carries it late into a stretch the reference play is itself
  black for, which the line's reason shows;
- `end`: it got to its last frame, showing, and ends where the reference play
  (the original's code) ends: the saved game and save RAM, compared as
  tools/strict_check.py compares them (`ct_native --end-state`);
- `drifts`: the same, but the saved game or save RAM differs from the
  reference's. `ct_native`'s frames keep the original's time only as well as
  the frame-level time model does (docs/REIMPLEMENTATION.md), so a movie's
  presses, recorded against the original, can land on other frames, and a
  long movie go elsewhere: a battle lost, a place entered early. The report
  names what differs.

With each, the places the run entered against the reference's
(tools/places.py): how many it entered on time, among those in the
reference's order, and where it parted from them.

**The check** (`--check`) fails on a run that ends `dark` or crashes (an exit
status other than 0, or a hang), and on one that enters fewer of the
reference's places on time than tools/places_baseline.txt says (the ratchet
the time model is held to; `--update-places` takes what a change brings); the
rest is reported. The reference end states and places come from the suite's
shadow runs (`--reference DIR`, which run with `--locations`), or from a
`--native off` run of `ct_dev` here (`--ct-dev`); with neither, they aren't
compared.
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

import places
from strict_check import ROOT, SavedGame, differences, movies, ram_names, reference_play, seconds_for

PLACES_BASELINE = ROOT / "tools/places_baseline.txt"
# Movies that may end dark, each with why: a run the native program's time
# carries late into a stretch the reference play is itself black for.
DARK_EXCEPTIONS = ROOT / "tools/native_dark_exceptions.txt"

# A picture black this long at the end has gone blank (10 seconds).
DARK_FRAMES = 600
SRAM_CRC = re.compile(r"^# sram crc32 ([0-9A-Fa-f]{8})", re.M)
STOP = re.compile(r"^frame (\d+): the program's C goes no further: (\S+), (.*), runs only on the machine", re.M)
DARK = re.compile(r"^picture: dark since frame (\d+) \((\d+) frame\(s\), to the end\)", re.M)


def saves_by_sum():
    """{crc32: path} of the cartridge saves under oracle/saves."""
    return {zlib.crc32(p.read_bytes()): p for p in sorted((ROOT / "oracle/saves").rglob("*.srm"))}


def save_for(path, saves):
    """The save a movie starts from, None for a power-on movie, or False where it isn't here."""
    m = SRAM_CRC.search(path.read_text(errors="replace"))
    if not m:
        return None
    return saves.get(int(m.group(1), 16), False)


def run_native(binary, rom, path, frames, sram, extra):
    cmd = [str(binary), "--rom", str(rom), "--movie", str(path), "--frames", str(frames)] + extra
    if sram:
        cmd += ["--sram", str(sram)]
    try:
        proc = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=seconds_for(frames))
        return proc.stderr.decode(errors="replace"), proc.returncode
    except subprocess.TimeoutExpired as e:
        return (e.stderr or b"").decode(errors="replace"), None


def reference(args, name, path, frames, work):
    """The reference play's end state and places: the suite's shadow run's, or
    a run of the original's code here (--ct-dev); (None, None) with neither."""
    if not args.ct_dev:
        end = args.reference / ("end-%s.bin" % name) if args.reference else None
        return (end.read_bytes() if end and end.exists() else None), places.reference_places(args.reference, name)
    return reference_play(args.ct_dev.resolve(), args.rom, name, path, frames, args.reference, work)


def run_one(args, name, path, frames, sram, extra, saved, names, work):
    """((how, detail), places.Kept or None): how the movie ended in ct_native,
    and its places against the reference's."""
    if sram is False:
        return ("no save", "its save isn't under oracle/saves"), None
    end = work / ("%s.native.bin" % name)
    text, rc = run_native(args.ct_native.resolve(), args.rom, path, frames, sram,
                          extra + ["--end-state", str(end), "--locations"])
    if args.logs:
        (args.logs / ("%s.log" % name)).write_text(text)
    ref_end, ref_places = reference(args, name, path, frames, work)
    kept = places.Kept(ref_places, places.parse(text)) if ref_places is not None else None
    return ended(text, rc, end, frames, ref_end, saved, names), kept


def ended(text, rc, end, frames, reference, saved, names):
    """(how, detail) for a ct_native run, its end state against the reference's."""
    m = STOP.search(text)
    if m:
        return "stopped", "at frame %s of %d, %s (%s)" % (m.group(1), frames, m.group(2), m.group(3))
    if rc is None:
        return "hung", None
    if rc != 0:
        return "crashed", "exit status %d" % rc
    m = DARK.search(text)
    if m and int(m.group(2)) >= DARK_FRAMES:
        return "dark", "the picture black from frame %s to the end (%s frames)" % (m.group(1), m.group(2))
    if reference is None:
        return "end", "not compared"
    drift = differences(end.read_bytes() if end.exists() else b"", reference, saved, names)
    return ("drifts", drift) if drift else ("end", None)


def main():
    ap = argparse.ArgumentParser(prog="python3 tools/native_check.py")
    ap.add_argument("--ct-native", type=pathlib.Path, required=True)
    ap.add_argument("--ct-dev", type=pathlib.Path, help="for the reference end states the --reference runs don't have")
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--assets", type=pathlib.Path, help="the asset pack (ct_native --assets)")
    ap.add_argument("--movies", nargs="*", default=[])
    ap.add_argument("-j", "--jobs", type=int, default=2)
    ap.add_argument("--logs", type=pathlib.Path, help="keep each run's stderr, with the places it went, as <movie>.log")
    ap.add_argument("--reference", type=pathlib.Path,
                    help="the reference end states, end-<movie>.bin (the suite's shadow runs write them)")
    ap.add_argument("--check", action="store_true",
                    help="fail on a run that ends dark or crashes, or enters fewer places on time than its baseline")
    ap.add_argument("--update-places", action="store_true",
                    help="write the places each movie enters on time as their baseline (tools/places_baseline.txt)")
    args = ap.parse_args()

    rows = movies(set(args.movies))
    if not rows:
        sys.exit("no movies to run")
    if args.logs:
        args.logs.mkdir(parents=True, exist_ok=True)
    extra = ["--assets", str(args.assets)] if args.assets else []
    saves = saves_by_sum()
    saved, names = SavedGame(args.rom), ram_names()
    with tempfile.TemporaryDirectory(prefix="native-") as scratch:
        work = pathlib.Path(scratch)
        if not args.assets:
            # one pack for every run, imported by the first
            extra = ["--assets", str(work / "ct.assets")]
            run_native(args.ct_native.resolve(), args.rom, rows[0][1], 1, None, extra)

        def one(row):
            name, path, frames = row
            return name, run_one(args, name, path, frames, save_for(path, saves), extra, saved, names, work)

        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            done = dict(pool.map(one, rows))
    results = {name: outcome for name, (outcome, _kept) in done.items()}
    kept = {name: k for name, (_outcome, k) in done.items() if k is not None}

    failed, late = [], []
    base = read_places_baseline()
    excepted = read_dark_exceptions()
    for name, _path, frames in rows:
        how, detail = results[name]
        line = "%s: %s%s" % (name, how, (": " + detail) if detail else "")
        if name in kept:
            line += "; " + kept[name].summary()
            must = base.get(name)
            if must is None:
                late.append(name)
                line += "  (not in the places' baseline)"
            elif kept[name].kept < must:
                late.append(name)
                line += "  <-- fewer on time than its baseline (%d)" % must
        if how == "dark" and name in excepted:
            line += "  (excepted: tools/native_dark_exceptions.txt)"
        elif how in ("dark", "crashed", "hung"):
            failed.append(name)
        print(line)
    reached = sum(1 for n, _p, _f in rows if results[n][0] in ("end", "drifts"))
    same = sum(1 for n, _p, _f in rows if results[n] == ("end", None))
    compared = sum(1 for n, _p, _f in rows if results[n][0] == "drifts" or results[n] == ("end", None))
    summary = "%d of %d movies reach their end in the native program, showing; %d of the %d compared as the " \
              "reference play ends" % (reached, len(rows), same, compared)
    if kept:
        summary += "; %d of the reference's %d places entered on time" % (
            sum(k.kept for k in kept.values()), sum(k.total for k in kept.values()))
    if args.update_places:
        base.update({name: k.kept for name, k in kept.items()})
        write_places_baseline(base)
        print(summary + "; places' baseline written")
        return 0
    if args.check and (failed or late):
        print(summary + "; FAILED: " + "; ".join((["dark or crashed: " + ", ".join(failed)] if failed else []) +
                                                (["places: " + ", ".join(late)] if late else [])))
        return 1
    print(summary)
    return 0


PLACES_HEADER = """# How many of the reference play's places each library movie enters on time in
# the native program (tools/native_check.py, tools/places.py): within
# places.ON_TIME frames of the reference's frame, among the places it enters in
# the reference's order. The gate fails a movie that enters fewer;
# `tools/native_check.py --update-places` takes the counts a change brings.
"""


def read_dark_exceptions():
    """The movies tools/native_dark_exceptions.txt lets end dark: a name a line, then `# why`."""
    if not DARK_EXCEPTIONS.exists():
        return set()
    return {line.partition("#")[0].strip() for line in DARK_EXCEPTIONS.read_text().splitlines()} - {""}


def read_places_baseline():
    base = {}
    if PLACES_BASELINE.exists():
        for line in PLACES_BASELINE.read_text().splitlines():
            fields = line.partition("#")[0].split()
            if len(fields) == 2:
                base[fields[0]] = int(fields[1])
    return base


def write_places_baseline(base):
    order = [name for name, _p, _f in movies(set())]
    lines = [PLACES_HEADER.rstrip("\n")] + ["%s %d" % (name, base[name]) for name in order if name in base]
    PLACES_BASELINE.write_text("\n".join(lines) + "\n", newline="\n")


if __name__ == "__main__":
    sys.exit(main())
