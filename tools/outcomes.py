"""Recorded outcomes, independent of a live reference play.

The suite writes outcome-<movie>.txt beside its shadow logs and <movie>.txt
in outcomes/strict and outcomes/native, reusing those runs. --record merges
reference artifacts into the ledger; --check reads hand-port artifacts only.
No executable or ROM is needed to check. --update only adds protected matches.
See docs/REIMPLEMENTATION.md, "Recorded outcomes" for the recording workflow.
"""
import argparse
import concurrent.futures
import hashlib
import pathlib
import re
import sys
import tempfile

if __package__:
    from . import places
else:
    import places

ROOT = pathlib.Path(__file__).resolve().parent.parent
LEDGER = ROOT / "tools/outcomes.txt"
BASELINE = ROOT / "tools/outcomes_baseline.txt"
EXCEPTIONS = ROOT / "tools/outcomes_exceptions.txt"
EVERY, NEAR = 300, 2
PICTURE = re.compile(r"^outcome-picture: (\d+) ([0-9a-f]{16})$", re.M)
FAULT = re.compile(r"halted:|accessor out of range|page refused:|asset outside the catalogue|"
                   r"asset differs from the cartridge|the program's C goes no further|no generated code for")
KINDS = {"input", "game", "sram", "pictures", "places"}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def checkpoints(frames):
    return sorted({1, frames, *range(EVERY, frames + 1, EVERY)})


def picture_frames(frames):
    return {f + d for f in checkpoints(frames) for d in range(-NEAR, NEAR + 1) if 1 <= f + d <= frames}


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
    rows = {(name, "input"): ["1", str(frames), sha(movie.read_bytes()), sha(selection.encode())],
            (name, "run"): [mode, str(rc if rc is not None else -1), str(int(bool(FAULT.search(text))))],
            (name, "pictures"): [f + ":" + h for f, h in PICTURE.findall(text)],
            (name, "places"): ["%d:%d" % p for p in places.parse(text)]}
    for kind, offsets, base in (("game", saved.wram, 0), ("sram", saved.sram, 0x20000)):
        rows[name, kind] = [sha(bytes(state[base + i] for i in offsets))] if len(state) == 0x22000 else []
    write(directory / ("outcome-%s.txt" % name), rows, "# Outcome artifact v1; digests and numbers only.")


def pairs(tokens, digest=False):
    result = []
    for token in tokens:
        a, b = token.split(":")
        if digest and not re.fullmatch(r"[0-9a-f]{16}", b):
            raise ValueError("bad picture digest")
        result.append((int(a), b if digest else int(b)))
    if any(a < 1 for a, _ in result) or any(a >= b for (a, _), (b, _) in zip(result, result[1:])):
        raise ValueError("frames must be positive and strictly increasing")
    return result


def validate(rows, name, movie, frames, mode=None):
    expected = KINDS | ({"run"} if mode else set())
    if {k for n, k in rows if n == name} != expected:
        raise ValueError("%s: missing or unknown kinds" % name)
    meta = rows[name, "input"]
    if len(meta) != 4 or meta[:3] != ["1", str(frames), sha(movie.read_bytes())]:
        raise ValueError("%s: stale movie or unsupported outcome version" % name)
    if not re.fullmatch(r"[0-9a-f]{64}", meta[3]):
        raise ValueError("bad saved-game selection digest")
    if mode and rows[name, "run"] != [mode, "0", "0"]:
        raise ValueError("%s: incomplete or faulted %s run" % (name, mode))
    for kind in ("game", "sram"):
        if len(rows[name, kind]) != 1 or not re.fullmatch(r"[0-9a-f]{64}", rows[name, kind][0]):
            raise ValueError("%s: missing %s digest" % (name, kind))
    pictures = pairs(rows[name, "pictures"], True)
    if {f for f, _ in pictures} != picture_frames(frames):
        raise ValueError("%s: incomplete picture checkpoints" % name)
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
    ref_px, px = dict(pairs(reference[name, "pictures"], True)), dict(pairs(run[name, "pictures"], True))
    result["pictures"] = {}
    for f in checkpoints(frames):
        h = ref_px[f]
        result["pictures"][str(f)] = ("match" if px.get(f) == h else "drift" if any(
            px.get(f + d) == h for d in range(-NEAR, NEAR + 1)) else "differ")
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
        if len(fields) != 3 or fields[1] not in ("strict", "native") or fields[2] not in KINDS - {"input"} or not reason.strip():
            raise ValueError("%s:%d: expected movie mode kind # reason" % (path.name, number))
        key = tuple(fields)
        if key in out:
            raise ValueError("duplicate outcome exception")
        out[key] = reason.strip()
    return out


def ratchet(result, name, mode, baseline, exc, update=False):
    failures = []
    for kind, items in result.items():
        key = (name + "/" + mode, kind)
        good = {i for i, how in items.items() if how != "differ"}
        must = set(baseline.get(key, []))
        if not must <= good:
            failures.append(kind + " regressed: " + ",".join(sorted(must - good)))
        if key not in baseline and not update:
            failures.append(kind + " has no baseline")
        if any(how == "differ" for how in items.values()) and (name, mode, kind) not in exc:
            failures.append(kind + " differs without a reason")
        if update:
            baseline[key] = sorted(must | good, key=lambda x: int(x) if x.isdecimal() else -1)
    return failures


def play(args, library, directory):
    """Run only the hand ports. In particular, never call reference_play."""
    from strict_check import SavedGame, STRICT, run_ct_dev
    from native_check import run_native, save_for, saves_by_sum
    saved, saves = SavedGame(args.rom), saves_by_sum()
    with tempfile.TemporaryDirectory(prefix="outcomes-") as scratch:
        work = pathlib.Path(scratch)
        extra = ["--assets", str(args.assets)] if args.assets else []
        if args.mode == "native" and not extra:
            pack = work / "ct.assets"
            extra = ["--assets", str(pack)]
            _text, rc = run_native(args.ct_native.resolve(), args.rom, library[0][1], 1, None, extra)
            if rc != 0 or not pack.exists():
                raise ValueError("cannot import native assets")

        def one(row):
            name, movie, frames = row
            end = work / (name + ".bin")
            options = extra + ["--locations", "--outcomes", "--end-state", str(end)]
            if args.mode == "strict":
                text, rc = run_ct_dev(args.ct_dev.resolve(), args.rom, movie, frames, STRICT + options)
            else:
                sram = save_for(movie, saves)
                if sram is False:
                    raise ValueError(name + ": missing cartridge save")
                text, rc = run_native(args.ct_native.resolve(), args.rom, movie, frames, sram, options)
            capture(directory, name, movie, frames, text, rc, end, saved, args.mode)

        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 2))) as pool:
            list(pool.map(one, library))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--record", action="store_true")
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--update", action="store_true", help="add matches to the ratchet; never remove them")
    ap.add_argument("--reference", type=pathlib.Path, help="suite shadow output, for --record only")
    ap.add_argument("--runs", type=pathlib.Path, help="strict/native outcome artifacts")
    ap.add_argument("--mode", choices=("strict", "native"))
    binary = ap.add_mutually_exclusive_group()
    binary.add_argument("--ct-dev", type=pathlib.Path, help="make a fresh strict run, no reference play")
    binary.add_argument("--ct-native", type=pathlib.Path, help="make a fresh native run, no reference play")
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--assets", type=pathlib.Path)
    ap.add_argument("-j", "--jobs", type=int, default=2)
    ap.add_argument("--movies", nargs="*", default=[])
    ap.add_argument("--ledger", type=pathlib.Path, default=LEDGER)
    ap.add_argument("--baseline", type=pathlib.Path, default=BASELINE)
    ap.add_argument("--exceptions", type=pathlib.Path, default=EXCEPTIONS)
    args = ap.parse_args(argv)
    if args.ct_dev or args.ct_native:
        chosen_mode = "strict" if args.ct_dev else "native"
        if args.record or args.mode and args.mode != chosen_mode:
            ap.error("binary must agree with --mode, and cannot record a reference")
        args.mode = chosen_mode
    from strict_check import movies
    library = movies(set(args.movies))
    if not library or set(args.movies) - {n for n, _, _ in library}:
        ap.error("no movies or unknown movie names")
    if args.record and not args.reference or not args.record and (not args.runs or not args.mode):
        ap.error("--record needs --reference; --check/--update need --runs and --mode")
    try:
        ledger = read(args.ledger) if args.ledger.exists() else {}
        base = read(args.baseline) if args.baseline.exists() else {}
        exc = exceptions(args.exceptions) if not args.record else {}
        if args.ct_dev or args.ct_native:
            play(args, library, args.runs)
        failed, clean, counts = [], 0, dict(match=0, drift=0, differ=0)
        for name, movie, frames in library:
            directory = args.reference if args.record else args.runs
            run = read(directory / ("outcome-%s.txt" % name))
            validate(run, name, movie, frames, "reference" if args.record else args.mode)
            if args.record:
                ledger.update({key: value for key, value in run.items() if key[0] == name and key[1] != "run"})
                continue
            validate(ledger, name, movie, frames)
            result = compare(ledger, run, name, frames)
            problems = ratchet(result, name, args.mode, base, exc, args.update)
            failed.extend(name + ": " + p for p in problems)
            clean += not any(h == "differ" for items in result.values() for h in items.values())
            parts = []
            for kind, items in result.items():
                numbers = {how: sum(h == how for h in items.values()) for how in counts}
                for how, n in numbers.items():
                    counts[how] += n
                detail = "; ".join("%s %s" % (how, ",".join(i for i, h in items.items() if h == how))
                                   for how in ("drift", "differ") if numbers[how])
                parts.append("%s %d match%s" % (kind, numbers["match"], ("; " + detail) if detail else ""))
            print("%s %s: %s%s" % (name, args.mode, " | ".join(parts), " <-- FAILED" if problems else ""))
        if args.record:
            write(args.ledger, ledger, "# Recorded reference outcomes v1. RGB FNV-1a64; state/input SHA-256.\n"
                  "# Frames are completed frames; pictures every 300, first/last, neighbours +/-2.\n"
                  "# input: version frames movie-sha256 saved-game-selection-sha256.\n"
                  "# game/sram exclude strict_check.TIMED_CELLS and slot checksums; places frame:place.")
            print("recorded %d movies" % len(library))
            return 0
        if args.update and not failed:
            write(args.baseline, base, "# Protected outcome matches: movie/mode kind identities (end, picture frame, place index).\n"
                  "# --update adds matches, never drops one; temporal drift counts as a match.")
        for problem in failed:
            print("FAILED: " + problem)
        print("%s outcomes: %d/%d ratchet pass; %d without differences; %d match, %d drift, %d differ%s" % (
            args.mode, len(library) - len({p.split(':')[0] for p in failed}), len(library), clean,
            counts["match"], counts["drift"], counts["differ"], "; FAILED" if failed else ""))
        return int(bool(failed))
    except (OSError, ValueError, KeyError) as error:
        print("outcomes: FAILED: %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
