"""Inventory ported instruction bytes no library movie executes.

Uses burndown.py's live-code discovery with a conservative audit walk that
keeps conditional handoffs. Addresses and counts are measurements, not
disassembly or cartridge contents.
"""
import argparse
import collections
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools import burndown, dontcare_check as dc
from tools.suite import collect
from tools.disasm import coverage
from tools.disasm.closure import reached_code
from tools.disasm.cpu import decode
from tools import unreached_fuzz

SUBSYSTEMS = ('attract', 'data', 'core', 'warp', 'overworld', 'event')
# This separately registered field entry owns the screen command's restart
# arm. Include the dependency so the entire uncovered dispatch is accounted.
DEPENDENCIES = {'field_restart_wait_for_vblank_loop': 'screen-command restart dependency'}


def in_scope(subsystem, name):
    return subsystem in SUBSYSTEMS or name in DEPENDENCIES


def runs(values):
    result = []
    for value in sorted(values):
        if result and result[-1][1] + 1 == value:
            result[-1][1] = value
        else:
            result.append([value, value])
    return result


def walk(an, entry, owners):
    """Conservative audit reach, not burndown's implementation estimate.

    A stop/call-out declaration says that a port *can* hand back there;
    it does not prove an unconditional boundary. Follow both continuations
    and all decoded callees until another registered entry owns the work.
    Original-only handoffs included this way need explicit ledger evidence.
    Missing instructions or unresolved dispatches cannot mean zero bytes.
    """
    seen, own, inline, unresolved = set(), set(), set(), set()
    queue = [(entry, False)]
    while queue:
        pc, inside = queue.pop()
        if (pc, inside) in seen:
            continue
        seen.add((pc, inside))
        insn = burndown.insn_at(an, pc)
        if insn is None:
            unresolved.add(f'{pc:06X}: no decoded instruction')
            continue
        (inline if inside else own).update(range(pc, pc + insn.size))
        nexts, callees = burndown.successors(an, insn)
        if insn.name in burndown.CALLS and not callees:
            unresolved.add(f'{pc:06X}: unresolved call')
        if not nexts and not callees and insn.name not in burndown.RETURNS:
            unresolved.add(f'{pc:06X}: unresolved {insn.name} successor')
        for target in callees:
            if target not in owners:
                queue.append((target, True))
        for target in nexts:
            if target == entry or target not in owners:
                queue.append((target, inside))
    return own, inline, sorted(unresolved)


def analyze(rom):
    an = burndown.Analyzer(burndown.Rom(rom.read_bytes()),
                           burndown.hints_mod.load(ROOT / 're/hints.toml'))
    an.run()
    ports = list({p.addr: p for p in burndown.read_ports(ROOT, an)}.values())
    sources, globals_ = dc.load_sources(ROOT)
    entries, _ = dc.registry_entries(next(s for s in sources if s.path == 'game/registry.c'),
                                    dc.Evaluator(sources, globals_))
    files = {name: s.path for s in sources if s.path.endswith('.c') for name in s.funcs}
    # Macro-generated entry bodies (notably EVENT_ENTRIES) are declared in
    # their subsystem headers rather than found by the C function scanner.
    mentions = collections.defaultdict(set)
    for s in sources:
        parts = s.path.split('/')
        if len(parts) >= 3:
            for word in set(re.findall(r'\b[A-Za-z_]\w*\b', s.text)):
                mentions[word].add(parts[1])
    subsystems = {}
    for e in entries:
        if e.fn in files:
            sub = files[e.fn].split('/')[1]
        else:
            candidates = mentions[e.fn]
            if len(candidates) != 1:
                raise ValueError(f'{e.name}: ambiguous source owner: {sorted(candidates)}')
            sub = next(iter(candidates))
        subsystems[e.addr] = sub
    live = reached_code(an)
    live_bytes = {b for pc in live if pc >> 16 not in burndown.RETIRED
                  for b in range(pc, pc + max(i.size for i in an.code[pc].values()))}
    owners = {p.addr for p in ports}
    walks, unresolved = [], []
    for p in ports:
        if in_scope(subsystems[p.addr], p.name):
            own, inline, issues = walk(an, p.addr, owners)
            unresolved.extend(f'{p.name} ({p.addr:06X}): {issue}' for issue in issues)
        else:
            own, inline = burndown.walk(an, p.addr, owners, p)
        walks.append((p, own, inline))
    if unresolved:
        raise ValueError('unresolved port walks:\n' + '\n'.join(unresolved))
    # Own regions take precedence over inlined regions; ties use entry order.
    assigned = set()
    owned = collections.defaultdict(set)
    for column in (1, 2):
        for row in walks:
            region = row[column] & live_bytes - assigned
            owned[row[0].addr].update(region)
            assigned.update(region)
    result = []
    for p in ports:
        sub = subsystems[p.addr]
        if not in_scope(sub, p.name):
            continue
        region = owned[p.addr]
        result.append(dict(subsystem=sub, entry=f'{p.addr:06X}', name=p.name,
                           size=len(region), reach=runs(region),
                           digest=hashlib.sha256(','.join(map(str, sorted(region))).encode()).hexdigest(),
                           instructions={f'{pc:06X}': [decode(an.read, pc, bool(w & 2), bool(w & 1)).size
                                                      for w in range(4)]
                                         for pc in sorted(region) if pc in an.code}))
    return result


def expand(spans):
    return {b for lo, hi in spans for b in range(lo, hi + 1)}


def spans_text(spans):
    return ','.join(f'{lo:06X}-{hi:06X}' for lo, hi in spans) or '-'


def parse_spans(value):
    if value == '-':
        return []
    spans = []
    for part in value.split(','):
        if not re.fullmatch(r'[0-9A-F]{6}-[0-9A-F]{6}', part):
            raise ValueError(f'bad byte run {part}')
        lo, hi = (int(p, 16) for p in part.split('-'))
        if hi < lo:
            raise ValueError(f'reversed byte run {part}')
        spans.append([lo, hi])
    if runs(expand(spans)) != spans:
        raise ValueError(f'noncanonical byte runs {value}')
    return spans


def file_hash(path):
    # Committed inputs are text; checkout newline conversion is not a change.
    return hashlib.sha256(path.read_text(encoding='utf-8').encode()).hexdigest()


def movie_hashes():
    return {name: file_hash(path) for name, path, _ in collect('all', None)}


def touched(row, pcs):
    result = set()
    for pc, widths in pcs.items():
        sizes = row['instructions'].get(f'{pc:06X}')
        if sizes:
            for m, x in widths:
                result.update(range(pc, pc + sizes[2 * m + x]))
    return result


def read_ledger(path):
    records = []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        if line and not line.startswith('#'):
            columns = line.split('\t')
            fields = {'movie': 4, 'port': 8, 'fuzz': 11, 'safe': 6}
            if len(columns) != fields.get(columns[0]) or not columns[-1].strip():
                raise ValueError(f'{path.name}:{n}: malformed record or missing reason')
            records.append(columns)
    return records


def check_fuzz_evidence(rows, records, directory):
    """Fresh runs must actually execute every byte their ledger rows claim."""
    by_entry = {r['entry']: r for r in rows}
    errors = []
    for c in records:
        if c[0] != 'fuzz' or c[1] not in by_entry:
            continue
        _, entry, spans, _, case, seed, trials, _, digest, _, _ = c
        prefix = directory / case.replace(':', '--')
        try:
            result = json.loads(prefix.with_suffix('.json').read_text())
            log = prefix.with_suffix('.log').read_text()
            pcs = coverage.load([prefix.with_suffix('.cov')])
            ok = (unreached_fuzz.clean(result, log) and result.get('fingerprint') == digest
                  and result.get('seed') == int(seed) and result.get('iterations') == int(trials)
                  and expand(parse_spans(spans)) <= touched(by_entry[entry], pcs))
        except (OSError, ValueError):
            ok = False
        if not ok:
            errors.append(f'{entry}: fresh fuzz evidence missing, failed or insufficient: {case}')
    return errors


def check_records(rows, records, movies, recipes, states, root=ROOT, measured=False):
    errors, ports, proofs, recorded_movies = [], {}, collections.defaultdict(set), {}
    for c in records:
        if c[0] == 'movie':
            _, name, digest, _ = c
            if name in recorded_movies:
                errors.append(f'duplicate movie: {name}')
            recorded_movies[name] = digest
            if movies.get(name) != digest:
                errors.append(f'{name}: movie removed or changed; remeasure the full library')
        elif c[0] == 'port':
            _, sub, entry, name, digest, size, spans, _ = c
            if entry in ports:
                errors.append(f'duplicate port: {entry}')
            missing = expand(parse_spans(spans))
            if len(missing) != int(size):
                errors.append(f'{name}: wrong byte count')
            ports[entry] = (sub, name, digest, missing)
        elif c[0] in ('fuzz', 'safe'):
            entry, spans = c[1:3]
            covered = expand(parse_spans(spans))
            if not covered or len(covered) != int(c[3]):
                errors.append(f'{entry}: empty proof or wrong byte count')
            if covered & proofs[entry]:
                errors.append(f'{entry}: overlapping evidence')
            proofs[entry].update(covered)
            if c[0] == 'fuzz':
                _, _, _, _, case, seed, trials, matched, digest, mismatches, _ = c
                if int(seed) < 0 or not 0 < int(matched) <= int(trials) or int(mismatches) != 0:
                    errors.append(f'{entry}: invalid fuzz result')
                if case not in recipes or digest != unreached_fuzz.fingerprint(recipes[case], states):
                    errors.append(f'{entry}: stale fuzz recipe {case}')
            else:
                for dependency in c[4].split(','):
                    name, digest = dependency.split('=')
                    path = root / name
                    if not path.is_file() or file_hash(path) != digest:
                        errors.append(f'{entry}: safety argument changed in {name}')
    if not recorded_movies:
        errors.append('no full-library movie manifest')
    for entry, (_, name, _, missing) in ports.items():
        if proofs[entry] != missing:
            errors.append(f'{name}: evidence does not cover exactly its unreached bytes')
    for c in records:
        if c[0] == 'fuzz' and c[1] in ports and c[4] in recipes:
            if not unreached_fuzz.covers(recipes[c[4]], ports[c[1]][1]):
                errors.append(f'{c[1]}: {c[4]} is not a reviewed caller of this C implementation')
    for entry in proofs.keys() - ports.keys():
        errors.append(f'{entry}: evidence without an inventoried port')
    for r in rows:
        old = ports.get(r['entry'])
        if old is None or old[:3] != (r['subsystem'], r['name'], r['digest']):
            errors.append(f"{r['name']}: new entry or changed reach; remeasure and check its uncovered paths")
        elif measured and not expand(r['runs']) <= old[3]:
            errors.append(f"{r['name']}: newly unreached bytes without evidence")
    return errors


def record(rows, movies, evidence_dir, safety, target):
    """Write only when every missing byte has clean comparisons or a reviewed reason."""
    import tomllib
    states, recipes = unreached_fuzz.recipes()
    evidence = []
    for path in sorted(evidence_dir.glob('*.json')):
        result = json.loads(path.read_text())
        case = unreached_fuzz.label(result['recipe'])
        if not unreached_fuzz.clean(result, path.with_suffix('.log').read_text()):
            continue
        if case not in recipes or result['recipe'] != recipes[case]:
            continue
        # Only results produced by the reproducible runner, for this recipe.
        if result.get('fingerprint') != unreached_fuzz.fingerprint(recipes[case], states):
            continue
        evidence.append((case, result, coverage.load([path.with_suffix('.cov')])))
    safe = tomllib.loads(safety.read_text())['safe']
    records = [['movie', name, digest, 'Full standard library coverage union; additional movies can only add coverage.']
               for name, digest in sorted(movies.items())]
    for row in rows:
        missing = expand(row['runs'])
        reason = 'Conservative live reach; own regions before inlined helpers.'
        if row['name'] in DEPENDENCIES:
            reason += ' ' + DEPENDENCIES[row['name']] + '.'
        records.append(['port', row['subsystem'], row['entry'], row['name'], row['digest'],
                        str(len(missing)), spans_text(row['runs']), reason])
        for case, result, pcs in sorted(evidence, key=lambda e: e[1]['recipe']['name'] != row['name']):
            if not unreached_fuzz.covers(result['recipe'], row['name']):
                continue
            checked = missing & touched(row, pcs)
            if not checked:
                continue
            records.append(['fuzz', row['entry'], spans_text(runs(checked)), str(len(checked)), case,
                            str(result['seed']), str(result['iterations']), str(result['matched']),
                            result['fingerprint'], '0', result['recipe']['why']])
            missing -= checked
        for proof in safe:
            if proof['name'] != row['name']:
                continue
            checked = expand(parse_spans(proof['runs']))
            if not checked <= missing:
                raise ValueError(f"{row['name']}: safety ranges overlap fuzz evidence or are not unreached")
            sources = proof['sources'] + ['game/registry.c']
            if row['subsystem'] == 'event':
                sources += ['game/event/script.h', 'game/event/entries.c']
            elif row['subsystem'] == 'overworld':
                sources += ['game/overworld/entries.c']
            dependencies = ','.join(f'{p}={file_hash(ROOT / p)}' for p in sources)
            records.append(['safe', row['entry'], spans_text(runs(checked)), str(len(checked)), dependencies, proof['why']])
            missing -= checked
        if missing:
            raise ValueError(f"{row['name']}: unchecked {spans_text(runs(missing))}")
    errors = check_records(rows, records, movies, recipes, states, measured=True)
    if errors:
        raise ValueError('\n'.join(errors))
    header = ('# Full-library unreached port ledger. Addresses/counts only; no ROM or trace contents.\n'
              '# movie\tname\tsha256\treason\n'
              '# port\tsubsystem\tentry\tname\treach-sha256\tbytes\truns\treason\n'
              '# fuzz\tentry\truns\tbytes\trecipe\tseed\ttrials\tmatched\trecipe-sha256\tmismatches\treason\n'
              '# safe\tentry\truns\tbytes\tsource=sha256,...\treason\n')
    target.write_text(header + '\n'.join('\t'.join(c) for c in records) + '\n')


def inventory(rom, paths, cache=None):
    import zlib
    if zlib.crc32(rom.read_bytes()) != 0x2D206BF7:
        raise ValueError('expected the verified Chrono Trigger (USA) ROM')
    inputs = sorted((ROOT / 'game').rglob('*.[ch]')) + sorted((ROOT / 're').glob('*.toml'))
    inputs += [ROOT / 'tools/burndown.py', ROOT / 'tools/dontcare_check.py', pathlib.Path(__file__)]
    inputs += sorted((ROOT / 'tools/disasm').glob('*.py'))
    digest = hashlib.sha256(rom.read_bytes())
    for path in inputs:
        digest.update(path.relative_to(ROOT).as_posix().encode())
        digest.update(path.read_bytes())
    key = digest.hexdigest()
    saved = json.loads(cache.read_text()) if cache and cache.exists() else {}
    if saved.get('key') == key:
        rows = saved['rows']
    else:
        rows = analyze(rom)
        if cache:
            cache.parent.mkdir(parents=True, exist_ok=True)
            cache.write_text(json.dumps(dict(key=key, rows=rows)))
    executed = set()
    sizes = {pc: sizes for r in rows for pc, sizes in r['instructions'].items()}
    for pc, widths in coverage.load(paths).items():
        if f'{pc:06X}' in sizes:
            for m, x in widths:
                size = sizes[f'{pc:06X}'][2 * m + x]
                executed.update(range(pc, pc + size))
    for row in rows:
        missing = expand(row['reach']) - executed
        row['runs'] = runs(missing)
        row['uncovered'] = len(missing)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=pathlib.Path, default=ROOT / 'roms/ct-usa.sfc')
    parser.add_argument('--coverage-dir', type=pathlib.Path)
    parser.add_argument('--json', type=pathlib.Path)
    parser.add_argument('--cache', type=pathlib.Path, default=ROOT / 'build/unreached/reach.json')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--ledger', type=pathlib.Path, default=ROOT / 'tools/unreached_baseline.txt')
    parser.add_argument('--record', action='store_true', help='write a complete, evidenced ledger (requires full coverage)')
    parser.add_argument('--fuzz-dir', type=pathlib.Path, default=ROOT / 'build/unreached/fuzz')
    parser.add_argument('--safety', type=pathlib.Path, default=ROOT / 'tools/unreached_safe.toml')
    parser.add_argument('--recheck-fuzz', action='store_true', help='verify the ledger against fresh --fuzz-dir results')
    args = parser.parse_args()
    if not args.coverage_dir and (not args.check or args.record):
        parser.error('inventory/record requires --coverage-dir for the full standard library')
    movies = movie_hashes()
    paths = [args.coverage_dir / f'cov-{name}.txt' for name in sorted(movies)] if args.coverage_dir else []
    absent = [p for p in paths if not p.exists()]
    if absent:
        print('Missing library coverage: ' + ', '.join(p.name for p in absent), file=sys.stderr)
        return 1
    rows = inventory(args.rom, paths, args.cache)
    if args.record:
        record(rows, movies, args.fuzz_dir, args.safety, args.ledger)
    if args.check:
        states, recipes = unreached_fuzz.recipes()
        records = read_ledger(args.ledger)
        errors = check_records(rows, records, movies, recipes, states,
                               measured=bool(args.coverage_dir))
        if args.recheck_fuzz:
            errors += check_fuzz_evidence(rows, records, args.fuzz_dir)
        for error in errors:
            print(error)
        print('unreached FAIL' if errors else f'unreached OK: {len(rows)} entries, {len(movies)} library movies')
        return int(bool(errors))
    for row in rows:
        if row['uncovered']:
            print('{subsystem}\t{entry}\t{name}\t{uncovered}\t'.format(**row) +
                  ','.join(f'{lo:06X}-{hi:06X}' for lo, hi in row['runs']))
    for sub in dict.fromkeys(list(SUBSYSTEMS) + [r['subsystem'] for r in rows]):
        print(f"{sub}: {sum(r['uncovered'] for r in rows if r['subsystem'] == sub)} bytes")
    if args.json:
        args.json.write_text(json.dumps(rows, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError) as exc:
        print(f'unreached FAIL: {exc}', file=sys.stderr)
        sys.exit(1)
