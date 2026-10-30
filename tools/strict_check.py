"""How far the hand ports alone carry each library movie, and the gate's
ratchet on it.

    python3 tools/strict_check.py --ct-dev build/main/ct_dev            # every movie: how it ends
    python3 tools/strict_check.py --ct-dev build/main/ct_dev --check    # the ratchet (the gate)
    python3 tools/strict_check.py --ct-dev build/main/ct_dev --update   # take today's outcomes as the baseline
    python3 tools/strict_check.py --ct-dev build/main/ct_dev --movies menu field -j 2

The product has no emulator in it, so what the hand ports don't cover has
nothing to run. `ct_dev --native on --generated off --no-interpreter strict
--sound-driver c` plays a movie that way (docs/PLAYING.md, "Without the
interpreter"): the hand ports and the C sound driver, no code generated from
the ROM and no interpreter, and the first instruction no port runs stops the
run (`frame F: no generated code for BB:AAAA ...; stopping`). This runs every
movie so and reports, per movie, one of three outcomes:

- `end`: the run gets to the movie's last frame and exits 0 with no `halted:`
  line (an STP, or an instruction nothing runs), no `accessor out of range`
  line and no `page refused` line (docs/DISASSEMBLY.md, "The accessors"), and
  it ends where the reference play (the original's code, `--native off`) ends:
  the saved game -- the eight runs of work RAM a save holds, as the save code's
  table at `$FF:FA7B` lists them (the party and its records, the items, the
  story's flags, where the party is) -- and the cartridge's save RAM are the
  reference's byte for byte (`ct_dev --end-state`), but for the cells the
  game's own timing writes (TIMED_CELLS);
- `drifts`: the same, except that the saved game or save RAM differs from the
  reference's. Hand ports estimate their own time, so the shipping mode runs a
  loading or a fade a frame or more faster or slower than the original, and a
  movie's presses, recorded against the original, land on other frames: a
  menu's cursor stops on another item, a battle's random numbers start from
  another frame count and its blows land otherwise, the story gets a step
  further by the last frame. The strict run is the shipping mode's, byte for
  byte, where nothing falls back (new-game was measured so). The report names
  the work RAM variables that differ, and the baseline line says so;
- the frame and address where it stopped, or the fault it ended with.

Nothing else is compared: the rest of work RAM, video memory and the sprites
hold timers, animation phases, scroll and buffers that the same timing moves;
the vblank check and the routes answer for the shipping mode's picture and
play. The reference end states come from the suite's shadow runs
(`--reference DIR`, which run the original's code), or from a `--native off`
run of each movie here.

**The ratchet** (`--check`, tools/strict_baseline.txt): a line per movie, its
name and `end`, `drifts` or the frame it must reach, and after `#` why. It
fails where a movie does worse than its line -- an `end` that drifts, a
`drifts` or `end` that doesn't reach its end cleanly, a frame not reached --
and on a movie the baseline doesn't list. A fault (a stray accessor, a refused
page, a failed exit, a hang) reaches no frame. A movie that does better is
reported for `--update` to take; `--update` keeps the reasons of lines whose
outcome is unchanged and writes what it found for the rest, which a reader
replaces with what the game is doing there.

The runs need no shadow mode, so they are fast; the suite runs them with
-j 2, taking the reference end states from its shadow runs.
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import tomllib

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tools/strict_baseline.txt"

STRICT = ["--native", "on", "--generated", "off", "--no-interpreter", "strict", "--sound-driver", "c"]
STOP = re.compile(r"^frame (\d+): no generated code for (\S+)", re.M)
FRAMES = re.compile(r"^# frames (\d+)", re.M)
# What a run that got to its last frame must not print (dev/ct_dev.c).
HALTED = re.compile(r"^halted: frame (\d+) at (\S+)", re.M)
FAULTS = (("accessor out of range", re.compile(r"^.*accessor out of range.*$", re.M)),
          ("page refused", re.compile(r"^page refused: .*$", re.M)))

# --end-state's layout: work RAM, then save RAM.
WRAM_BYTES, SRAM_BYTES = 0x20000, 0x2000
# The save code's table of what a saved game holds ($FF:FA7B, file offset):
# eight runs of work RAM, five bytes each, a 24-bit address and a length.
SAVED_RUNS_AT, SAVED_RUNS, SAVED_RUN_BYTES = 0x3FFA7B, 8, 5
# Save RAM: three slots of a saved game each (menu/saves.h), and the slots'
# sums, which cover the timed cells too.
SAVE_SLOTS, SAVED_GAME_BYTES, SLOT_SUMS = 3, 0xA00, range(0x1FF0, 0x1FF6)
# The saved cells the game's own timing writes, left out: the play clock
# ($7E:0400, six digits, a frame a tick), and the menus' frame count as they
# left for the game ($7E:29AD), which the battles start their random numbers
# from.
TIMED_CELLS = set(range(0x0400, 0x0406)) | {0x29AD}


def movies(names):
    """The library's movies, as (name, path, frames): the power-on ones, then
    the saves'."""
    out = []
    groups = ["oracle/movies", "oracle/movies/saves"]
    if names:
        groups.append("oracle/movies/hand")
    for group in groups:
        for p in sorted((ROOT / group).glob("*.ctm")):
            if names and p.stem not in names:
                continue
            m = FRAMES.search(p.read_text(errors="replace"))
            if m:
                out.append((p.stem, p, int(m.group(1))))
    return out


def seconds_for(frames):
    """Far more than a run takes: one that takes longer has hung."""
    return 120 + frames // 50


class SavedGame:
    """Which bytes of the end state are compared: the work RAM offsets a saved
    game holds, and the save RAM offsets, each without the timed cells."""

    def __init__(self, rom):
        table = rom.read_bytes()[SAVED_RUNS_AT:SAVED_RUNS_AT + SAVED_RUNS * SAVED_RUN_BYTES]
        held = []
        for i in range(SAVED_RUNS):
            run = table[i * SAVED_RUN_BYTES:(i + 1) * SAVED_RUN_BYTES]
            addr, bank, length = run[0] | run[1] << 8, run[2], run[3] | run[4] << 8
            base = (bank - 0x7E) << 16 if bank in (0x7E, 0x7F) else 0   # bank $00's low 8 KiB is work RAM's
            held += [base + addr + k for k in range(length)]
        timed_places = {place for place, o in enumerate(held) if o in TIMED_CELLS}
        self.wram = [o for o in held if o not in TIMED_CELLS]
        left_out = {slot * SAVED_GAME_BYTES + place for slot in range(SAVE_SLOTS) for place in timed_places}
        left_out |= set(SLOT_SUMS)
        self.sram = [o for o in range(SRAM_BYTES) if o not in left_out]


def ram_names():
    """Work RAM offset -> the RAM map's variable there (the first declared)."""
    names = {}
    data = tomllib.loads((ROOT / "re/ram.toml").read_text())
    for v in data.get("var", []):
        if "addr" not in v:
            continue
        bank, at = v["addr"].split(":")
        start = (int(bank, 16) - 0x7E) << 16 | int(at, 16) if int(bank, 16) in (0x7E, 0x7F) else int(at, 16)
        size = v.get("size", 1) if isinstance(v.get("size", 1), int) else 1
        for o in range(start, start + size * v.get("count", 1)):
            names.setdefault(o, v["name"])
    return names


def differences(state, reference, saved, names):
    """Where the end state differs from the reference's, in the compared
    bytes: None, or a description naming the variables."""
    if len(state) != WRAM_BYTES + SRAM_BYTES or len(reference) != WRAM_BYTES + SRAM_BYTES:
        return "no end state to compare"
    game = [o for o in saved.wram if state[o] != reference[o]]
    sram = [o for o in saved.sram if state[WRAM_BYTES + o] != reference[WRAM_BYTES + o]]
    if not game and not sram:
        return None
    parts = []
    if game:
        cells = []
        for o in game:
            name = names.get(o, "$%02X:%04X" % (0x7E + (o >> 16), o & 0xFFFF))
            if name not in cells:
                cells.append(name)
        parts.append("%d byte(s) of the saved game (%s)" % (len(game), ", ".join(cells[:6]) +
                                                            (", ..." if len(cells) > 6 else "")))
    if sram:
        parts.append("%d byte(s) of save RAM" % len(sram))
    return "; ".join(parts)


def run_ct_dev(binary, rom, path, frames, extra):
    """stderr and exit status (None where it hung)."""
    cmd = [str(binary), "--rom", str(rom), "--movie", str(path), "--frames", str(frames)] + extra
    try:
        proc = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=seconds_for(frames))
        return proc.stderr.decode(errors="replace"), proc.returncode
    except subprocess.TimeoutExpired as e:
        return (e.stderr or b"").decode(errors="replace"), None


def reference_end(binary, rom, name, path, frames, reference_dir, work):
    """The reference play's end state: the suite's shadow run's, or a run of the original's code here."""
    if reference_dir:
        kept = reference_dir / ("end-%s.bin" % name)
        if kept.exists():
            return kept.read_bytes()
    out = work / ("%s.reference.bin" % name)
    run_ct_dev(binary, rom, path, frames, ["--native", "off", "--end-state", str(out)])
    return out.read_bytes() if out.exists() else b""


# The outcomes, as (frame reached or None for the end, detail, how).
def run_one(binary, rom, name, path, frames, log, saved, names, reference_dir, work):
    """One movie strict, and how it ended."""
    end = work / ("%s.strict.bin" % name)
    text, rc = run_ct_dev(binary, rom, path, frames, STRICT + ["--end-state", str(end)])
    if log:
        log.write_text(text)
    m = STOP.search(text)
    if m:
        return int(m.group(1)), m.group(2), "stopped"
    m = HALTED.search(text)
    if m:
        return int(m.group(1)), m.group(2), "halted"
    if rc is None:
        return 0, None, "hung"
    for what, pattern in FAULTS:
        m = pattern.search(text)
        if m:
            return 0, m.group(0).strip(), what
    if rc != 0:
        return 0, None, "exit status %d" % rc
    drift = differences(end.read_bytes() if end.exists() else b"",
                        reference_end(binary, rom, name, path, frames, reference_dir, work), saved, names)
    if drift:
        return None, drift, "drifts"
    return None, None, "end"


def rank(outcome, frames):
    """Outcomes in order, better last: a frame reached, then drifting, then the end."""
    stop, _detail, how = outcome
    if how == "end":
        return (2, frames)
    if how == "drifts":
        return (1, frames)
    return (0, stop)


def read_baseline(path):
    """{movie: (`end`, `drifts` or the frame it must reach, the line's reason)}."""
    base = {}
    if not path.exists():
        return base
    for line in path.read_text().splitlines():
        body, _, why = line.partition("#")
        fields = body.split()
        if len(fields) != 2:
            continue
        base[fields[0]] = (fields[1] if fields[1] in ("end", "drifts") else int(fields[1]), why.strip())
    return base


def baseline_rank(must, frames):
    if must == "end":
        return (2, frames)
    if must == "drifts":
        return (1, frames)
    return (0, must)


def describe(result, frames):
    stop, detail, how = result
    if how == "end":
        return "end (%d frames)" % frames
    if how == "drifts":
        return "drifts (%d frames): %s" % (frames, detail)
    if how == "stopped":
        return "stops at frame %d of %d at %s" % (stop, frames, detail)
    if how == "halted":
        return "halts at frame %d of %d at %s" % (stop, frames, detail)
    if detail:
        return "%s: %s (of %d frames)" % (how, detail, frames)
    return "%s (of %d frames)" % (how, frames)


HEADER = """# How each library movie ends on the hand ports alone (tools/strict_check.py):
# `ct_dev --native on --generated off --no-interpreter strict --sound-driver c`.
# A line per movie: `end` (its last frame, with no halt, stray accessor or
# refused page, and the saved game and save RAM as the reference play leaves
# them, but for the play clock and the battles' random start), `drifts` (the
# same, but the shipping mode's own timing has moved the play, so the saved
# game differs from the reference's where the line says), or the frame it must
# reach; then after `#` why. The gate fails where a movie does worse;
# `--update` takes a movie that does better.
"""


def write_baseline(path, rows, results, old):
    lines = [HEADER.rstrip("\n")]
    for name, _path, frames in rows:
        stop, detail, how = results[name]
        kept = old.get(name)
        if how == "end":
            lines.append("%s end" % name)
            continue
        if how == "drifts":
            why = kept[1] if kept and kept[0] == "drifts" and kept[1] else "differs from the reference: " + detail
            lines.append("%s drifts  # %s" % (name, why))
            continue
        why = kept[1] if kept and kept[0] == stop and kept[1] else (
            "at %s" % detail if how in ("stopped", "halted") else describe(results[name], frames))
        lines.append("%s %d  # %s" % (name, stop, why))
    path.write_text("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(prog="python3 tools/strict_check.py")
    ap.add_argument("--ct-dev", type=pathlib.Path, required=True)
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--movies", nargs="*", default=[])
    ap.add_argument("-j", "--jobs", type=int, default=2)
    ap.add_argument("--baseline", type=pathlib.Path, default=BASELINE)
    ap.add_argument("--logs", type=pathlib.Path, help="keep each run's stderr here, as <movie>.log")
    ap.add_argument("--reference", type=pathlib.Path,
                    help="the reference end states, end-<movie>.bin (the suite's shadow runs write them); "
                         "a movie with none here is run with --native off")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="fail where a movie does worse than its baseline")
    mode.add_argument("--update", action="store_true", help="write today's outcomes as the baseline")
    args = ap.parse_args()

    rows = movies(set(args.movies))
    if not rows:
        sys.exit("no movies to run")
    if args.logs:
        args.logs.mkdir(parents=True, exist_ok=True)

    saved, names = SavedGame(args.rom), ram_names()
    with tempfile.TemporaryDirectory(prefix="strict-") as scratch:
        work = pathlib.Path(scratch)

        def one(row):
            name, path, frames = row
            log = args.logs / ("%s.log" % name) if args.logs else None
            return name, run_one(args.ct_dev.resolve(), args.rom, name, path, frames, log, saved, names,
                                 args.reference, work)

        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            results = dict(pool.map(one, rows))

    base = read_baseline(args.baseline)
    worse, better, unlisted = [], [], []
    for name, _path, frames in rows:
        line = "%s: %s" % (name, describe(results[name], frames))
        if name not in base:
            unlisted.append(name)
            line += "  (not in the baseline)"
        else:
            must = base[name][0]
            got, wanted = rank(results[name], frames), baseline_rank(must, frames)
            if got < wanted:
                worse.append(name)
                line += "  <-- worse than its baseline (%s)" % must
            elif got > wanted:
                better.append(name)
                line += "  (better than its baseline, %s)" % must
        print(line)

    count = {how: sum(1 for n, _p, _f in rows if results[n][2] == how) for how in ("end", "drifts")}
    summary = "%d of %d movies reach their end on the hand ports alone, %d of them as the reference play ends" % (
        count["end"] + count["drifts"], len(rows), count["end"])
    if args.update:
        old = read_baseline(args.baseline)
        # Keep omitted library movies and opt-in hand runs as they were.
        for name, _path, _frames in movies(set(old) | set(results)):
            if name in old and name not in results:
                must = old[name][0]
                results[name] = (None, old[name][1], must) if must in ("end", "drifts") else (
                    must, None, "stopped")
        rows = [r for r in movies(set(old) | set(results)) if r[0] in results]
        write_baseline(args.baseline, rows, results, old)
        print(summary + "; baseline written")
        return 0
    if args.check:
        if better:
            print("better than the baseline (--update takes them): " + ", ".join(better))
        if worse or unlisted:
            print(summary + "; FAILED: " + ", ".join(
                (["worse: " + ", ".join(worse)] if worse else []) +
                (["not in the baseline: " + ", ".join(unlisted)] if unlisted else [])))
            return 1
    print(summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
