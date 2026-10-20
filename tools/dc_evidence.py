"""Re-run the library evidence the tools/v_readers.py dc-evidence notes cite.

    python3 tools/dc_evidence.py --ct-dev build/main/ct_dev [--out DIR] [-j N] [--movies NAME...]

A call-out that declares the overflow flag don't-care on tools/v_readers.py's
evidence (docs/PORTING.md, "Declarations liveness can't prove") cites two runs
of the movie library, and the counts in its note are only as good as the last
time someone made them. This makes them again:

- every movie in the shipping mode (--native on --generated on) twice, once with
  `ct_dev --poison-site` at every site the notes name and `--poison-set V`,
  whose traces must be the same (the picture's hash included) outside the stack
  page, $7E:0600-06FF: a register the callee pushes and pulls again stays there
  in memory, which is all a scrambled flag nothing reads leaves behind;
- every movie in the shipping mode once more with `ct_dev --poison-return` at
  every entry a `ct_dev --poison-return` note in game/registry.c names and
  `--poison-set C`, whose trace must be the same as the first (an entry's
  carry after it returns, which liveness has read where the run shows no
  reader acts on it);
- every movie on the interpreter alone (--native off --generated off) with
  `ct_dev --index-check` at every dispatch an `index_reach` hint bounds
  (re/hints.toml), whose X must never be past $FE.

It prints each note's sites with the count it cites and the count the run made,
and exits 1 where a trace differs, an index is past $FE, a bounded dispatch
never ran (unexercised: the run shows nothing for it), or a site had V inverted
fewer than 100 times, unless the note rests such sites on the static result
and another of its sites made 100 (update a note's counts to the ones printed).
"""

import argparse
import concurrent.futures as cf
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.dontcare_check import MIN_SCRAMBLED, RETURN_EVIDENCE, V_EVIDENCE, v_evidence_counts  # noqa: E402
from tools.suite import collect  # noqa: E402

STACK_PAGE = "$7E:0600-06FF"
POISON = re.compile(r"^poison-site: ([0-9A-F]{6}) calls (\d+), V inverted (\d+)$", re.M)
RETURNS = re.compile(r"^poison-return: ([0-9A-F]{6}) returns (\d+) \((\d+) to a bridge\), C inverted (\d+)$", re.M)
INDEX = re.compile(r"^index-check: ([0-9A-F]{6}) runs (\d+), largest X \$([0-9A-F]{4}), past \$FE (\d+)$", re.M)


def notes(root: pathlib.Path) -> list:
    """(file, target, site -> cited count, whether the note rests a site under
    MIN_SCRAMBLED on the static result) of every tools/v_readers.py note in game/."""
    out = []
    for path in sorted((root / "game").rglob("*.[ch]")):
        text = path.read_text(encoding="utf-8")
        flat = " ".join(re.sub(r"/\*|\*/|//|^\s*\*|\s\*\s", " ", text, flags=re.M).split())
        for m in V_EVIDENCE.finditer(flat):
            out.append((path.relative_to(root).as_posix(), m.group("target"), v_evidence_counts(m),
                        bool(m.group("static"))))
    return out


def return_notes(root: pathlib.Path) -> list:
    """(entry, cited returns with C inverted, cited returns to a bridge) of
    every ct_dev --poison-return note in game/registry.c."""
    text = (root / "game/registry.c").read_text(encoding="utf-8")
    flat = " ".join(re.sub(r"/\*|\*/|//|^\s*\*|\s\*\s", " ", text, flags=re.M).split())
    return [(int(m.group("entry"), 16), int(m.group("inverted")), int(m.group("bridged") or 0))
            for m in RETURN_EVIDENCE.finditer(flat)]


def index_sites(root: pathlib.Path) -> list:
    from tools.disasm import hints as hints_mod
    return sorted(hints_mod.load(root / "re/hints.toml").index_reach)


def same_trace(a, b) -> tuple:
    diff = subprocess.run([sys.executable, "-m", "tools.trace", "diff", "--framebuffer", "--ignore", STACK_PAGE,
                           str(a), str(b)], cwd=ROOT, capture_output=True, text=True)
    tail = (diff.stdout or diff.stderr).strip().splitlines()
    return diff.returncode == 0 and bool(tail) and "no divergence" in tail[-1], tail[-1] if tail else ""


def run_movie(binary, rom, out, movie, sites, dispatches, entries):
    name, path, frames = movie
    base = [str(rom), "--movie", str(path), "--frames", str(frames)]
    traces = {tag: out / f"{name}-{tag}.cttr" for tag in ("plain", "poisoned", "returns")}
    runs = {
        "plain": ["--native", "on", "--generated", "on", "--trace", str(traces["plain"])],
        "poisoned": ["--native", "on", "--generated", "on", "--trace", str(traces["poisoned"]), "--poison-set", "V"]
                    + sum((["--poison-site", f"{s:06X}"] for s in sites), []),
        "index": ["--native", "off", "--generated", "off"] + sum((["--index-check", f"{d:06X}"] for d in dispatches), []),
    }
    if entries:
        runs["returns"] = (["--native", "on", "--generated", "on", "--trace", str(traces["returns"]), "--poison-set", "C"]
                           + sum((["--poison-return", f"{e:06X}"] for e in entries), []))
    procs = {tag: subprocess.Popen([str(binary), "--rom"] + base + extra, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT) for tag, extra in runs.items()}
    logs = {tag: p.communicate()[0].decode(errors="replace").replace(chr(13), "") for tag, p in procs.items()}
    failed = [tag for tag, p in procs.items() if p.returncode != 0]
    same, tail = same_trace(traces["plain"], traces["poisoned"])
    if entries:
        returns_same, returns_tail = same_trace(traces["plain"], traces["returns"])
        if not returns_same:
            same, tail = False, "returns: " + returns_tail
    for t in traces.values():
        t.unlink(missing_ok=True)
    poisoned = {int(s, 16): int(v) for s, _, v in POISON.findall(logs["poisoned"])}
    poisoned_returns = {int(e, 16): (int(v), int(b)) for e, _, b, v in RETURNS.findall(logs.get("returns", ""))}
    index = {int(d, 16): (int(r), int(x, 16), int(o)) for d, r, x, o in INDEX.findall(logs["index"])}
    return name, failed, same, tail, poisoned, index, poisoned_returns


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python3 tools/dc_evidence.py", description=__doc__.split("\n")[0])
    ap.add_argument("--ct-dev", required=True, type=pathlib.Path)
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms" / "ct-usa.sfc")
    ap.add_argument("--out", type=pathlib.Path, help="where the traces go while compared (default: a temporary directory)")
    ap.add_argument("-j", "--jobs", type=int, default=1, help="movies at once (three runs each)")
    ap.add_argument("--movies", nargs="*", help="only these (the counts then won't match the notes')")
    args = ap.parse_args(argv)

    found = notes(ROOT)
    returned = return_notes(ROOT)
    if not found and not returned:
        print("no tools/v_readers.py or ct_dev --poison-return dc-evidence notes in game/")
        return 0
    entries = sorted({e for e, _, _ in returned})
    inverted_returns = {e: 0 for e in entries}
    bridged_returns = {e: 0 for e in entries}
    sites = sorted({s for _, _, counts, _ in found for s in counts})
    dispatches = index_sites(ROOT)
    out = args.out or pathlib.Path(tempfile.mkdtemp(prefix="dc_evidence-"))
    out.mkdir(parents=True, exist_ok=True)
    binary = out / ("ct_dev" + args.ct_dev.suffix)
    shutil.copy2(args.ct_dev, binary)
    movies = collect("all", args.movies)
    inverted = {s: 0 for s in sites}
    index = {d: [0, 0, 0] for d in dispatches}
    bad = 0
    with cf.ThreadPoolExecutor(max(1, args.jobs)) as ex:
        for name, failed, same, tail, poisoned, idx, poisoned_returns in ex.map(
                lambda m: run_movie(binary, args.rom, out, m, sites, dispatches, entries), movies):
            for e, (v, b) in poisoned_returns.items():
                inverted_returns[e] = inverted_returns.get(e, 0) + v
                bridged_returns[e] = bridged_returns.get(e, 0) + b
            for s, v in poisoned.items():
                inverted[s] = inverted.get(s, 0) + v
            for d, (runs, largest, over) in idx.items():
                index[d][0] += runs
                index[d][1] = max(index[d][1], largest)
                index[d][2] += over
            if failed or not same:
                bad += 1
            print(f"{name}: {'same' if same else 'DIFFERS'}{' (a run failed: ' + ', '.join(failed) + ')' if failed else ''}"
                  f": {tail}", flush=True)
    for d, (runs, largest, over) in sorted(index.items()):
        unexercised = runs == 0
        print(f"index ${d >> 16:02X}:{d & 0xFFFF:04X}: {runs} runs, largest X ${largest:04X}, past $FE {over}"
              + ("  <-- unexercised" if unexercised else "") + ("  <-- past $FE" if over else ""))
        bad += over != 0 or (unexercised and not args.movies)
    for path, target, counts, static in found:
        made = {s: inverted.get(s, 0) for s in counts}
        short = [s for s, n in made.items() if n < MIN_SCRAMBLED]
        ok = not short or (static and any(n >= MIN_SCRAMBLED for n in made.values()))
        bad += not ok and not args.movies
        print(f"{path}: {target}: " + ", ".join(f"{s:06X} cites {counts[s]}, made {made[s]}" for s in counts)
              + ("" if ok else f"  <-- under {MIN_SCRAMBLED} at " + ", ".join(f"{s:06X}" for s in short)
                 + ("" if static else " (and the note doesn't rest them on the static result)")))
    for e, cited, cited_bridged in returned:
        made = inverted_returns.get(e, 0)
        bridged = bridged_returns.get(e, 0)
        ok = made >= MIN_SCRAMBLED
        bad += not ok and not args.movies
        print(f"game/registry.c: {e:06X} returns with C inverted: cites {cited} ({cited_bridged} to a bridge), "
              f"made {made} ({bridged} to a bridge)" + ("" if ok else f"  <-- under {MIN_SCRAMBLED}"))
    if not args.out:
        shutil.rmtree(out, ignore_errors=True)
    print("dc_evidence: " + ("OK" if not bad else f"{bad} problem(s)"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
