"""Run sampled native movies to completion and check saves, places and dark screens."""
import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

if __package__ in (None, ''):
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from tools import outcomes, places
from tools.native_state import ROOT, SavedGame, seconds_for


def read_picture_baseline():
    """Native fingerprints accepted with the title fill, independent of the oracle."""
    pins = {}
    path = ROOT / 'tools/native_picture_baseline.txt'
    for line in path.read_text().splitlines():
        body, _, reason = line.partition('#')
        if not body.strip():
            continue
        fields = body.split()
        if (len(fields) != 2 or not re.fullmatch(r'[0-9a-f]{64}', fields[1]) or
                not reason.strip() or fields[0] in pins):
            raise ValueError('malformed or duplicate native picture fingerprint')
        pins[fields[0]] = fields[1]
    return pins


def picture_matches(text, expected):
    tokens = [frame + ':' + digest for frame, digest in
              re.findall(r'^outcome-picture: (\d+) ([0-9a-f]{16})\r?$', text, re.M)]
    return bool(tokens) and outcomes.observed_digest(tokens) == expected

DARK_EXCEPTIONS = ROOT / 'tools/native_dark_exceptions.txt'
PLACES_BASELINE = ROOT / "tools/places_baseline.txt"
DARK_FRAMES = 600
SRAM_CRC = re.compile(r'^# sram crc32 ([0-9A-Fa-f]{8})', re.M)
DARK = re.compile(r'^picture: dark since frame (\d+) \((\d+) frame\(s\), to the end\)', re.M)

def saves_by_sum():
    return {zlib.crc32(p.read_bytes()): p for p in sorted((ROOT / 'saves').rglob('*.srm'))}

def save_for(path, saves):
    m = SRAM_CRC.search(path.read_text(errors='replace'))
    return saves.get(int(m.group(1), 16), False) if m else None

def run_native(binary, rom, path, frames, sram, extra):
    command = [str(binary), '--rom', str(rom), '--movie', str(path), '--frames', str(frames)] + extra
    if sram:
        command += ['--sram', str(sram)]
    try:
        p = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=seconds_for(frames))
        return p.stderr.decode(errors='replace'), p.returncode
    except subprocess.TimeoutExpired as e:
        return (e.stderr or b'').decode(errors='replace'), None

def ended(text, rc, frames):
    if rc is None:
        return 'hung', 'timeout'
    if rc != 0 or outcomes.FAULT.search(text):
        return 'crashed', 'exit status %s or runtime diagnostic' % rc
    if not re.search(r'^ct_native: %d frame\(s\);' % frames, text, re.M):
        return 'incomplete', 'missing completed-frame report'
    m = DARK.search(text)
    if m and int(m.group(2)) >= DARK_FRAMES:
        start = int(m.group(1))
        entered = [p for f, p in places.parse(text) if f <= start]
        where = ' in $%03X' % entered[-1] if entered else ''
        return 'dark', 'the picture black from frame %d%s to the end (%s frames)' % (start, where, m.group(2))
    return 'end', ''

def read_dark_exceptions():
    """The dark stretches tools/native_dark_exceptions.txt excuses, by movie:
    `movie $PLACE FRAME # why`, the place the run is in as its picture goes
    black and the first frame it may go black on."""
    out = {}
    if DARK_EXCEPTIONS.exists():
        for line in DARK_EXCEPTIONS.read_text().splitlines():
            fields = line.partition("#")[0].split()
            if len(fields) == 3:
                out[fields[0]] = (int(fields[1].lstrip("$"), 16), int(fields[2]))
    return out


DARK_DETAIL = re.compile(r"from frame (\d+) in \$([0-9A-F]+) ")


def dark_is_excepted(stretch, detail):
    """Whether a run's dark ending is the stretch its exception names: black
    from the named frame or later, in the named place. Dark anywhere else
    (sooner, or in another place) still fails."""
    m = DARK_DETAIL.search(detail or "")
    if stretch is None or not m:
        return False
    place, first = stretch
    return int(m.group(2), 16) == place and int(m.group(1)) >= first


def read_places_baseline():
    base = {}
    if PLACES_BASELINE.exists():
        for line in PLACES_BASELINE.read_text().splitlines():
            fields = line.partition("#")[0].split()
            if len(fields) == 2:
                base[fields[0]] = int(fields[1])
    return base


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--ct-native', type=pathlib.Path, required=True)
    ap.add_argument('--rom', type=pathlib.Path, default=ROOT / 'roms/ct-usa.sfc')
    ap.add_argument('--assets', type=pathlib.Path)
    ap.add_argument('--movies', nargs='*', default=[])
    ap.add_argument('--outcomes', type=pathlib.Path, required=True)
    ap.add_argument('-j', '--jobs', type=int, default=2)
    args = ap.parse_args(argv)
    try:
        rows = outcomes.movie_library(args.movies, set())
        if not rows:
            raise ValueError('no movies selected')
        args.outcomes.mkdir(parents=True, exist_ok=True)
        saved, saves, exceptions = SavedGame(args.rom), saves_by_sum(), read_dark_exceptions()
        ledger, baseline = outcomes.read(outcomes.LEDGER), read_places_baseline()
        picture_pins = read_picture_baseline()
        outcomes.require_movies(set(picture_pins))
        with tempfile.TemporaryDirectory(prefix='native-') as scratch:
            work = pathlib.Path(scratch)
            pack = args.assets or work / 'ct.assets'
            extra = ['--assets', str(pack)]
            if not pack.exists():
                _, rc = run_native(args.ct_native.resolve(), args.rom, rows[0][1], 1, None, extra)
                if rc or not pack.exists():
                    raise ValueError('could not import native assets')
            def one(row):
                name, movie, frames = row
                sram = save_for(movie, saves)
                if sram is False:
                    return name, False, 'missing cartridge save'
                end = work / (name + '.bin')
                text, rc = run_native(args.ct_native.resolve(), args.rom, movie, frames, sram,
                                      extra + ['--locations', '--end-state', str(end)] +
                                      (['--outcomes'] if name in picture_pins else []))
                outcomes.capture(args.outcomes, name, movie, frames, text, rc, end, saved, 'native')
                how, detail = ended(text, rc, frames)
                ok = how == 'end' or how == 'dark' and dark_is_excepted(exceptions.get(name), detail)
                kept = places.Kept(outcomes.pairs(ledger[name, 'places']), places.parse(text))
                ok = ok and name in baseline and kept.kept >= baseline[name]
                if name in picture_pins and not picture_matches(text, picture_pins[name]):
                    ok = False
                    detail += '; native picture fingerprint changed'
                return name, ok, how + (': ' + detail if detail else '') + '; ' + kept.summary()
            with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 2))) as pool:
                results = list(pool.map(one, rows))
        for name, ok, detail in results:
            print('%s: %s%s' % (name, detail, '' if ok else ' <-- FAILED'))
        print('native: %d/%d movies complete without unexplained dark screens' % (sum(ok for _, ok, _ in results), len(results)))
        return int(any(not ok for _, ok, _ in results))
    except (OSError, ValueError) as error:
        print('native: FAILED: %s' % error, file=sys.stderr)
        return 1

if __name__ == '__main__':
    sys.exit(main())
