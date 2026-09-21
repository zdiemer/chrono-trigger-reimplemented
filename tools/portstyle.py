"""How much of a port is reimplementation and how much is transliterated assembly.

    python3 tools/portstyle.py                      # per subsystem, then the worst files
    python3 tools/portstyle.py --files              # every file
    python3 tools/portstyle.py --subsystem menu
    python3 tools/portstyle.py --check              # the ratchet the suite runs

A port is meant to be a reimplementation: the gate measures observable
behaviour, and nothing requires a port to reach it the way the ROM does
(CLAUDE.md, "Writing a port from generated code"). The failure mode is a port
that translates the assembly instead -- a run of machine operations on emulated
registers, with the original's variables spelled as addresses -- which passes
the gate and decompiles nothing.

What is counted is that failure mode, not machine access in general: the
emulated registers (`c->a`, `s->cpu.x`) and the operations that move, combine
and test them (`m_lda16`, `m_tax`, `m_asl16`, `m_sep`). Reading and writing
memory (`m_read8`, `m_write16`, `m_abs`, `m_dp`), calling out (`m_jsr`) and
handing back (`m_stop_at`) are how a port talks to the machine at all, so they
are not counted; nor is `runtime/`, which implements the machine, or
`game/registry.c`, which is a table.

The measure is operations per line of the file. It is a smell, not a proof: a
low score doesn't make a port good, and a routine that genuinely manipulates
the processor (an interrupt entry, a mode switch) scores high and should. Use
it to find what needs reading, and to keep new work from adding to the pile.

The report carries a second, independent number: the share of a subsystem's
functions that take a `Snes *`, and so can only run on an emulated SNES
whatever they look like inside. A port can read well and still be welded to the
machine -- `game/battle/effect_paths.c` scores 0.03 with every function on the
machine -- so the two say different things and Phase 3a has to move both. The
goal is the machine in a thin wrapper at each entry and the logic in functions
that take the game's own state (PLAN.md, "Phase 3a").

`--check` is a ratchet against tools/portstyle_baseline.txt, which lists the
files written before this bar existed and the score each had. It fails when a
file not on the list scores above THRESHOLD, or a listed file gets worse.
Rewriting one to the bar means deleting its line (docs/PLAN.md, "The
transcription backlog"). It never passes on its own: a file under the threshold
can still be transliterated, which is what review is for.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GAME = ROOT / "game"
BASELINE = ROOT / "tools" / "portstyle_baseline.txt"

# Operations on the emulated registers: loads and stores of them, transfers,
# arithmetic and shifts through them, the flag and width instructions, and the
# registers themselves. Not memory access, call-outs or hand-backs.
OPS = re.compile(
    r"\bm_(?:"
    r"lda8|lda16|ldx|ldy|"
    r"tax|tay|txa|tya|txy|tyx|tsx|txs|tcd|tdc|tcs|tsc|"
    r"adc8|adc16|sbc8|sbc16|and8|and16|ora8|ora16|eor8|eor16|"
    r"asl8|asl16|lsr8|lsr16|rol8|rol16|ror8|ror16|"
    r"cmp8|cmp16|cpx|cpy|inc8|inc16|dec8|dec16|inx|dex|iny|dey|"
    r"bit8|bit16|trb8|trb16|tsb8|tsb16|"
    r"sep|rep|php|plp|xba|xce|mvn|mvp"
    r")\b"
    r"|\bc->(?:a|x|y|p|s|d|db)\b"
    r"|\bs->cpu\.(?:a|x|y|p|s|d|db)\b"
)

THRESHOLD = 0.10        # operations per line a new port may not exceed

# A function that takes the machine is coupled to it: it can only run on an
# emulated SNES, whatever it looks like inside. The logic belongs in functions
# that take the game's own state and plain values, with the translation to and
# from the original's registers and direct page in a thin wrapper at the entry
# (CLAUDE.md's 65816 rule, and PLAN.md's Phase 3a). Counted separately from the
# style score, because a port can read well and still be welded to the machine:
# game/battle/effect_paths.c scores 0.03 and every one of its functions takes a
# Snes *.
DEFN = re.compile(r"^(?:static\s+)?[A-Za-z_][\w ]*\**\s*\**(\w+)\s*\(([^;{]*)\)\s*$", re.M)
MACHINE = re.compile(r"\bSnes\s*\*")

SKIP = {"game/registry.c"}


def strip_comments(src: str) -> str:
    """Comments don't count: a port that explains itself isn't transliterating."""
    src = re.sub(r"/\*.*?\*/", lambda mo: "\n" * mo.group(0).count("\n"), src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def subsystem_of(path: pathlib.Path) -> str:
    rel = path.relative_to(GAME)
    return rel.parts[0] if len(rel.parts) > 1 else "core"


def coupling(text: str) -> tuple:
    """(functions taking the machine, functions defined) in stripped source."""
    defns = DEFN.findall(text)
    return sum(1 for _, args in defns if MACHINE.search(args)), len(defns)


def score(path: pathlib.Path) -> tuple:
    """(operations per line, operations, lines) for one file."""
    text = strip_comments(path.read_text(encoding="utf-8"))
    lines = max(sum(1 for line in text.splitlines() if line.strip()), 1)
    ops = len(OPS.findall(text))
    return ops / lines, ops, lines


def measure() -> list:
    """[(rate, ops, lines, "game/sub/file.c")] for every port, worst first."""
    rows = []
    for path in sorted(GAME.rglob("*.c")):
        rel = path.relative_to(ROOT).as_posix()
        if rel in SKIP:
            continue
        rate, ops, lines = score(path)
        rows.append((rate, ops, lines, rel))
    rows.sort(reverse=True)
    return rows


def read_baseline() -> dict:
    if not BASELINE.exists():
        return {}
    out = {}
    for line in BASELINE.read_text(encoding="utf-8").splitlines():
        line = line.split("#")[0].strip()
        if line:
            path, rate = line.rsplit(None, 1)
            out[path] = float(rate)
    return out


def write_baseline(rows: list) -> None:
    lines = [
        "# Ports written before the reimplementation bar existed, and the score each had.",
        "# tools/portstyle.py --check fails when a file not listed here scores above the",
        "# threshold, or a listed file gets worse. Rewriting one to the bar means deleting",
        "# its line; the list is meant to empty (docs/PLAN.md, \"The transcription backlog\").",
        "",
    ]
    for rate, _, _, path in sorted(rows, key=lambda r: r[3]):
        if rate > THRESHOLD:
            lines.append("%-44s %.3f" % (path, rate))
    BASELINE.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def check(rows: list, out=sys.stdout) -> int:
    base = read_baseline()
    findings = []
    for rate, _, _, path in rows:
        allowed = base.get(path)
        if allowed is None:
            if rate > THRESHOLD:
                findings.append("%s: %.3f operations per line, over the %.2f a new port may have. "
                                "It is transliterating the assembly rather than reimplementing the "
                                "routine (CLAUDE.md, \"Writing a port from generated code\")."
                                % (path, rate, THRESHOLD))
        elif rate > allowed + 0.005:
            findings.append("%s: %.3f operations per line, worse than the %.3f it was written with. "
                            "A change to a port on the backlog may not add to it."
                            % (path, rate, allowed))
    for path in sorted(set(base) - {r[3] for r in rows}):
        findings.append("%s: on the backlog but gone; delete its line from %s."
                        % (path, BASELINE.relative_to(ROOT).as_posix()))
    for f in findings:
        print("  " + f, file=out)
    left = sum(1 for rate, _, _, path in rows if path in base)
    print("portstyle: %d finding%s, %d file%s left on the backlog"
          % (len(findings), "" if len(findings) == 1 else "s", left, "" if left == 1 else "s"), file=out)
    return 1 if findings else 0


def report(rows: list, show_files: bool, only: str | None, worst: int, out=sys.stdout) -> None:
    base = read_baseline()
    by_sub: dict = {}
    for rate, ops, lines, path in rows:
        sub = subsystem_of(ROOT / path)
        if only and sub != only:
            continue
        o, l = by_sub.get(sub, (0, 0))
        by_sub[sub] = (o + ops, l + lines)

    coup: dict = {}
    for _, _, _, path in rows:
        sub = subsystem_of(ROOT / path)
        if only and sub != only:
            continue
        machine, defns = coupling(strip_comments((ROOT / path).read_text(encoding="utf-8")))
        m, d = coup.get(sub, (0, 0))
        coup[sub] = (m + machine, d + defns)

    print("%-12s %8s %8s %10s %9s %9s" % ("subsystem", "ops", "lines", "per line",
                                          "fns", "on machine"), file=out)
    for sub in sorted(by_sub, key=lambda s: -by_sub[s][0] / max(by_sub[s][1], 1)):
        ops, lines = by_sub[sub]
        m, d = coup.get(sub, (0, 0))
        print("%-12s %8d %8d %10.2f %9d %8d%%" % (sub, ops, lines, ops / max(lines, 1),
                                                  d, round(100 * m / max(d, 1))), file=out)
    ops = sum(o for o, _ in by_sub.values())
    lines = sum(l for _, l in by_sub.values())
    m = sum(v[0] for v in coup.values())
    d = sum(v[1] for v in coup.values())
    print("%-12s %8d %8d %10.2f %9d %8d%%" % ("all", ops, lines, ops / max(lines, 1),
                                              d, round(100 * m / max(d, 1))), file=out)

    shown = [r for r in rows if not only or subsystem_of(ROOT / r[3]) == only]
    if not show_files:
        shown = shown[:worst]
        print("\nthe %d furthest from the bar:" % len(shown), file=out)
    else:
        print("\nevery port:", file=out)
    print("%-44s %8s %8s %10s  %s" % ("file", "ops", "lines", "per line", "backlog"), file=out)
    for rate, ops, lines, path in shown:
        print("%-44s %8d %8d %10.2f  %s"
              % (path, ops, lines, rate, "yes" if path in base else ""), file=out)

    carried = sum(l for rate, _, l, p in rows if p in base)
    total = sum(l for _, _, l, _ in rows)
    if not only:
        print("\n%d of %d lines (%.0f%%) are in ports on the transcription backlog."
              % (carried, total, 100.0 * carried / max(total, 1)), file=out)

    # game/wram.h is the documented escape from the typed accessors, for buffers
    # the game locates at run time. Every use is a finding and the list is meant
    # to empty, so it is reported rather than left to be discovered by grep.
    users = {}
    for _, _, _, path in rows:
        n = len(re.findall(r"\bwram_(?:read|write)(?:8|16)\b",
                           strip_comments((ROOT / path).read_text(encoding="utf-8"))))
        if n:
            users[path] = n
    if users and not only:
        print("work RAM reached by address through game/wram.h (each a finding): %d in %d file(s): %s"
              % (sum(users.values()), len(users),
                 ", ".join("%s %d" % (p, n) for p, n in sorted(users.items()))), file=out)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python3 tools/portstyle.py",
                                 description=__doc__.split("\n")[0])
    ap.add_argument("--files", action="store_true", help="list every port, not just the worst")
    ap.add_argument("--subsystem", help="only this subsystem")
    ap.add_argument("--worst", type=int, default=15, help="how many files to list (default 15)")
    ap.add_argument("--check", action="store_true", help="the ratchet: exit 1 on a finding")
    ap.add_argument("--write-baseline", action="store_true",
                    help="record today's scores as the backlog (once, when the list is created)")
    args = ap.parse_args(argv)

    rows = measure()
    if args.write_baseline:
        write_baseline(rows)
        print("wrote %s" % BASELINE.relative_to(ROOT).as_posix())
        return 0
    if args.check:
        return check(rows)
    report(rows, args.files, args.subsystem, args.worst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
