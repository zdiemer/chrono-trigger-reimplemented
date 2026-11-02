"""Compare scene/warp continuations outside their enclosing ports' stretches.

The combined shadow run still measures every port. These additional runs
remove only the ancestors which claim an earlier comparison point, so the
continuation can reach its own point. The flickering entries each own a
complete trip through real interrupt waits. No counts from these runs are
mixed into timing, fallback or combined-run comparison totals.
"""
import argparse
import concurrent.futures
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
BOOT = ("scene_boot",)
SETUP = BOOT + ("scene_program_start",)
# entry, movie, frames, excluded ancestors, include-only entries
CASES = (
    ("scene_program_start", "title-start", 2000, BOOT, ()),
    ("scene_setup_and_first_frame", "title-start", 2000, SETUP, ()),
    ("scene_first_frame", "title-start", 2000,
     SETUP + ("scene_setup_and_first_frame", "scene_title_set_up"), ()),
    ("scene_program_hands_back", "title-start", 2000, ("scene_program_restart",), ()),
    ("scene_clear_work_ram", "title-start", 2000, SETUP, ()),
    ("scene_after_work_ram_clear", "title-start", 2000, SETUP + ("scene_clear_work_ram",), ()),
    ("warp_flickering_trip_waits", "endings-bucket-lavos", 20000, (), ("warp_flickering_trip_waits",)),
    ("warp_flickering_frame", "endings-bucket-lavos", 20000, (), ("warp_flickering_frame",)),
)


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


def run(case, binary, rom, out):
    name, movie, frames, excluded, included = case
    path = next((ROOT / group / (movie + ".ctm") for group in ("oracle/movies", "oracle/movies/saves")
                 if (ROOT / group / (movie + ".ctm")).exists()))
    command = [str(binary), "--rom", str(rom), "--movie", str(path), "--frames", str(frames),
               "--native", "shadow", "--generated", "off", "--shadow-details", "256"]
    for option, names in (("--exclude", excluded), ("--include", included)):
        for entry in names:
            command += [option, entry]
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
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    cases = [c for c in CASES if args.movies is None or c[1] in args.movies]
    with concurrent.futures.ThreadPoolExecutor(max(1, min(args.j, 2))) as pool:
        results = list(pool.map(lambda c: run(c, args.ct_dev.resolve(), args.rom.resolve(), args.out), cases))
    for name, ok in results:
        print("%s: %s" % (name, "compared" if ok else "FAILED"))
    failed = sum(not ok for _, ok in results)
    print("isolated comparisons: %d checked, %d failed" % (len(results), failed))
    return int(bool(failed))


if __name__ == "__main__":
    raise SystemExit(main())
