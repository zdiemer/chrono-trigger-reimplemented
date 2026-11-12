"""The native product gate: sampled movies, fast routes, source checks and unit tests."""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUDGET = 600

if __package__ in (None, ""):
    sys.path.insert(0, str(ROOT))
from tools.movies.route_check import all_routes

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


def long_batch(args, binary):
    """Collect gameplay findings without making them a landing requirement."""
    args.out.mkdir(parents=True, exist_ok=True)
    runs = args.out / 'outcomes'
    py = sys.executable
    stages = [
        ('movies', [py, 'tools/native_check.py', '--ct-native', str(binary), '--rom', str(args.rom),
                    '--outcomes', str(runs), '-j', str(args.jobs)]),
        ('outcomes', [py, 'tools/outcomes.py', '--check', '--runs', str(runs)]),
        ('routes', [py, '-m', 'tools.movies.route_check', '--drive', str(binary), '--rom', str(args.rom),
                    '--scratch', str(args.out / 'routes'), '--protocol-log', '-j', str(args.jobs), *all_routes()]),
    ]
    started = time.monotonic()
    findings = []
    print('Non-blocking batch: findings become fixes; never a commit, merge or push requirement.', flush=True)
    for name, command in stages:
        log = args.out / (name + '.log')
        print(name + ': ' + str(log), flush=True)
        before = time.monotonic()
        with log.open('w', encoding='utf-8') as output:
            try:
                status = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT).returncode
            except OSError as error:
                output.write(str(error) + '\n')
                print('batch setup failed: could not launch %s: %s (log: %s)' % (name, error, log))
                return 1
        text = log.read_text(encoding='utf-8', errors='replace')
        # Each stage exits 0 when clean and 1 with findings; anything else, a crash or an empty
        # log is the batch failing to run, not a finding.
        if status not in (0, 1) or 'Traceback (most recent call last)' in text or not text.strip():
            print('batch setup failed: %s exited %d without its results (log: %s)' % (name, status, log))
            return 1
        findings.append(dict(stage=name, status=status, seconds=time.monotonic() - before, log=log.name))
    report = args.out / 'findings.json'
    report.write_text(json.dumps(findings, indent=2) + '\n')
    print('times: ' + ', '.join('%s %.1fs' % (r['stage'], r['seconds']) for r in findings))
    print('wall time: %.1fs (non-blocking batch; no gate budget)' % (time.monotonic() - started))
    print('batch complete: %d stage(s) with findings; %s' % (sum(r['status'] != 0 for r in findings), report))
    return 0


def main(argv=None):
    started = time.monotonic()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--ct-native', type=pathlib.Path, required=True)
    ap.add_argument('--rom', type=pathlib.Path, default=ROOT / 'roms/ct-usa.sfc')
    ap.add_argument('--out', type=pathlib.Path)
    ap.add_argument('--long', action='store_true', help='non-blocking whole-library and every-route batch')
    ap.add_argument('--movies', nargs='+')
    ap.add_argument('-j', '--jobs', type=int, choices=(1, 2), default=2)
    args = ap.parse_args(argv)
    if args.long and args.movies:
        ap.error('--long runs the whole library; omit --movies')
    args.out = args.out or ROOT / ('build/suite-long' if args.long else 'build/suite')
    binary = args.ct_native.resolve()
    if args.long:
        if not binary.is_file():
            ap.error('build ct_native first')
        return long_batch(args, binary)
    sample, bad = gate_movies()
    names = args.movies or sample
    if bad:
        print('\n'.join(bad))
        return 1
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
