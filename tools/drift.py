"""Where a movie's strict play first parts from the reference play, and why:
the timing drift strict mode is allowed, or a behaviour difference.

    python3 tools/drift.py --ct-dev build/main/ct_dev --movies events-cyrus
    python3 tools/drift.py --ct-dev build/main/ct_dev --drifting -j 2      # every `drifts` line of the baseline
    python3 tools/drift.py --ct-dev build/main/ct_dev --drifting --baseline  # ... as baseline lines, with the verdict

A movie's presses are the reference play's frame for frame, and the hand
ports estimate their own time, so a strict run (tools/strict_check.py) whose
loads and fades take another number of frames meets those presses at other
moments and can end elsewhere. That is drift. A port that computes something
else is a bug, and ends elsewhere too; the end state can't tell the two
apart. This plays the movie with `ct_dev --state-trace` (work RAM and save
RAM, every frame) three ways -- the reference (the original's code), strict
mode, and the hand ports given the original's time (`--port-timing oracle`:
each port call is charged what the original's code takes for it, and one it
can't time so runs the original's code; both with the C sound driver) -- and
reads two timelines from each:

- **milestones**: the frames on which a place is entered ($7E:0100), a song
  starts (current_song), the field starts to fade in or goes dark
  (field_brightness: a load's end, a scene's cut), a
  battle member's command menu opens (each turn), a text window opens, the
  story steps on (story_line). Each kind is matched in order with the
  reference's, giving the shift d (frames late; negative, early) it lands by;
  a menu or window whose shift jumps by more than JUMP is paired with another
  occasion, and the two part there;
- **the saved game**: the bytes a save holds (strict_check.SavedGame, without
  the play clock and the battles' random start, and without the place and song,
  which are milestones), as the states it passes through (held SETTLE frames or
  more), matched in order the same way, stepping over a state one run passes
  through and the other skips (two changes a frame apart in the other order).

The run parts from the reference at the earliest of: a saved-game state the
reference doesn't pass through there, or a milestone sequence that goes
otherwise (another place, another song), or the end of a run that stops short of
(or goes past) the reference's states. The report says what moved before it --
the milestone that set the shift in force, the play clock's offset (the field
seeds its random draws from it), a battle starting from another random number,
and, as context only (both runs replay the same presses), the input the game
meets there -- but none of that decides the verdict: a strict run is a frame
off from its first load, and a port's mistake later would look the same. The
run with the original's time decides it. The movie is

- **drift** when that run plays as the reference to its end: the original's
  time alone makes the difference;
- **bug** when it parts anywhere or ends short: the state differs for a reason
  time doesn't explain. The report names the frame and the first differing
  variables; `ct_dev --native shadow` and the traces (--keep) find the port;
- **unproven** with --no-timed.

That run times only the calls it can and runs the original's code for the
rest, so the report gives how many of its calls did (from ct_dev's native
report): the verdict answers for time over the ports' calls, and shadow mode
answers for their results.

--baseline prints each movie as a tools/strict_baseline.txt line, keeping as
they are the findings a reader verified by hand (after `hand-verified:` on the
movie's line).

The traces are ROM-derived; they go to a scratch directory and are deleted, or
are kept with --keep DIR (outside the repository).
"""

import argparse
import concurrent.futures
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

if __package__:
    from . import strict_check
    from .trace import Trace, buttons_str
else:
    import strict_check
    from trace import Trace, buttons_str

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Milestones, read from work RAM.
LOCATION_AT, LOCATION_BITS = 0x0100, 0x1FF
CURRENT_SONG = 0x29AE
FIELD_BRIGHTNESS = 0x0119          # field_brightness: 0 while the field is dark
BATTLE_MENU_MEMBER = 0x95D5        # whose battle command menu is open (0-2), else none
TEXT_WINDOW = 0x0127               # non-zero while a text window is open and waiting
STORY_LINE = 0x10000               # $7F:0000
MEMBERS = 3
# The battles' random start ($7E:29AD), a saved cell the timing writes: not
# compared as state, but read at each battle's first menu.
RANDOM_KEPT = 0x29AD
# The play clock ($7E:0400: frames, seconds, minutes, tens of minutes, hours,
# tens of hours), which ticks a frame a frame while the game plays and which
# the game draws from (the field's random index at a place's start,
# $C0:0B86): how far a run's play is ahead of the reference's in game time.
PLAY_TIME, PLAY_TIME_RADICES = 0x0400, (60, 60, 10, 6, 10, 10)
# Saved cells matched as milestones, not as state: the place, the song.
MILESTONE_CELLS = {LOCATION_AT, LOCATION_AT + 1, CURRENT_SONG}
WATCHED = [(LOCATION_AT, LOCATION_AT + 2), (CURRENT_SONG, CURRENT_SONG + 1), (FIELD_BRIGHTNESS, FIELD_BRIGHTNESS + 1),
           (BATTLE_MENU_MEMBER, BATTLE_MENU_MEMBER + 1), (TEXT_WINDOW, TEXT_WINDOW + 1),
           (STORY_LINE, STORY_LINE + 1)]
# A saved-game state held fewer frames than this is a step on the way to the
# next (power-on clearing work RAM, a record copied over two frames).
SETTLE = 4
# Milestones whose values repeat, and the change of shift between two of them
# taken for a pairing with another occasion: two seconds.
REPEATING = {"turn", "text window"}
JUMP = 120
# The timed run's report (ct_dev's native report): each hand port's calls,
# and the calls of the original's code (generated blocks) run in their place
# or where nothing is ported.
PORT_CALLS = re.compile(r"^  [0-9A-F]{6} \S+\s+calls (\d+)", re.M)
GENERATED_CALLS = re.compile(r"^  generated blocks: .* calls (\d+)", re.M)
# The mark on a baseline line's findings that a reader verified by hand;
# --baseline keeps them as they are.
HAND = "hand-verified: "
# The frames before a parting searched for a press the game meets otherwise.
PRESS_WINDOW = 30
# A recorded movie's runs (ct_dev) may take this many times strict_check's
# allowance: the run with the original's time copies the machine at each call.
SLOWER = 4


def milestones(wram):
    member = wram[BATTLE_MENU_MEMBER]
    return {
        "place": (wram[LOCATION_AT] | wram[LOCATION_AT + 1] << 8) & LOCATION_BITS,
        "song": wram[CURRENT_SONG],
        "field lit": wram[FIELD_BRIGHTNESS] != 0,
        "turn": member if member < MEMBERS else None,
        "text window": wram[TEXT_WINDOW] != 0,
        "story": wram[STORY_LINE],
    }


def describe_event(kind, value):
    if kind == "place":
        return "place $%03X entered" % value
    if kind == "song":
        return "song $%02X" % value
    if kind == "field lit":
        return "the field fades in" if value else "the field goes dark"
    if kind == "turn":
        return "member %d's battle menu" % value if value is not None else "battle menu closes"
    if kind == "text window":
        return "text window opens" if value else "text window closes"
    return "story step $%02X" % value


class Timeline:
    """One run, kept small: each milestone's changes, the saved game's states,
    the battles' random start at each first menu, and the movie's input."""

    def __init__(self, path, saved):
        trace = Trace(path)
        wi = trace.region_index("WRAM")
        self.saved_offsets = [o for o in saved.wram if o not in MILESTONE_CELLS]
        spans = []
        for o in sorted(self.saved_offsets):
            if spans and spans[-1][1] == o:
                spans[-1][1] = o + 1
            else:
                spans.append([o, o + 1])
        self.events = {}            # kind -> [(frame, value)]
        self.states = []            # [(frame, the saved game's bytes)]
        self.inputs = []
        self.clock = []             # the play clock, in frames, as each frame ends
        self.random_kept = {}       # frame -> $29AD as a battle's first menu opens
        self.end = 0
        last, snapshot = None, None
        for frame, mem in trace.states():
            wram, runs = mem[wi], frame.runs[wi]
            n = frame.frame + 1      # frames run so far, as ct_dev --locations counts
            self.inputs.append(frame.input)
            self.clock.append(play_clock(wram))
            self.end = n
            if frame.keyframe or touches(runs, WATCHED):
                now = milestones(wram)
                for kind, value in now.items():
                    if last is None or last[kind] != value:
                        self.events.setdefault(kind, []).append((n, value))
                if now["turn"] is not None and (last is None or last["turn"] is None):
                    self.random_kept[n] = wram[RANDOM_KEPT]
                last = now
            if frame.keyframe or touches(runs, spans):
                now = bytes(wram[o] for o in self.saved_offsets)
                if now != snapshot:
                    self.states.append((n, now))
                    snapshot = now
        del frame, mem, wram, runs     # the trace's views, before the mapping closes
        trace.data.close()
        trace._file.close()
        self.states = settled(self.states, self.end)

    def clock_at(self, n):
        return self.clock[n - 1] if 0 < n <= len(self.clock) else None

    def input_at(self, n):
        """The movie's input on the frame that makes n frames run."""
        return self.inputs[n - 1] if 0 < n <= len(self.inputs) else 0


def play_clock(wram):
    total, scale = 0, 1
    for i, radix in enumerate(PLAY_TIME_RADICES):
        total += wram[PLAY_TIME + i] * scale
        scale *= radix
    return total


def touches(runs, spans):
    return any(off < hi and off + len(chunk) > lo for off, chunk in runs for lo, hi in spans)


def settled(states, end):
    """The states held SETTLE frames or more (and the last), the same state
    twice in a row merged."""
    out = []
    for i, (frame, state) in enumerate(states):
        until = states[i + 1][0] if i + 1 < len(states) else end + SETTLE
        if until - frame < SETTLE or (out and out[-1][1] == state):
            continue
        out.append((frame, state))
    return out


class Shift:
    """A milestone or saved-game change matched with the reference's: the
    frames it landed late by (negative: early)."""

    def __init__(self, what, ref_frame, run_frame):
        self.what, self.ref_frame, self.run_frame = what, ref_frame, run_frame
        self.d = run_frame - ref_frame

    def __str__(self):
        return "%s lands %+d frames, at frame %d (reference %d)" % (self.what, self.d, self.run_frame, self.ref_frame)


def match(reference, run, what, jump=None):
    """Match two [(frame, value)] in order: the Shifts of the matched part
    (the first record is the starting value, not a change), and where the
    values part, (reference frame, run frame, index), or None. With `jump`,
    a shift that differs from the one before by more parts them too: a kind
    whose values repeat (a member's menu, a window) can pair the reference's
    with another occasion's once one run has had one more."""
    shifts, before = [], 0
    for i, (rf, rv) in enumerate(reference):
        if i >= len(run):
            return shifts, None
        sf, sv = run[i]
        if rv != sv or (jump is not None and i and abs(sf - rf - before) > jump):
            return shifts, (rf, sf, i)
        before = sf - rf
        if i:
            shifts.append(Shift(what(rv), rf, sf))
    return shifts, None


def match_states(reference, run):
    """Match two runs' saved-game states in order, as match does, but where one
    run passes through a state the other skips -- two changes a frame or two
    apart landing in the other order, or together -- step over it. The Shifts,
    where they part ((reference frame, run frame, i, j) or None), and how many
    of each were matched."""
    shifts, i, j = [], 0, 0
    while i < len(reference) and j < len(run):
        (rf, rv), (sf, sv) = reference[i], run[j]
        if rv == sv:
            if i and j:
                shifts.append(Shift("a saved-game change", rf, sf))
            i, j = i + 1, j + 1
            continue
        ahead = next(((a, b) for a, b in ((1, 0), (0, 1), (1, 1), (2, 0), (0, 2))
                      if i + a < len(reference) and j + b < len(run) and reference[i + a][1] == run[j + b][1]), None)
        if ahead is None:
            return shifts, (rf, sf, i, j), (i, j)
        i, j = i + ahead[0], j + ahead[1]
    return shifts, None, (i, j)


# The location triples the map declares by their first byte: the place, then
# the party's position and facing in it.
TRIPLES = {0x0100: "location", 0x0105: "location_previous", 0x010A: "location_stored"}
TRIPLE_BYTES = 5


def cell_names(names, offsets):
    out = []
    for o in offsets:
        triple = next((n for at, n in TRIPLES.items() if at <= o < at + TRIPLE_BYTES), None)
        name = triple or names.get(o, "$%02X:%04X" % (0x7E + (o >> 16), o & 0xFFFF))
        if name not in out:
            out.append(name)
    return out


def classify(ref, run, names):
    """How `run` keeps to `ref`: a dict with the verdict and its facts."""
    shifts, partings = [], []
    for kind, theirs in ref.events.items():
        s, parted = match(theirs, run.events.get(kind, []), lambda v, k=kind: describe_event(k, v),
                          JUMP if kind in REPEATING else None)
        shifts += s
        if parted:
            rf, sf, i = parted
            partings.append((min(rf, sf), "%s at frame %d, where the reference has %s at frame %d" % (
                describe_event(kind, run.events[kind][i][1]), sf, describe_event(kind, theirs[i][1]), rf), None))
    s, parted, (mi, mj) = match_states(ref.states, run.states)
    shifts += s
    if parted:
        rf, sf, i, j = parted
        theirs, ours = ref.states[i][1], run.states[j][1]
        before = ref.states[i - 1][1] if i else theirs
        cells = [ref.saved_offsets[k] for k in range(len(theirs)) if theirs[k] != ours[k]]
        moved = [ref.saved_offsets[k] for k in range(len(theirs))
                 if theirs[k] != ours[k] and (before[k] != theirs[k] or before[k] != ours[k])] or cells
        variables = cell_names(names, moved)
        partings.append((min(rf, sf), "the saved game changes at frame %d to a state the reference doesn't reach "
                         "(its next change is at frame %d): %s%s" % (
                             sf, rf, ", ".join(variables[:4]), ", ..." if len(variables) > 4 else ""), variables))
    shifts.sort(key=lambda x: x.ref_frame)
    out = {"shifts": shifts}
    if not partings:
        last = shifts[-1].d if shifts else 0
        if mi == len(ref.states) and mj == len(run.states):
            out["verdict"] = "same"
        else:
            out["verdict"] = "ends off"
            short = mi < len(ref.states)
            longer, k = (ref, mi) if short else (run, mj)
            frame = longer.states[k][0]
            first, after = longer.states[k - 1][1], longer.states[-1][1]
            changed = cell_names(names, [ref.saved_offsets[i] for i in range(len(first)) if first[i] != after[i]])
            out["why"] = "the run passes through the reference's saved-game states but %s (%s), %+d frames off at the end" % (
                ("ends before the reference's last %d, from frame %d" if short else
                 "goes %d past them, from frame %d") % (len(longer.states) - k, frame),
                ", ".join(changed[:4]) + (", ..." if len(changed) > 4 else ""), last)
        return out
    at, text, variables = min(partings, key=lambda p: p[0])
    out.update(parting_frame=at, parting=text, variables=variables)
    before = [x for x in shifts if x.ref_frame < at]
    out["d"] = d = before[-1].d if before else 0
    change = None
    for prev, x in zip([None] + before, before):
        if x.d != (prev.d if prev else 0):
            change = x
    out["set_by"] = change
    entered = [x for x in before if x.what.startswith("place ")]
    here = [x for x in before if x.d and (not entered or x.ref_frame >= entered[-1].ref_frame)]
    out["moved_here"] = here[-3:]
    a, b = ref.clock_at(at - 1), run.clock_at(at - 1 + d)
    out["clock"] = b - a if a is not None and b is not None else 0
    out["verdict"] = "moved" if d or here else "aligned"
    out["press"] = next((n for n in range(max(1, at - PRESS_WINDOW - abs(d)), at + 1)
                         if ref.input_at(n) != run.input_at(n + d)), None)
    kept = [(f, ref.random_kept[f], run.random_kept.get(f + d)) for f in sorted(ref.random_kept) if f <= at]
    out["random"] = next(((f, a, b) for f, a, b in reversed(kept) if b is not None and a != b), None)
    return out


def decide(result, timed):
    """The verdict on a strict run, from what it did (classify) and what the
    run with the original's time did: drift only where that run plays as the
    reference to its end, so time alone accounts for the difference; a bug
    where it doesn't, however far off the strict run's milestones were."""
    if result["verdict"] == "same":
        return "same"
    if timed is None:
        return "unproven"
    return "drift" if timed["verdict"] == "same" else "bug"


def original_code(text):
    """From a timed run's report: (hand-port calls, calls that ran the
    original's code), or None."""
    generated = GENERATED_CALLS.search(text)
    if not generated:
        return None
    return sum(int(m.group(1)) for m in PORT_CALLS.finditer(text)), int(generated.group(1))


def frames_off(d):
    return "%d frame%s %s" % (abs(d), "" if abs(d) == 1 else "s", "later" if d > 0 else "earlier")


def reason(out, timed, calls=None, hand=None):
    """The verdict as one line, for tools/strict_baseline.txt: the tool's
    findings, then any a reader verified by hand (`hand`, kept as it was)."""
    parts = []
    if "parting" in out:
        moved = out["set_by"] if out["d"] else (out["moved_here"][-1] if out["moved_here"] else None)
        if moved:
            parts.append("%s lands %s at frame %d" % (moved.what, frames_off(moved.d), moved.run_frame))
        if out.get("clock"):
            parts.append("the play clock runs %d frame(s) %s" % (abs(out["clock"]), "ahead" if out["clock"] > 0 else "behind"))
        if out.get("random"):
            f, a, b = out["random"]
            parts.append("the battle at frame %d starts from random $%02X, not $%02X" % (f, b, a))
        parts.append("it parts at frame %d (%s)" % (out["parting_frame"], out["parting"]))
    elif out.get("why"):
        parts.append(out["why"])
    if timed is not None:
        parts.append(("with the original's time it plays as the reference to the end" if timed["verdict"] == "same"
                      else "with the original's time it parts at frame %d too" % timed.get("parting_frame", 0)) +
                     original_share(calls))
    if hand:
        parts.append(HAND + hand)
    return "%s: %s" % (out["final"], "; ".join(parts))


def original_share(calls):
    if not calls:
        return ""
    ported, original = calls
    # a block of the original's code is counted per call, finer than a port's call
    return " (hand ports %d calls, the original's code %d block calls)" % (ported, original)


def report(name, out, ref, run, timed, baseline=False, calls=None, hand=None):
    if baseline:
        return "%s drifts  # %s" % (name, reason(out, timed, calls, hand))
    lines = ["%s: %s" % (name, out["final"])]
    if "parting" in out:
        lines.append("  parts at frame %d: %s" % (out["parting_frame"], out["parting"]))
        if out["d"]:
            lines.append("  shift in force: %+d frames%s" % (out["d"], ", since " + str(out["set_by"]) if out["set_by"] else ""))
        elif out["moved_here"]:
            lines.append("  in this place, " + "; ".join(str(x) for x in out["moved_here"]))
        if out.get("clock"):
            lines.append("  the play clock there runs %d frame(s) %s the reference's" % (
                abs(out["clock"]), "ahead of" if out["clock"] > 0 else "behind"))
        if out.get("press") is not None:
            # context, not evidence: both runs replay the same presses
            n = out["press"]
            lines.append("  (context) aligned by the shift, the reference's frame %d has %s, the run's frame %d %s" % (
                n, buttons_str(ref.input_at(n)), n + out["d"], buttons_str(run.input_at(n + out["d"]))))
        if out.get("random"):
            f, a, b = out["random"]
            lines.append("  the battle whose first menu opens at reference frame %d starts from random $%02X, "
                         "the run's from $%02X" % (f, a, b))
    elif out.get("why"):
        lines.append("  " + out["why"])
    if timed is not None:
        if "parting" in timed:
            lines.append("  with the original's time: parts at frame %d: %s" % (timed["parting_frame"], timed["parting"]))
        else:
            lines.append("  with the original's time: %s" % (
                "plays as the reference to the end" if timed["verdict"] == "same" else timed["why"]))
        if calls:
            lines.append("   " + original_share(calls))
    return "\n".join(lines)


# The runs: the reference, strict mode, and the ports with the original's time.
MODES = (("reference", ["--native", "off"]),
         ("strict", strict_check.STRICT),
         ("timed", ["--native", "on", "--port-timing", "oracle", "--port-horizon", "interrupts"]))


def play(binary, rom, path, frames, extra, out):
    cmd = [str(binary), "--rom", str(rom), "--movie", str(path), "--frames", str(frames),
           "--state-trace", str(out)] + extra
    proc = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                          timeout=strict_check.seconds_for(frames) * SLOWER)
    return proc.returncode, proc.stderr.decode(errors="replace")


def one(args, row, saved, names, work, hands):
    name, path, frames = row
    timelines, calls = {}, None
    for mode, extra in MODES if args.timed else MODES[:2]:
        out = work / ("%s.%s.cttr" % (name, mode))
        log = out.with_suffix(".log")
        if args.reuse and out.exists() and (mode != "timed" or log.exists()):
            rc, text = 0, log.read_text(errors="replace") if log.exists() else ""
        else:
            rc, text = play(args.ct_dev, args.rom, path, frames, extra, out)
            if args.keep:
                log.write_text(text)
        if not out.exists():
            return "%s: the %s run failed (exit %s): %s" % (name, mode, rc, " ".join(text.strip().splitlines()[-1:]))
        if mode == "timed":
            calls = original_code(text)
        timelines[mode] = Timeline(out, saved)
        if not args.keep:
            out.unlink()
    return judge(name, timelines, names, args.baseline, calls, hands.get(name))


def judge(name, timelines, names, baseline=False, calls=None, hand=None):
    """Classify the strict run and decide it against the timed run's."""
    ref, run = timelines["reference"], timelines["strict"]
    result = classify(ref, run, names)
    timed = classify(ref, timelines["timed"], names) if "timed" in timelines else None
    result["final"] = decide(result, timed)
    return report(name, result, ref, run, timed, baseline, calls, hand)


def hand_findings(baseline):
    """{movie: the hand-verified findings on its baseline line}."""
    out = {}
    for name, (_must, why) in strict_check.read_baseline(baseline).items():
        if HAND in why:
            out[name] = why.split(HAND, 1)[1]
    return out


def drifting(baseline):
    return [n for n, (must, _why) in strict_check.read_baseline(baseline).items() if must == "drifts"]


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 tools/drift.py")
    ap.add_argument("--ct-dev", type=pathlib.Path, required=True)
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--movies", nargs="*", default=[])
    ap.add_argument("--drifting", action="store_true", help="every movie tools/strict_baseline.txt marks drifts")
    ap.add_argument("-j", "--jobs", type=int, default=1, help="movies at a time (each runs ct_dev in turn)")
    ap.add_argument("--no-timed", dest="timed", action="store_false", help="skip the run with the original's time")
    ap.add_argument("--keep", type=pathlib.Path, help="keep the state traces here (ROM-derived: not in the repo)")
    ap.add_argument("--baseline", action="store_true",
                    help="print each movie as a tools/strict_baseline.txt line, its reason the verdict")
    ap.add_argument("--reuse", action="store_true", help="with --keep, read the traces there instead of playing")
    args = ap.parse_args(argv)
    if args.keep and args.keep.resolve().is_relative_to(ROOT):
        sys.exit("--keep %s: the traces are ROM-derived and stay out of the repository" % args.keep)
    wanted = set(args.movies) | (set(drifting(strict_check.BASELINE)) if args.drifting else set())
    rows = strict_check.movies(wanted)
    if not rows:
        sys.exit("no movies to run")
    saved, names = strict_check.SavedGame(args.rom), strict_check.ram_names()
    scratch = None
    if args.keep:
        args.keep.mkdir(parents=True, exist_ok=True)
        work = args.keep
    else:
        scratch = tempfile.mkdtemp(prefix="drift-")
        work = pathlib.Path(scratch)
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            hands = hand_findings(strict_check.BASELINE)
            for text in pool.map(lambda row: one(args, row, saved, names, work, hands), rows):
                print(text, flush=True)
    finally:
        if scratch:
            shutil.rmtree(scratch, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
