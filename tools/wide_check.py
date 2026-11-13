"""The wider view changes only the picture: play each movie at the original's
256x224 and at a wider view, and fail where the two end in different work RAM
or save RAM (docs/PLATFORM.md, "The wider view, as built")."""
import argparse
import concurrent.futures
import pathlib
import sys
import tempfile

if __package__ in (None, ''):
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from tools import outcomes
from tools.native_check import ended, run_native, save_for, saves_by_sum
from tools.native_state import ROOT, WRAM_BYTES
from tools.suite import gate_movies

ORIGINAL_VIEW, WIDEST_VIEW = '256x224', '480x224'


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

            with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 2))) as pool:
                results = list(pool.map(one, rows))
        for name, ok, detail in results:
            print('%s: %s%s' % (name, detail, '' if ok else ' <-- DIFFERS'))
        print('wide: %d/%d movies end the same at %s as at %s' % (
            sum(ok for _, ok, _ in results), len(results), args.view, ORIGINAL_VIEW))
        return int(any(not ok for _, ok, _ in results))
    except (OSError, ValueError) as error:
        print('wide: FAILED: %s' % error, file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
