"""The GBA build's census of what a player sees go wrong: every picture the
GBA shows compared with ct_native's, and every stretch where its pictures come
late or are left out (docs/PLATFORM.md, "The GBA build, as built").

    python3 targets/gba/host/census.py --mgba MGBA_PREFIX --program build/gba/ct_gba.bin \\
        --ct-native build/main/ct_native --out DIR [--movies NAME...] [--hand NAME...]

Movies. For each movie, ct_native plays it at the GBA's view (`--view
240x160`), writing every frame's picture and its time model's decisions; the
runner (mgba_run.c) plays it on `ct.gba` with those decisions replayed, so
the GBA plays the same frames, at the GBA's own speed, and writes the picture
every GBA frame shows (`--record`), with the game's frame the driver put on
the screen last. Each GBA frame's picture must be that game frame's picture
in ct_native exactly, but for the GBA's 15-bit colour (both are compared at 5
bits a channel) and the colour profile the cartridge was packed with. A GBA
frame that differs is a mismatch, of one of these kinds:

- `torn`: each line is that frame's or the frame before's (or after's): the
  picture changed while the GBA drew it.
- `frame`: the whole picture is the frame before's or after's.
- `colour`: the same picture with its colours off by at most COLOUR_STEPS
  steps of 31 (fades, colour math).
- `picture`: anything else.

Mismatches of a kind close in time (GAP game frames) and in place on the
screen go into one cluster, ranked by how visible it is: the differing
pixels summed over the GBA frames that show them (its size times its
duration). Each cluster gets a PNG of its worst frame: ct_native's, the
GBA's and the differing pixels in white.

Pacing. From the same records, each game frame's GBA frames (from the GBA
frame it began in to the one the next began in) and whether its picture was
ever on the screen: a frame over SLOW GBA frames is slow and a frame whose
picture never showed was left out; both are grouped into stretches (STRETCH
game frames apart at most) ranked by the GBA frames they cost over one a game
frame. A GBA frame run of over HANG frames with no game frame begun is a hang.

By hand (`--hand`): the GBA plays without ct_native's decisions, its own time
deciding its frames as on hardware, from power-on with no input
(`attract-idle`) or with the pads of a script (`new-game-by-hand`). ct_native
can't be held to its frames then, so what is checked is the pacing, the hangs,
and pictures that ct_native never showed anywhere near that frame
(`unmatched`; the game drifts from ct_native's as its frames' lag differs,
so these are leads, not proof).

Writes DIR/census.md and DIR/census.json and the PNGs in DIR; its working files
go in DIR/work and are deleted movie by movie (`--keep` keeps them). Needs the
staged cartridge, as run.py does.
"""
import argparse
import array
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
import pack  # noqa: E402
import run  # noqa: E402

WIDTH, HEIGHT = 240, 160       # the GBA's screen
CELL = 8                       # the screen's cells, for where a mismatch is
COLOUR_STEPS = 2               # a `colour` mismatch's largest difference, of 31
GAP = 10                       # game frames between mismatches of one cluster
NEAR = 16                      # pixels between a cluster's box and a mismatch it takes
SLOW = 2                       # GBA frames a game frame may take
STRETCH = 30                   # game frames between slow frames of one stretch
HANG = 120                     # GBA frames with no game frame begun: two seconds
UNMATCHED_WINDOW = 600         # game frames either side a hand-played picture is looked for in ct_native's

# The six that cover the GBA's milestones, then the gate's movies under 6,000 frames.
MOVIES = ("title-start", "new-game", "gato-battle", "events-jetbike-race", "menus-sweep", "attract-demo",
          "field-65m-bc-dactyl", "field-12kbc-1f6", "field-12kbc-1f4", "menus-new-game-plus", "menus-save-point",
          "field-floor-switch", "field-northern-ruins-switch", "field-65m-bc-dactyl-flight", "battle-menu-buttons",
          "events-end-of-time-gate")

# Hand-played scripts: frames, then (first frame, frames held, buttons) as a movie writes them.
NEW_GAME_PADS = ([(1620, 5, "START"), (2400, 4, "A"), (3000, 4, "START"), (3200, 4, "START")]
                 + [(5400 + 90 * i, 3, "A") for i in range(20)]
                 + [(7300, 60, "DOWN"), (7400, 60, "LEFT"), (7500, 40, "UP"), (7600, 60, "RIGHT"),
                    (7700, 120, "DOWN+B"), (7900, 3, "A"), (8000, 3, "A"), (8100, 60, "LEFT")])
HAND = {
    "attract-idle": (18000, []),             # five minutes of power-on with no input: the intro and attract loop
    "new-game-by-hand": (8400, NEW_GAME_PADS),   # title, new game, naming, Crono's room and a walk
}

# ---- Pictures ------------------------------------------------------------------------------------

FIVE_BITS = bytes(v >> 3 for v in range(256))
EIGHT_BITS = bytes((v << 3 | v >> 2) if v < 32 else 255 for v in range(256))
FROM_15 = [bytes((c & 31, c >> 5 & 31, c >> 10 & 31)) for c in range(1 << 16)]


def png_read(path):
    """An 8-bit RGB or RGBA PNG's width, height and RGB bytes, a row after another."""
    data = pathlib.Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s: not a PNG" % path)
    at, idat, width = 8, [], None
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            if depth != 8 or colour not in (2, 6) or body[12]:
                raise ValueError("%s: not 8-bit RGB" % path)
            channels = 3 if colour == 2 else 4
        elif kind == b"IDAT":
            idat.append(body)
        at += 12 + length
    raw = zlib.decompress(b"".join(idat))
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)]
        if kind:
            line = unfilter(kind, bytearray(line), previous, channels)
        previous = line
        rows.append(bytes(line) if channels == 3 else
                    b"".join(bytes(line[x:x + 3]) for x in range(0, stride, 4)))
    return width, height, b"".join(rows)


def unfilter(kind, line, previous, step):
    """A PNG line's filter undone (types 1-4)."""
    for i in range(len(line)):
        left = line[i - step] if i >= step else 0
        up = previous[i]
        corner = previous[i - step] if i >= step else 0
        if kind == 1:
            guess = left
        elif kind == 2:
            guess = up
        elif kind == 3:
            guess = (left + up) // 2
        else:
            p = left + up - corner
            guess = min((abs(p - left), 0, left), (abs(p - up), 1, up), (abs(p - corner), 2, corner))[2]
        line[i] = (line[i] + guess) & 255
    return line


def png_write(path, width, height, rgb):
    """RGB bytes as an 8-bit PNG."""
    stride = width * 3
    raw = b"".join(b"\0" + rgb[y * stride:(y + 1) * stride] for y in range(height))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    pathlib.Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                                   + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def five_bits(rgb):
    """8-bit RGB as 5 bits a channel, as the GBA shows it."""
    return rgb.translate(FIVE_BITS)


def from_15(data):
    """The runner's 15-bit pixels (red low) as 5 bits a channel."""
    pixels = array.array("H")
    pixels.frombytes(data)
    if sys.byteorder != "little":
        pixels.byteswap()
    return b"".join([FROM_15[p] for p in pixels])


def eight_bits(picture):
    """5 bits a channel back to 8, for a PNG."""
    return picture.translate(EIGHT_BITS)


def colour_profile(name):
    """A map of 5-bit pixels through the cartridge's colour profile (pack.py's), or None."""
    if name == "original":
        return None
    table = {}

    def mapped(picture):
        out = bytearray(len(picture))
        for i in range(0, len(picture), 3):
            key = picture[i:i + 3]
            c = table.get(key)
            if c is None:
                c = pack.port_style(key[0] | key[1] << 5 | key[2] << 10)
                c = table[key] = bytes((c & 31, c >> 5 & 31, c >> 10 & 31))
            out[i:i + 3] = c
        return bytes(out)
    return mapped


# ---- Comparing ---------------------------------------------------------------------------------

def compare(a, b, width=WIDTH, height=HEIGHT):
    """Where two pictures of 5-bit pixels differ: None where they don't, else the
    differing pixels, their box (x0, y0, x1, y1, inclusive), the cells
    (CELL-pixel squares, numbered across then down) and the largest step on a
    channel."""
    if a == b:
        return None
    stride, span = width * 3, CELL * 3
    columns = (width + CELL - 1) // CELL
    pixels = most = 0
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    cells = set()
    for y in range(height):
        row = y * stride
        if a[row:row + stride] == b[row:row + stride]:
            continue
        for column in range(columns):
            at = row + column * span
            end = min(at + span, row + stride)
            sa, sb = a[at:end], b[at:end]
            if sa == sb:
                continue
            cells.add((y // CELL) * columns + column)
            for i in range(0, end - at, 3):
                if sa[i:i + 3] != sb[i:i + 3]:
                    pixels += 1
                    x = column * CELL + i // 3
                    x0, x1 = min(x0, x), max(x1, x)
                    y0, y1 = min(y0, y), max(y1, y)
                    most = max(most, abs(sa[i] - sb[i]), abs(sa[i + 1] - sb[i + 1]), abs(sa[i + 2] - sb[i + 2]))
    return {"pixels": pixels, "box": [x0, y0, x1, y1], "cells": sorted(cells), "steps": most}


def lines_from(picture, sources, width=WIDTH, height=HEIGHT):
    """For each line of the picture, the first of `sources` (pictures or None)
    whose line it is, or -1."""
    stride = width * 3
    out = []
    for y in range(height):
        line = picture[y * stride:(y + 1) * stride]
        out.append(next((i for i, s in enumerate(sources) if s is not None and s[y * stride:(y + 1) * stride] == line), -1))
    return out


def kind_of(gba, native, before, after, difference):
    """What a mismatching GBA picture is (the module's kinds), given ct_native's
    picture of its frame and of the frames either side."""
    if gba == before or gba == after:
        return "frame"
    lines = lines_from(gba, [native, before, after])
    if -1 not in lines and any(i > 0 for i in lines):
        return "torn"
    if difference["steps"] <= COLOUR_STEPS:
        return "colour"
    return "picture"


# ---- Clusters ----------------------------------------------------------------------------------

def near(box, other, margin=NEAR):
    return not (other[0] > box[2] + margin or other[2] < box[0] - margin or
                other[1] > box[3] + margin or other[3] < box[1] - margin)


def cluster(mismatches, gap=GAP):
    """Mismatches (dicts with frame, gba, kind and compare()'s fields, in the
    GBA's order) gathered into clusters: one kind, close in game frames and on
    the screen. Each cluster: its kind, first and last game frame, GBA frames,
    pixels summed (its visibility), box, cells, largest step and worst
    mismatch; the most visible first."""
    open_, done = [], []
    for m in mismatches:
        found = None
        for c in open_:
            if c["kind"] == m["kind"] and m["frame"] - c["last"] <= gap and near(c["box"], m["box"]):
                found = c
                break
        if found is None:
            found = {"kind": m["kind"], "first": m["frame"], "last": m["frame"], "gba_frames": 0, "visibility": 0,
                     "box": list(m["box"]), "cells": set(), "steps": 0, "worst": m}
            open_.append(found)
        found["last"] = max(found["last"], m["frame"])
        found["gba_frames"] += 1
        found["visibility"] += m["pixels"]
        b = found["box"]
        found["box"] = [min(b[0], m["box"][0]), min(b[1], m["box"][1]), max(b[2], m["box"][2]), max(b[3], m["box"][3])]
        found["cells"].update(m["cells"])
        found["steps"] = max(found["steps"], m["steps"])
        if m["pixels"] > found["worst"]["pixels"]:
            found["worst"] = m
        still = []
        for c in open_:
            (still if m["frame"] - c["last"] <= gap else done).append(c)
        open_ = still
    done += open_
    for c in done:
        c["cells"] = len(c["cells"])
    return sorted(done, key=lambda c: (-c["visibility"], c["first"]))


# ---- Pacing ------------------------------------------------------------------------------------

def read_index(path):
    """The runner's --record index: (GBA frame, game frames begun, frame shown, picture) a line."""
    with open(path) as f:
        return [tuple(int(w) for w in line.split()) for line in f if line.strip()]


def pacing(index):
    """Each game frame's GBA frames, the frames whose picture never showed, and
    the hangs, from the runner's index. A game frame n began in the first GBA
    frame whose count of frames begun reached n; it took the GBA frames until
    the next began. Frames before the first GBA frame recorded and the last
    one begun (whose end isn't seen) are left out."""
    began = {}
    shown = set()
    hangs = []
    still_since = None
    latest = None
    for at, (gba, taken, showing, _) in enumerate(index):
        for n in range(taken if latest is None else latest + 1, taken + 1):
            began[n] = gba
        latest = taken if latest is None else max(latest, taken)
        shown.add(showing)
        if at and taken == index[at - 1][1]:
            still_since = still_since if still_since is not None else index[at - 1][0]
        else:
            if still_since is not None and gba - still_since > HANG:
                hangs.append({"gba_from": still_since, "gba_to": gba, "frame": taken - 1})
            still_since = None
    if still_since is not None and index[-1][0] - still_since > HANG:
        hangs.append({"gba_from": still_since, "gba_to": index[-1][0], "frame": index[-1][1], "unfinished": True})
    frames = sorted(began)
    took = {n: began[n + 1] - began[n] for n in frames[1:-1] if n + 1 in began}
    first_shown = min((s for s in shown if s), default=0)
    left_out = sorted(n for n in took if n >= first_shown and n not in shown)
    return took, left_out, hangs


def stretches(took, left_out, slow=SLOW, gap=STRETCH):
    """The slow frames (over `slow` GBA frames) and the frames left out, in
    stretches no more than `gap` game frames apart: each its first and last
    game frame, its slow frames, frames left out, longest frame, and the GBA
    frames it cost over one a game frame; the costliest first."""
    events = sorted(set([n for n, t in took.items() if t > slow] + list(left_out)))
    out = []
    for n in events:
        if out and n - out[-1]["last"] <= gap:
            out[-1]["last"] = n
        else:
            out.append({"first": n, "last": n})
    lost = set(left_out)
    for s in out:
        frames = range(s["first"], s["last"] + 1)
        spans = [took.get(n, 1) for n in frames]
        s["slow"] = sum(1 for t in spans if t > slow)
        s["left_out"] = sum(1 for n in frames if n in lost)
        s["longest"] = max(spans)
        s["gba_frames"] = sum(spans)
        s["cost"] = s["gba_frames"] - len(spans)
    return sorted(out, key=lambda s: (-s["cost"], -s["left_out"], s["first"]))


# ---- Running -----------------------------------------------------------------------------------

class Pictures:
    """The runner's recorded pictures, read by number."""

    def __init__(self, path):
        self.f = open(path, "rb")
        self.size = WIDTH * HEIGHT * 2
        self.cache = {}

    def get(self, number):
        if number not in self.cache:
            if len(self.cache) > 8:
                self.cache.clear()
            self.f.seek(number * self.size)
            self.cache[number] = from_15(self.f.read(self.size))
        return self.cache[number]

    def close(self):
        self.f.close()


class NativeFrames:
    """ct_native's pictures from its --shot-dir, by game frame (counted from 1), as 5-bit pixels."""

    def __init__(self, directory, profile=None):
        self.directory, self.profile, self.cache = pathlib.Path(directory), profile, {}

    def get(self, frame):
        if frame not in self.cache:
            if len(self.cache) > 64:
                for k in sorted(self.cache)[:32]:
                    del self.cache[k]
            path = self.directory / ("frame_%06d.png" % frame)
            picture = None
            if path.exists():
                width, height, rgb = png_read(path)
                if (width, height) != (WIDTH, HEIGHT):
                    raise SystemExit("census: ct_native's pictures are %dx%d, not %dx%d" % (width, height, WIDTH, HEIGHT))
                picture = five_bits(rgb)
                if self.profile:
                    picture = self.profile(picture)
            self.cache[frame] = picture
        return self.cache[frame]


def movie_path(name):
    for p in (ROOT / "movies" / (name + ".ctm"), ROOT / "movies" / "saves" / (name + ".ctm")):
        if p.exists():
            return p
    raise SystemExit("census: no movie %s" % name)


def write_movie(path, frames, pads):
    lines = ["# census: a scripted pad sequence", "# frames %d" % frames, "# <start_frame> <frame_count> <buttons>"]
    lines += ["%d %d %s" % pad for pad in pads]
    path.write_text("\n".join(lines) + "\n")


def native_run(args, movie, frames, shots, srm, decisions=None, end=None):
    command = [str(args.ct_native), "--rom", str(args.rom), "--headless", "--mute", "--movie", str(movie),
               "--frames", str(frames), "--view", "%dx%d" % (WIDTH, HEIGHT), "--shot-every", "1", "--shot-dir", str(shots)]
    if decisions:
        command += ["--decisions", str(decisions)]
    if end:
        command += ["--end-state", str(end)]
    if srm:
        command += ["--sram", str(srm)]
    done = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if done.returncode:
        print(done.stderr)
        raise SystemExit("census: ct_native didn't play %s" % movie.name)


def gba_run(args, movie, frames, record, srm, work, decisions=None, stall=None):
    command = [str(args.runner), str(args.image), "--movie", str(movie), "--frames", str(frames), "--record", str(record)]
    if decisions:
        command += ["--decisions", str(decisions)]
    if stall:
        command += ["--stall", str(stall)]
    if srm:
        sav = work / "start.sav"
        sav.write_bytes(run.gba_save(srm))
        command += ["--sav", str(sav)]
    env = dict(os.environ)
    env["PATH"] = str(args.mgba / "bin") + os.pathsep + env.get("PATH", "")
    done = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    return done.returncode, done.stdout


def picture_png(path, native, gba, difference_only=None):
    """ct_native's picture, the GBA's and their differing pixels in white, side by side."""
    blank = bytes(WIDTH * HEIGHT * 3)
    native = native or blank
    rows = []
    stride = WIDTH * 3
    for y in range(HEIGHT):
        a, b = native[y * stride:(y + 1) * stride], gba[y * stride:(y + 1) * stride]
        diff = b"".join(b"\xff\xff\xff" if a[i:i + 3] != b[i:i + 3] else b"\0\0\0" for i in range(0, stride, 3))
        rows.append(eight_bits(a) + b"\x40\x40\x40" * 4 + eight_bits(b) + b"\x40\x40\x40" * 4 + diff)
    png_write(path, WIDTH * 3 + 8, HEIGHT, b"".join(rows))


def census_movie(args, name, work):
    movie = movie_path(name)
    text = movie.read_text(encoding="utf-8")
    frames = int(re.search(r"^# frames (\d+)", text, re.M).group(1))
    srm = run.starting_save(text)
    shots, record = work / "native", work / "gba.pic"
    shots.mkdir(parents=True, exist_ok=True)
    decisions, end = work / "movie.decisions", work / "movie.end"
    started = time.monotonic()
    native_run(args, movie, frames, shots, srm, decisions, end)
    rc, log = gba_run(args, movie, frames, record, srm, work, decisions)
    state = end.read_bytes()
    found = run.END.search(log)
    same = bool(found) and (found.group(1), found.group(2)) == ("%08X" % zlib.crc32(state), "%08X" % zlib.crc32(state[0x20000:]))
    index = read_index(str(record) + ".idx")
    gba = Pictures(record)
    native = NativeFrames(shots, colour_profile(args.colours))
    mismatches, seen = [], {}
    for gba_frame, taken, shown, number in index:
        if shown < 1:
            continue
        mine, theirs = gba.get(number), native.get(shown)
        if theirs is None:
            continue
        key = (number, zlib.crc32(theirs))
        if key not in seen:
            difference = compare(mine, theirs)
            if difference:
                difference["kind"] = kind_of(mine, theirs, native.get(shown - 1), native.get(shown + 1), difference)
            seen[key] = difference
        difference = seen[key]
        if difference:
            m = dict(difference, frame=shown, gba=gba_frame, picture=number)
            mismatches.append(m)
    clusters = cluster(mismatches)
    took, left_out, hangs = pacing(index)
    result = {
        "movie": name, "frames": frames, "gba_frames": len(index), "ends_as_ct_native": same,
        "runner_exit": rc, "gba_frames_compared": sum(1 for i in index if i[2] >= 1),
        "mismatching_gba_frames": len(mismatches),
        "clusters": [], "stretches": stretches(took, left_out), "hangs": hangs,
        "left_out": len(left_out), "slow_frames": sum(1 for t in took.values() if t > SLOW),
    }
    for rank, c in enumerate(clusters):
        w = c.pop("worst")
        entry = dict(c, worst_frame=w["frame"], worst_gba_frame=w["gba"], worst_pixels=w["pixels"])
        if rank < args.pngs:
            png = args.out / ("%s-%03d-%s-%d.png" % (name, rank + 1, c["kind"], w["frame"]))
            picture_png(png, native.get(w["frame"]), gba.get(w["picture"]))
            entry["png"] = png.name
        result["clusters"].append(entry)
    gba.close()
    result["seconds"] = round(time.monotonic() - started, 1)
    return result


def line_crcs(picture, width=WIDTH, height=HEIGHT):
    stride = width * 3
    return [zlib.crc32(picture[y * stride:(y + 1) * stride]) for y in range(height)]


def closest_picture(picture, frame, where, lines, window=UNMATCHED_WINDOW):
    """The frame of ct_native's within `window` of `frame` that shows the same
    picture (True, that frame), or else the one with the most of its lines (False,
    that frame, or None where there are none)."""
    exact = [f for f in where.get(zlib.crc32(picture), []) if abs(f - frame) <= window]
    if exact:
        return True, min(exact, key=lambda f: abs(f - frame))
    mine = line_crcs(picture)
    best, best_score = None, -1
    for f in range(max(1, frame - window), frame + window + 1):
        theirs = lines.get(f)
        if theirs is None:
            continue
        score = sum(1 for a, b in zip(mine, theirs) if a == b)
        if score > best_score or (score == best_score and abs(f - frame) < abs(best - frame)):
            best, best_score = f, score
    return False, best


def census_hand(args, name, work):
    frames, pads = HAND[name]
    movie = work / (name + ".ctm")
    write_movie(movie, frames, pads)
    shots, record = work / "native", work / "gba.pic"
    shots.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    native_run(args, movie, frames, shots, None)
    rc, log = gba_run(args, movie, frames, record, None, work, stall=HANG)
    index = read_index(str(record) + ".idx")
    gba = Pictures(record)
    native = NativeFrames(shots, colour_profile(args.colours))
    # ct_native's pictures by their CRC, each with the frames it showed in,
    # and each frame's lines' CRCs
    where, lines = {}, {}
    for frame in range(1, frames + 1):
        picture = native.get(frame)
        if picture is not None:
            where.setdefault(zlib.crc32(picture), []).append(frame)
            lines[frame] = line_crcs(picture)
    unmatched, checked = [], {}
    for gba_frame, taken, shown, number in index:
        if shown < 1:
            continue
        if number not in checked:
            checked[number] = closest_picture(gba.get(number), shown, where, lines)
        exact, frame = checked[number]
        if exact or frame is None:
            continue
        difference = compare(gba.get(number), native.get(frame))
        if difference:
            unmatched.append(dict(difference, kind="unmatched", frame=shown, gba=gba_frame, picture=number,
                                  offset=frame - shown))
    clusters = cluster(unmatched)
    took, left_out, hangs = pacing(index)
    stopped = re.search(r"stopped taking frames at frame (\d+)", log)
    result = {
        "hand": name, "frames": frames, "gba_frames": len(index), "runner_exit": rc,
        "stopped_at": int(stopped.group(1)) if stopped else None,
        "unmatched_gba_frames": len(unmatched), "clusters": [], "stretches": stretches(took, left_out),
        "hangs": hangs, "left_out": len(left_out), "slow_frames": sum(1 for t in took.values() if t > SLOW),
    }
    for rank, c in enumerate(clusters):
        w = c.pop("worst")
        entry = dict(c, worst_frame=w["frame"], worst_gba_frame=w["gba"], worst_pixels=w["pixels"], offset=w["offset"])
        if rank < args.pngs:
            png = args.out / ("%s-%03d-%s-%d.png" % (name, rank + 1, c["kind"], w["frame"]))
            picture_png(png, native.get(w["frame"] + w["offset"]), gba.get(w["picture"]))
            entry["png"] = png.name
        result["clusters"].append(entry)
    gba.close()
    result["seconds"] = round(time.monotonic() - started, 1)
    return result


# ---- The report --------------------------------------------------------------------------------

def report(results, top=40):
    """The census as Markdown: the clusters of every run ranked together, then
    the stretches of pacing, then each run's totals."""
    lines = ["# GBA census", ""]
    rows = []
    for r in results:
        for c in r["clusters"]:
            rows.append((r.get("movie") or r.get("hand"), c))
    rows.sort(key=lambda rc: -rc[1]["visibility"])
    for title, kinds in (("Pictures", ("picture", "torn")), ("Frames and colours", ("frame", "colour")),
                         ("Played by hand: pictures ct_native showed nowhere near (leads)", ("unmatched",))):
        ranked = [rc for rc in rows if rc[1]["kind"] in kinds]
        lines += ["## %s: %d cluster(s)" % (title, len(ranked)), "",
                  "| # | run | kind | game frames | GBA frames | pixels summed | box (x0,y0-x1,y1) | steps | PNG |",
                  "|---|---|---|---|---|---|---|---|---|"]
        for i, (name, c) in enumerate(ranked[:top]):
            b = c["box"]
            lines.append("| %d | %s | %s | %d-%d | %d | %d | %d,%d-%d,%d | %d | %s |" % (
                i + 1, name, c["kind"], c["first"], c["last"], c["gba_frames"], c["visibility"],
                b[0], b[1], b[2], b[3], c["steps"], c.get("png", "")))
        if len(ranked) > top:
            lines.append("")
            lines.append("and %d more in census.json" % (len(ranked) - top))
        lines.append("")
    spans = sorted(((r.get("movie") or r.get("hand"), s) for r in results for s in r["stretches"]),
                   key=lambda rs: (-rs[1]["cost"], -rs[1]["left_out"]))
    lines += ["## Pacing: %d stretch(es)" % len(spans), "",
              "| # | run | game frames | slow | left out | longest | GBA frames | cost |",
              "|---|---|---|---|---|---|---|---|"]
    for i, (name, s) in enumerate(spans[:top]):
        lines.append("| %d | %s | %d-%d | %d | %d | %d | %d | %d |" % (
            i + 1, name, s["first"], s["last"], s["slow"], s["left_out"], s["longest"], s["gba_frames"], s["cost"]))
    lines += ["", "## Runs", "",
              "| run | game frames | GBA frames | ends as ct_native | mismatching GBA frames | slow | left out | hangs | seconds |",
              "|---|---|---|---|---|---|---|---|---|"]
    for r in results:
        mismatching = r.get("mismatching_gba_frames", r.get("unmatched_gba_frames"))
        ends = r.get("ends_as_ct_native", "by hand")
        if r.get("stopped_at") is not None:
            ends = "stopped at %d" % r["stopped_at"]
        lines.append("| %s | %d | %d | %s | %d | %d | %d | %d | %s |" % (
            r.get("movie") or r.get("hand"), r["frames"], r["gba_frames"], ends, mismatching, r["slow_frames"],
            r["left_out"], len(r["hangs"]), r["seconds"]))
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mgba", type=pathlib.Path, required=True, help="mGBA's install prefix")
    ap.add_argument("--program", type=pathlib.Path, required=True)
    ap.add_argument("--ct-native", type=pathlib.Path, required=True)
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--out", type=pathlib.Path, required=True, help="where the report, the PNGs and the work go")
    ap.add_argument("--movies", nargs="*", default=list(MOVIES), help="movies to play against ct_native (%(default)s)")
    ap.add_argument("--hand", nargs="*", default=list(HAND), choices=list(HAND), help="runs played by hand")
    ap.add_argument("--colours", choices=pack.PROFILES, default="original", help="the cartridge's colour profile")
    ap.add_argument("--fast-rom", action="store_true", help="pack with the cartridge's faster waits")
    ap.add_argument("--pngs", type=int, default=50, help="PNGs a run, its most visible clusters first")
    ap.add_argument("--keep", action="store_true", help="keep the working files")
    args = ap.parse_args(argv)
    args.out.mkdir(parents=True, exist_ok=True)
    args.mgba = args.mgba.resolve()
    out_dir = args.program.resolve().parent
    args.runner = run.runner(args.mgba, out_dir)
    args.image = out_dir / "ct.gba"
    records = out_dir / "script.records"
    written = subprocess.run([str(args.ct_native), "--rom", str(args.rom), "--headless", "--script-records", str(records)],
                             stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if written.returncode or not records.exists():
        raise SystemExit("census: ct_native didn't write the script records")
    if pack.main(["--program", str(args.program), "--rom", str(args.rom), "--out", str(args.image), "--profile", args.colours,
                  "--script-records", str(records)] + (["--fast-rom"] if args.fast_rom else [])):
        return 2
    results = []
    for kind, names, one in (("movie", args.movies, census_movie), ("hand", args.hand, census_hand)):
        for name in names:
            work = args.out / "work" / name
            shutil.rmtree(work, ignore_errors=True)
            work.mkdir(parents=True)
            result = one(args, name, work)
            results.append(result)
            print("census: %s %s: %d cluster(s), %d stretch(es), %d hang(s), %.0fs" % (
                kind, name, len(result["clusters"]), len(result["stretches"]), len(result["hangs"]), result["seconds"]),
                flush=True)
            if not args.keep:
                shutil.rmtree(work, ignore_errors=True)
            (args.out / "census.json").write_text(json.dumps(results, indent=1))
            (args.out / "census.md").write_text(report(results))
    print("census: %s" % (args.out / "census.md"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
