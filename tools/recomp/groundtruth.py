"""How close a draft comes to the hand port of the same routine (docs/RECOMP.md, M5).

    python3 -m tools.recomp.groundtruth BB:AAAA [BB:AAAA ...] [--m 8 --x 16] [--show]

The routines already ported were validated call by call against the original
in shadow mode, so each is a known-correct target for the draft of its
address. This drafts them (tools/recomp/show.py) and measures, in terms a
porter cares about:

- **accesses**: the work RAM addresses the port reads and writes (its literals,
  RAM_ names and direct-page offsets, resolved with the D the draft found) that
  the draft accesses too, and how many of each side's are spelled with a
  game/ram.h name;
- **call-outs**: the routines the port calls with m_jsr/m_jsl that the draft
  calls too (and the ones it calls the port doesn't: the port inlined them);
- **structure**: if, else, loops, switch and goto in each;
- **text**: how much of the port's text the draft already gives (port lines
  with a close line in the draft, and a token-level similarity), and how many
  emulated-register operations (m_lda8, c->x, m_push16 ...) each has;
- the port's own helpers (static functions in its file that it calls) count as
  part of the port.

It prints numbers only, never a draft (--show prints the draft too, to stdout,
like tools.recomp.show). Nothing it prints is committed.
"""

import argparse
import difflib
import re
import sys
from pathlib import Path

from tools import dontcare_check as dc
from tools.disasm.memmap import Rom, fmt, parse
from tools.ram_map import name_values

from . import names, show
from .ir import CallOut, Const, Load, Store
from .lift import canonical, region

REPO_ROOT = Path(__file__).resolve().parents[2]
GAME = REPO_ROOT / "game"

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
STRING = re.compile(r'"(?:\\.|[^"\\\n])*"')
FUNC = re.compile(r"^(?:static\s+)?(?:inline\s+)?[\w \*]+?\b(\w+)\s*\(([^;{)]*)\)\s*\{", re.M)
CALL = re.compile(r"\bm_js[rl](?:_dc)?\(\s*s\s*,\s*([^,]+),\s*([^,)]+)")
SHUFFLE = re.compile(r"\bm_(?:lda8|lda16|ldx|ldy|ldx16|tax|tay|txa|tyx|txy|tdc|xba|inx|iny|rep|sep|set_a8|set_index|"
                     r"set_nz8|set_nz16|set_flag|asl8|asl16|lsr8|lsr16|cmp8|cmp16|adc8|adc16|sbc8|sbc16|"
                     r"push8|push16|pull8|pull16|jsr_stack_bytes)\b|\bc->(?:a|x|y|p|d|db|s)\b|\bs->cpu\.")
ARITHMETIC_NAMES = {names.io_name(a) for a in names.ARITHMETIC}
STRUCT = {"if": r"\bif\s*\(", "else": r"\belse\b", "loop": r"\b(?:for|while)\s*\(|\bdo\s*\{",
          "switch": r"\bswitch\s*\(", "goto": r"\bgoto\b"}


# ---- the port ---------------------------------------------------------------------------------


def functions(text: str) -> dict:
    """name -> (start, end, body text) of every function defined in C text."""
    out = {}
    for m in FUNC.finditer(text):
        i = m.end() - 1
        depth = 0
        for j in range(i, len(text)):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    out[m.group(1)] = (m.start(), j + 1, text[m.start():j + 1])
                    break
    return out


def constants(text: str) -> dict:
    """NAME -> value for the enums and #defines in C text."""
    out = {}
    for m in re.finditer(r"#define\s+(\w+)\s+\(?(0x[0-9A-Fa-f]+|\d+)u?\)?", text):
        out[m.group(1)] = int(m.group(2), 0)
    for body in re.findall(r"enum\s*\w*\s*\{([^}]*)\}", text):
        for m in re.finditer(r"(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", body):
            out[m.group(1)] = int(m.group(2), 0)
    return out


def find_port(addr: int, game: Path = GAME):
    """(path, function name, [texts of the function and its static helpers],
    constants in scope, [those functions' names]) for the port whose
    `// $BB:AAAA` comment names addr."""
    tag = f"// ${addr >> 16:02X}:{addr & 0xFFFF:04X}"
    for path in sorted(game.rglob("*.c")):
        raw = path.read_text(encoding="utf-8")
        at = raw.find(tag)
        while at >= 0 and raw[at + len(tag):at + len(tag) + 1] not in ("", "\n", " ", "\r"):
            at = raw.find(tag, at + 1)
        if at < 0:
            continue
        funcs = functions(raw)
        after = [(s, n) for n, (s, e, _) in funcs.items() if s > at]
        if not after:
            continue
        name = min(after)[1]
        consts = constants(raw)
        for inc in re.findall(r'#include "([^"]+)"', raw):
            for base in (path.parent, game):
                if (base / inc).exists():
                    consts.update(constants((base / inc).read_text(encoding="utf-8")))
        # the port and the static helpers it reaches in its own file
        statics = {n for n in funcs if re.search(r"^static\b[^\n]*\b%s\s*\(" % re.escape(n), raw, re.M)}
        seen, work = [], [name]
        while work:
            n = work.pop()
            if n in seen:
                continue
            seen.append(n)
            body = strip(funcs[n][2])
            for callee in re.findall(r"\b(\w+)\s*\(", body):
                if callee in statics and callee not in seen:
                    work.append(callee)
        return path, name, [funcs[n][2] for n in seen], consts, seen
    return None


class Resolver:
    """The port's file-local names, through tools/dontcare_check.py's reading
    of C: its macros (COUNTER for RAM_ABS(RAM_SOUND_DRIVER_UPLOAD_COUNTER)),
    enums and constants, and game/ram.h's, and the targets of call-outs made
    through the file's helpers."""

    def __init__(self, path: Path, root: Path = REPO_ROOT):
        sources, globals_ = dc.load_sources(root)
        rel = path.relative_to(root).as_posix()
        self.src = next((s for s in sources if s.path == rel), None) or dc.Source(rel, path.read_text(encoding="utf-8"))
        self.ev = dc.Evaluator(sources, globals_)

    def value(self, expr: str):
        """(value, spelled with a RAM_ name) of a constant expression, or (None, False)."""
        try:
            node = dc.parse(expr)
            vals = {v[0] for v, _ in self.ev.values([node], self.src, None)}
        except (dc.Unreadable, ValueError, IndexError):
            return None, False
        if len(vals) != 1:
            return None, False
        return vals.pop(), self.ev.mentions(node, self.src)

    def call_outs(self, funcs) -> set:
        return dc.call_out_targets(self.src, set(funcs), self.ev)

    def address_params(self, name: str, depth: int = 0):
        """Which of a helper's parameters (by position, `s` being 0) end up in
        the address of a read or a write, directly or through another helper
        of the file; None if the file doesn't define it."""
        f = self.src.funcs.get(name)
        if f is None:
            return None
        out = set()
        for fn, args in calls(self.src.text[f.start:f.end]):
            if re.fullmatch(r"m_(?:read|write)(?:8|16)", fn) and len(args) >= 2:
                spots = [args[1]]
            elif fn in self.src.funcs and fn != name and depth < 6:
                spots = [args[j] for j in sorted(self.address_params(fn, depth + 1) or ()) if j < len(args)]
            else:
                continue
            for i, p in enumerate(f.params):
                if any(re.search(r"\b%s\b" % re.escape(p), spot) for spot in spots):
                    out.add(i)
        return out


def strip(text: str) -> str:
    return STRING.sub('""', COMMENT.sub(" ", text))


# ---- measures ---------------------------------------------------------------------------------


def _value(expr: str, consts: dict, names: dict):
    expr = expr.strip()
    m = re.fullmatch(r"\(?\s*(0x[0-9A-Fa-f]+|\d+)u?\s*\)?", expr)
    if m:
        return int(m.group(1), 0)
    if expr in names:
        return names[expr]
    if expr in consts:
        return consts[expr]
    return None


def calls(text: str):
    """(function name, [argument texts]) for every call in C text, arguments
    split at their top-level commas."""
    for m in re.finditer(r"\b(\w+)\s*\(", text):
        i, depth, args, start = m.end(), 1, [], m.end()
        while i < len(text) and depth:
            ch = text[i]
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    args.append(text[start:i])
            elif ch == "," and depth == 1:
                args.append(text[start:i])
                start = i + 1
            i += 1
        yield m.group(1), [a.strip() for a in args]


def _address(expr: str, consts: dict, names: dict, d, db, resolver=None):
    """(24-bit address, spelled by a RAM_ name) of an address expression's first
    term: a literal, a RAM_ name, a constant, m_dp / m_abs / RAM_ABS of one,
    or with a resolver, any constant expression of the file's names."""
    base = re.split(r"\s*[+|]\s*", expr.strip().strip("()"), maxsplit=1)[0].strip()
    if resolver is not None and not re.match(r"m_(?:dp|abs)\(", base):
        v, named = resolver.value(base)
        if v is not None:
            return v, named
    named = "RAM_" in base
    m = re.fullmatch(r"m_dp\(\s*s\s*,\s*(\w+)\s*\)", base)
    if m:
        off = _value(m.group(1), consts, names)
        return (None if off is None or d is None else 0x7E0000 | ((d + off) & 0xFFFF)), named
    m = re.fullmatch(r"(?:m_abs\(\s*s\s*,|RAM_ABS\()\s*(\w+)\s*\)", base)
    if m:
        off = _value(m.group(1), consts, names)
        return (None if off is None or db is None else canonical(db << 16 | (off & 0xFFFF))), named
    return _value(base, consts, names), named


def port_addresses(text: str, consts: dict, names: dict, d, db, resolver=None) -> tuple:
    """(work RAM addresses, how many were spelled by a RAM_ name, how many as a
    number) the port's reads and writes reach, as far as its text says: the
    first argument of m_read/m_write, and the address-like literals (4 or 6 hex
    digits) it passes to its own helpers, direct-page offsets resolved with d
    and absolute ones with db. A literal the text computes an address from in
    some other way isn't seen."""
    out, named, numbered = set(), 0, 0
    for fn, args in calls(text):
        if len(args) < 2 or args[0] != "s":
            continue
        kind = None
        if re.fullmatch(r"m_(?:read|write)(?:8|16)", fn):
            cands = [args[1]]
        elif fn.startswith("m_") or fn.startswith("vid_") or fn.startswith("snd_"):
            continue
        else:
            # the port's own helpers: dp8(s, 0x77), absx8(s, 0xA12F), ...
            kind = "dp" if "dp" in fn else ("abs" if "abs" in fn else None)
            known = resolver.address_params(fn) if resolver is not None else None
            cands = [a for i, a in enumerate(args) if i > 0 and (known is None or i in known) and
                     (a.startswith("RAM_") or re.fullmatch(r"0x[0-9A-Fa-f]{%s}u?" % ("2,4" if kind else "4,6"), a) or
                      (resolver is not None and re.fullmatch(r"[A-Z][A-Z0-9_]*", a) and resolver.value(a)[1]))]
        for arg in cands:
            a, by_name = _address(arg, consts, names, d, db, resolver)
            if a is None:
                continue
            long_literal = bool(re.fullmatch(r"0x[0-9A-Fa-f]{6}u?", arg.strip()))
            if kind == "dp":
                a = None if d is None else 0x7E0000 | ((d + a) & 0xFFFF)
            elif a <= 0xFFFF and not long_literal and not by_name:
                a = canonical(((db if db is not None else 0x7E) << 16) | a) if a >= 0x100 or kind == "abs" else \
                    (None if d is None else 0x7E0000 | ((d + a) & 0xFFFF))
            if a is None or region(a) != "ram":
                continue
            out.add(canonical(a))
            named += by_name
            numbered += not by_name
    return out, named, numbered


def port_stops(text: str) -> list:
    """Where the port hands back to the interpreter (c->pc = ...)."""
    return sorted({int(v, 16) for v in re.findall(r"(?:c->|s->cpu\.)pc\s*=\s*0x([0-9A-Fa-f]{4})", text)})


def draft_addresses(d) -> tuple:
    """(work RAM addresses, named, numbered) of the draft's reads and writes."""
    out, named, numbered = set(), 0, 0
    for s in show._walk_stmts(d.tree):
        exprs = list(s.exprs())
        for e in exprs:
            for x in e.walk():
                a = None
                if isinstance(x, (Load,)):
                    a = x.addr
                if a is None:
                    continue
                base = a
                while hasattr(base, "l") and getattr(base, "op", "") == "+":
                    base = base.l
                if isinstance(base, Const) and region(base.v) == "ram":
                    out.add(canonical(base.v))
        if isinstance(s, Store):
            base = s.addr
            while hasattr(base, "l") and getattr(base, "op", "") == "+":
                base = base.l
            if isinstance(base, Const) and region(base.v) == "ram":
                out.add(canonical(base.v))
    for a in out:
        if d.namer is not None and d.namer.name(a):
            named += 1
        else:
            numbered += 1
    return out, named, numbered


def port_callouts(text: str, consts: dict, resolver=None, funcs=()) -> set:
    """The routines the port calls out to: with a resolver, through its
    file's helpers (a stub that calls m_jsr with its parameter, a platform
    call-out); otherwise the literal targets of m_jsr/m_jsl in its text."""
    if resolver is not None:
        return resolver.call_outs(funcs)
    out = set()
    for m in CALL.finditer(text):
        t = _value(m.group(2), consts, {})
        out.add(t if t is not None else "computed")
    return out


def draft_callouts(d) -> set:
    out = set()
    for s in show._walk_stmts(d.tree):
        if isinstance(s, CallOut):
            out.add(s.target if s.target is not None else "computed")
    return out


def structure_counts(text: str) -> dict:
    return {k: len(re.findall(p, text)) for k, p in STRUCT.items()}


TOKEN = re.compile(r"0x[0-9A-Fa-f]+|\w+|[^\s\w]")


def tokens(text: str) -> list:
    text = re.sub(r"\bc->|\bs->cpu\.", "", text)
    return TOKEN.findall(text)


def line_coverage(port: str, draft: str, cutoff: float = 0.75) -> tuple:
    """(port lines with a draft line at least `cutoff` similar, port lines)."""
    def norm(t):
        return [" ".join(tokens(x)) for x in t.splitlines() if len(tokens(x)) > 2]
    pl, dl = norm(port), norm(draft)
    hit = 0
    for line in pl:
        best = difflib.get_close_matches(line, dl, n=1, cutoff=cutoff)
        hit += bool(best)
    return hit, len(pl)


def left_to_do(d, logic: str) -> dict:
    """What a porter still has to do to the draft's logic: unnamed work RAM,
    I/O registers to put behind a platform service, gotos, temporaries, lines
    that set machine state, call-outs, and hand-backs."""
    return {
        "unnamed": len(set(re.findall(r"\b0x7[EF][0-9A-F]{4}\b", logic))),
        "io": len([n for n in re.findall(r"/\* ([A-Z0-9]+) \*/", logic) if n not in ARITHMETIC_NAMES]),
        "goto": len(re.findall(r"\bgoto\b", logic)),
        "temps": len(d.routine.temps and set(re.findall(r"\bt\d+\b", logic))),
        "machine": len(re.findall(r"s->cpu\.", logic)),
        "callouts": len(re.findall(r"\bcall_\w+\(", logic)),
        "handbacks": len([s for s in d.exits if s.kind == "stop"]),
    }


def measure(k, d, port) -> dict:
    path, pname, texts, consts, funcs = port
    ptext = strip("\n".join(texts))
    logic = "\n".join(d._logic(d.rend))
    names = name_values(k.ram) if k.ram else {}
    resolver = Resolver(path)
    pa, pnamed, pnum = port_addresses(ptext, consts, names, d.d, d.db, resolver)
    da, dnamed, dnum = draft_addresses(d)
    pc, dc = port_callouts(ptext, consts, resolver, funcs), draft_callouts(d)
    hit, lines = line_coverage(ptext, logic)
    sm = difflib.SequenceMatcher(None, tokens(ptext), tokens(logic), autojunk=False)
    return {
        "port": f"{path.relative_to(REPO_ROOT).as_posix()}:{pname}" + (f" (+{len(texts) - 1} helpers)"
                                                                     if len(texts) > 1 else ""),
        "addr_port": len(pa), "addr_common": len(pa & da), "addr_draft": len(da),
        "port_named": pnamed, "port_numbered": pnum, "draft_named": dnamed, "draft_numbered": dnum,
        "calls_port": len(pc), "calls_common": len(pc & dc), "calls_draft": len(dc),
        "calls_only_draft": sorted(fmt(t) if isinstance(t, int) else t for t in dc - pc),
        "calls_only_port": sorted(fmt(t) if isinstance(t, int) else t for t in pc - dc),
        "struct_port": structure_counts(ptext), "struct_draft": structure_counts(logic),
        "lines_hit": hit, "lines": lines, "similarity": sm.ratio(),
        "shuffle_port": len(SHUFFLE.findall(ptext)), "shuffle_draft": len(SHUFFLE.findall(logic)),
        "port_stops": [f"{v:04X}" for v in port_stops(ptext)],
        "draft_left": left_to_do(d, logic),
        "port_lines": len([x for x in ptext.splitlines() if x.strip()]),
        "draft_lines": len([x for x in logic.splitlines() if x.strip()]),
    }


def report(rows: list, out=sys.stdout):
    def s(d):
        return " ".join(f"{k}{v}" for k, v in d.items() if v)
    for addr, m in rows:
        print(f"{fmt(addr)}  {m['port']}", file=out)
        print(f"  accesses   port {m['addr_port']}, in the draft too {m['addr_common']}; draft {m['addr_draft']}"
              f"   named: port {m['port_named']}/{m['port_named'] + m['port_numbered']}, draft "
              f"{m['draft_named']}/{m['draft_named'] + m['draft_numbered']}", file=out)
        print(f"  call-outs  port {m['calls_port']}, in the draft too {m['calls_common']}; draft "
              f"{m['calls_draft']}" + (f"  (draft only: {', '.join(m['calls_only_draft'])})"
                                       if m['calls_only_draft'] else "")
              + (f"  (port only: {', '.join(m['calls_only_port'])})" if m['calls_only_port'] else ""), file=out)
        print(f"  structure  port [{s(m['struct_port'])}]  draft [{s(m['struct_draft'])}]", file=out)
        print(f"  text       port lines with a close draft line {m['lines_hit']}/{m['lines']}, token similarity "
              f"{m['similarity']:.2f}; lines port {m['port_lines']} draft {m['draft_lines']}; register "
              f"operations port {m['shuffle_port']} draft {m['shuffle_draft']}", file=out)
        if m["port_stops"]:
            print(f"  port hands back at {', '.join(m['port_stops'])}", file=out)
        print(f"  left in the draft  {s(m['draft_left']) or 'nothing'}", file=out)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 -m tools.recomp.groundtruth", description=__doc__.split("\n")[0])
    ap.add_argument("addr", nargs="+")
    ap.add_argument("--m", type=int, choices=(8, 16))
    ap.add_argument("--x", type=int, choices=(8, 16))
    ap.add_argument("--show", action="store_true", help="print each draft too")
    ap.add_argument("--rom", type=Path, default=REPO_ROOT / "roms" / "ct-usa.sfc")
    ap.add_argument("--root", type=Path, default=REPO_ROOT,
                    help="where the default coverage and call globs are looked for")
    args = ap.parse_args(argv)
    if not args.rom.exists():
        raise SystemExit(f"{args.rom} not found; stage the ROM with tools/rom.py first")
    addrs = [parse(a) for a in args.addr]
    k = show.Knowledge(Rom(args.rom.read_bytes()), REPO_ROOT / "re" / "hints.toml",
                       show.expand(show.DEFAULT_COVERAGE, args.root), show.expand(show.DEFAULT_CALLS, args.root),
                       REPO_ROOT / "game" / "registry.c", REPO_ROOT / "re" / "ram.toml",
                       [(a, args.m, args.x) for a in addrs])
    rows = []
    for a in addrs:
        port = find_port(a)
        if port is None:
            print(f"{fmt(a)}: no port with a `// ${fmt(a)}` comment in game/", file=sys.stderr)
            continue
        ws = show.entry_widths(k.an, a, args.m, args.x)
        if len(ws) != 1:
            print(f"{fmt(a)}: give --m and --x (widths {ws})", file=sys.stderr)
            continue
        d = show.Draft(k, a, ws[0][0], ws[0][1])
        d.build()
        text = d.render()
        if args.show:
            print(text)
        rows.append((a, measure(k, d, port)))
    report(rows)


if __name__ == "__main__":
    main()
