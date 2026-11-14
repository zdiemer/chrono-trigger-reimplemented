"""The lean build plays as the normal build does: play each gate movie with
both and fail where the two end differently, or in different work RAM or save
RAM (docs/PLATFORM.md, "Memory, stacks and per-frame work against small
targets"). The lean build (CT_LEAN) leaves out the diagnostics, the overlay
crossing tally, the port's own checks and the timing report, so a movie that
ends otherwise in it is a finding: something the game did depended on them.

    python3 tools/lean_check.py --ct-native build/main/ct_native --lean build/lean/ct_native [--movies NAME...]
"""
import argparse
import concurrent.futures
import pathlib
import sys
import tempfile

if __package__ in (None, ''):
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from tools import outcomes
from tools.native_check import ended, run_native, save_for, saves_by_sum
from tools.native_state import ROOT
from tools.suite import gate_movies
from tools.wide_check import differences


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--ct-native', type=pathlib.Path, required=True, help='the normal build')
    ap.add_argument('--lean', type=pathlib.Path, required=True, help='the CT_LEAN build')
    ap.add_argument('--rom', type=pathlib.Path, default=ROOT / 'roms/ct-usa.sfc')
    ap.add_argument('--movies', nargs='*', help='default: the gate sample (tools/gate_movies.txt)')
    ap.add_argument('-j', '--jobs', type=int, default=2, choices=(1, 2))
    args = ap.parse_args(argv)
    try:
        names = args.movies
        if not names:
            names, bad = gate_movies()
            if bad:
                raise ValueError('; '.join(bad))
        rows = outcomes.movie_library(names, set())
        saves = saves_by_sum()
        normal, lean = args.ct_native.resolve(), args.lean.resolve()
        with tempfile.TemporaryDirectory(prefix='lean-') as scratch:
            work = pathlib.Path(scratch)
            pack = work / 'ct.assets'
            _, rc = run_native(normal, args.rom, rows[0][1], 1, None, ['--assets', str(pack)])
            if rc or not pack.exists():
                raise ValueError('could not import native assets')

            def play(row, binary, kind):
                name, movie, frames = row
                end = work / ('%s-%s.bin' % (name.replace('/', '-'), kind))
                text, rc = run_native(binary, args.rom, movie, frames, save_for(movie, saves) or None,
                                      ['--headless', '--mute', '--assets', str(pack), '--end-state', str(end)])
                how, detail = ended(text, rc, frames)
                return how, detail, end.read_bytes() if end.exists() else b''

            def one(row):
                was, now = play(row, normal, 'normal'), play(row, lean, 'lean')
                if not was[2] or not now[2]:
                    return row[0], False, 'no end state (%s normally, %s lean)' % (was[0], now[0])
                if was[0] != now[0]:
                    return row[0], False, 'ended %s normally but %s lean: %s' % (was[0], now[0], now[1])
                found = differences(was[2], now[2])
                return row[0], not found, ', '.join(found) or 'same work RAM and save RAM'

            with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
                results = list(pool.map(one, rows))
        for name, ok, detail in results:
            print('%s: %s%s' % (name, detail, '' if ok else ' <-- DIFFERS'))
        same = sum(ok for _, ok, _ in results)
        print('lean: %d/%d movies end the same in the lean build as in the normal one' % (same, len(results)))
        return int(same != len(results))
    except (OSError, ValueError) as error:
        print('lean: FAILED: %s' % error, file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
