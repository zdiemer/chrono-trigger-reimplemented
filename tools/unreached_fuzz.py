"""Replay the targeted trials in unreached_cases.toml, recording matched coverage.

Base recipes live in fuzz_routines.toml. Cases replace a fill of the same area
or a range of the same register; other arguments are appended. No game state,
coverage file or cartridge data is committed.
"""
import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import subprocess
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools import fuzz_compare


def label(recipe):
    return recipe['name'] + (':' + recipe['variant'] if recipe.get('variant') else '')


def merge_args(base, extra):
    out = list(base)
    pairs = iter(extra)
    flags = {'--long', '--interrupt', '--jump', '--line-handler', '--apu-loopback', '--poison-dc'}
    for option in pairs:
        if option in flags:
            if option not in out:
                out.append(option)
            continue
        value = next(pairs)
        for i in range(len(out) - 1):
            if out[i] != option:
                continue
            if option in ('--fill', '--range') and out[i + 1].split('=')[0] != value.split('=')[0]:
                continue
            if option in ('--keep', '--stop-at-call'):
                continue
            out[i + 1] = value
            break
        else:
            out.extend([option, value])
    return out


def recipes():
    states, base = fuzz_compare.load()
    result = {label(r): r for r in base}
    path = ROOT / 'tools/unreached_cases.toml'
    config = tomllib.loads(path.read_text())
    pending = list(config.get('case', []))
    while pending:
        ready = next((i for i, c in enumerate(pending) if c['base'] in result), None)
        if ready is None:
            raise ValueError('unknown or cyclic case bases')
        case = pending.pop(ready)
        parent = result[case['base']]
        parent_args = list(parent['args'])
        for option in case.get('remove', []):
            while option in parent_args:
                i = parent_args.index(option)
                del parent_args[i:i + 2]
        if case.get('profile'):
            parent_args = merge_args(parent_args, config['profile'][case['profile']]['args'])
        recipe = dict(parent, variant=case['variant'],
                      args=merge_args(parent_args, case.get('args', [])), why=case['why'])
        for key in ('iterations', 'state', 'seed'):
            if key in case:
                recipe[key] = case[key]
        key = label(recipe)
        if key in result:
            raise ValueError(f'duplicate case {key}')
        result[key] = recipe
    for recipe in result.values():
        shared = [owner for owner, callers in config.get('shared', {}).items() if recipe['name'] in callers]
        if shared:
            recipe['covers'] = shared
    return states, result


def covers(recipe, owner):
    return recipe['name'] == owner or owner in recipe.get('covers', [])


def fingerprint(recipe, states):
    state = states[recipe['state']]
    movie = ROOT / state['movie']
    return hashlib.sha256(json.dumps([recipe, state, hashlib.sha256(movie.read_text(encoding='utf-8').encode()).hexdigest()],
                                     sort_keys=True).encode()).hexdigest()


def clean(result, log):
    return (result.get('exit') == 0 and result.get('matched', 0) > 0
            and result.get('mismatched') == 0
            and not any(s in log for s in ('accessor out of range', 'page refused',
                                           'asset outside the catalogue', 'asset differs from the cartridge')))


def make_state(ct_dev, rom, directory, name, spec):
    """Do not reuse a state after its movie, specification or interpreter changes."""
    path = directory / (name + '.state')
    manifest = path.with_suffix('.json')
    movie = ROOT / spec['movie']
    inputs = dict(spec=spec, movie=hashlib.sha256(movie.read_text(encoding='utf-8').encode()).hexdigest(),
                  interpreter=hashlib.sha256(ct_dev.read_bytes()).hexdigest(),
                  rom=hashlib.sha256(rom.read_bytes()).hexdigest())
    saved = json.loads(manifest.read_text()) if manifest.exists() else {}
    if saved.get('inputs') == inputs and path.exists() and saved.get('state') == hashlib.sha256(path.read_bytes()).hexdigest():
        return path
    if path.exists():
        path.unlink()
    fuzz_compare.make_state(ct_dev, rom, directory, name, spec)
    manifest.write_text(json.dumps(dict(inputs=inputs, state=hashlib.sha256(path.read_bytes()).hexdigest())) + '\n')
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--rom', type=pathlib.Path, default=ROOT / 'roms/ct-usa.sfc')
    ap.add_argument('--ct-fuzz', type=pathlib.Path, required=True)
    ap.add_argument('--ct-dev', type=pathlib.Path, required=True)
    ap.add_argument('--out', type=pathlib.Path, default=ROOT / 'build/unreached')
    selection = ap.add_mutually_exclusive_group(required=True)
    selection.add_argument('--case', action='append', help='name[:variant], repeatable')
    selection.add_argument('--ledger', type=pathlib.Path, help='replay all fuzz recipes cited in this ledger')
    ap.add_argument('--iterations', type=int, default=1000)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('-j', type=int, default=2, choices=(1, 2))
    args = ap.parse_args()
    states, all_recipes = recipes()
    cases = args.case
    ledger_trials = {}
    if args.ledger:
        for line in args.ledger.read_text().splitlines():
            if line.startswith('fuzz\t'):
                columns = line.split('\t')
                case, seed, trials = columns[4:7]
                limits = (int(seed), int(trials))
                if case in ledger_trials and ledger_trials[case] != limits:
                    ap.error(f'{case}: inconsistent trial specifications in ledger')
                ledger_trials[case] = limits
        cases = sorted(ledger_trials)
    chosen = [all_recipes[key] for key in cases]
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'fuzz').mkdir(exist_ok=True)
    for name in sorted({r['state'] for r in chosen}):
        make_state(args.ct_dev.resolve(), args.rom.resolve(), args.out / 'states', name, states[name])

    def run(recipe):
        prefix = args.out / 'fuzz' / label(recipe).replace(':', '--')
        seed = recipe.get('seed', args.seed)
        iterations = min(recipe.get('iterations', args.iterations), args.iterations)
        seed, iterations = ledger_trials.get(label(recipe), (seed, iterations))
        cmd = [str(args.ct_fuzz.resolve()), '--rom', str(args.rom.resolve()), '--load-state',
               str(args.out / 'states' / (recipe['state'] + '.state')), '--routine',
               recipe.get('at', recipe['name']).replace(':', ''), '--seed', str(seed),
               '--iterations', str(iterations), '--details', '3', '--coverage', str(prefix.with_suffix('.cov'))]
        cmd += recipe['args']
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        prefix.with_suffix('.log').write_text(proc.stdout)
        match = fuzz_compare.RESULT.search(proc.stdout)
        result = ({k: v if k == 'name' else int(v) for k, v in match.groupdict().items()}
                  if match else {'error': proc.stdout[-500:]})
        result.update(recipe=recipe, seed=seed, cmd=cmd, exit=proc.returncode,
                      fingerprint=fingerprint(recipe, states))
        prefix.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
        ok = clean(result, proc.stdout)
        print(label(recipe), 'OK' if ok else 'FAILED',
              {k: v for k, v in result.items() if k not in ('recipe', 'cmd', 'fingerprint')}, flush=True)
        return ok

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.j) as pool:
        results = list(pool.map(run, chosen))
    return 0 if all(results) else 1


if __name__ == '__main__':
    sys.exit(main())
