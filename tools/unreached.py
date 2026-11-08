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

SUBSYSTEMS = ('attract', 'core', 'warp', 'overworld', 'event', 'field', 'scene')
# Subsystems inventoried ahead of their teardown. Bytes no comparison or
# reviewed argument covers yet are recorded as `open` rows rather than refused,
# so the ledger names exactly what is left (docs/REIMPLEMENTATION.md, "The code
# the movie library does not reach"). A ratchet: `--check` fails when a
# subsystem's open bytes pass its cap here, and `--record` adds no open byte
# the previous ledger didn't have, unless `--admit-open` names the subsystem
# (its first inventory). Lower the cap as rows close.
OPEN = {}
OPEN_REASON = {}
# Subsystems whose gate has retired while rows of theirs stay in the ledger for
# their still-gated callers. A teardown adds its subsystem here and takes it out
# of OPEN; `--check` fails while it has an open row.
RETIRED = ('core', 'event', 'field')
# Entries outside SUBSYSTEMS that an audited dispatch hands its work to, so the
# whole dispatch is accounted (name: why). The screen command's restart wait,
# field_restart_wait_for_vblank_loop, was one until the field joined the audit.
DEPENDENCIES = {}
# Why an entry's unreached bytes changed with its reach unchanged (entry: why),
# kept on its ledger row.
REMEASURED = {
    'C0034B': 'Remeasured: the library union now executes C00371-C00378 (the gathering of two members, '
              'at m8 x8 in gato-battle\'s reference play), which the previous record\'s union did not, so the '
              'unreached_two_members row that covered them left; inventoried alone, without the field, it '
              'measures the same 4 bytes.',
}


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


def retired_routines(root):
    tags = root / 'game/time_tags.c'
    text = tags.read_text(encoding='utf-8') if tags.exists() else ''
    return {int(a, 16) for a in re.findall(r'\.addr = 0x([0-9A-Fa-f]{6})', text)}


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
    live_bytes = {b for pc in live if not burndown.retired(pc)
                  for b in range(pc, pc + max(i.size for i in an.code[pc].values()))}
    # A routine retired from the gate with its subsystem (game/time_tags.c)
    # ends a walk as its entry did: its unreached bytes were evidenced before it
    # retired, and its guards are assertions now (docs/REIMPLEMENTATION.md, "The
    # core services' teardown").
    owners = {p.addr for p in ports} | retired_routines(ROOT)
    walks, unresolved = [], []
    for p in ports:
        if burndown.retired(p.addr):
            # A retained mode-entry wrapper is still an ownership boundary,
            # but its subsystem's already-evidenced bytes have retired.
            walks.append((p, set(), set()))
            continue
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
        if not in_scope(sub, p.name) or burndown.retired(p.addr):
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


def registry_row(root, name):
    """A registered entry's fields, macros expanded, without its time estimate:
    recalibrating cost_milli changes no safety argument."""
    source = dc.Source('game/registry.c', (root / 'game/registry.c').read_text(encoding='utf-8'))
    raw = dc.strip_comments(source.raw)
    m = re.search(r'NativeEntry\s+\w+\s*\[\s*\]\s*=\s*\{', raw)
    if not m:
        return None
    body = raw[m.end():dc.matching(raw, m.end() - 1, '{', '}')]
    body = re.sub(r'^[ \t]*#[ \t]*(define|undef)\b(?:\\\n|[^\n])*', '', body, flags=re.M)
    for item in dc.split_args(dc.expand_macros(body, source.defines)):
        if not item.startswith('{'):
            continue
        parts = [p.strip() for p in dc.split_args(item[1:dc.matching(item, 0, '{', '}')])]
        fields = dict(re.match(r'\.(\w+)\s*=\s*(.*)$', p, re.S).groups() for p in parts if p.startswith('.'))
        if fields.get('name', '').strip('"') == name:
            return {k: v for k, v in fields.items() if k != 'cost_milli'}
    return None


def definition(root, spec):
    """The text a safety argument relies on: a whole file (`path`) or one
    definition in it (`path:symbol`) -- a C function, macro, enum constant or
    initialized table, with every line of the file that names it (its
    callers, the table rows citing it, an X-macro list's row) -- or a
    registry row, or a RAM-map variable. Comments and layout are not part of
    it. None when the symbol is gone."""
    path, _, symbol = spec.partition(':')
    file = root / path
    if not file.is_file():
        return None
    if not symbol:
        return file.read_text(encoding='utf-8')
    if path == 'game/registry.c':
        row = registry_row(root, symbol)
        return None if row is None else json.dumps(row, sort_keys=True)
    if path.endswith('.toml'):
        import tomllib
        for var in tomllib.loads(file.read_text(encoding='utf-8')).get('var', []):
            if var.get('name') == symbol:
                return json.dumps({k: v for k, v in var.items() if k not in ('meaning', 'source')}, sort_keys=True)
        return None
    s = dc.Source(path, file.read_text(encoding='utf-8'))
    uses = '\n'.join(line for line in s.text.splitlines() if re.search(r'\b%s\b' % re.escape(symbol), line))
    if symbol in s.funcs:
        f = s.funcs[symbol]
        text = f"{symbol}({','.join(f.params)}){s.text[f.start:f.end]}"
    elif symbol in s.defines:
        text = repr(s.defines[symbol])
    elif symbol in s.consts:
        text = s.consts[symbol]
    else:
        m = re.search(r'\b%s\s*(?:\[[^\]]*\])?\s*=\s*' % re.escape(symbol), s.text)
        if m and s.text[m.end()] == '{':
            text = s.text[m.start():dc.matching(s.text, m.end(), '{', '}') + 1]
        else:
            text = ''
    text = re.sub(r'\s+', ' ', text + '\n' + uses).strip()
    return text or None


def dependency_hash(root, spec):
    if ':' not in spec:
        path = root / spec
        return file_hash(path) if path.is_file() else None
    text = definition(root, spec)
    return None if text is None else hashlib.sha256(text.encode()).hexdigest()


def safety_dependencies(root, row, cited):
    """What a reviewed argument rests on: the definitions it cites, the
    entry's registry row, and the C its row runs and guards with. An event
    command's entry is generated from its row in the commands' list."""
    row_fields = registry_row(root, row['name']) or {}
    depends = list(cited) + [f"game/registry.c:{row['name']}"]
    sources, _ = dc.load_sources(root)
    generated = False
    for field in ('fn', 'guard'):
        name = row_fields.get(field, '').strip()
        found = [f'{s.path}:{name}' for s in sources if s.path.endswith('.c') and name in s.funcs]
        generated |= field == 'fn' and not found
        depends += found
    if row['subsystem'] == 'event' and generated:
        depends += [f"game/event/event.h:{row['name']}", 'game/event/entries.c:EVENT_DEFINE_ENTRY',
                    'game/event/bridge.c:event_command_entry', 'game/event/bridge.c:event_command_accepts']
    return list(dict.fromkeys(depends))


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
            fields = {'movie': 4, 'port': 8, 'fuzz': 11, 'safe': 6, 'open': 5}
            if len(columns) != fields.get(columns[0]) or not columns[-1].strip():
                raise ValueError(f'{path.name}:{n}: malformed record or missing reason')
            records.append(columns)
    return records


def check_fuzz_evidence(rows, records, directory, root=ROOT):
    """Fresh runs of this tree's C must actually execute every byte their
    ledger rows claim."""
    by_entry = {r['entry']: r for r in rows}
    implementation = unreached_fuzz.implementation_hash(root)
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
                  and result.get('implementation') == implementation
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
        elif c[0] in ('fuzz', 'safe', 'open'):
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
            elif c[0] == 'safe':
                for dependency in c[4].split(','):
                    spec, digest = dependency.rsplit('=', 1)
                    if dependency_hash(root, spec) != digest:
                        errors.append(f'{entry}: safety argument changed in {spec}')
    if not recorded_movies:
        errors.append('no full-library movie manifest')
    for entry, (_, name, _, missing) in ports.items():
        if proofs[entry] != missing:
            errors.append(f'{name}: evidence does not cover exactly its unreached bytes')
    for c in records:
        if c[0] == 'fuzz' and c[1] in ports and c[4] in recipes:
            if not unreached_fuzz.covers(recipes[c[4]], ports[c[1]][1]):
                errors.append(f'{c[1]}: {c[4]} is not a reviewed caller of this C implementation')
    open_bytes = collections.Counter()
    for c in records:
        if c[0] == 'open' and c[1] in ports:
            sub = ports[c[1]][0]
            open_bytes[sub] += int(c[3])
            if sub in RETIRED:
                errors.append(f'{c[1]}: open bytes in {sub}, which has retired: its teardown needs none')
            elif sub not in OPEN:
                errors.append(f'{c[1]}: open bytes in {sub}, which is not inventoried ahead of its teardown')
    for sub, n in sorted(open_bytes.items()):
        if sub in OPEN and n > OPEN[sub]:
            errors.append(f'{sub}: {n} open bytes, more than its cap of {OPEN[sub]}: open evidence may not grow')
    for sub in sorted(set(OPEN) & set(RETIRED)):
        errors.append(f'{sub}: retired, and still allowed open bytes: take it out of OPEN')
    for entry in proofs.keys() - ports.keys():
        errors.append(f'{entry}: evidence without an inventoried port')
    for r in rows:
        old = ports.get(r['entry'])
        if old is None or old[:3] != (r['subsystem'], r['name'], r['digest']):
            errors.append(f"{r['name']}: new entry or changed reach; remeasure and check its uncovered paths")
        elif measured and not expand(r['runs']) <= old[3]:
            errors.append(f"{r['name']}: newly unreached bytes without evidence")
    return errors


def failing_cases(root=ROOT):
    """The recipes known to mismatch, each with the TODO.md item that says why
    (`[failing]` in tools/unreached_cases.toml): a known failure is reported
    and contributes nothing; any other is an error."""
    import tomllib
    return tomllib.loads((root / 'tools/unreached_cases.toml').read_text()).get('failing', {})


def evidence_errors(results, failing, cited):
    """`results` maps each fresh case run to whether it was clean. A recipe that
    mismatches (or reports a diagnostic) is an error, not a reason for an open
    row, unless it is a known failure; a known failure that now passes leaves
    the list; and every recipe the previous ledger cited must have a fresh,
    clean result, or its evidence would quietly be lost."""
    errors = []
    for case, ok in sorted(results.items()):
        if not ok and case not in failing:
            errors.append(f'{case}: fuzz recipe fails; fix it or list it under [failing] with its reason')
        elif ok and case in failing:
            errors.append(f'{case}: passes now; take it off [failing]')
    for case in sorted(cited):
        if not results.get(case):
            errors.append(f'{case}: cited by the previous ledger, and no fresh clean result for it')
    return errors


def previous_ledger(target):
    """The open bytes per entry and the fuzz recipes cited in the ledger being replaced."""
    opened, cited = collections.defaultdict(set), set()
    if target.exists():
        for c in read_ledger(target):
            if c[0] == 'open':
                opened[c[1]] |= expand(parse_spans(c[2]))
            elif c[0] == 'fuzz':
                cited.add(c[4])
    return opened, cited


def open_errors(row, missing, opened, admit_open):
    """Open bytes are capped by the previous ledger's, entry by entry: lost
    evidence can't become open. Only a subsystem's first inventory admits new
    ones (`--admit-open`)."""
    if row['subsystem'] not in OPEN:
        return [f"{row['name']}: unchecked {spans_text(runs(missing))}"]
    added = missing - opened.get(row['entry'], set())
    if added and row['subsystem'] not in admit_open:
        return [f"{row['name']}: {len(added)} bytes would become open ({spans_text(runs(added))}): "
                'evidence lost or never there; open bytes come only with a first inventory (--admit-open)']
    return []


def record(rows, movies, evidence_dir, safety, target, admit_open=()):
    """Write only when every missing byte has clean comparisons or a reviewed
    reason, or, for a subsystem inventoried ahead of its teardown, was open
    before."""
    import tomllib
    states, recipes = unreached_fuzz.recipes()
    opened, cited = previous_ledger(target)
    evidence, results = [], {}
    for path in sorted(evidence_dir.glob('*.json')):
        result = json.loads(path.read_text())
        case = unreached_fuzz.label(result['recipe'])
        if case not in recipes or result['recipe'] != recipes[case]:
            continue
        # Only results produced by the reproducible runner, for this recipe.
        if (result.get('fingerprint') != unreached_fuzz.fingerprint(recipes[case], states)
                or result.get('implementation') != unreached_fuzz.implementation_hash(ROOT)):
            continue
        results[case] = unreached_fuzz.clean(result, path.with_suffix('.log').read_text())
        if results[case]:
            evidence.append((case, result, coverage.load([path.with_suffix('.cov')])))
    errors = evidence_errors(results, failing_cases(), cited)
    if errors:
        raise ValueError('\n'.join(errors))
    safe = tomllib.loads(safety.read_text())['safe']
    records = [['movie', name, digest, 'Full standard library coverage union; additional movies can only add coverage.']
               for name, digest in sorted(movies.items())]
    for row in rows:
        missing = expand(row['runs'])
        reason = 'Conservative live reach; own regions before inlined helpers.'
        if row['name'] in DEPENDENCIES:
            reason += ' ' + DEPENDENCIES[row['name']] + '.'
        if row['entry'] in REMEASURED:
            reason += ' ' + REMEASURED[row['entry']]
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
            # `entry` tells apart rows registered under one name at two addresses.
            if proof['name'] != row['name'] or proof.get('entry', row['entry']) != row['entry']:
                continue
            checked = expand(parse_spans(proof['runs']))
            if not checked <= missing:
                raise ValueError(f"{row['name']}: safety ranges overlap fuzz evidence or are not unreached")
            dependencies = []
            for spec in safety_dependencies(ROOT, row, proof['depends']):
                digest = dependency_hash(ROOT, spec)
                if digest is None:
                    raise ValueError(f"{row['name']}: safety argument cites {spec}, which is not there")
                dependencies.append(f'{spec}={digest}')
            dependencies = ','.join(dependencies)
            records.append(['safe', row['entry'], spans_text(runs(checked)), str(len(checked)), dependencies, proof['why']])
            missing -= checked
        if missing:
            errors += open_errors(row, missing, opened, admit_open)
            records.append(['open', row['entry'], spans_text(runs(missing)), str(len(missing)),
                            OPEN_REASON.get(row['subsystem'], '')])
    errors += check_records(rows, records, movies, recipes, states, measured=True)
    if errors:
        raise ValueError('\n'.join(errors))
    header = ('# Full-library unreached port ledger. Addresses/counts only; no ROM or trace contents.\n'
              '# movie\tname\tsha256\treason\n'
              '# port\tsubsystem\tentry\tname\treach-sha256\tbytes\truns\treason\n'
              '# fuzz\tentry\truns\tbytes\trecipe\tseed\ttrials\tmatched\trecipe-sha256\tmismatches\treason\n'
              '# safe\tentry\truns\tbytes\tpath[:symbol]=sha256,...\treason\n'
              '# open\tentry\truns\tbytes\treason (not yet evidenced: the subsystem cannot retire)\n')
    target.write_text(header + '\n'.join('\t'.join(c) for c in records) + '\n', newline='\n')


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


def open_summary(records):
    """Bytes and entries still open per subsystem: what its teardown waits on."""
    subsystem = {c[2]: c[1] for c in records if c[0] == 'port'}
    totals = collections.defaultdict(lambda: [0, 0])
    for c in records:
        if c[0] == 'open':
            totals[subsystem.get(c[1], '?')][0] += int(c[3])
            totals[subsystem.get(c[1], '?')][1] += 1
    return ', '.join(f'{sub} {b} bytes in {n} entries' for sub, (b, n) in sorted(totals.items()))


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
    parser.add_argument('--admit-open', action='append', default=[], metavar='SUBSYSTEM',
                        help="with --record, a subsystem's first inventory: its unchecked bytes may become open")
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
        record(rows, movies, args.fuzz_dir, args.safety, args.ledger, args.admit_open)
    if args.check:
        states, recipes = unreached_fuzz.recipes()
        records = read_ledger(args.ledger)
        errors = check_records(rows, records, movies, recipes, states,
                               measured=bool(args.coverage_dir))
        if args.recheck_fuzz:
            errors += check_fuzz_evidence(rows, records, args.fuzz_dir)
        for error in errors:
            print(error)
        names = {c[2]: c[3] for c in records if c[0] == 'port'}
        for c in records:
            if c[0] == 'open':
                print(f'open: {names.get(c[1], "?")} {c[1]} {c[3]} bytes {c[2]}')
        still = open_summary(records)
        print('unreached FAIL' if errors else f'unreached OK: {len(rows)} entries, {len(movies)} library movies'
              + (f'; open, cannot retire: {still}' if still else ''))
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
