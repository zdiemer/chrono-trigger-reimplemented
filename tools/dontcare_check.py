"""Check every don't-care declaration in game/ against the liveness analysis.

    python3 tools/dontcare_check.py [--rom roms/ct-usa.sfc] [--root DIR] [--calls GLOB...] [--list] [--tree DIR]

Shadow mode doesn't compare what a port declares dead (docs/PORTING.md,
Modes), so a wrong declaration hides a real difference from the gate, and the
shipping mode then runs with a wrong register. A declaration is a claim about
the original: that nothing reads those registers and flags from a point on
before setting them. tools/disasm/liveness.py knows what the original reads,
so each claim is checked against it, at the point where runtime/port/native.c
applies it:

- a registry entry's `.dont_care` (game/registry.c) is left out of the
  comparison where the port finishes: when it returns, the declared set must
  be dead after the routine it is an entry of returns; and at every stop the
  port declares (below), on top of that stop's own set;
- `m_stop_dont_care(s, at, set)`: the port stops at the original's
  instruction `at`, which must not read `set` (with the entry's) before
  setting it; or `at` is the return instruction the port carries out last, and
  the set must be dead after that return;
- `m_stop_at_own(s, at, set)`: the same, but the set is the stop's own and the
  entry's isn't added there (shadow mode leaves out `set` alone), so `set` alone
  must be dead at `at`, and the entry's set only where the port returns or makes
  a stop that isn't its own;
- `m_jsr_dc(s, site, target, set)`, `m_jsl_dc(...)`, the platform's
  `au_driver_call(pf, PF_CALL(site, target), set)`, a service call made from
  `PF_CALL_DC(site, target, set)`, and the bridges' `bridge_call(s, site,
  target, set, machine)` and `bridge_call_long`: the machines are compared
  when the original reaches `target` from `site`, and the port's registers
  are what the callee runs with, so the set must be dead at the callee's entry
  for that call: neither the callee nor the caller after it returns may read
  it (`Liveness.live_into_call`).

A call-out's claim must also hold without trusting a target set the machine
decides at run time. Where the callee reaches a transfer whose targets are read
from work RAM (a table or pointer there, an operand the program writes) or come
from data (a `derive` hint), liveness follows the set it was argued to be for
the game's own data; but a caller that puts the target there itself (the scene
port stores a script's operand and calls the program's dispatcher) can put
anything there, and the fuzzer does. So each call-out declaration is checked a
second time against liveness solved with every such transfer keeping everything
live (`Liveness(strict=True)`), as is a call whose target the port computes at
a site that is such a transfer. A declaration only the trusting solve supports
is a finding, unless the comment beside it names a fuzz run that scrambled what
it declares at its call-outs and found no difference:

    /* dc-evidence: ct_fuzz --poison-dc ROUTINE[:VARIANT], seed S: N states,
       M call-outs scrambled, 0 mismatched */

(the run's routine, seed and counts in place of the capitals; `--poison-site
BBAAAA` for a run that scrambled one site's call-outs.) The note goes in the
comment block just above the statement that makes the declaration, or on its
lines. The check reads it: the routine (and variant)
must be one tools/fuzz_routines.toml lists and a registered entry that reaches
the declaration, the run must have compared states and scrambled at least
MIN_SCRAMBLED call-outs, and found 0 mismatched. A note where liveness proves
the set without one is a finding too (it would outlive what it stood for).

Where the unproven part of a call-out's set is the overflow flag alone and
fuzzing can't reach it (a callee that waits on frames past any budget), the
note can instead name the static check that follows the callee's tables as far
as their dispatch can reach, and a run of the library that inverted the flag at
the call itself:

    /* dc-evidence: tools/v_readers.py BB:AAAA: V dead, its tables' indexes at
       most $FE; ct_dev --poison-set V at each site over the library, calls with
       V inverted: BBAAAA N, BBAAAA N; 0 differing[; a site under 100 rests on
       the static result] */

The check then runs tools/v_readers.py's solve itself: the callee must be the
call's target and have V dead into it there, every `index_reach` dispatch must
be the shape its bound is argued from, with A's high byte known clear at it,
and every word it reaches traced code; the call's own site must be among the
sites the note counts, V among the set, every trace the same as the
unscrambled run's, and the call's own site must have had V inverted at least
MIN_SCRAMBLED times -- or, where the note says a site under that rests on the
static result, some site of the note must have. tools/dc_evidence.py makes the run, with ct_dev
--index-check over the same library for the indexes, and says where a note's
counts no longer hold (docs/PORTING.md, "Declarations liveness can't prove").

An entry's own set after it returns can carry a note too, for the carry alone,
in the comment block just above the entry in game/registry.c:

    /* dc-evidence: ct_dev --poison-return BBAAAA --poison-set C over the library:
     * N returns, C inverted M; 0 differing */

where liveness has the carry read past the entry's return to a reader whose
result nothing acts on: the run inverted it there in the shipping mode, M at
least MIN_SCRAMBLED times, and no trace differed (tools/dc_evidence.py makes it
again).

The declarations are read from the source, not run: their arguments must be
constants, or built from the parameters of the function they are in (every
call of it in the same file is followed), its locals, and the rows of a table
in the file. A declaration the check can't read is a finding, as is one
liveness doesn't agree with. `DC_A` is the whole accumulator, `DC_AH` its high
byte alone and `DC_AL` its low byte alone; liveness has the two bytes apart.
With `--list`, a declaration that could leave out more than it does says what
(`could add`): liveness has it dead there too.

Needs the ROM and the analysis's inputs (coverage and call logs, as for
tools.recomp.show): more call logs only make liveness more conservative. Exit
status is 0 when every declaration is readable and agrees with liveness (and
every call-out's, with the strict solve or a fuzz note).
"""

import argparse
import glob
import pathlib
import re
import sys
import tomllib
from dataclasses import dataclass, field

ROOT = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.disasm import liveness as _lv  # noqa: E402
from tools.disasm.liveness import ALL, describe  # noqa: E402

HEADERS = ("runtime/port/native.h", "runtime/cpu/cpu65816.h")
DEFAULT_COVERAGE = ("traces/coverage-*.txt", "traces/saves/cov-*.txt")
DEFAULT_CALLS = ("traces/calls-*.txt", "traces/saves/calls-*.txt")
STOPS = ("m_stop_dont_care", "m_stop_at", "m_stop_at_own")
# The bridges' vocabulary (game/field/bridge.h, game/battle/bridge.h,
# game/menu/bridge.h): bridge_call(s, site, target, set, machine) and
# bridge_call_long(...) and bridge_call_on_page (on another page) with field_run and field_run_long (a sibling port run as
# C under the call's frame, or the same call where its guard refuses), the battle's battle_call(s, site, target, set) and
# battle_call_long(...), the menus' menu_call and menu_call_long, and the waits
# that go through the timing service (field_wait_frame, battle_wait_frame,
# battle_wait_line: the same arguments, then their own), are
# m_jsr_dc and m_jsl_dc and a stop of the bridge's own with no set at the
# call's return; their arguments are read where they are called, and the
# helpers' own bodies (BRIDGES) aren't declarations of their own.
DELEGATES = ("bridge_call", "bridge_call_long", "field_run", "field_run_long", "field_run_holding", "field_run_on_page",
             "bridge_call_on_page", "field_run_or_hand_over",
             "battle_call",
             "battle_call_long", "menu_call", "menu_call_long", "menu_call_returning",
             "field_wait_frame", "battle_wait_frame", "battle_wait_line")
BRIDGES = {
    "game/field/bridge.c": ("bridge_call", "bridge_call_long", "bridge_back", "field_run", "field_run_long",
                            "field_run_holding", "field_run_on_page", "bridge_call_on_page", "field_run_or_hand_over",
                            "run_entry", "field_wait_frame"),
    "game/battle/bridge.c": ("battle_call", "battle_call_long", "battle_back", "run_entry", "battle_stop_after_call",
                             "battle_wait_frame", "battle_wait_line"),
    "game/menu/bridge.c": ("menu_call", "menu_call_long", "menu_call_returning", "menu_back"),
}
BRIDGE_FILE, BRIDGE_BODIES = "game/field/bridge.c", BRIDGES["game/field/bridge.c"]
DECLARATIONS = ("m_jsr_dc", "m_jsl_dc", "au_driver_call", "PF_CALL_DC") + STOPS + DELEGATES
CALL_OUTS = ("m_jsr", "m_jsl", "m_jsr_dc", "m_jsl_dc", "au_driver_call", "PF_CALL_DC") + DELEGATES
RETURNS = re.compile(r"\bm_rt[sli]\s*\(")
RETURN_OPCODES = {0x60: "RTS", 0x6B: "RTL", 0x40: "RTI"}
MAX_DEPTH = 6
FUZZ_ROUTINES = "tools/fuzz_routines.toml"
# The evidence a call-out declaration liveness can't prove carries beside it
# (the module's docstring; docs/PORTING.md, "Declarations liveness can't prove").
EVIDENCE_TAG = "dc-evidence:"
EVIDENCE = re.compile(
    r"dc-evidence:\s*ct_fuzz\s+--poison-(?:dc|site\s+[0-9A-Fa-f]{6})\s+(?P<routine>\w+)(?::(?P<variant>\w+))?,"
    r"\s*seed\s+(?P<seed>\d+):\s*(?P<states>\d+)\s+states,\s*(?P<scrambled>\d+)\s+call-outs\s+scrambled,"
    r"\s*(?P<mismatched>\d+)\s+mismatched\b")
# The fewest call-outs a run must have scrambled to count: a run whose states
# mostly stop before the call (a callee past the budget) says little.
MIN_SCRAMBLED = 100
# The other form: the overflow flag, by tools/v_readers.py and a library run.
V_EVIDENCE = re.compile(
    r"dc-evidence:\s*tools/v_readers\.py\s+(?P<target>[0-9A-Fa-f]{2}:[0-9A-Fa-f]{4}):\s*V\s+dead,"
    r"\s*its\s+tables'\s+indexes\s+at\s+most\s+\$FE(?:\s*\([^)]*\))?;"
    r"\s*ct_dev\s+--poison-set\s+(?P<set>[AXYNVZC]+)\s+at\s+each\s+site\s+over\s+the\s+library,"
    r"\s*calls\s+with\s+V\s+inverted:\s*(?P<counts>[0-9A-Fa-f]{6}\s+\d+(?:\s*,\s*[0-9A-Fa-f]{6}\s+\d+)*);"
    r"\s*(?P<differing>\d+)\s+differing"
    r"(?P<static>;\s*a\s+site\s+under\s+100\s+rests\s+on\s+the\s+static\s+result)?")


# The third form: an entry's carry after it returns, by a library run that
# inverted it there in the shipping mode (ct_dev --poison-return), in the
# comment block just above the entry in game/registry.c.
RETURN_EVIDENCE = re.compile(
    r"dc-evidence:\s*ct_dev\s+--poison-return\s+(?P<entry>[0-9A-Fa-f]{6})\s+--poison-set\s+(?P<set>[AXYNVZC]+)"
    r"\s+over\s+the\s+library:\s*(?P<returns>\d+)\s+returns,\s*C\s+inverted\s+(?P<inverted>\d+);"
    r"\s*(?P<differing>\d+)\s+differing")


def v_evidence_counts(m) -> dict:
    """site -> calls with V inverted, from a tools/v_readers.py note."""
    out = {}
    for part in m.group("counts").split(","):
        site, count = part.split()
        out[int(site, 16)] = int(count)
    return out


class Unreadable(Exception):
    """A declaration's argument the check can't evaluate."""


# ---- DC_* sets -----------------------------------------------------------------------------------
# runtime/port/native.h's DC_* bits are liveness's, except that DC_A is the
# whole accumulator (liveness's A and AH) and DC_AL, which liveness has no
# DC_* name for, is liveness's A: the low byte alone.

DC_AL = 1 << 10
DC_WIDTH = 1 << 11      # the accumulator's width, which liveness checks apart (width_read_after_return)


def dead_of_dc(dc: int) -> int:
    """What a DC_* set declares dead, as liveness bits (the width apart)."""
    return _lv.dead_of_dc(dc & ~(DC_AL | DC_WIDTH)) | (_lv.A if dc & DC_AL else 0)


def dc_text(dc: int) -> str:
    names = [n for n in _lv.dc_text(dc & ~(DC_AL | DC_WIDTH)).split(" | ") if n != "0"] + \
        (["DC_AL"] if dc & DC_AL else []) + (["DC_WIDTH"] if dc & DC_WIDTH else [])
    return " | ".join(names) or "0"


def dc_of_dead(dead: int) -> int:
    """The DC_* set that declares exactly these liveness bits dead."""
    out = dead & ~_lv.AW
    if dead & _lv.AW == _lv.AW:
        out |= _lv.A
    elif dead & _lv.AH:
        out |= _lv.AH
    elif dead & _lv.A:
        out |= DC_AL
    return out


# ---- C source ----------------------------------------------------------------------------------

def strip_comments(text: str) -> str:
    """Comments blanked, newlines kept (so offsets and line numbers survive);
    string literals are left as they are."""
    def blank(m):
        return m.group(0) if m.group(0).startswith('"') else re.sub(r"[^\n]", " ", m.group(0))
    return re.sub(r'"(?:\\.|[^"\\\n])*"|/\*.*?\*/|//[^\n]*', blank, text, flags=re.S)


def matching(text: str, i: int, open_: str = "(", close: str = ")") -> int:
    """Index of the bracket closing the one at text[i]."""
    depth = 0
    for j in range(i, len(text)):
        if text[j] == open_:
            depth += 1
        elif text[j] == close:
            depth -= 1
            if depth == 0:
                return j
    raise ValueError("unbalanced")


def split_args(text: str) -> list:
    """Top-level comma-separated parts."""
    out, depth, start = [], 0, 0
    for i, ch in enumerate(text):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            out.append(text[start:i].strip())
            start = i + 1
    if text[start:].strip():
        out.append(text[start:].strip())
    return out


def struct_fields(body: str) -> list:
    """The field names of a struct's body, in order: every declarator of every
    declaration (`uint16_t flag, copy;` is two fields), without its pointer
    stars, array bounds or bit-field width."""
    names = []
    for decl in body.split(";"):
        if not decl.strip():
            continue
        for declarator in split_args(decl):
            declarator = re.sub(r"\[[^\]]*\]|:\s*\w+\s*$", "", declarator)
            words = re.findall(r"[A-Za-z_]\w*", declarator)
            if words:
                names.append(words[-1])
    return names


@dataclass
class Func:
    name: str
    params: list
    start: int          # the body, between its braces
    end: int
    static: bool = False


@dataclass
class Call:
    """A call of `name` in a source: its arguments' text and where it is."""
    name: str
    args: list
    offset: int


KEYWORDS = {"if", "while", "for", "switch", "return", "sizeof", "do", "else"}
TYPE_WORDS = {"uint8_t": 8, "uint16_t": 16, "uint32_t": 32, "int8_t": 8, "int16_t": 16, "int32_t": 32,
              "int": 32, "unsigned": 32, "bool": 1}


class Source:
    """One C file: its macros, enum constants, tables, functions and calls."""

    def __init__(self, path: str, text: str):
        self.path = path
        self.raw = text
        self.text = strip_comments(text)
        self.defines: dict = {}      # name -> (params or None, body)
        self.consts: dict = {}       # enum constant -> expression text
        self.tables: dict = {}       # name -> (field names, [row: {field: expr text}])
        self.funcs: dict = {}        # name -> Func
        self.arrays: list = []       # (name, offset, [element expr text]) of arrays of scalars
        self._scan()

    def line(self, offset: int) -> int:
        return self.text.count("\n", 0, offset) + 1

    def _scan(self):
        t = self.text
        for m in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+(\w+)(\([^)]*\))?((?:\\\n|[^\n])*)", t, re.M):
            params = [p.strip() for p in m.group(2)[1:-1].split(",")] if m.group(2) else None
            self.defines[m.group(1)] = (params, m.group(3).replace("\\\n", " ").strip())
        for m in re.finditer(r"\benum\b\s*\w*\s*\{", t):
            body = t[m.end():matching(t, m.end() - 1, "{", "}")]
            prev = None
            for item in split_args(body):
                name, _, value = item.partition("=")
                name = name.strip()
                if not re.fullmatch(r"\w+", name):
                    continue
                expr = value.strip() if value.strip() else ("0" if prev is None else f"({prev}) + 1")
                self.consts[name] = expr
                prev = name
        # static const tables of struct rows: `...struct { fields } name[] = { rows };`
        for m in re.finditer(r"\bstruct\s*\{([^{}]*)\}\s*(\w+)\s*\[\s*\w*\s*\]\s*=\s*\{", t):
            fields = struct_fields(m.group(1))
            end = matching(t, m.end() - 1, "{", "}")
            rows = []
            for row in split_args(t[m.end():end]):
                row = re.sub(r"^\[[^\]]*\]\s*=\s*", "", row).strip()
                if not row.startswith("{"):
                    continue
                values = split_args(row[1:matching(row, 0, "{", "}")])
                rows.append(dict(zip(fields, values)))
            self.tables[m.group(2)] = (fields, rows)
        for m in re.finditer(r"\b(\w+)\s*\[\s*\w*\s*\]\s*=\s*\{", t):
            end = matching(t, m.end() - 1, "{", "}")
            values = split_args(t[m.end():end])
            if values and not any(v.startswith(("{", "[", ".")) for v in values):
                self.arrays.append((m.group(1), m.start(), values))
        # function definitions at the top level: a line starting with a type and
        # a name, parameters, then the body's brace
        for m in re.finditer(r"^((?:[A-Za-z_]\w*[ \t\*]+)+)(\w+)[ \t]*\(", t, re.M):
            name = m.group(2)
            if name in KEYWORDS or set(m.group(1).split()) & KEYWORDS:
                continue
            try:
                close = matching(t, m.end() - 1)
            except ValueError:
                continue
            rest = t[close + 1:close + 200]
            if not rest.lstrip().startswith("{"):
                continue
            open_ = close + 1 + (len(rest) - len(rest.lstrip()))
            params = []
            for p in split_args(t[m.end():close]):
                pointer = re.search(r"\(\s*\*\s*(\w+)\s*\)", p)
                words = re.findall(r"\w+", p)
                if pointer:
                    params.append(pointer.group(1))
                elif words and words != ["void"]:
                    params.append(words[-1])
            self.funcs[name] = Func(name, params, open_ + 1, matching(t, open_, "{", "}"),
                                    "static" in m.group(1).split())

    def func_at(self, offset: int):
        for f in self.funcs.values():
            if f.start <= offset < f.end:
                return f
        return None

    def calls(self, names) -> list:
        out = []
        for m in re.finditer(r"\b(%s)\s*\(" % "|".join(map(re.escape, names)), self.text):
            open_ = m.end() - 1
            try:
                close = matching(self.text, open_)
            except ValueError:
                continue
            line_start = self.text.rfind("\n", 0, m.start()) + 1
            prefix = self.text[line_start:m.start()]
            words = prefix.split()
            if words and re.fullmatch(r"[\w\s\*]+", prefix) and words[-1] not in ("return", "else", "case", "do"):
                continue      # its definition or a prototype
            out.append(Call(m.group(1), split_args(self.text[open_ + 1:close]), m.start()))
        return out

    def notes_at(self, offset: int) -> str:
        """The comment text beside the call at offset: the comment block just
        above the statement it is in, and comments on the statement's lines,
        joined into one line."""
        t = self.text
        start = max(t.rfind(";", 0, offset), t.rfind("{", 0, offset), t.rfind("}", 0, offset)) + 1
        while start < offset and t[start].isspace():
            start += 1
        try:
            end = matching(t, t.index("(", offset))
        except ValueError:
            end = offset
        lines = self.raw.split("\n")
        first, last = self.line(start), self.line(end)
        block = lines[first - 1:last]
        k = first - 2
        while k >= 0:
            stripped = lines[k].strip()
            if not (stripped.startswith(("//", "/*", "*")) or stripped.endswith("*/")):
                break
            block.insert(0, lines[k])
            k -= 1
        text = " ".join(block)
        text = re.sub(r"/\*|\*/|//|^\s*\*|\s\*\s", " ", text)
        return " ".join(text.split())

    def called_names(self, f: Func) -> set:
        return set(re.findall(r"\b(\w+)\s*\(", self.text[f.start:f.end])) - KEYWORDS

    def locals_of(self, f: Func, name: str) -> list:
        """The expressions a local of f is given, by declaration or assignment."""
        body = self.text[f.start:f.end]
        return [m.group(1) for m in re.finditer(r"(?<![\w.>])%s\s*=(?!=)\s*([^;]*);" % re.escape(name), body)]


# ---- expressions -------------------------------------------------------------------------------

TOKEN = re.compile(r"\s*(0[xX][0-9A-Fa-f]+[uUlL]*|\d+[uUlL]*|\w+|->|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^~!<>?:()\[\].,{}])")


def tokenize(text: str) -> list:
    out, i = [], 0
    text = text.strip()
    while i < len(text):
        m = TOKEN.match(text, i)
        if not m:
            raise Unreadable(f"can't read {text!r}")
        out.append(m.group(1))
        i = m.end()
    return out


BINARY = {"||": 1, "&&": 2, "|": 3, "^": 4, "&": 5, "==": 6, "!=": 6, "<": 7, "<=": 7, ">": 7, ">=": 7,
          "<<": 8, ">>": 8, "+": 9, "-": 9, "*": 10, "/": 10, "%": 10}


class Parser:
    def __init__(self, text: str):
        self.toks = tokenize(text)
        self.i = 0
        self.text = text

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else None

    def take(self, want=None):
        tok = self.peek()
        if tok is None or (want is not None and tok != want):
            raise Unreadable(f"can't read {self.text!r}")
        self.i += 1
        return tok

    def parse(self):
        e = self.expr()
        if self.peek() is not None:
            raise Unreadable(f"can't read {self.text!r}")
        return e

    def expr(self):
        c = self.binary(1)
        if self.peek() == "?":
            self.take()
            a = self.expr()
            self.take(":")
            b = self.expr()
            return ("cond", c, a, b)
        return c

    def binary(self, level):
        left = self.unary()
        while self.peek() in BINARY and BINARY[self.peek()] >= level:
            op = self.take()
            right = self.binary(BINARY[op] + 1)
            left = ("bin", op, left, right)
        return left

    def unary(self):
        tok = self.peek()
        if tok in ("~", "!", "-", "+"):
            self.take()
            return ("un", tok, self.unary())
        if tok == "(" and self.i + 2 < len(self.toks) and self.toks[self.i + 1] in TYPE_WORDS \
                and self.toks[self.i + 2] == ")":
            self.take()
            bits = TYPE_WORDS[self.take()]
            self.take(")")
            return ("cast", bits, self.unary())
        return self.postfix(self.primary())

    def primary(self):
        tok = self.take()
        if tok == "(":
            e = self.expr()
            self.take(")")
            return e
        if re.fullmatch(r"\d\w*|0[xX]\w+", tok):
            return ("num", int(tok.rstrip("uUlL"), 0))
        if re.fullmatch(r"\w+", tok):
            if self.peek() == "(":
                self.take()
                args = []
                while self.peek() != ")":
                    args.append(self.expr())
                    if self.peek() == ",":
                        self.take()
                self.take(")")
                return ("call", tok, args)
            return ("id", tok)
        raise Unreadable(f"can't read {self.text!r}")

    def postfix(self, e):
        while self.peek() in ("[", ".", "->"):
            tok = self.take()
            if tok == "[":
                start = self.i
                idx = self.expr()
                text = " ".join(self.toks[start:self.i])
                self.take("]")
                e = ("index", e, idx, text)
            else:
                e = ("field", e, self.take())
        return e


def parse(text: str):
    return Parser(text).parse()


def _substitute(node, env: dict):
    """A macro body's parse with its parameters replaced by the arguments'."""
    if node[0] == "id" and node[1] in env:
        return env[node[1]]
    return tuple(_substitute(x, env) if isinstance(x, tuple) else
                 ([_substitute(a, env) for a in x] if isinstance(x, list) else x) for x in node)


def merge(a: dict, b: dict):
    """Two sets of choices, or None if they disagree."""
    out = dict(a)
    for k, v in b.items():
        if out.get(k, v) != v:
            return None
        out[k] = v
    return out


def apply(op, x, y=None):
    if op == "||":
        return int(bool(x) or bool(y))
    if op == "&&":
        return int(bool(x) and bool(y))
    return {"|": lambda: x | y, "^": lambda: x ^ y, "&": lambda: x & y, "==": lambda: int(x == y),
            "!=": lambda: int(x != y), "<": lambda: int(x < y), "<=": lambda: int(x <= y),
            ">": lambda: int(x > y), ">=": lambda: int(x >= y), "<<": lambda: x << y, ">>": lambda: x >> y,
            "+": lambda: x + y, "-": lambda: x - y, "*": lambda: x * y,
            "/": lambda: x // y if y else 0, "%": lambda: x % y if y else 0}[op]()


class Evaluator:
    """Values of expressions in a function of a source, as a list of (value,
    choices): choices say which caller supplied a parameter and which row of a
    table a lookup took, so values that came from the same call or row stay
    together."""

    def __init__(self, sources: list, globals_: list):
        self.sources = sources
        self.globals = globals_          # sources whose constants and macros every file sees
        self.only_callers = None         # a parameter's values from calls in these functions alone

    def const(self, src: Source, name: str):
        for s in [src] + self.globals:
            if name in s.consts:
                return s, s.consts[name]
            if name in s.defines and s.defines[name][0] is None:
                return s, s.defines[name][1]
        return None

    def macro(self, src: Source, name: str):
        for s in [src] + self.globals:
            if name in s.defines and s.defines[name][0] is not None:
                return s.defines[name]
        return None

    def values(self, nodes: list, src: Source, f: Func | None, depth: int = 0) -> list:
        """[(tuple of values, choices)] for nodes evaluated together."""
        out = [((), {})]
        for node in nodes:
            nxt = []
            for vals, ch in out:
                for v, c in self.eval(node, src, f, depth, ch):
                    m = merge(ch, c)
                    if m is not None:
                        nxt.append((vals + (v,), m))
            out = nxt
        seen, unique = set(), []
        for vals, ch in out:
            key = (vals, tuple(sorted(ch.items())))
            if key not in seen:
                seen.add(key)
                unique.append((vals, ch))
        return unique

    def eval(self, node, src, f, depth, choices) -> list:
        kind = node[0]
        if kind == "num":
            return [(node[1], {})]
        if kind == "id":
            return self.ident(node[1], src, f, depth, choices)
        if kind == "un":
            return [({"~": lambda v: ~v, "!": lambda v: int(not v), "-": lambda v: -v, "+": lambda v: v}[node[1]](v), c)
                    for v, c in self.eval(node[2], src, f, depth, choices)]
        if kind == "cast":
            bits = node[1]
            return [((int(bool(v)) if bits == 1 else v & ((1 << bits) - 1)), c)
                    for v, c in self.eval(node[2], src, f, depth, choices)]
        if kind == "bin":
            out = []
            for (x, y), c in self.values([node[2], node[3]], src, f, depth):
                if merge(c, choices) is not None:
                    out.append((apply(node[1], x, y), c))
            return out
        if kind == "cond":
            out = []
            try:
                conds = self.eval(node[1], src, f, depth, choices)
            except Unreadable:
                conds = [(1, {}), (0, {})]     # decided at run time: either value
            for cv, c in conds:
                within = merge(choices, c)
                if within is None:
                    continue
                for v, c2 in self.eval(node[2] if cv else node[3], src, f, depth, within):
                    m = merge(c, c2)
                    if m is not None:
                        out.append((v, m))
            return out
        if kind == "field" and node[1][0] == "index" and node[1][1][0] == "id":
            return self.table_field(node[1][1][1], node[1][2], node[1][3], node[2], src, f, depth, choices)
        if kind == "index" and node[1][0] == "id":
            return self.element(node[1][1], node[3], src, f, depth, choices)
        if kind == "call":
            macro = self.macro(src, node[1])
            if macro is not None and len(macro[0]) == len(node[2]):
                params, body = macro
                env = dict(zip(params, node[2]))
                return self.eval(_substitute(parse(body), env), src, f, depth, choices)
        raise Unreadable(f"can't evaluate {node!r}")

    def mentions(self, node, src, prefix: str = "RAM_", depth: int = 0) -> bool:
        """Whether an expression spells a name starting with prefix, through
        the macros and constants it uses."""
        if depth > 8:
            return False
        kind = node[0]
        if kind == "id":
            if node[1].startswith(prefix):
                return True
            c = self.const(src, node[1])
            return c is not None and self.mentions(parse(c[1]), c[0], prefix, depth + 1)
        if kind == "call":
            macro = self.macro(src, node[1])
            if macro is not None and prefix in macro[1]:
                return True
            return any(self.mentions(a, src, prefix, depth + 1) for a in node[2])
        return any(isinstance(x, tuple) and self.mentions(x, src, prefix, depth + 1) for x in node[1:])

    def ident(self, name, src, f, depth, choices) -> list:
        if name in ("true", "false"):
            return [(int(name == "true"), {})]
        if f is not None:
            defs = src.locals_of(f, name)
            if name in f.params and not defs:
                return self.param(name, src, f, depth, choices)
            if defs:
                if len(set(defs)) != 1:
                    raise Unreadable(f"{name} is set more than once in {f.name}")
                return self.eval(parse(defs[0]), src, f, depth, choices)
        c = self.const(src, name)
        if c is not None:
            return self.eval(parse(c[1]), c[0], None, depth, choices)
        raise Unreadable(f"{name} isn't a constant, a local or a parameter")

    def param(self, name, src, f, depth, choices) -> list:
        """A parameter's values: what every call of f in the same file passes."""
        if depth >= MAX_DEPTH:
            raise Unreadable(f"{name}: callers nested too deep")
        idx = f.params.index(name)
        key = ("caller", src.path, f.name)
        out = []
        calls = [c for c in src.calls([f.name]) if self._caller_ok(src, c)]
        if not calls:
            raise Unreadable(f"{name}: nothing in {src.path} calls {f.name}")
        for i, call in enumerate(calls):
            if key in choices and choices[key] != i:
                continue
            if idx >= len(call.args):
                raise Unreadable(f"{name}: a call of {f.name} passes fewer arguments")
            caller = src.func_at(call.offset)
            for v, c in self.eval(parse(call.args[idx]), src, caller, depth + 1, {}):
                m = merge(c, {key: i})
                if m is not None:
                    out.append((v, m))
        return out

    def element(self, name, idx_text, src, f, depth, choices) -> list:
        """An element of an array of constants, whichever the index picks: the
        function's own array of that name, else the file's, else (a
        parameter) the arrays its callers pass."""
        arrays = [a for a in src.arrays if a[0] == name]
        own = [a for a in arrays if f is not None and f.start <= a[1] < f.end]
        if f is not None and name in f.params and not own:
            return self.param_array(name, idx_text, src, f, depth, choices)
        pick = own or [a for a in arrays if not any(g.start <= a[1] < g.end for g in src.funcs.values())]
        if len(pick) != 1:
            raise Unreadable(f"{name}[...] isn't an array of constants in {src.path}")
        _, offset, values = pick[0]
        key = ("element", src.path, name, offset, idx_text)
        out = []
        for i, text in enumerate(values):
            if key in choices and choices[key] != i:
                continue
            for v, c in self.eval(parse(text), src, None, depth, {}):
                m = merge(c, {key: i})
                if m is not None:
                    out.append((v, m))
        return out

    def param_array(self, name, idx_text, src, f, depth, choices) -> list:
        """An element of an array a parameter points at: the arrays every call
        of f in the file passes."""
        if depth >= MAX_DEPTH:
            raise Unreadable(f"{name}: callers nested too deep")
        idx = f.params.index(name)
        key = ("caller", src.path, f.name)
        out = []
        for i, call in enumerate(src.calls([f.name])):
            if (key in choices and choices[key] != i) or not self._caller_ok(src, call):
                continue
            arg = call.args[idx].strip()
            if not re.fullmatch(r"\w+", arg):
                raise Unreadable(f"{name}: {f.name} is passed {arg!r}")
            for v, c in self.element(arg, idx_text, src, src.func_at(call.offset), depth + 1, {}):
                m = merge(c, {key: i})
                if m is not None:
                    out.append((v, m))
        return out

    def _caller_ok(self, src, call) -> bool:
        if self.only_callers is None:
            return True
        f = src.func_at(call.offset)
        return f is not None and f.name in self.only_callers

    def table_field(self, table, idx, idx_text, fieldname, src, f, depth, choices) -> list:
        if table not in src.tables:
            raise Unreadable(f"{table}[...] isn't a table in {src.path}")
        fields, rows = src.tables[table]
        if fieldname not in fields:
            raise Unreadable(f"{table} has no field {fieldname}")
        key = ("row", src.path, table, idx_text, f.name if f else None)
        out = []
        for i, row in enumerate(rows):
            if key in choices and choices[key] != i:
                continue
            if fieldname not in row:
                continue
            for v, c in self.eval(parse(row[fieldname]), src, None, depth, {}):
                m = merge(c, {key: i})
                if m is not None:
                    out.append((v, m))
        return out


# ---- the declarations --------------------------------------------------------------------------

@dataclass
class Entry:
    addr: int
    name: str
    fn: str
    dont_care: int
    idle: bool
    generated: bool
    line: int
    note: str = ""


@dataclass
class Decl:
    kind: str           # "stop" or "call"
    path: str
    line: int
    func: str | None
    dont_care: int
    at: int = 0         # stop: the instruction
    site: int = 0       # call: the JSR/JSL and its target
    target: int = 0
    own: bool = False   # stop: the set is the stop's own (m_stop_at_own), the entry's not added
    offset: int = 0     # where the declaring call is in its source (for the notes beside it)


@dataclass
class Finding:
    where: str
    what: str
    extra: list = field(default_factory=list)

    def text(self) -> str:
        return f"{self.where}: {self.what}"


def expand_macros(text: str, defines: dict) -> str:
    """Function-like and object-like macros expanded (#x stringified), until
    nothing changes; enough for a table of initializers."""
    for _ in range(8):
        before = text
        for name, (params, body) in defines.items():
            if params is None:
                if name.isupper() or name.startswith("NEEDS_"):
                    text = re.sub(r"\b%s\b" % re.escape(name), lambda m, b=body: b, text)
                continue
            pos = 0
            while True:
                m = re.search(r"\b%s\s*\(" % re.escape(name), text[pos:])
                if not m:
                    break
                open_ = pos + m.end() - 1
                close = matching(text, open_)
                args = split_args(text[open_ + 1:close])
                out = body
                for p, a in zip(params, args):
                    out = re.sub(r"#\s*%s\b" % re.escape(p), lambda m, a=a: '"%s"' % a, out)
                    out = re.sub(r"\b%s\b" % re.escape(p), lambda m, a=a: a, out)
                text = text[:pos + m.start()] + out + text[close + 1:]
                pos = pos + m.start() + len(out)
        if text == before:
            break
    return text


def registry_entries(src: Source, ev: Evaluator) -> tuple:
    """The entries of game/registry.c's table, and findings for ones it can't read."""
    raw = strip_comments(src.raw)
    m = re.search(r"NativeEntry\s+\w+\s*\[\s*\]\s*=\s*\{", raw)
    if not m:
        return [], [Finding(src.path, "no NativeEntry table")]
    end = matching(raw, m.end() - 1, "{", "}")
    body = raw[m.end():end]
    body = re.sub(r"^[ \t]*#[ \t]*(define|undef)\b(?:\\\n|[^\n])*", "", body, flags=re.M)
    table = expand_macros(body, src.defines)
    entries, findings = [], []
    for item in split_args(table):
        if not item.startswith("{"):
            continue
        fields = {}
        for part in split_args(item[1:matching(item, 0, "{", "}")]):
            fm = re.match(r"\.(\w+)\s*=\s*(.*)$", part, re.S)
            if fm:
                fields[fm.group(1)] = fm.group(2).strip()
        name = fields.get("name", "?").strip('"')
        try:
            addr = only(ev.values([parse(fields["addr"])], src, None), "the address")
            dc = only(ev.values([parse(fields["dont_care"])], src, None), "the set") if "dont_care" in fields else 0
        except (Unreadable, KeyError) as e:
            findings.append(Finding(f"{src.path}: {name}", f"can't read the entry: {e}"))
            continue
        at = re.search(r"\b%s\b" % re.escape(name), src.raw)
        line = src.line(at.start()) if at else 0
        entries.append(Entry(addr, name, fields.get("fn", name).strip(), dc, fields.get("idle") == "true",
                             fields.get("generated") == "true", line, comment_above(src.raw, line)))
    return entries, findings


def comment_above(raw: str, line: int) -> str:
    """The comment block just above a line (1-based), joined into one line."""
    lines = raw.split("\n")
    block, k = [], line - 2
    while k >= 0:
        stripped = lines[k].strip()
        if not (stripped.startswith(("//", "/*", "*")) or stripped.endswith("*/")):
            break
        block.insert(0, lines[k])
        k -= 1
    text = re.sub(r"/\*|\*/|//|^\s*\*|\s\*\s", " ", " ".join(block))
    return " ".join(text.split())


def return_evidence_problem(m, e, bad: int) -> str | None:
    """Why a ct_dev --poison-return note doesn't stand for the entry e's set
    after it returns, where liveness has `bad` read, or None."""
    if bad & ~_lv.C:
        return f"its dc-evidence inverts and counts the carry alone, not {describe(bad & ~_lv.C)}"
    named = int(m.group("entry"), 16)
    if named != e.addr:
        return f"its dc-evidence names {fmt(named)}, not the entry's {fmt(e.addr)}"
    if "C" not in m.group("set"):
        return "its dc-evidence run's --poison-set leaves C alone"
    if int(m.group("differing")):
        return f"its dc-evidence run had {m.group('differing')} trace(s) differing"
    if int(m.group("inverted")) < MIN_SCRAMBLED:
        return f"its dc-evidence run inverted C {m.group('inverted')} times, fewer than {MIN_SCRAMBLED}"
    return None


def only(values: list, what: str) -> int:
    got = {v[0][0] for v in values}
    if len(got) != 1:
        raise Unreadable(f"{what} has {len(got)} values")
    return got.pop()


def declarations(sources: list, ev: Evaluator) -> tuple:
    """Every stop and call-out declaration, one per value its arguments can
    take, and findings for the ones that can't be read."""
    decls, findings = [], []
    for src in sources:
        for call in src.calls(DECLARATIONS):
            f = src.func_at(call.offset)
            if f is not None and f.name in BRIDGES.get(src.path, ()):
                continue
            where = f"{src.path}:{src.line(call.offset)}"
            try:
                if call.name in STOPS:
                    if len(call.args) != 3:
                        raise Unreadable(f"{call.name} takes (s, at, dont_care)")
                    nodes = [parse(call.args[1]), parse(call.args[2])]
                elif call.name == "au_driver_call":
                    origin = parse(call.args[1])
                    if origin[0] != "call" or origin[1] != "PF_CALL":
                        raise Unreadable("au_driver_call's origin isn't PF_CALL(site, target)")
                    nodes = [origin[2][0], origin[2][1], parse(call.args[2])]
                elif call.name == "PF_CALL_DC":
                    if len(call.args) != 3:
                        raise Unreadable("PF_CALL_DC takes (site, target, dont_care)")
                    nodes = [parse(a) for a in call.args]
                else:
                    nodes = [parse(a) for a in call.args[1:4]]
                try:
                    vals = ev.values(nodes, src, f)
                except Unreadable:
                    if call.name in STOPS:
                        raise
                    # a target computed at run time (a jump table's): the original's call at the site says
                    vals = [((v[0], None, v[1]), c) for v, c in ev.values([nodes[0], nodes[2]], src, f)]
                if not vals:
                    raise Unreadable("no values")
            except Unreadable as e:
                findings.append(Finding(where, f"can't read {call.name}: {e}"))
                continue
            for v, _ in vals:
                if call.name in STOPS:
                    decls.append(Decl("stop", src.path, src.line(call.offset), f.name if f else None, v[1] & 0xFFFF,
                                      at=v[0] & 0xFFFFFF, own=call.name == "m_stop_at_own", offset=call.offset))
                else:
                    decls.append(Decl("call", src.path, src.line(call.offset), f.name if f else None, v[2] & 0xFFFF,
                                      site=v[0] & 0xFFFFFF, target=None if v[1] is None else v[1] & 0xFFFFFF,
                                      offset=call.offset))
    return decls, findings


def reaching(sources: list, fn: str) -> set:
    """(path, function) pairs the function reaches by C calls, itself
    included. A name is the same file's function if it has one, else a
    function of that name that isn't static."""
    public = {}
    for src in sources:
        for name, f in src.funcs.items():
            if not f.static:
                public.setdefault(name, []).append(src)
    out = set()
    work = [(src, fn) for src in public.get(fn, [])]
    while work:
        src, name = work.pop()
        if (src.path, name) in out:
            continue
        out.add((src.path, name))
        for callee in src.called_names(src.funcs[name]):
            if callee in src.funcs:
                work.append((src, callee))
            else:
                work += [(s, callee) for s in public.get(callee, [])]
    return out


# ---- the check ---------------------------------------------------------------------------------

def fmt(addr: int) -> str:
    return f"${addr >> 16:02X}:{addr & 0xFFFF:04X}"


def excess(declared_dc: int, live: int) -> int:
    """The declared-dead registers liveness says may be read (liveness bits)."""
    return dead_of_dc(declared_dc) & live


def verdict(declared_dc: int, live: int) -> str:
    """A --list row's verdict: what liveness has read of the set, or ok, with
    what else it has dead there."""
    bad = excess(declared_dc, live)
    if bad:
        return "reads " + describe(bad)
    more = ALL & ~live & ~dead_of_dc(declared_dc)
    return "ok" + (f" (could add {dc_text(dc_of_dead(more))})" if more else "")


def call_out_targets(src: Source, funcs, ev: Evaluator) -> set:
    """The routines the call-outs in the named functions of src reach
    (m_jsr, m_jsl, their _dc forms and au_driver_call), followed through the
    file's helpers as the declarations are; "computed" for one that can't be
    read."""
    out = set()
    saved, ev.only_callers = ev.only_callers, set(funcs)
    for call in src.calls(CALL_OUTS):
        f = src.func_at(call.offset)
        if f is None or f.name not in funcs:
            continue
        try:
            if call.name == "au_driver_call":
                origin = parse(call.args[1])
                if origin[0] != "call" or origin[1] != "PF_CALL":
                    raise Unreadable("not PF_CALL")
                node = origin[2][1]
            elif call.name == "PF_CALL_DC":
                node = parse(call.args[1])
            else:
                node = parse(call.args[2])
            out |= {v[0] & 0xFFFFFF for v, _ in ev.values([node], src, f)}
        except (Unreadable, IndexError):
            out.add("computed")
    ev.only_callers = saved
    return out


def site_targets(lv, site: int) -> list:
    """What the original's call at site calls (a jump table's targets too)."""
    out = set()
    for key in lv.contexts_at(site):
        for insn in lv.an.code[site].values():
            if insn.name in ("JSR", "JSL"):
                out.update(t for t in lv._callees(site, insn) if t is not None)
    return sorted(out)


def site_dynamic(lv, site: int) -> str | None:
    """Why the original's instruction at site has targets the machine decides
    at run time (Liveness.dynamic_transfer), or None."""
    for insn in lv.an.code.get(site, {}).values():
        why = lv.dynamic_transfer(site, insn)
        if why is not None:
            return why
    return None


def v_evidence_problem(m, d, target: int, unproven: int, bounded) -> str | None:
    """Why a tools/v_readers.py note doesn't stand for the call d to target, or
    None. `bounded` gives tools/v_readers.py's solve (made once, when needed)."""
    from tools import v_readers
    if unproven & ~_lv.V:
        return f"its dc-evidence is tools/v_readers.py's, which speaks for V alone, not {describe(unproven & ~_lv.V)}"
    named = int(m.group("target").replace(":", ""), 16)
    if named != target:
        return f"its dc-evidence names {fmt(named)}, not the call's target {fmt(target)}"
    counts = v_evidence_counts(m)
    if d.site not in counts:
        return f"its dc-evidence run didn't poison the call's own site {fmt(d.site)}"
    if "V" not in m.group("set"):
        return "its dc-evidence run's --poison-set leaves V alone"
    if int(m.group("differing")):
        return f"its dc-evidence run had {m.group('differing')} trace(s) differing"
    if counts[d.site] < MIN_SCRAMBLED:
        if not m.group("static"):
            return (f"its dc-evidence run inverted V {counts[d.site]} times at {fmt(d.site)}, fewer than "
                    f"{MIN_SCRAMBLED}, and the note doesn't rest that site on the static result")
        if max(counts.values()) < MIN_SCRAMBLED:
            return f"its dc-evidence run inverted V at no site of the note {MIN_SCRAMBLED} times"
    lv, problems = bounded()
    if problems:
        return "tools/v_readers.py can't bound its tables: " + "; ".join(problems)
    if not v_readers.v_dead_into(lv, target):
        return f"tools/v_readers.py has V read into {fmt(target)}"
    return None


def evidence_problem(note, d, entries: list, reach: dict, sources: list, fuzz_routines: set) -> str | None:
    """Why a dc-evidence note doesn't stand for the declaration d, or None."""
    m = EVIDENCE.search(note)
    if not m:
        return ("its dc-evidence note isn't in the form `dc-evidence: ct_fuzz --poison-dc ROUTINE[:VARIANT], "
                "seed S: N states, M call-outs scrambled, 0 mismatched`")
    routine, variant = m.group("routine"), m.group("variant") or ""
    if (routine, variant) not in fuzz_routines:
        return f"its dc-evidence names {routine}{':' + variant if variant else ''}, which {FUZZ_ROUTINES} doesn't list"
    named = [e for e in entries if e.name == routine]
    if not named:
        return f"its dc-evidence names {routine}, which isn't a registered entry"
    for e in named:
        if e.name not in reach:
            reach[e.name] = reaching(sources, e.fn)
    if not any((d.path, d.func) in reach[e.name] for e in named):
        return f"its dc-evidence names {routine}, which doesn't reach {d.func}"
    if int(m.group("mismatched")):
        return f"its dc-evidence run mismatched {m.group('mismatched')} state(s)"
    if not int(m.group("states")):
        return "its dc-evidence run compared no states"
    if int(m.group("scrambled")) < MIN_SCRAMBLED:
        return (f"its dc-evidence run scrambled {m.group('scrambled')} call-outs, fewer than {MIN_SCRAMBLED} "
                "(most of its states stop before the call: narrow them)")
    return None


def check(entries: list, decls: list, sources: list, lv, opcode_at, strict=None,
          fuzz_routines: set | None = None) -> tuple:
    """(findings, rows): rows describe every declaration checked, for --list.
    With `strict` (liveness solved with Liveness(strict=True)), a call-out's
    set must hold there too, or carry a dc-evidence note."""
    findings, rows = [], []
    reach = {}
    for e in entries:
        if e.idle or e.generated:
            continue
        reach[e.name] = reaching(sources, e.fn)
    by_source = {src.path: src for src in sources}
    noted = {}          # (path, offset) -> whether some value of the declaration needed its note
    solved = {}

    def bounded():
        """tools/v_readers.py's solve, made the first time a note names it."""
        if "lv" not in solved:
            from tools import v_readers
            solved["lv"] = v_readers.solve(lv.an, getattr(lv, "dynamic_calls", {}))
        return solved["lv"]

    def entry_ok(e, point_live, where, what):
        bad = excess(e.dont_care, point_live)
        rm = RETURN_EVIDENCE.search(e.note) if EVIDENCE_TAG in e.note else None
        if rm and not bad:
            findings.append(Finding(where, f"{e.name}'s dc-evidence stands for a set liveness proves {what}"))
            return
        if rm:
            problem = return_evidence_problem(rm, e, bad)
            if problem is None:
                rows.append(f"{where}: {e.name} {dc_text(e.dont_care)} {what}: ok by a library run "
                            f"(liveness has {describe(bad)} read)")
                return
            rows.append(f"{where}: {e.name} {dc_text(e.dont_care)} {what}: unproven: {problem}")
            findings.append(Finding(where, f"{e.name}'s .dont_care ({dc_text(e.dont_care)}) {what}, where liveness "
                                           f"has {describe(bad)} read, and {problem}"))
            return
        rows.append(f"{where}: {e.name} {dc_text(e.dont_care)} {what}: " + verdict(e.dont_care, point_live))
        if bad:
            findings.append(Finding(where, f"{e.name}'s .dont_care ({dc_text(e.dont_care)}) {what}, "
                                           f"where liveness has {describe(bad)} read"))

    by_path = {src.path: src for src in sources}
    for e in entries:
        if e.idle or e.generated or not e.dont_care:
            continue
        returns = any(RETURNS.search(by_path[path].text[f.start:f.end])
                      for path, name in reach[e.name] for f in [by_path[path].funcs[name]])
        if not returns:
            continue      # it only stops, at the points it declares
        live = lv.live_after_return(e.addr)
        where = f"game/registry.c:{e.line}"
        if live is None:
            findings.append(Finding(where, f"{e.name}: {fmt(e.addr)} isn't code the analysis knows"))
            continue
        entry_ok(e, live, where, f"after {fmt(e.addr)}'s routine returns")
        if e.dont_care & DC_WIDTH and lv.width_read_after_return(e.addr):
            findings.append(Finding(where, f"{e.name}'s .dont_care has DC_WIDTH, where a caller of {fmt(e.addr)} "
                                           "may depend on the accumulator's width before setting it"))

    for d in decls:
        where = f"{d.path}:{d.line}"
        if d.dont_care & DC_WIDTH:
            findings.append(Finding(where, "DC_WIDTH is for an entry's own set where it returns, not a stop's "
                                           "or a call-out's"))
            continue
        if d.kind == "call":
            targets = [d.target] if d.target is not None else site_targets(lv, d.site)
            if not targets:
                findings.append(Finding(where, f"the call from {fmt(d.site)}: its target is computed, and the "
                                               "original's instruction there isn't a call the analysis resolved"))
                continue
            src = by_source.get(d.path)
            note = src.notes_at(d.offset) if src is not None else ""
            has_note = EVIDENCE_TAG in note
            if has_note:
                noted.setdefault((d.path, d.offset), False)
            computed_why = site_dynamic(lv, d.site) if d.target is None else None
            for target in targets:
                live = lv.callee_reads(target)
                bad = excess(d.dont_care, live)
                row = f"{where}: call {fmt(d.site)} -> {fmt(target)} {dc_text(d.dont_care)}: "
                if bad:
                    rows.append(row + verdict(d.dont_care, live))
                    findings.append(Finding(where, f"the call from {fmt(d.site)} to {fmt(target)} declares "
                                                   f"{dc_text(d.dont_care)}, where liveness has {describe(bad)} read"))
                    continue
                strict_live = None if strict is None else (ALL if computed_why else strict.callee_reads(target))
                strict_bad = 0 if strict_live is None else excess(d.dont_care, strict_live)
                if not strict_bad:
                    rows.append(row + verdict(d.dont_care, live if strict_live is None else live | strict_live))
                    continue
                if computed_why:
                    how = f"the port computes the target of the transfer at {fmt(d.site)}: {computed_why}"
                else:
                    sites = lv.dynamic_sites(target)
                    pc = min(sites) if sites else None
                    how = (f"the callee reaches {fmt(pc)}: {sites[pc]}" if pc is not None
                           else "the callee reaches a transfer whose targets are decided at run time")
                unproven = (f"liveness has it dead only by trusting a target set decided at run time ({how}); "
                            f"without that, {describe(strict_bad)} may be read")
                if has_note:
                    noted[(d.path, d.offset)] = True
                    vm = V_EVIDENCE.search(note)
                    if vm:
                        problem = v_evidence_problem(vm, d, target, strict_bad, bounded)
                    else:
                        problem = evidence_problem(note, d, entries, reach, sources, fuzz_routines or set())
                    if problem is None:
                        rows.append(row + f"ok by {'tools/v_readers.py and a library run' if vm else 'fuzz evidence'} "
                                          f"({unproven})")
                        continue
                    rows.append(row + "unproven: " + problem)
                    findings.append(Finding(where, f"the call from {fmt(d.site)} to {fmt(target)} declares "
                                                   f"{dc_text(d.dont_care)}: {unproven}, and {problem}"))
                    continue
                rows.append(row + f"unproven ({unproven})")
                findings.append(Finding(where, f"the call from {fmt(d.site)} to {fmt(target)} declares "
                                               f"{dc_text(d.dont_care)}: {unproven}. Pass what may be read, or fuzz "
                                               "the call-out with ct_fuzz --poison-dc and note the run beside it "
                                               "(dc-evidence, docs/PORTING.md)"))
            continue
        # a stop: the entries that reach it add their own sets, unless it is the stop's own
        owners = [] if d.own else [e for e in entries if e.name in reach and (d.path, d.func) in reach[e.name]]
        op = opcode_at(d.at)
        if op in RETURN_OPCODES:
            live = lv.live_after_return(d.at)
            what = f"after the {RETURN_OPCODES[op]} at {fmt(d.at)}"
        else:
            live = lv.live_at(d.at)
            what = f"at {fmt(d.at)}"
        if live is None:
            findings.append(Finding(where, f"the stop at {fmt(d.at)} isn't code the analysis knows"))
            continue
        for e in owners or [None]:
            if e and e.dont_care & DC_WIDTH:
                findings.append(Finding(where, f"{e.name}'s .dont_care has DC_WIDTH, which shadow mode would apply at "
                                               f"this stop too: make the stop its own (m_stop_at_own)"))
            dc = d.dont_care | (e.dont_care if e else 0)
            bad = excess(dc, live)
            who = f" (with {e.name}'s {dc_text(e.dont_care)})" if e and e.dont_care else ""
            who += " (its own)" if d.own else ""
            rows.append(f"{where}: stop {what} {dc_text(d.dont_care)}{who}: " + verdict(dc, live))
            if bad:
                findings.append(Finding(where, f"the stop declares {dc_text(d.dont_care)}{who} {what}, "
                                               f"where liveness has {describe(bad)} read"))
    if strict is not None:
        for (path, offset), needed in noted.items():
            if not needed:
                findings.append(Finding(f"{path}:{by_source[path].line(offset)}",
                                        "a dc-evidence note on a call-out whose set liveness proves without it: "
                                        "remove the note"))
    return findings, rows


def load_fuzz_routines(path: pathlib.Path) -> set:
    """(name, variant) of every routine the fuzz constraints file lists."""
    if not path.exists():
        return set()
    with open(path, "rb") as fh:
        data = tomllib.load(fh)
    return {(r.get("name", ""), r.get("variant", "")) for r in data.get("routine", [])}


def load_sources(root: pathlib.Path) -> tuple:
    sources = []
    for path in sorted((root / "game").rglob("*.[ch]")):
        rel = path.relative_to(root).as_posix()
        sources.append(Source(rel, path.read_text(encoding="utf-8")))
    headers = [Source(h, (root / h).read_text(encoding="utf-8")) for h in HEADERS if (root / h).exists()]
    game_headers = [s for s in sources if s.path.endswith(".h")]
    return sources, headers + game_headers


def run(root: pathlib.Path, lv, opcode_at, strict=None) -> tuple:
    """(findings, rows, counts) for the tree at root."""
    return run_sources(*load_sources(root), lv, opcode_at, strict=strict,
                       fuzz_routines=load_fuzz_routines(root / FUZZ_ROUTINES))


def run_sources(sources: list, globals_: list, lv, opcode_at, strict=None, fuzz_routines: set | None = None) -> tuple:
    """(findings, rows, counts) for these sources, one of them game/registry.c.
    With `strict`, call-outs are checked against it too (check)."""
    ev = Evaluator(sources, globals_)
    registry = next(s for s in sources if s.path == "game/registry.c")
    entries, f1 = registry_entries(registry, ev)
    decls, f2 = declarations([s for s in sources if s.path.endswith(".c")], ev)
    findings, rows = check(entries, decls, sources, lv, opcode_at, strict, fuzz_routines)
    counts = {"entries": sum(1 for e in entries if e.dont_care and not e.idle and not e.generated),
              "stops": sum(1 for d in decls if d.kind == "stop"), "calls": sum(1 for d in decls if d.kind == "call")}
    unique = list(dict.fromkeys(f.text() for f in f1 + f2 + findings))
    return [Finding(*t.split(": ", 1)) for t in unique], list(dict.fromkeys(rows)), counts


def expand(patterns, root) -> list:
    out = []
    for p in patterns:
        out += sorted(glob.glob(str(root / p)) if not pathlib.Path(p).is_absolute() else glob.glob(p))
    return [pathlib.Path(p) for p in out]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python3 tools/dontcare_check.py", description=__doc__.split("\n")[0])
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms" / "ct-usa.sfc")
    ap.add_argument("--hints", type=pathlib.Path, default=ROOT / "re" / "hints.toml")
    ap.add_argument("--root", type=pathlib.Path, default=ROOT,
                    help="where the default coverage and call globs are looked for")
    ap.add_argument("--coverage", action="append", default=[], help="more coverage files (globs)")
    ap.add_argument("--calls", action="append", default=[], help="more call logs (globs)")
    ap.add_argument("--list", action="store_true", help="print every declaration and its verdict")
    ap.add_argument("--tree", type=pathlib.Path, default=ROOT,
                    help="the source tree whose game/ to check (default: this one; a draftcheck scratch tree works)")
    args = ap.parse_args(argv)

    from tools.disasm import coverage as coverage_mod
    from tools.disasm import hints as hints_mod
    from tools.disasm.analyze import Analyzer
    from tools.disasm.liveness import Liveness
    from tools.disasm.memmap import Rom, bus_to_offset

    if not args.rom.exists():
        print(f"{args.rom}: not found (tools/rom.py stage <path>)", file=sys.stderr)
        return 2
    rom = Rom(args.rom.read_bytes())
    coverage = expand(DEFAULT_COVERAGE, args.root) + expand(args.coverage, pathlib.Path.cwd())
    calls = expand(DEFAULT_CALLS, args.root) + expand(args.calls, pathlib.Path.cwd())
    an = Analyzer(rom, hints_mod.load(args.hints), coverage=coverage_mod.load(coverage) if coverage else None)
    an.run()
    dynamic = coverage_mod.load_calls(calls) if calls else {}
    lv = Liveness(an, dynamic)
    lv.solve()
    strict = Liveness(an, dynamic, strict=True)
    strict.solve()

    def opcode_at(addr):
        off = bus_to_offset(addr)
        return rom.data[off] if off is not None else None

    findings, rows, counts = run(args.tree, lv, opcode_at, strict)
    # The `[[keeps]]` hints liveness takes on trust, checked against the code
    # (tools/disasm/keeps.py): a wrong one kills a register every declaration
    # past it could lean on.
    from tools.disasm import keeps as keeps_mod
    wrong_keeps = keeps_mod.check(an)
    for f in wrong_keeps:
        print(f)
    if args.list:
        for r in rows:
            print(r)
    for f in findings:
        print(f.text())
    print(f"{len(findings)} finding(s) over {counts['entries']} entries with a don't-care set, "
          f"{counts['stops']} stop and {counts['calls']} call-out declarations"
          + ("" if calls else " (no call logs: liveness may be too trusting)"))
    return 1 if findings or wrong_keeps else 0


if __name__ == "__main__":
    sys.exit(main())
