"""Recorded native save and place outcomes; checks use no live reference program."""
import argparse
import hashlib
import pathlib
import re
import sys

if __package__:
    from . import places
else:
    import places

ROOT = pathlib.Path(__file__).resolve().parent.parent
LEDGER = ROOT / "tools/outcomes.txt"
BASELINE = ROOT / "tools/outcomes_baseline.txt"
EXCEPTIONS = ROOT / "tools/outcomes_exceptions.txt"
FAULT = re.compile(r"accessor out of range|page refused:|contract broken:|asset outside the catalogue|"
                   r"asset differs from the cartridge|the program's C goes no further|strays: [1-9]")
KINDS = {"input", "game", "sram", "places"}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def movie_sha(movie):
    """A movie is text, so a checkout's line endings don't make it another movie."""
    return sha(movie.read_bytes().replace(b"\r\n", b"\n"))


def observed_digest(values):
    """Pin the entire observed kind, including every visit."""
    return sha(" ".join(values).encode("ascii"))


def require_movies(names, root=None):
    """Validate membership and headers before any caller can filter movies out."""
    root = ROOT if root is None else root
    paths = {}
    for path in (root / "movies").rglob("*.ctm"):
        if path.stem in names:
            if path.stem in paths:
                raise ValueError(path.stem + ": duplicate movie name")
            paths[path.stem] = path
    missing = set(names) - paths.keys()
    if missing:
        raise ValueError("missing recorded or selected movies: " + ", ".join(sorted(missing)))
    rows = {}
    for name, path in sorted(paths.items()):
        headers = [line for line in path.read_text().splitlines() if line.startswith("# frames")]
        if len(headers) != 1 or not re.fullmatch(r"# frames [1-9][0-9]*[ \t]*", headers[0]):
            raise ValueError(name + ": malformed '# frames N' header")
        rows[name] = (name, path, int(headers[0].split()[2]))
    return rows


def movie_library(names, recorded):
    directories = [ROOT / "movies", ROOT / "movies/saves"]
    if names:
        directories.append(ROOT / "movies/hand")
    selected = set(names) or {p.stem for directory in directories for p in directory.glob("*.ctm")}
    rows = require_movies(selected | set(recorded))
    return [rows[name] for name in sorted(selected)]


def read(path):
    """Strict, greppable `movie kind values...` rows; no silent malformed rows."""
    rows = {}
    for number, line in enumerate(path.read_text().splitlines(), 1):
        fields = line.partition("#")[0].split()
        if not fields:
            continue
        if len(fields) < 2 or tuple(fields[:2]) in rows:
            raise ValueError("%s:%d: malformed or duplicate row" % (path.name, number))
        rows[tuple(fields[:2])] = fields[2:]
    return rows


def write(path, rows, header):
    path.parent.mkdir(parents=True, exist_ok=True)
    text = header.rstrip() + "\n" + "".join(" ".join((*key, *rows[key])) + "\n" for key in sorted(rows))
    # Commit a complete file only after every requested movie has validated.
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(text, newline="\n")
    temporary.replace(path)


def capture(directory, name, movie, frames, text, rc, end, saved, mode):
    """Digest a run before its scratch end state disappears. Never store bytes."""
    state = end.read_bytes() if end.exists() else b""
    selection = ",".join(map(str, saved.wram)) + ";" + ",".join(map(str, saved.sram))
    rows = {(name, "input"): ["1", str(frames), movie_sha(movie), sha(selection.encode())],
            (name, "run"): [mode, str(rc if rc is not None else -1), str(int(bool(FAULT.search(text))))],
            (name, "places"): ["%d:%d" % p for p in places.parse(text)]}
    for kind, offsets, base in (("game", saved.wram, 0), ("sram", saved.sram, 0x20000)):
        rows[name, kind] = [sha(bytes(state[base + i] for i in offsets))] if len(state) == 0x22000 else []
    write(directory / ("outcome-%s.txt" % name), rows, "# Outcome artifact v1; digests and numbers only.")


def pairs(tokens):
    result = []
    for token in tokens:
        a, b = token.split(":")
        result.append((int(a), int(b)))
    if any(a < 1 for a, _ in result) or any(a >= b for (a, _), (b, _) in zip(result, result[1:])):
        raise ValueError("frames must be positive and strictly increasing")
    return result


def validate(rows, name, movie, frames, mode=None):
    expected = KINDS | ({"run"} if mode else set())
    if {k for n, k in rows if n == name} != expected:
        raise ValueError("%s: missing or unknown kinds" % name)
    meta = rows[name, "input"]
    if len(meta) != 4 or meta[:3] != ["1", str(frames), movie_sha(movie)]:
        raise ValueError("%s: stale movie or unsupported outcome version" % name)
    if not re.fullmatch(r"[0-9a-f]{64}", meta[3]):
        raise ValueError("bad saved-game selection digest")
    if mode and rows[name, "run"] != [mode, "0", "0"]:
        raise ValueError("%s: incomplete or faulted %s run" % (name, mode))
    for kind in ("game", "sram"):
        if len(rows[name, kind]) != 1 or not re.fullmatch(r"[0-9a-f]{64}", rows[name, kind][0]):
            raise ValueError("%s: missing %s digest" % (name, kind))
    entered = pairs(rows[name, "places"])
    if not entered or entered[-1][0] > frames or any(p < 0 or p > 511 for _, p in entered):
        raise ValueError("%s: missing or invalid places" % name)


def compare(reference, run, name, frames):
    """Per observable: match, drift or differ. Drift is accepted, not failure."""
    if reference[name, "input"] != run[name, "input"]:
        raise ValueError("%s: saved-game projection changed; record deliberately" % name)
    result = {}
    for kind in ("game", "sram"):
        result[kind] = {"end": "match" if reference[name, kind] == run[name, kind] else "differ"}
    ref_places, entered = pairs(reference[name, "places"]), pairs(run[name, "places"])
    result["places"] = {}
    same_order = True
    for i in range(max(len(ref_places), len(entered))):
        same_order &= i < len(ref_places) and i < len(entered) and ref_places[i][1] == entered[i][1]
        off = entered[i][0] - ref_places[i][0] if same_order else None
        result["places"][str(i)] = ("match" if off == 0 else "drift" if off is not None and
                                    abs(off) <= places.ON_TIME else "differ")
    return result


def exceptions(path):
    out = {}
    for number, line in enumerate(path.read_text().splitlines(), 1):
        body, _, reason = line.partition("#")
        fields = body.split()
        if not fields:
            continue
        if (len(fields) != 4 or fields[1] != "native" or
                fields[2] not in KINDS - {"input"} or not re.fullmatch(r"[0-9a-f]{64}", fields[3]) or
                not reason.strip()):
            raise ValueError("%s:%d: expected movie mode kind observed-sha256 # reason" % (path.name, number))
        key = tuple(fields[:3])
        if key in out:
            raise ValueError("duplicate outcome exception")
        out[key] = (fields[3], reason.strip())
    return out


def ratchet(result, run, name, mode, baseline, exc, update=False):
    failures = []
    for kind, items in result.items():
        key = (name + "/" + mode, kind)
        good = {i for i, how in items.items() if how != "differ"}
        must = set(baseline.get(key, []))
        if not must <= good:
            failures.append(kind + " regressed: " + ",".join(sorted(must - good)))
        if key not in baseline and not update:
            failures.append(kind + " has no baseline")
        if any(how == "differ" for how in items.values()):
            exception = exc.get((name, mode, kind))
            if not exception:
                failures.append(kind + " differs without a reason")
            elif observed_digest(run[name, kind]) != exception[0]:
                failures.append(kind + " changed beyond its recorded exception; observed " +
                                observed_digest(run[name, kind]) + "; re-record with a reason")
        if update:
            baseline[key] = sorted(must | good, key=lambda x: int(x) if x.isdecimal() else -1)
    return failures


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--runs', type=pathlib.Path, required=True)
    ap.add_argument('--movies', nargs='*', default=[])
    ap.add_argument('--ledger', type=pathlib.Path, default=LEDGER)
    ap.add_argument('--baseline', type=pathlib.Path, default=BASELINE)
    ap.add_argument('--exceptions', type=pathlib.Path, default=EXCEPTIONS)
    args = ap.parse_args(argv)
    try:
        ledger, base, exc = read(args.ledger), read(args.baseline), exceptions(args.exceptions)
        library = movie_library(args.movies, {name for name, _ in ledger})
        failed = []
        for name, movie, frames in library:
            run = read(args.runs / ('outcome-%s.txt' % name))
            validate(run, name, movie, frames, 'native')
            validate(ledger, name, movie, frames)
            problems = ratchet(compare(ledger, run, name, frames), run, name, 'native', base, exc)
            failed.extend(name + ': ' + p for p in problems)
        for problem in failed:
            print('FAILED: ' + problem)
        print('native outcomes: %d/%d ratchet pass' % (len(library) - len({p.split(':')[0] for p in failed}), len(library)))
        return int(bool(failed))
    except (OSError, ValueError, KeyError) as error:
        print('outcomes: FAILED: %s' % error, file=sys.stderr)
        return 1

if __name__ == '__main__':
    sys.exit(main())
