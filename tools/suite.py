"""The native product gate: sampled movies, fast routes, source checks and unit tests."""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUDGET = 600

GATE_MOVIES = ROOT / "tools/gate_movies.txt"


def gate_movies():
    """The names tools/gate_movies.txt lists (`name  # reason` a line), and its
    lines that are malformed or name no movie in the library."""
    names, bad = [], []
    library = {p.stem for p in ROOT.glob("movies/**/*.ctm")}
    for number, line in enumerate(GATE_MOVIES.read_text().splitlines(), 1):
        body, _, reason = line.partition("#")
        words = body.split()
        if not words:
            continue
        if len(words) != 1 or not reason.strip():
            bad.append("%s:%d: not `name  # reason`" % (GATE_MOVIES.name, number))
        elif words[0] not in library:
            bad.append("%s:%d: %s is no movie in the library" % (GATE_MOVIES.name, number, words[0]))
        elif words[0] in names:
            bad.append("duplicate sampled movie: " + words[0])
        else:
            names.append(words[0])
    if not names:
        bad.append("empty gate sample")
    return names, bad


def main(argv=None):
    started = time.monotonic()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--ct-native', type=pathlib.Path, required=True)
    ap.add_argument('--rom', type=pathlib.Path, default=ROOT / 'roms/ct-usa.sfc')
    ap.add_argument('--out', type=pathlib.Path, default=ROOT / 'build/suite')
    ap.add_argument('--movies', nargs='+')
    ap.add_argument('-j', '--jobs', type=int, choices=(1, 2), default=2)
    args = ap.parse_args(argv)
    sample, bad = gate_movies()
    names = args.movies or sample
    if bad:
        print('\n'.join(bad))
        return 1
    binary = args.ct_native.resolve()
    ctest = shutil.which('ctest')
    cache = binary.parent / 'CMakeCache.txt'
    if not ctest and cache.exists():
        for line in cache.read_text().splitlines():
            if line.startswith('CMAKE_COMMAND:INTERNAL='):
                ctest = str(pathlib.Path(line.split('=', 1)[1]).with_name('ctest.exe' if os.name == 'nt' else 'ctest'))
    if not binary.is_file() or not ctest or not (binary.parent / 'CTestTestfile.cmake').is_file():
        print('FAILED: configure and build ct_native with BUILD_TESTING=ON first')
        return 1
    args.out.mkdir(parents=True, exist_ok=True)
    runs = args.out / 'outcomes'
    py = sys.executable
    stages = [
        ('unit', [py, '-m', 'unittest', 'discover', 'tests']),
        ('native-unit', [ctest, '--test-dir', str(binary.parent), '--output-on-failure', '--no-tests=error', '-j', str(args.jobs)]),
        ('movies', [py, 'tools/native_check.py', '--ct-native', str(binary), '--rom', str(args.rom), '--outcomes', str(runs), '-j', str(args.jobs), '--movies', *names]),
        ('outcomes', [py, 'tools/outcomes.py', '--check', '--runs', str(runs), '--movies', *names]),
        ('routes', [py, '-m', 'tools.movies.route_check', '--drive', str(binary), '--rom', str(args.rom), '--scratch', str(args.out / 'routes'), '--timeout', '90', '-j', str(args.jobs)]),
        ('portstyle', [py, 'tools/portstyle.py', '--check']),
        ('portsmell', [py, 'tools/portsmell.py', '--check']),
    ]
    times, failed = [], []
    for name, command in stages:
        print('\n=== ' + name + ' ===', flush=True)
        before = time.monotonic()
        try:
            status = subprocess.run(command, cwd=ROOT, env=dict(os.environ, CT_TEST_BUILD=str(binary.parent))).returncode
        except OSError as error:
            print(error)
            status = 1
        times.append((name, time.monotonic() - before))
        if status:
            failed.append(name)
    elapsed = time.monotonic() - started
    print('times: ' + ', '.join('%s %.1fs' % item for item in times))
    print('wall time: %.1fs (budget < %ds, unit tests included)' % (elapsed, BUDGET))
    if elapsed >= BUDGET:
        failed.append('wall-time budget')
    print('FAILED: ' + ', '.join(failed) if failed else 'suite OK')
    return int(bool(failed))

if __name__ == '__main__':
    sys.exit(main())
