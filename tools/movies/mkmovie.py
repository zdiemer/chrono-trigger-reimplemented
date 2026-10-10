"""Buttons and the `.ctm` input movie format (docs/ORACLE.md), and a CLI that
turns a recorded run into a movie.

A run driven by ctdrv.Machine logs the buttons it held on every frame; this
module turns that list into the movie's events and back.

    python3 -m tools.movies.mkmovie <pads file> <out.ctm> [--frames N]
        [--header header.txt] [--sram save.srm]

The pads file is what `Machine.save_state(path)` writes next to a state
(`<path>.pads`): decimal pad values, one per frame, whitespace separated. The
header file's lines are copied into the movie; `{FRAMES}` and `{CRC}` in them
are replaced with the movie's length and the `--sram` save's CRC-32.
"""
import argparse
import zlib

# JOY1 bit order, as in oracle/src/movie.h.
BUTTONS = {
    "R": 1 << 4,
    "L": 1 << 5,
    "X": 1 << 6,
    "A": 1 << 7,
    "RIGHT": 1 << 8,
    "LEFT": 1 << 9,
    "DOWN": 1 << 10,
    "UP": 1 << 11,
    "START": 1 << 12,
    "SELECT": 1 << 13,
    "Y": 1 << 14,
    "B": 1 << 15,
}

# The order names are written in, matching the movies already in oracle/movies.
BUTTON_ORDER = ["B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT", "A", "X", "L", "R"]

# A button on the second controller is named with this prefix ("P2:SELECT"), and
# held in the pad word's high 16 bits (JOY2, the same bit order).
CONTROLLER2 = "P2:"
CONTROLLER2_SHIFT = 16


def pad_of(buttons):
    """Pad word for "A", "UP+B", "P2:SELECT+P2:START", 0 or a pad word (which
    passes through)."""
    if not buttons or buttons == "-":
        return 0
    if isinstance(buttons, int):
        return buttons
    pad = 0
    for name in buttons.split("+"):
        if name.startswith(CONTROLLER2):
            pad |= BUTTONS[name[len(CONTROLLER2):]] << CONTROLLER2_SHIFT
        else:
            pad |= BUTTONS[name]
    return pad


def name_of(pad):
    """The `+`-joined name of a pad word, as movies write it."""
    names = [name for name in BUTTON_ORDER if pad & BUTTONS[name]]
    names += [CONTROLLER2 + name for name in BUTTON_ORDER if pad >> CONTROLLER2_SHIFT & BUTTONS[name]]
    return "+".join(names)


def controllers_of(pad):
    """A pad word as the two controllers' words."""
    return pad & 0xFFFF, pad >> CONTROLLER2_SHIFT & 0xFFFF


def events_from_pads(pads):
    """[(start_frame, count, buttons)] for the runs of equal, non-zero pads."""
    events = []
    frame, end = 0, len(pads)
    while frame < end:
        if not pads[frame]:
            frame += 1
            continue
        run = frame
        while run < end and pads[run] == pads[frame]:
            run += 1
        events.append((frame, run - frame, pads[frame]))
        frame = run
    return events


def pads_from_events(events, frames):
    """The per-frame pad list of a movie's events (they may overlap: OR them)."""
    pads = [0] * frames
    for start, count, buttons in events:
        for frame in range(start, min(start + count, frames)):
            pads[frame] |= buttons
    return pads


def parse_movie(text):
    """Events of a movie's text, as (start, count, pad) triples."""
    events = []
    for line in text.splitlines():
        fields = line.split("#")[0].split()
        if len(fields) < 3:
            continue
        events.append((int(fields[0]), int(fields[1]), pad_of(fields[2])))
    return events


def load_movie_pads(path, frames):
    """The per-frame pad list of a movie file, up to `frames`."""
    with open(path, encoding="utf-8") as handle:
        return pads_from_events(parse_movie(handle.read()), frames)


def write_movie(path, pads, header_lines=()):
    """Write pads as a movie. Header lines are written first, each starting `#`."""
    lines = list(header_lines)
    lines += ["%d %d %s" % (start, count, name_of(pad)) for start, count, pad in events_from_pads(pads)]
    with open(path, "w", newline="\n", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


def sram_crc32(path):
    with open(path, "rb") as handle:
        return "%08X" % zlib.crc32(handle.read())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("pads", help="pad values, one per frame (a state's .pads file)")
    parser.add_argument("out", help="movie to write")
    parser.add_argument("--frames", type=int, default=0, help="truncate to this many frames")
    parser.add_argument("--header", help="file whose lines go into the movie's header")
    parser.add_argument("--sram", help="cartridge save the movie starts from, for {CRC}")
    args = parser.parse_args(argv)

    with open(args.pads, encoding="utf-8") as handle:
        pads = [int(value) for value in handle.read().split()]
    if args.frames:
        pads = pads[: args.frames]
    header = []
    if args.header:
        with open(args.header, encoding="utf-8") as handle:
            header = [line.rstrip("\n") for line in handle]
        crc = sram_crc32(args.sram) if args.sram else ""
        header = [line.replace("{FRAMES}", str(len(pads))).replace("{CRC}", crc) for line in header]
    write_movie(args.out, pads, header)
    print("%s: %d frames, %d events" % (args.out, len(pads), len(events_from_pads(pads))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
