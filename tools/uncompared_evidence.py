"""Compare interrupted routines independently of the combined shadow runs.

The combined shadow run still measures every port. These additional runs
remove only the ancestors which claim an earlier comparison point, so the
continuation can reach its own point. No counts from these runs are
mixed into timing, fallback or combined-run comparison totals.
The effect graphics loader instead uses a complete fuzz comparison with
notifications for both of the counters its interpreted wait observes.

A case that compared keeps its verdict in the store every worktree shares
(tools/evidence_cache.py, keyed on the binary, the ROM, the movie, the run's
options and this Python); a later run takes it from there while none of those
has changed. --no-cache runs every case again.
"""
import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import evidence_cache, fuzz_compare  # noqa: E402

# entry, movie, frames, excluded ancestors, include-only entries. The scene
# program's start, setup, first frame, clear and hand-back had their cases here
# until the program retired the gate (docs/REIMPLEMENTATION.md, "The scene
# program's teardown"): none is an entry now.
# The held menu continuations retired their comparisons with the menus.
# The battle frame dispatch still sits inside the battle loop: run it
# independently so every selected call reaches its own comparison.
CASES = (
    ("battle_frame_dispatch", "battle-menu-buttons", 5086, ("battle_nmi", "battle_nmi_alternate"), ()),
)

# The unpacker retired into this loader in 03ba3f61, making every measured
# call cross an interrupt. Counter notifications let its complete result be
# compared without handlers, including the work after its first upload.
FUZZ_NAME = "battle_load_effect_graphics"
FUZZ_MOVIES = {"battle-tech-sweep", "endings-beyond-time", "endings-telepod-lavos", "endings-bucket-lavos"}


def fuzz_compared(text, name, iterations):
    rows = [m for m in fuzz_compare.RESULT.finditer(text) if m["name"] == name]
    return (len(rows) == 1 and int(rows[0]["iterations"]) == iterations and
            int(rows[0]["matched"]) == iterations and int(rows[0]["mismatched"]) == 0 and
            int(rows[0]["skipped"]) == 0 and not re.search(
                r"accessor out of range|asset outside the catalogue|contract broken:|halted:", text))


def fuzz_evidence(binary, rom, out, cache, seed):
    _states, recipes = fuzz_compare.load()
    recipe = next(r for r in recipes if r["name"] == FUZZ_NAME and r.get("variant") == "notifications")
    options = ["--routine", FUZZ_NAME, "--iterations", str(recipe["iterations"]), "--seed", str(seed)] + recipe["args"]
    key = evidence_cache.make_key(tool="uncompared_fuzz", binary=evidence_cache.binary_digest(binary),
                                  rom=evidence_cache.file_digest(rom), options=options,
                                  judge=evidence_cache.tools_digest(__file__, fuzz_compare.__file__))
    name = FUZZ_NAME + ":seed-" + str(seed)
    if cache.get(key) is not None:
        return name, True, True
    log = out / (name.replace(":", "-") + ".log")
    try:
        with log.open("wb") as f:
            rc = subprocess.run([str(binary), "--rom", str(rom)] + options, stdout=f,
                                stderr=subprocess.STDOUT, timeout=120).returncode
    except subprocess.TimeoutExpired:
        return name, False, False
    ok = rc == 0 and fuzz_compared(log.read_text(errors="replace"), FUZZ_NAME, recipe["iterations"])
    if ok:
        cache.put(key, dict(compared=True))
    return name, ok, False


def compared(text, name):
    """Every invocation of the selected entry must have actually matched."""
    row = re.search(r"^\s+[0-9A-F]{6} " + re.escape(name) +
                    r"\s+calls (\d+)\s+matched (\d+)\s+mismatched (\d+)\s+interrupted (\d+)(.*)$", text, re.M)
    if not row:
        return False
    calls, matched, mismatched, interrupted = map(int, row.groups()[:4])
    return (calls > 0 and matched == calls and not mismatched and not interrupted and
            not re.search(r"\b(?:abandoned|skipped) [1-9]", row[5]) and
            not re.search(r"\bmismatched [1-9]|page refused|accessor out of range|asset outside the catalogue|halted:", text))


def movie_path(movie):
    return next((ROOT / group / (movie + ".ctm") for group in ("oracle/movies", "oracle/movies/saves")
                 if (ROOT / group / (movie + ".ctm")).exists()))


def options(case):
    """The run's options after the movie and its length."""
    _name, _movie, _frames, excluded, included = case
    command = ["--native", "shadow", "--generated", "off", "--shadow-details", "256"]
    for option, names in (("--exclude", excluded), ("--include", included)):
        for entry in names:
            command += [option, entry]
    return command


def case_key(case, binary, rom):
    """Everything a case's verdict depends on (tools/evidence_cache.py)."""
    name, movie, frames, _excluded, _included = case
    return evidence_cache.make_key(
        tool="uncompared_evidence", entry=name, binary=evidence_cache.binary_digest(binary),
        rom=evidence_cache.file_digest(rom), movie=evidence_cache.text_digest(movie_path(movie)), frames=frames,
        options=options(case), judge=evidence_cache.tools_digest(__file__))


def cached_run(case, binary, rom, out, cache):
    """run, or the stored verdict of a case that compared with these inputs."""
    key = case_key(case, binary, rom)
    if cache.get(key) is not None:
        return case[0], True, True
    name, ok = run(case, binary, rom, out)
    if ok:
        cache.put(key, dict(compared=True))
    return name, ok, False


def run(case, binary, rom, out):
    name, movie, frames, _excluded, _included = case
    command = [str(binary), "--rom", str(rom), "--movie", str(movie_path(movie)), "--frames", str(frames)] + options(case)
    log = out / (name + ".log")
    try:
        with log.open("wb") as f:
            rc = subprocess.run(command, stdout=f, stderr=subprocess.STDOUT, timeout=120 + frames // 25).returncode
    except subprocess.TimeoutExpired:
        return name, False
    return name, rc == 0 and compared(log.read_text(errors="replace"), name)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ct-dev", type=pathlib.Path, required=True)
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "build/uncompared-evidence")
    ap.add_argument("--movies", nargs="*", help="only evidence for these movies; omit for all fixtures")
    ap.add_argument("-j", type=int, default=2)
    ap.add_argument("--cache", type=pathlib.Path,
                    help="where compared cases are kept (default: build/evidence-cache/uncompared in the main "
                         "checkout, which every worktree shares)")
    ap.add_argument("--no-cache", action="store_true",
                    help="run every case again rather than take a stored verdict (also CT_EVIDENCE_CACHE=off)")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    cases = [c for c in CASES if args.movies is None or c[1] in args.movies]
    cache = evidence_cache.Cache(args.cache or evidence_cache.default_dir(args.ct_dev, "uncompared"),
                                 read=not args.no_cache)
    with concurrent.futures.ThreadPoolExecutor(max(1, min(args.j, 2))) as pool:
        results = list(pool.map(lambda c: cached_run(c, args.ct_dev.resolve(), args.rom.resolve(), args.out, cache),
                                cases))
    if args.movies is None or FUZZ_MOVIES.intersection(args.movies):
        fuzz = args.ct_dev.resolve().with_name("ct_fuzz" + args.ct_dev.suffix)
        for seed in (1, 2):
            results.append(fuzz_evidence(fuzz, args.rom.resolve(), args.out, cache, seed))
    for name, ok, hit in results:
        print("%s: %s%s" % (name, "compared" if ok else "FAILED", " (cached)" if hit else ""))
    failed = sum(not ok for _, ok, _ in results)
    print("isolated comparisons: %d checked, %d failed (%s)" % (len(results), failed, cache.summary()))
    return int(bool(failed))


if __name__ == "__main__":
    raise SystemExit(main())
