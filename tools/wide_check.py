"""The wider view changes only the picture: play each movie at the original's
256x224 and at a wider view, and fail where the two end in different work RAM
or save RAM (docs/PLATFORM.md, "The wider view, as built"). Then look at the
margins of moments where they once showed what the original's screen never
does (MARGINS), and fail where they hold more colours than they should."""
import argparse
import concurrent.futures
import pathlib
import struct
import sys
import tempfile
import zlib

if __package__ in (None, ''):
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from tools import outcomes
from tools.native_check import ended, run_native, save_for, saves_by_sum
from tools.native_state import ROOT, WRAM_BYTES
from tools.suite import gate_movies

ORIGINAL_VIEW, WIDEST_VIEW = '256x224', '480x224'
ORIGINAL_WIDTH = 256

# Moments whose margins hold no more than a colour or two: (movie, frame,
# what it is, the margin looked at, how many colours it may hold). The margin
# is both sides whole, or the view's rightmost columns down to a line, which
# must be black.
# Crono's mother opens the blinds with a send of BG1's whole tile map, whose
# columns past the screen are the next room's (#331): the margins are the
# room's backdrop and the black of the lines below the picture. Denadoro's
# battle moves the picture across past the room's right edge, where the view
# is black, not BG3's grey-blue fill (#399).
BOTH_SIDES = None
MARGINS = [('new-game', 6164, "Crono's mother opens the blinds", BOTH_SIDES, 2),
           ('new-game', 6165, "Crono's mother opens the blinds", BOTH_SIDES, 2),
           ('saves/battle-denadoro', 7291, "a Denadoro battle past the room's edge", (60, 150), 1)]


def png_pixels(path):
    """(width, height, rows of RGB bytes) of a picture ct_native wrote: 8-bit
    RGB, each row unfiltered (frontend/media.c)."""
    data = pathlib.Path(path).read_bytes()
    at, idat, width, height = 8, b'', 0, 0
    while at < len(data):
        length, kind = struct.unpack('>I4s', data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if kind == b'IHDR':
            width, height = struct.unpack('>II', body[:8])
        elif kind == b'IDAT':
            idat += body
        at += 12 + length
    raw, row = zlib.decompress(idat), 1 + width * 3
    return width, height, [raw[y * row + 1:(y + 1) * row] for y in range(height)]


def margin_colours(path, region=BOTH_SIDES):
    """The distinct colours either side of the original's screen, or in the
    view's rightmost `columns` down to `lines` (region)."""
    width, _, rows = png_pixels(path)
    side = (width - ORIGINAL_WIDTH) // 2
    xs = list(range(side)) + list(range(width - side, width))
    if region is not BOTH_SIDES:
        columns, lines = region
        xs, rows = list(range(width - columns, width)), rows[:lines]
    seen = set()
    for line in rows:
        for x in xs:
            seen.add(line[x * 3:x * 3 + 3])
    return seen


def differences(narrow, wide, shown=4):
    """Where two end states differ, as `wram $7E:1234` / `sram $0123` ranges, at most `shown`."""
    if len(narrow) != len(wide):
        return ['end states of %d and %d bytes' % (len(narrow), len(wide))]
    spans, start = [], None
    for at in range(len(narrow) + 1):
        same = at == len(narrow) or narrow[at] == wide[at]
        if not same and start is None:
            start = at
        elif same and start is not None:
            spans.append((start, at))
            start = None
    def name(at):
        if at < WRAM_BYTES:
            return 'wram $%02X:%04X' % (0x7E + (at >> 16), at & 0xFFFF)
        return 'sram $%04X' % (at - WRAM_BYTES)
    out = ['%s (%d bytes)' % (name(a), b - a) for a, b in spans[:shown]]
    if len(spans) > shown:
        out.append('and %d more' % (len(spans) - shown))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--ct-native', type=pathlib.Path, required=True)
    ap.add_argument('--rom', type=pathlib.Path, default=ROOT / 'roms/ct-usa.sfc')
    ap.add_argument('--view', default=WIDEST_VIEW)
    ap.add_argument('--movies', nargs='*', help='default: the gate sample (tools/gate_movies.txt)')
    ap.add_argument('-j', '--jobs', type=int, default=2)
    args = ap.parse_args(argv)
    try:
        names = args.movies
        if not names:
            names, bad = gate_movies()
            if bad:
                raise ValueError('; '.join(bad))
        rows = outcomes.movie_library(names, set())
        saves, binary = saves_by_sum(), args.ct_native.resolve()
        with tempfile.TemporaryDirectory(prefix='wide-') as scratch:
            work = pathlib.Path(scratch)
            pack = work / 'ct.assets'
            _, rc = run_native(binary, args.rom, rows[0][1], 1, None, ['--assets', str(pack)])
            if rc or not pack.exists():
                raise ValueError('could not import native assets')

            def play(row, view):
                name, movie, frames = row
                end = work / ('%s-%s.bin' % (name, view))
                text, rc = run_native(binary, args.rom, movie, frames, save_for(movie, saves) or None,
                                      ['--assets', str(pack), '--view', view, '--end-state', str(end)])
                how, detail = ended(text, rc, frames)
                return how, detail, end.read_bytes() if end.exists() else b''

            def one(row):
                narrow, wide = play(row, ORIGINAL_VIEW), play(row, args.view)
                if not narrow[2] or not wide[2]:
                    return row[0], False, 'no end state (%s at %s, %s at %s)' % (
                        narrow[0], ORIGINAL_VIEW, wide[0], args.view)
                if narrow[0] != wide[0]:
                    return row[0], False, 'ended %s at %s but %s at %s: %s' % (
                        narrow[0], ORIGINAL_VIEW, wide[0], args.view, wide[1])
                found = differences(narrow[2], wide[2])
                return row[0], not found, ', '.join(found) or 'same work RAM and save RAM'

            def margins(moment):
                name, frame, what, region, most = moment
                movie = ROOT / 'movies' / (name + '.ctm')
                shot = work / ('%s-%d.png' % (name.replace('/', '-'), frame))
                text, rc = run_native(binary, args.rom, movie, frame, save_for(movie, saves) or None,
                                      ['--assets', str(pack), '--view', args.view, '--screenshot', str(shot)])
                if rc != 0 or not shot.exists():
                    return '%s at %d' % (name, frame), False, 'no picture (exit status %s)' % rc
                colours = margin_colours(shot, region)
                black = region is BOTH_SIDES or colours == {bytes(3)}
                return ('%s at %d' % (name, frame), len(colours) <= most and black,
                        '%s: %d colour(s) in the margin%s' % (what, len(colours), '' if black else ', not black'))

            with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 2))) as pool:
                results = list(pool.map(one, rows))
                pictures = list(pool.map(margins, MARGINS))
        for name, ok, detail in results:
            print('%s: %s%s' % (name, detail, '' if ok else ' <-- DIFFERS'))
        for name, ok, detail in pictures:
            print('%s: %s%s' % (name, detail, '' if ok else ' <-- MORE THAN IT SHOULD SHOW'))
        results += pictures
        print('wide: %d/%d movies end the same at %s as at %s, and %d/%d moments show only what their margins should' % (
            sum(ok for _, ok, _ in results[:len(rows)]), len(rows), args.view, ORIGINAL_VIEW,
            sum(ok for _, ok, _ in pictures), len(pictures)))
        return int(any(not ok for _, ok, _ in results))
    except (OSError, ValueError) as error:
        print('wide: FAILED: %s' % error, file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
