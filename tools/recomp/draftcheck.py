"""A scratch tree where routines run as their drafts, to shadow them.

    python3 -m tools.recomp.draftcheck --out <scratch dir outside the repo> [name | BB:AAAA ...] [--m 8 --x 16]

Drafts compile against the runtime and do what the original does
(tools/recomp/show.py), so the way to know a draft is right is the way a port
is known to be right: register it and run shadow mode over the movie library.
This exports the committed tree (`git archive HEAD`, or --rev) into --out/src and drafts
routines there:

- every routine game/registry.c registers, or those named: each draft goes to
  game/drafts/<name>.c and the registry entry points at it, with the draft's
  own precondition, don't-care set and guard on the D and DB it assumed;
- an address given as BB:AAAA that nothing registers yet gets an entry of its
  own (--m/--x give its widths when the analysis has several).

Then, in --out:

    cmake -S src -B build -G Ninja && cmake --build build --target ct_dev
    build/ct_dev --rom <rom> --movie <movie> --frames N --native shadow

and every call to a drafted routine is checked against the original, at each
call-out and at the return. Entries that are idle loops or carry a guard of
their own, routines the analysis enters with more than one set of widths and
routines that start with a wait loop are left as they are and listed.

Drafts are ROM-derived, so --out must be outside the repository.
"""

import argparse
import re
import shutil
import subprocess
import sys
import traceback
from pathlib import Path

from tools.disasm.memmap import Rom, fmt, parse

from . import show

REPO_ROOT = Path(__file__).resolve().parents[2]
ADDRESS = re.compile(r"\$?[0-9A-Fa-f]{2}:[0-9A-Fa-f]{4}")


def entries(text: str) -> list:
    """(start, end, address, name, the entry's other fields) of every registry
    entry, in the order they appear."""
    out = []
    for m in re.finditer(r'\{\s*\.addr = 0x([0-9A-F]{6}), \.name = "(\w+)", \.fn = (\w+),(.*?)\},', text, re.S):
        out.append((m.start(), m.end(), int(m.group(1), 16), m.group(2), m.group(4)))
    for m in re.finditer(r"EVENT_CMD\(0x([0-9A-F]{6}), (\w+)\),", text):
        out.append((m.start(), m.end(), int(m.group(1), 16), m.group(2), " NEEDS_M8_X16_BINARY"))
    return sorted(out)


def widths_of(rest: str):
    m = re.search(r"NEEDS_M(8|16)(?:_X(8|16))?", rest)
    return (int(m.group(1)) if m else None, int(m.group(2)) if m and m.group(2) else None)


class Tree:
    """The exported tree, collecting drafts and the entries that register them."""

    def __init__(self, k, src: Path):
        self.k = k
        self.src = src
        self.reg_path = src / "game" / "registry.c"
        self.text = self.reg_path.read_text(encoding="utf-8")
        (src / "game" / "drafts").mkdir(exist_ok=True)
        self.decls, self.files, self.drafted, self.skipped = [], [], [], {}

    def draft(self, addr: int, name: str, m, x, cost: str | None = None) -> str | None:
        """The registry entry for the draft of addr, or None (the reason is in
        self.skipped)."""
        ws = show.entry_widths(self.k.an, addr, m, x)
        if len(ws) != 1 or (addr, ws[0]) not in self.k.an.contexts:
            self.skipped[name] = "widths " + (", ".join(f"m{8 if a else 16} x{8 if b else 16}" for a, b in ws)
                                              or "unknown")
            return None
        dname = f"draft_{name}"
        try:
            d = show.Draft(self.k, addr, ws[0][0], ws[0][1], name=dname)
            d.build()
            if any(d.graph.entry in heads for heads, _ in d.waits):
                self.skipped[name] = "starts with a wait loop"
                return None
            body = d.render()
        except Exception as e:     # a draft that fails is reported, not fatal
            self.skipped[name] = f"failed: {e!r}"
            traceback.print_exc(limit=3)
            return None
        reg = d.registration()
        guard = ""
        if reg["guard"]:
            body += f"\nbool {dname}_guard(Snes *s)\n{{\n    return {' && '.join(reg['guard'])};\n}}\n"
            guard = f", .guard = {dname}_guard"
            self.decls.append(f"bool {dname}_guard(Snes *s);")
        (self.src / "game" / "drafts" / f"{name}.c").write_text(show.PREAMBLE + "\n" + body, encoding="utf-8",
                                                              newline="\n")
        self.files.append(f"  game/drafts/{name}.c")
        self.decls.append(f"void {dname}(Snes *s);")
        self.drafted.append(name)
        return (f'{{ .addr = 0x{addr:06X}, .name = "{name}", .fn = {dname}, .p_mask = 0x{reg["p_mask"]:02X}, '
                f'.p_value = 0x{reg["p_value"]:02X}, .cost_milli = {cost or 4000}, '
                f'.dont_care = {reg["dont_care"]}{guard} }},')

    def replace_registered(self, only: set):
        for start, end, addr, name, rest in reversed(entries(self.text)):
            if only is not None and name not in only and addr not in only:
                continue
            if ".idle" in rest or ".guard" in rest:
                self.skipped[name] = "an idle entry or one with its own guard"
                continue
            m, x = widths_of(rest)
            cost = re.search(r"\.cost_milli = (\d+)", rest)
            entry = self.draft(addr, name, m, x, cost.group(1) if cost else None)
            if entry is not None:
                self.text = self.text[:start] + entry + self.text[end:]

    def add(self, addr: int, m, x):
        """A draft of an address nothing registers, with an entry of its own."""
        name = f"sub_{addr:06X}"
        entry = self.draft(addr, name, m, x)
        if entry is None:
            return
        end = self.text.rindex("\n};\n\nconst int game_native_count")
        self.text = self.text[:end] + "\n    " + entry + self.text[end:]

    def finish(self):
        (self.src / "game" / "drafts" / "drafts.h").write_text(
            "#ifndef CT_DRAFTS_H\n#define CT_DRAFTS_H\n#include <stdbool.h>\n#include \"snes/system.h\"\n"
            + "\n".join(self.decls) + "\n#endif\n", encoding="utf-8", newline="\n")
        text = self.text.replace('#include "registry.h"\n', '#include "registry.h"\n#include "drafts/drafts.h"\n', 1)
        self.reg_path.write_text(text, encoding="utf-8", newline="\n")
        cm = self.src / "CMakeLists.txt"
        ct = cm.read_text(encoding="utf-8")
        anchor = "add_library(ct_game STATIC\n  game/registry.c\n"
        if anchor not in ct:
            raise SystemExit("CMakeLists.txt: can't find the ct_game source list")
        cm.write_text(ct.replace(anchor, anchor + "\n".join(self.files) + "\n", 1), encoding="utf-8", newline="\n")


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 -m tools.recomp.draftcheck", description=__doc__.split("\n")[0])
    ap.add_argument("which", nargs="*", help="registry entry names, or BB:AAAA addresses (default: every entry)")
    ap.add_argument("--out", type=Path, required=True, help="scratch directory, outside the repository")
    ap.add_argument("--m", type=int, choices=(8, 16), help="widths for the addresses given")
    ap.add_argument("--x", type=int, choices=(8, 16))
    ap.add_argument("--rom", type=Path, default=REPO_ROOT / "roms" / "ct-usa.sfc")
    ap.add_argument("--rev", default="HEAD",
                    help="the tree to export (a commit; `git stash create` gives one of uncommitted work)")
    ap.add_argument("--root", type=Path, default=REPO_ROOT,
                    help="where the default coverage and call globs are looked for")
    args = ap.parse_args(argv)
    out = args.out.resolve()
    if out == REPO_ROOT or REPO_ROOT in out.parents:
        raise SystemExit("--out must be outside the repository: drafts are ROM-derived")
    if not args.rom.exists():
        raise SystemExit(f"{args.rom} not found; stage the ROM with tools/rom.py first")
    src = out / "src"
    if src.exists():
        shutil.rmtree(src)
    src.mkdir(parents=True)
    tree = subprocess.run(["git", "-C", str(REPO_ROOT), "archive", args.rev], capture_output=True, check=True).stdout
    subprocess.run(["tar", "-x", "-C", src.as_posix()], input=tree, check=True)

    registry = entries((REPO_ROOT / "game" / "registry.c").read_text(encoding="utf-8"))
    by_addr = {a: name for _, _, a, name, _ in registry}
    addrs = [parse(w) for w in args.which if ADDRESS.fullmatch(w)]
    names = {w for w in args.which if not ADDRESS.fullmatch(w)}
    only = None if not args.which else names | {by_addr[a] for a in addrs if a in by_addr}
    wanted = [(a, *widths_of(rest)) for _, _, a, name, rest in registry if only is None or name in only]
    wanted += [(a, args.m, args.x) for a in addrs if a not in by_addr]
    coverage = show.expand(show.DEFAULT_COVERAGE, args.root)
    calls = show.expand(show.DEFAULT_CALLS, args.root)
    if not coverage or not calls:
        print(f"no coverage or call files under {args.root}: the analysis will know less (--root)", file=sys.stderr)
    k = show.Knowledge(Rom(args.rom.read_bytes()), REPO_ROOT / "re" / "hints.toml", coverage, calls,
                       REPO_ROOT / "game" / "registry.c", REPO_ROOT / "re" / "ram.toml", wanted)
    t = Tree(k, src)
    t.replace_registered(only)
    for a in addrs:
        if a not in by_addr:
            t.add(a, args.m, args.x)
    t.finish()
    print(f"{len(t.drafted)} drafts registered in {src}; {len(t.skipped)} left as they are")
    for name, why in sorted(t.skipped.items()):
        print(f"  {name}: {why}")
    for a in addrs:
        if a not in by_addr and f"sub_{a:06X}" in t.drafted:
            print(f"  {fmt(a)}: registered as sub_{a:06X}")


if __name__ == "__main__":
    sys.exit(main())
