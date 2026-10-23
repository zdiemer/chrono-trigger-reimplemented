"""What in `game/` still needs the emulated machine to run, counted from the source.

    python3 tools/machine_joints.py              # the totals
    python3 tools/machine_joints.py --files      # per file, most first

The census (`tools/census.py`) counts how often real play still reaches a port
through the machine; this counts the joints in the source that have to go
before the machine can leave the product (docs/PLAN.md, "What done means,
measured", and "The native program, and the registry taken down"). Each row
is a pattern, so a count is an estimate of sites, not of work: one entry
wrapper is one return through the emulated stack, and it leaves with its
subsystem's registry rows rather than one by one.
"""

import argparse
import collections
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "game"

# (name, pattern, what retires it)
JOINTS = [
    ("registered entries", None, "ordinary functions their callers call"),
    ("returns through the emulated stack", r"\bm_rt[sli]\(", "the entry wrappers deleted"),
    ("stops at the original's addresses", r"\bm_stop_at(?:_own)?\(|\bnative_\w*dont_care_at_stop\(",
     "the next step called as C"),
    ("call-outs to entries by address", r"\bcall_out\w*\(|\bnative_(?:yield_|jump_)?call_dc\(",
     "the callee called as C"),
    ("siblings found in the registry", r"\bnative_hand_entry\(", "the sibling called as C"),
    ("time tags that take the machine", r"\bNATIVE_TIME_AS\(", "a tag with no machine"),
    ("register escapes", r"\bvid_snes_\w+\(|\bau_voices_snes_register\(", "a service call"),
    ("video service calls", r"\bvid_(?!snes_)\w+\(", "one host video backend, not per call"),
    ("scaffolding writes", r"\bORIGINAL_EFFECT\(", "its subsystem retiring the gate"),
]

ENTRY = re.compile(r"0x[0-9A-Fa-f]{6}")


def sources():
    return sorted(p for p in GAME.rglob("*") if p.suffix in (".c", ".h"))


def registry_entries():
    text = (GAME / "registry.c").read_text(encoding="utf-8")
    return len(ENTRY.findall(text))


def count(per_file):
    totals = collections.Counter()
    by_file = collections.defaultdict(collections.Counter)
    for path in sources():
        if path.name == "registry.c":
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for name, pattern, _ in JOINTS:
            if pattern is None:
                continue
            n = len(re.findall(pattern, text))
            if n:
                totals[name] += n
                by_file[path.relative_to(ROOT).as_posix()][name] += n
    totals["registered entries"] = registry_entries()
    return totals, by_file


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--files", action="store_true", help="per file, most joints first")
    args = ap.parse_args()
    totals, by_file = count(args.files)
    if args.files:
        for path, c in sorted(by_file.items(), key=lambda kv: -sum(kv[1].values())):
            print(f"{sum(c.values()):6d}  {path}: " + ", ".join(f"{n} {k}" for k, n in c.most_common()))
        return
    for name, _, retires in JOINTS:
        print(f"{totals[name]:6d}  {name}  (goes when: {retires})")


if __name__ == "__main__":
    main()
