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
  doing under it;
- `end`: it got to its last frame, showing, and ends where the reference play
  (the original's code) ends: the saved game and save RAM, compared as
  tools/strict_check.py compares them (`ct_native --end-state`);
- `drifts`: the same, but the saved game or save RAM differs from the
  reference's. `ct_native`'s frames aren't the machine's (nothing in it takes
  the original's time), so a movie's presses, recorded against the original,
  land on other frames, and a long movie goes elsewhere: a battle lost, a
  place entered early. The report names what differs, and `--locations` lists
  the places the run went through, frame by frame, to set beside the
  reference's (`tools/trace.py peek <trace> '$7E:0100' --len 2`).

**The check** (`--check`) fails on a run that ends `dark` or crashes (an exit
status other than 0, or a hang); the rest is reported. The reference end states
come from the suite's shadow runs (`--reference DIR`), or from a `--native off`
run of `ct_dev` here (`--ct-dev`); with neither, the end states aren't compared.
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

from strict_check import ROOT, SavedGame, differences, movies, ram_names, run_ct_dev, seconds_for

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


def reference_end(args, name, path, frames, work):
    if args.reference:
        kept = args.reference / ("end-%s.bin" % name)
        if kept.exists():
            return kept.read_bytes()
    if not args.ct_dev:
        return None
    out = work / ("%s.reference.bin" % name)
    run_ct_dev(args.ct_dev.resolve(), args.rom, path, frames, ["--native", "off", "--end-state", str(out)])
    return out.read_bytes() if out.exists() else b""


def run_one(args, name, path, frames, sram, extra, saved, names, work):
    """(how, detail): how the movie ended in ct_native."""
    if sram is False:
        return "no save", "its save isn't under oracle/saves"
    end = work / ("%s.native.bin" % name)
    text, rc = run_native(args.ct_native.resolve(), args.rom, path, frames, sram,
                          extra + ["--end-state", str(end)] + (["--locations"] if args.logs else []))
    if args.logs:
        (args.logs / ("%s.log" % name)).write_text(text)
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
    reference = reference_end(args, name, path, frames, work)
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
    ap.add_argument("--check", action="store_true", help="fail on a run that ends dark or crashes")
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
            results = dict(pool.map(one, rows))

    failed = []
    for name, _path, frames in rows:
        how, detail = results[name]
        print("%s: %s%s" % (name, how, (": " + detail) if detail else ""))
        if how in ("dark", "crashed", "hung"):
            failed.append(name)
    reached = sum(1 for n, _p, _f in rows if results[n][0] in ("end", "drifts"))
    same = sum(1 for n, _p, _f in rows if results[n] == ("end", None))
    compared = sum(1 for n, _p, _f in rows if results[n][0] == "drifts" or results[n] == ("end", None))
    summary = "%d of %d movies reach their end in the native program, showing; %d of the %d compared as the " \
              "reference play ends" % (reached, len(rows), same, compared)
    if args.check and failed:
        print(summary + "; FAILED (dark or crashed): " + ", ".join(failed))
        return 1
    print(summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
