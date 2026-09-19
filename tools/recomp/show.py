"""Print a routine as a draft for a hand port (docs/RECOMP.md, "Cleanup into hand ports (M5)").

    python3 -m tools.recomp.show BB:AAAA [BB:AAAA ...] [--m 8 --x 16] [--d 0100] [--db 7E]
                                 [--page NAME] [--asm] [--no-preamble]

A draft is the routine the way the porting rules (CLAUDE.md, "Writing a port
from generated code") ask a port to be shaped, derived mechanically:

- a **logic function** that takes the registers the routine reads as
  parameters and returns the ones its callers read, computing with C locals
  instead of an emulated CPU: flags only where liveness shows something reads
  them, values written where they are used, loops and conditionals where the
  control flow allows it, work RAM by its game/ram.h name;
- a **wrapper** at the original entry that moves the machine's registers into
  the logic's parameters and its results back, and returns the way the
  original does;
- **call-out stubs**, one per callee and register signature, that do the same
  around each m_jsr/m_jsl, so the logic calls `x = call_...(s, site, a)` and
  never touches an emulated register.

It prints to stdout, like tools.recomp.linear: a draft is a mechanical image
of the ROM, so it is never written into the repository. Drafts compile against
runtime/port/machine.h and game/ram.h and do what the original does (as
tools/recomp/draft.py says), so one registered in a scratch build is checked
by shadow mode like any port. They are drafts: what they say about names,
structure and meaning is a starting point for the porter, not a port.

D and DB come from the routine and its callers (tools/recomp/frame.py);
--d/--db give them by hand, and --page names a direct page whose D isn't fixed
(re/ram.toml's `caller`, `menu_record`), whose variables are then used by
offset.
"""

import argparse
import glob
import re
import sys
from pathlib import Path

from tools.disasm import coverage as coverage_mod
from tools.disasm import hints as hints_mod
from tools.disasm.analyze import Analyzer
from tools.disasm.cpu import Mode
from tools.disasm.liveness import ALL, NAMES as LIVE_NAMES, Liveness, dc_mask, dc_text, use_def
from tools.disasm.memmap import Rom, fmt, parse
from tools.ram_map import RamMap

from . import names as names_mod
from .draft import Eval, build_blocks, cleanup, live_after, restrict_exits, wait_loops
from .frame import Frames, single
from .ir import (ALL_KEYS, COMPARE, FLAG_NAMES, Assign, Bin, Branch, CallOut, Cast, Comment, Const, Exit, Expr, Fn,
                 Goto, Load, Mach, MachSet, Part, Pull, Push, Store, Switch, Un, Var, c_name, c_type, exact, simplify)
from .lift import Lifter, Routine, lift_routine, region
from .structure import Break, Continue, If, Label, Loop, structure

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_COVERAGE = ("traces/coverage-*.txt", "traces/saves/cov-*.txt")
DEFAULT_CALLS = ("traces/calls-*.txt", "traces/saves/calls-*.txt")
KEY_BITS = {"al": "A", "ah": "AH", "x": "X", "y": "Y", "n": "N", "v": "V", "z": "Z", "c": "C"}
FLAG_BITS = {"n": "CPU_FLAG_N", "v": "CPU_FLAG_V", "z": "CPU_FLAG_Z", "c": "CPU_FLAG_C"}
ORDER = ("a", "x", "y", "n", "v", "z", "c")

PREAMBLE = """\
/* Drafts from tools/recomp/show (docs/RECOMP.md, M5). ROM-derived: don't commit them. */
#include "port/machine.h"
#include "ram.h"

/* P's width bits as the original has them at a call-out or a return. */
static inline void draft_widths(Snes *s, bool m8, bool x8)
{
    s->cpu.p = (uint8_t)((s->cpu.p & ~(CPU_FLAG_M | CPU_FLAG_X)) | (m8 ? CPU_FLAG_M : 0) | (x8 ? CPU_FLAG_X : 0));
    if (x8) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
}

/* The byte PHP pushes: the widths it runs with, the flags the draft holds. */
static inline uint8_t draft_p(Snes *s, uint8_t mx, bool n, bool v, bool z, bool c)
{
    return (uint8_t)((s->cpu.p & (CPU_FLAG_I | CPU_FLAG_D)) | mx | (n ? CPU_FLAG_N : 0) | (v ? CPU_FLAG_V : 0)
                     | (z ? CPU_FLAG_Z : 0) | (c ? CPU_FLAG_C : 0));
}

static inline uint32_t draft_read24(Snes *s, uint32_t addr)
{
    return (uint32_t)m_read16(s, addr) | (uint32_t)m_read8(s, addr + 2) << 16;
}
"""


def mask_keys(mask: int) -> frozenset:
    out = set()
    for name, bit in LIVE_NAMES:
        if mask & bit:
            out |= {"A": {"al"}, "AH": {"ah"}, "X": {"x"}, "Y": {"y"}, "N": {"n"}, "V": {"v"}, "Z": {"z"},
                    "C": {"c"}}.get(name, set())
    return frozenset(out)


def regs_of(keys) -> list:
    out = []
    for r in ORDER:
        if (r == "a" and keys & {"al", "ah"}) or (r != "a" and r in keys):
            out.append(r)
    return out


def reg_type(r: str, a8: bool = False) -> str:
    if r == "a" and a8:
        return "uint8_t"
    return "bool" if r in FLAG_NAMES else "uint16_t"


def dc_bits(keys) -> str:
    """The DC_* set for what keys leaves out: DC_A for the whole of A, DC_AH
    when only its low byte is kept."""
    bits = []
    for r in ORDER:
        if r == "a":
            if not keys & {"al", "ah"}:
                bits.append("DC_A")
            elif "ah" not in keys and "al" in keys:
                bits.append("DC_AH")
        elif r not in keys:
            bits.append(f"DC_{r.upper()}")
    return " | ".join(bits)


def read_registry(path: Path) -> dict:
    """addr -> port name, from game/registry.c."""
    out = {}
    if not path.exists():
        return out
    text = path.read_text(encoding="utf-8")
    for m in re.finditer(r'\.addr\s*=\s*0x([0-9A-Fa-f]{6}),\s*\.name\s*=\s*"(\w+)"', text):
        out[int(m.group(1), 16)] = m.group(2)
    for m in re.finditer(r"\b[A-Z_]+\(0x([0-9A-Fa-f]{6}),\s*(\w+)\)", text):
        out.setdefault(int(m.group(1), 16), m.group(2))
    return out


# ---- the program's knowledge ---------------------------------------------------------------


class Knowledge:
    """The analysis, liveness, frames and maps a draft is made from."""

    def __init__(self, rom: Rom, hints: Path, coverage: list, calls: list, registry: Path, ram: Path,
                 entries=()):
        """entries: (address, m, x) of the routines to draft, m and x 8, 16 or
        None; one the analysis never entered gets a routine of its own there."""
        self.rom = rom
        self.an = Analyzer(rom, hints_mod.load(hints), coverage=coverage_mod.load(coverage) if coverage else None)
        self.an.run()
        for addr, m, x in entries:
            for widths in entry_widths(self.an, addr, m, x):
                if (addr, widths) not in self.an.contexts:
                    self.an.ctx(addr, widths, "show")
        self.an._fixpoint()
        self._finish(coverage_mod.load_calls(calls) if calls else {}, read_registry(registry),
                     RamMap(ram) if ram and ram.exists() else None)

    @classmethod
    def of(cls, an: Analyzer, dynamic: dict | None = None, registry: dict | None = None, ram=None):
        """From an analysis already run (tests use synthetic code)."""
        k = cls.__new__(cls)
        k.rom = an.rom
        k.an = an
        k._finish(dynamic or {}, registry or {}, ram)
        return k

    def _finish(self, dynamic: dict, registry: dict, ram):
        self.dynamic = dynamic
        self.lv = Liveness(self.an, self.dynamic)
        self.lv.solve()
        self.frames = Frames(self.an, self.dynamic)
        self.registry = registry
        self.ram = ram
        self._may_def: dict = {}

    def name_of(self, addr: int) -> str | None:
        return self.registry.get(addr) or self.an.hints.names.get(addr)

    def callee_live_in(self, target: int) -> frozenset:
        return mask_keys(self.lv._callee_live_in(target))

    def may_def(self, target: int, depth: int = 0) -> frozenset:
        """Keys a call to target may change (every register write it or its callees make)."""
        if target in self._may_def:
            return self._may_def[target]
        keys = [k for k in self.an.contexts if k[0] == target]
        if not keys or depth > 40:
            return ALL_KEYS
        self._may_def[target] = ALL_KEYS      # recursion: assume the worst
        out = set()
        for k in keys:
            c = self.an.contexts[k]
            for pc in c.insns:
                for widths, insn in self.an.code[pc].items():
                    n, mode = insn.name, insn.mode
                    if n in ("JSR", "JSL") and mode in (Mode.ABS, Mode.LONG):
                        t = insn.operand if n == "JSL" else (pc & 0xFF0000) | insn.operand
                        out |= self.may_def(t, depth + 1)
                    elif n == "JML" and mode is Mode.LONG:
                        out |= self.may_def(insn.operand, depth + 1)
                    elif mode in (Mode.ABSXIND, Mode.ABSIND, Mode.ABSLIND) or n in ("RTI", "XCE", "PLP"):
                        out |= ALL_KEYS
                    elif n == "SEP" and insn.operand & 0x10:
                        # 8-bit index registers lose their high bytes
                        out |= {"x", "y"} | mask_keys(use_def(insn, False)[1])
                    else:
                        out |= mask_keys(use_def(insn, False)[1])
                    if len(out) == len(ALL_KEYS):
                        break
        self._may_def[target] = frozenset(out)
        return self._may_def[target]

    def ret_live(self, entry: int) -> frozenset:
        c = [c for k, c in self.an.contexts.items() if k[0] == entry]
        if entry in self.lv.all_live_returns or not any(x.callers for x in c) and \
                not self.dynamic.get(entry):
            return ALL_KEYS
        return mask_keys(self.lv.ret_live[entry])

    def table(self, pc: int) -> list:
        jt = self.an.tables.get(pc)
        if jt is None or not jt.count:
            return []
        return self.an._table_targets(jt, jt.count) + list(jt.extra)


# ---- rendering -------------------------------------------------------------------------------

PREC = {"||": 4, "&&": 5, "|": 6, "^": 7, "&": 8, "==": 9, "!=": 9, "<": 10, "<=": 10, ">": 10, ">=": 10,
        "<<": 11, ">>": 11, "+": 12, "-": 12}
BITWISE = {"|", "^", "&", "<<", ">>"}


def hexnum(v: int, w: int) -> str:
    if v < 10:
        return str(v)
    digits = 2 if v <= 0xFF and w <= 8 else (4 if v <= 0xFFFF and w <= 16 else 6)
    return f"0x{v:0{digits}X}"


class Renderer:
    def __init__(self, namer, script_base=None, script_cmd=None):
        self.namer = namer
        self.script_base = script_base    # event handlers: the script buffer's address
        self.script_cmd = script_cmd      # the eventscript.Command it handles
        self.io: set = set()
        self.a8 = False

    def const(self, c: Const) -> str:
        if c.addr and c.w >= 16:
            a = c.v
            reg = region(a)
            if reg == "ram" and self.namer is not None:
                name = self.namer.name(a if a >> 16 in (0x7E, 0x7F) else 0x7E0000 | (a & 0xFFFF))
                if name:
                    return name
            if reg == "io":
                n = names_mod.io_name(a)
                self.io.add(a & 0xFFFF)
                return f"0x{a:06X} /* {n or 'I/O'} */"
            return f"0x{a:06X}"
        return hexnum(c.v, c.w)

    def expr(self, e: Expr, parent=None, side=None, need_exact=False) -> str:
        text = self._expr(e)
        if need_exact and not exact(e):
            return f"({c_type(e.w)}){text}" if _grouped(text) else f"({c_type(e.w)})({text})"
        if isinstance(e, Bin) and parent is not None:
            pp, cp = PREC[parent], PREC[e.op]
            wrap = cp < pp or (cp == pp and side == "r" and parent in ("-", "<<", ">>", "==", "!=", "<", "<=",
                                                                      ">", ">="))
            wrap = wrap or ((parent in BITWISE or e.op in BITWISE) and parent != e.op)
            wrap = wrap or (e.op in COMPARE and parent in COMPARE)
            if wrap:
                return f"({text})"
        return text

    def _expr(self, e: Expr) -> str:
        if isinstance(e, Const):
            return self.const(e)
        if isinstance(e, Var):
            return c_name(e.name)
        if isinstance(e, Part):
            return "(uint8_t)(a >> 8)" if e.hi else ("a" if self.a8 else "(uint8_t)a")
        if isinstance(e, Load):
            fn = {8: "m_read8", 16: "m_read16", 24: "draft_read24"}[e.w]
            note = self.operand_note(e.addr)
            return f"{fn}(s, {self.expr(e.addr)}{note})"
        if isinstance(e, Mach):
            return e.text
        if isinstance(e, Fn):
            return f"{e.name}({', '.join(['s'] + [self.expr(a) for a in e.args])})"
        if isinstance(e, Cast):
            inner = self.expr(e.e)
            if isinstance(e.e, (Bin, Un)) and not _grouped(inner):
                inner = f"({inner})"
            return f"({c_type(e.w)}){inner}"
        if isinstance(e, Un):
            inner = self.expr(e.e)
            if isinstance(e.e, Bin):
                inner = f"({inner})"
            return f"{e.op}{inner}"
        if isinstance(e, Bin):
            strict = e.op in COMPARE or e.op in (">>",)
            ln = self.expr(e.l, e.op, "l", need_exact=(strict or e.l.w < e.w) and e.op not in ("&",))
            rn = self.expr(e.r, e.op, "r", need_exact=(strict or e.r.w < e.w) and e.op not in ("&",))
            return f"{ln} {e.op} {rn}"
        raise AssertionError(e)

    def operand_note(self, addr: Expr) -> str:
        """` /* var */` for an event handler's read of its own operand."""
        if self.script_base is None:
            return ""
        k = 0
        e = addr
        if isinstance(e, Bin) and e.op == "+" and isinstance(e.r, Const) and not e.r.addr:
            k, e = e.r.v, e.l
        if isinstance(e, Bin) and e.op == "+" and isinstance(e.l, Const) and e.l.v == self.script_base \
                and isinstance(e.r, Var) and e.r.name == "cmd":
            what = names_mod.event_operand_at(self.script_cmd, k)
            return f" /* {what} */" if what else ""
        return ""


def _grouped(t: str) -> bool:
    """t is one parenthesized group: (...) whose first "(" closes at the end."""
    if not (t.startswith("(") and t.endswith(")")):
        return False
    depth = 0
    for i, ch in enumerate(t):
        depth += ch == "("
        depth -= ch == ")"
        if depth == 0:
            return i == len(t) - 1
    return False


def _balanced(t: str) -> bool:
    depth = 0
    for ch in t:
        depth += ch == "("
        depth -= ch == ")"
        if depth < 0:
            return False
    return depth == 0


# ---- a draft ---------------------------------------------------------------------------------


class Draft:
    def __init__(self, k: Knowledge, addr: int, m8: bool, x8: bool, d=None, db=None, page=None, asm=False,
                 name=None):
        self.k = k
        self.addr = addr
        self.m8, self.x8 = m8, x8
        self.asm = asm
        self.key = (addr, (m8, x8))
        an = k.an
        if self.key not in an.contexts:
            raise SystemExit(f"{fmt(addr)}: the analysis has no routine there with m{8 if m8 else 16} "
                             f"x{8 if x8 else 16}")
        self.graph = k.frames.graph(self.key)
        self.frames = k.frames.frames(self.key)
        ed, edb, self.entry_why = k.frames.entry(self.key)
        self.d_given, self.db_given = d is not None, db is not None
        self.d_set = frozenset({d}) if d is not None else ed
        self.db_set = frozenset({db}) if db is not None else edb
        self.d = single(self.d_set)
        self.db = single(self.db_set)
        self.commands = names_mod.commands_at(addr)
        self.subsystem = names_mod.BANK_SUBSYSTEM.get(addr >> 16)
        if self.commands and self.commands[0][0] == "event":
            self.subsystem = "event"
        self.page = page
        self.port_name = k.name_of(addr)
        self.name = name or self._name()

    def _name(self) -> str:
        if self.commands:
            return names_mod.command_symbol(self.commands[0][0], self.commands[0][1])
        if self.port_name:
            return self.port_name
        return f"sub_{self.addr:06X}"

    # -- pipeline --

    def build(self):
        k = self.k
        self.routine = Routine(self.addr, self.m8, self.x8, self.d_set, self.db_set, k.ret_live(self.addr),
                               k.callee_live_in, k.may_def, set(k.an.hints.noreturn))
        if self.page is not None and self.d is None and k.ram is not None:
            namer = names_mod.RamNamer(k.ram, self.subsystem)
            self.routine.dp_name = lambda o: namer.dp_name(self.page, o)
        tables = {}
        for node, insn in self.graph.insns.items():
            if insn.mode is Mode.ABSXIND:
                tables[node[0]] = k.table(node[0])
        stops = {a: n for a, n in k.registry.items() if a != self.addr}
        lifter = Lifter(self.graph, self.frames, self.routine, tables, k.rom, stops)
        lifted = lift_routine(lifter)
        self.event_cmd = self.commands[0][2] if self.commands and self.commands[0][0] == "event" else None
        if self.event_cmd is not None:
            rebase_script_reads(lifted, self.graph.entry, SCRIPT_BASE)
        self.waits = []
        for heads, what in wait_loops(lifted, self.graph.entry, region):
            self.waits.append((heads, what))
            for h in heads:
                if h == self.graph.entry:
                    continue
                l = lifted[h]
                l.stmts = [Exit("stop", Const(h[0] & 0xFFFF, 16), None, h[1], h[2],
                                note=f"the loop at {fmt(h[0])} waits on {', '.join(what)}")]
                l.fall = None
        # A routine that never reads or writes A's high byte itself works on an
        # 8-bit A: the machine's high byte is then exactly the original's
        # throughout (only callees change it), so the draft leaves it there
        self.a8 = not any(_touches_ah(s) for l in lifted.values() for s in l.stmts)
        for l in lifted.values():
            for s in l.stmts:
                if isinstance(s, CallOut):
                    s.outputs = frozenset(s.may_def) & ALL_KEYS
                    if self.a8:
                        s.a8 = True
                        s.inputs = frozenset(s.inputs) - {"ah"}
                        s.outputs = s.outputs - {"ah"}
                elif isinstance(s, Exit) and self.a8:
                    s.live = frozenset(s.live) - {"ah"}
        self.blocks = build_blocks(lifted, self.graph.entry)
        for _ in range(4):
            cleanup(self.blocks)
            before = [(s.live if isinstance(s, Exit) else None) for b in self.blocks for s in b.stmts]
            restrict_exits(self.blocks)
            after = [(s.live if isinstance(s, Exit) else None) for b in self.blocks for s in b.stmts]
            if before == after:
                break
        cleanup(self.blocks)
        live_map = {b.name: b.live_in for b in self.blocks}
        for b in self.blocks:
            for s, live in zip(b.stmts, live_after(b, live_map)):
                if isinstance(s, (Assign, Pull)) and s.lo:
                    s.keep_hi = "ah" in live
        self.params = regs_of(self.blocks[0].live_in)
        self.uses_cmd = any(isinstance(x, Var) and x.name == "cmd" for b in self.blocks for s in b.stmts
                            for e in s.exprs() for x in e.walk())
        self.exits = [s for b in self.blocks for s in b.stmts if isinstance(s, Exit)]
        outs = frozenset().union(*[s.live for s in self.exits]) if self.exits else frozenset()
        self.outputs = regs_of(outs)
        sigs = []
        for s in self.exits:
            if s.signature() not in sigs:
                sigs.append(s.signature())
        self.exit_sigs = sigs
        self.exit_live = {}
        for s in self.exits:
            self.exit_live.setdefault(s.signature(), set()).update(regs_of(s.live))
        self.has_jump = any(s.kind == "jump" for s in self.exits)
        self.items = self._flatten()
        self.tree = structure(self.items)
        self.stubs = self._stubs()

    def _flatten(self) -> list:
        exits_only = {}
        for b in self.blocks:
            real = [s for s in b.stmts if not isinstance(s, Comment)]
            if len(real) == 1 and isinstance(real[0], Exit) and b.name != self.blocks[0].name:
                exits_only[b.name] = real[0]
        items = []
        for i, b in enumerate(self.blocks):
            if b.name in exits_only and i > 0:
                continue
            items.append(Label(b.name))
            for s in b.stmts:
                if isinstance(s, Comment) and not self.asm:
                    continue
                if isinstance(s, Goto) and s.target in exits_only:
                    items.append(_copy_exit(exits_only[s.target]))
                elif isinstance(s, Branch) and s.target in exits_only:
                    items.append(If(s.cond, [_copy_exit(exits_only[s.target])]))
                else:
                    items.append(s)
            if b.fall is not None:
                if b.fall in exits_only:
                    items.append(_copy_exit(exits_only[b.fall]))
                else:
                    items.append(Goto(b.fall))
        # a switch still goes to its exits by label
        wanted = {t for s in items if isinstance(s, Switch) for t in [s.default] + [t for _, t in s.cases]}
        for b in self.blocks:
            if b.name in exits_only and b.name in wanted:
                items += [Label(b.name), _copy_exit(exits_only[b.name])]
        return items

    def _stubs(self) -> dict:
        stubs = {}
        for s in _walk_stmts(self.tree):
            if not isinstance(s, CallOut):
                continue
            sig = (s.target, s.long, s.m8, s.x8, frozenset(s.inputs), frozenset(s.outputs),
                   None if s.target_expr is None else "table")
            if sig not in stubs:
                base = self.k.name_of(s.target) if s.target is not None else None
                if s.target is None:
                    base = f"table_{s.site:06X}"
                elif not base:
                    base = f"sub_{s.target:06X}"
                name = f"call_{base}"
                n = sum(1 for v in stubs.values() if v[0] == name or v[0].startswith(name + "_"))
                stubs[sig] = (name if n == 0 else f"{name}_{n + 1}", s)
            s.stub = stubs[sig][0]
        return stubs

    # -- text --

    def render(self) -> str:
        self.namer = names_mod.RamNamer(self.k.ram, self.subsystem, self.d) if self.k.ram else None
        rend = Renderer(self.namer, SCRIPT_BASE if self.event_cmd is not None else None, self.event_cmd)
        rend.a8 = self.a8
        self.rend = rend
        logic = self._logic(rend)
        stubs = self._stub_text(rend)
        wrapper = self._wrapper()
        head = self._header(rend)
        return "\n".join(head + [""] + stubs + logic + [""] + wrapper) + "\n"

    def _ret_type(self):
        fields = [(r, reg_type(r, self.a8)) for r in self.outputs]
        if len(self.exit_sigs) > 1:
            fields.append(("exit", "uint8_t"))
        if self.has_jump:
            fields.append(("pc", "uint32_t"))
        if not fields:
            return "void", fields
        if len(fields) == 1:
            return fields[0][1], fields
        return f"{self.name}_out", fields

    def _logic(self, rend) -> list:
        rtype, fields = self._ret_type()
        params = ["Snes *s"] + [f"{reg_type(r, self.a8)} {c_name(r)}" for r in self.params]
        if self.uses_cmd:
            params.append("uint16_t cmd")
        out = []
        if rtype.endswith("_out"):
            out += [f"typedef struct {{ {' '.join(f'{t} {c_name(n)};' for n, t in fields)} }} {rtype};", ""]
        decls = self._locals()
        body = []
        for s in self.tree:
            body += self._stmt(s, rend, fields, rtype)
        if body and body[-1] == "return;":
            body.pop()
        # a routine that touches neither memory nor the machine needs no machine
        self.uses_machine = any(re.search(r"\bs\b", line) for line in body)
        if not self.uses_machine:
            params = params[1:] or ["void"]
        out.append(f"static {rtype} {self.name}_logic({', '.join(params)})")
        out.append("{")
        for t, vs in decls:
            out.append(f"    {t} {', '.join(vs)};")
        out += indent_lines(body, 1)
        out.append("}")
        return out

    def _locals(self) -> list:
        seen = {}
        for s in _walk_stmts(self.tree):
            vars_ = set()
            if isinstance(s, (Assign, Pull)) and s.dst:
                vars_.add(s.dst)
            if isinstance(s, CallOut):
                vars_ |= set(regs_of(s.outputs))
            for e in s.exprs():
                for x in e.walk():
                    if isinstance(x, Var):
                        vars_.add(x.name)
                    elif isinstance(x, Part):
                        vars_.add("a")
            if isinstance(s, CallOut):
                vars_ |= set(regs_of(s.inputs))
            for v in vars_:
                seen.setdefault(v, None)
        temps = self.routine.temps
        groups: dict = {}
        for v in seen:
            if v in self.params or v == "cmd":
                continue
            t = reg_type(v, self.a8) if v in ORDER else c_type(temps.get(v, 16))
            groups.setdefault(t, []).append(c_name(v))
        return [(t, sorted(vs, key=_var_order)) for t, vs in sorted(groups.items())]

    def _exit_value(self, s: Exit, fields, rtype) -> str:
        if rtype == "void":
            return "return;"
        vals = {}
        mine = regs_of(s.live)
        for r in self.outputs:
            # a field another exit returns: this one leaves the register alone
            vals[r] = c_name(r) if r in mine else "0"
        if len(self.exit_sigs) > 1:
            vals["exit"] = str(self.exit_sigs.index(s.signature()))
        if self.has_jump:
            vals["pc"] = self.rend.expr(s.pc) if s.kind == "jump" else "0"
        if len(fields) == 1:
            return f"return {vals[fields[0][0]]};"
        return f"return ({rtype}){{ {', '.join(f'.{c_name(n)} = {vals[n]}' for n, _ in fields)} }};"

    def _stmt(self, s, rend, fields, rtype) -> list:
        e = rend.expr
        if isinstance(s, Comment):
            return [f"/* {s.text} */"]
        if isinstance(s, Label):
            return [f"{s.name}:;"]
        if isinstance(s, Assign):
            if s.lo:
                if getattr(s, "keep_hi", True):
                    if isinstance(s.e, Const) and s.e.v == 0:
                        return ["a &= 0xFF00;"]
                    return [f"a = (a & 0xFF00) | {e(s.e, '|', 'r')};"]
                return [f"a = {e(s.e)};"]
            w = 16 if s.dst in ("a", "x", "y") else self.routine.temps.get(s.dst, 1)
            if s.dst == "a" and self.a8:
                w = 8
            return [f"{c_name(s.dst)} = {self.sink(s.e, w)};"]
        if isinstance(s, Eval):
            text = e(s.e)
            return [f"(void){text};" if isinstance(s.e, (Load, Fn, Mach, Var)) or _grouped(text) else f"(void)({text});"]
        if isinstance(s, Store):
            return [f"m_write{s.w}(s, {e(s.addr)}{rend.operand_note(s.addr)}, {self.sink(s.val, s.w)});"]
        if isinstance(s, Push):
            return [f"m_push{s.w}(s, {self.sink(s.val, s.w)});"]
        if isinstance(s, Pull):
            call = f"m_pull{s.w}(s)"
            if s.dst is None:
                return [f"{call};"]
            if s.lo:
                if getattr(s, "keep_hi", True):
                    return [f"a = (uint16_t)((a & 0xFF00) | {call});"]
                return [f"a = {call};"]
            return [f"{c_name(s.dst)} = {call};"]
        if isinstance(s, MachSet):
            text = s.template.format(e(s.e)) if s.e is not None else s.template
            return [text + (f"  /* {s.note} */" if s.note else "")]
        if isinstance(s, CallOut):
            return [self._call_text(s, rend)]
        if isinstance(s, Goto):
            return [f"goto {s.target};"]
        if isinstance(s, Branch):
            return [f"if ({e(s.cond)})", f"    goto {s.target};"]
        if isinstance(s, Exit):
            note = f"  /* {s.note} */" if s.note else ""
            return [self._exit_value(s, fields, rtype) + note]
        if isinstance(s, Switch):
            lines = [f"switch ({e(s.e)}) {{"]
            for v, t in s.cases:
                lines.append(f"case 0x{v:02X}: goto {t};")
            lines += [f"default: goto {s.default};", "}"]
            return lines
        if isinstance(s, If):
            then = [x for t in s.then for x in self._stmt(t, rend, fields, rtype)]
            lines = [f"if ({e(s.cond)}) {{", *["    " + x for x in then]]
            if s.els:
                els = [x for t in s.els for x in self._stmt(t, rend, fields, rtype)]
                if len(s.els) == 1 and isinstance(s.els[0], If):
                    lines += ["} else " + els[0]] + els[1:]
                    return lines
                lines += ["} else {", *["    " + x for x in els]]
            lines.append("}")
            return lines
        if isinstance(s, Loop):
            body = [x for t in s.body for x in self._stmt(t, rend, fields, rtype)]
            if s.kind == "do":
                return ["do {", *["    " + x for x in body], f"}} while ({e(s.cond)});"]
            if s.kind == "while":
                return [f"while ({e(s.cond)}) {{", *["    " + x for x in body], "}"]
            return ["for (;;) {", *["    " + x for x in body], "}"]
        if isinstance(s, Break):
            return ["break;"]
        if isinstance(s, Continue):
            return ["continue;"]
        raise AssertionError(s)

    def sink(self, e, w: int) -> str:
        """e where a C value of width w is taken: a cast to that width is implied,
        and so are the truncations inside it that one makes redundant."""
        if w in (8, 16):
            e = simplify(Cast(e, w))
            if isinstance(e, Cast) and e.w == w:
                return self.rend.expr(e.e)
        return self.rend.expr(e)

    def _call_text(self, s: CallOut, rend) -> str:
        ins, outs = regs_of(s.inputs | s.outputs), regs_of(s.outputs)
        args = [f"0x{s.site:06X}"]
        if s.target_expr is not None:
            args.append(f"0x{s.site & 0xFF0000:06X} | {rend.expr(s.target_expr)}")
        ret = outs[0] if len(outs) == 1 else None
        for r in ORDER:
            if r in outs and r != ret:
                args.append(f"&{c_name(r)}")
            elif r in ins:
                args.append(c_name(r))
        call = f"{s.stub}(s, {', '.join(args)});"
        where = f"  /* {fmt(s.target)} */" if s.target is not None and not self.k.name_of(s.target) else ""
        return (f"{c_name(ret)} = " if ret else "") + call + where

    def _stub_text(self, rend) -> list:
        out = []
        for sig, (name, s) in self.stubs.items():
            reads = regs_of(s.inputs)
            ins, outs = regs_of(s.inputs | s.outputs), regs_of(s.outputs)
            ret = outs[0] if len(outs) == 1 else None
            params = ["Snes *s", "uint32_t site"] + (["uint32_t target"] if s.target_expr is not None else [])
            for r in ORDER:
                if r in outs and r != ret:
                    params.append(f"{reg_type(r, self.a8)} *{c_name(r)}")
                elif r in ins:
                    params.append(f"{reg_type(r, self.a8)} {c_name(r)}")
            rtype = reg_type(ret, self.a8) if ret else "void"
            what = fmt(s.target) if s.target is not None else s.note
            nm = self.k.name_of(s.target) if s.target is not None else None
            desc = f"{what}{f' ({nm})' if nm and nm not in name else ''}"
            back = " ".join(x.upper() for x in outs) or "nothing"
            out.append(f"/* {desc}: the callee reads {' '.join(x.upper() for x in reads) or 'nothing'}; "
                       f"the draft reads {back} back. */")
            out.append(f"static {rtype} {name}({', '.join(params)})")
            out.append("{")
            for r in ORDER:
                if r in ins:
                    val = f"*{c_name(r)}" if (r in outs and r != ret) else c_name(r)
                    if r in FLAG_BITS:
                        out.append(f"    m_set_flag(s, {FLAG_BITS[r]}, {val});")
                    else:
                        out.append(f"    m_set_a8(s, {val});" if r == "a" and self.a8 else f"    s->cpu.{r} = {val};")
            out.append(f"    draft_widths(s, {str(s.m8).lower()}, {str(s.x8).lower()});")
            kind = "m_jsl" if s.long else "m_jsr"
            target = "target" if s.target_expr is not None else f"0x{s.target:06X}"
            dc = dc_bits(s.inputs | s.outputs)
            if dc:
                out.append(f"    {kind}_dc(s, site, {target}, {dc});")
            else:
                out.append(f"    {kind}(s, site, {target});")
            for r in ORDER:
                if r in outs:
                    src = f"m_flag(s, {FLAG_BITS[r]})" if r in FLAG_BITS else self._machine_reg(r)
                    if r == ret:
                        out.append(f"    return {src};")
                    else:
                        out.append(f"    *{c_name(r)} = {src};")
            out.append("}")
            out.append("")
        return out

    def _wrapper(self) -> list:
        rtype, fields = self._ret_type()
        args = (["s"] if self.uses_machine else []) + [self._machine_reg(r) for r in self.params] + \
            (["s->cpu.y"] if self.uses_cmd else [])
        call = f"{self.name}_logic({', '.join(args)})"
        out = [f"// ${fmt(self.addr)}", f"void {self.name}(Snes *s)", "{"]
        body = []
        if rtype == "void":
            body.append(f"{call};")
        else:
            body.append(f"{rtype} out = {call};")
        changes_p = any(isinstance(s, CallOut) or (isinstance(s, MachSet) and "cpu.p =" in s.template)
                        for s in _walk_stmts(self.tree))

        def exit_code(sig) -> list:
            kind, pc, pb, m8, x8 = sig
            code = []
            if kind == "noreturn":
                return ["return;"]
            if m8 is not None and x8 is not None and (changes_p or (m8, x8) != (self.m8, self.x8)):
                code.append(f"draft_widths(s, {str(m8).lower()}, {str(x8).lower()});")
            for r in self.outputs:
                if r not in self.exit_live.get(sig, ()):
                    continue
                v = "out" if len(fields) == 1 else f"out.{c_name(r)}"
                if r in FLAG_BITS:
                    if kind != "rti":
                        code.append(f"m_set_flag(s, {FLAG_BITS[r]}, {v});")
                else:
                    code.append(f"m_set_a8(s, {v});" if r == "a" and self.a8 else f"s->cpu.{r} = {v};")
            if kind == "rts":
                code.append("m_rts(s);")
            elif kind == "rtl":
                code.append("m_rtl(s);")
            elif kind == "rti":
                code.append("m_rti(s);")
            elif kind == "stop":
                if pb is not None:
                    code.append(f"s->cpu.pb = 0x{pb:02X};")
                code.append(f"s->cpu.pc = 0x{pc:04X};  /* the interpreter carries on there */")
            elif kind == "jump":
                v = "out" if len(fields) == 1 else "out.pc"
                code += [f"s->cpu.pb = (uint8_t)({v} >> 16);", f"s->cpu.pc = (uint16_t){v};"]
            return code

        if len(self.exit_sigs) <= 1:
            body += exit_code(self.exit_sigs[0]) if self.exit_sigs else []
        else:
            body.append("switch (out.exit) {")
            for i, sig in enumerate(self.exit_sigs):
                body.append(f"case {i}:")
                body += ["    " + x for x in exit_code(sig)] + ["    break;"]
            body.append("}")
        out += ["    " + x for x in body] + ["}"]
        return out

    def _machine_reg(self, r: str) -> str:
        if r in FLAG_BITS:
            return f"m_flag(s, {FLAG_BITS[r]})"
        if r == "a" and self.a8:
            return "(uint8_t)s->cpu.a"
        return f"s->cpu.{r}"

    def _header(self, rend) -> list:
        k = self.k
        lines = [f"Draft of ${fmt(self.addr)}"
                 + (f" (ported as {self.port_name})" if self.port_name and self.port_name != self.name else "")
                 + ", from tools/recomp/show. ROM-derived: don't commit it."]
        for interp, text, _ in self.commands:
            lines.append(f"It is the handler of {text}.")
        if self.event_cmd is not None:
            lines.append("Event commands are entered with Y = the command's offset in the script "
                         "(`cmd` here), and return X = the next command's offset with carry set to "
                         "keep running this object's script this frame.")
        w = f"m{8 if self.m8 else 16} x{8 if self.x8 else 16}"
        dtext = "unknown" if self.d is None else f"${self.d:04X}"
        dwhy = "given" if self.d_given else self.entry_why
        dbtext = "unknown" if self.db is None else f"${self.db:02X}"
        page = ""
        if self.d is not None and k.ram is not None:
            pages = [p for p in k.ram.pages.values() if p.d == self.d]
            same = [p for p in pages if p.subsystem in (self.subsystem, "core")] or pages
            if same:
                page = f" (page {', '.join(p.name for p in same)})"
        lines.append(f"Entered {w}; D = {dtext}{page}, DB = {dbtext} ({dwhy}).")
        if self.d is None:
            lines.append("Direct-page accesses go through m_dp: D isn't known, so they stay offsets.")
        ret = k.ret_live(self.addr)
        lines.append(f"Callers read after it returns: {_keys_text(ret)}; it reads on entry: "
                     f"{' '.join(r.upper() for r in self.params) or 'nothing'}"
                     + (" and Y as cmd" if self.uses_cmd else "") + ".")
        if self.routine.decimal_sensitive:
            lines.append("ADC/SBC are drafted in binary mode: register it with the _BINARY precondition.")
        if region(self.addr) == "ram":
            lines.append("It runs from work RAM, where other code can be at other times: register it with a guard "
                         "that checks the code is there (m_code_matches, docs/PORTING.md, Modes).")
        if any(self.graph.entry in heads for heads, _ in self.waits):
            lines.append("It starts with a loop that waits on something outside the CPU: register it as an idle "
                         "entry (docs/PORTING.md, Modes), not as a draft.")
        hands = sorted({s.note for s in self.exits if s.kind == "stop" and s.note})
        for h in hands:
            lines.append(f"Hands back to the interpreter: {h}.")
        hw = {a for a in rend.io if a not in names_mod.ARITHMETIC}
        if hw:
            regs = ", ".join(sorted({names_mod.io_name(a) or f"${a:04X}" for a in hw}))
            lines.append(f"Touches I/O registers ({regs}): a port reaches them through runtime/platform/.")
        if rend.io - hw:
            regs = ", ".join(sorted({names_mod.io_name(a) for a in rend.io - hw}))
            lines.append(f"Uses the multiplier or divider ({regs}), which ports may use directly.")
        mech = sorted({s.note for s in _walk_stmts(self.tree) if isinstance(s, MachSet) and s.note})
        if mech:
            lines.append(f"Sets machine state directly ({', '.join(mech)}): SNES mechanics a port "
                         "keeps only where a caller relies on it.")
        reg = self.registration()
        entry = f"{{ .addr = 0x{self.addr:06X}, .name = \"{self.name}\", .fn = {self.name}, {reg['needs']}, "                 f".dont_care = {reg['dont_care']}" + (", .guard = ..." if reg["guard"] else "") + " }"
        lines.append(f"Registers as {entry}" + (f", guarded on {' && '.join(reg['guard'])} (what the draft "
                                                 "assumed)" if reg["guard"] else "") + ".")
        return _comment(lines)

    def registration(self) -> dict:
        """How the draft registers: its precondition, don't-care set and the
        guard on the D and DB it assumed from its callers."""
        ret = self.k.ret_live(self.addr)
        dc = dc_mask(ALL & ~_keys_mask(ret))
        needs = f"NEEDS_M{8 if self.m8 else 16}_X{8 if self.x8 else 16}" +             ("_BINARY" if self.routine.decimal_sensitive else "")
        p_mask = 0x30 | (0x08 if self.routine.decimal_sensitive else 0)
        p_value = (0x20 if self.m8 else 0) | (0x10 if self.x8 else 0)
        guard = []
        if self.d is not None and not self.d_given and self._uses("d"):
            guard.append(f"s->cpu.d == 0x{self.d:04X}")
        if self.db is not None and not self.db_given and self._uses("db"):
            guard.append(f"s->cpu.db == 0x{self.db:02X}")
        return {"needs": needs, "p_mask": p_mask, "p_value": p_value, "dc": dc,
                "dont_care": dc_text(dc), "guard": guard}

    def _uses(self, what: str) -> bool:
        dp = (Mode.DP, Mode.DPX, Mode.DPY, Mode.DPIND, Mode.DPXIND, Mode.DPINDY, Mode.DPLIND, Mode.DPLINDY)
        ab = (Mode.ABS, Mode.ABSX, Mode.ABSY, Mode.DPIND, Mode.DPINDY, Mode.DPXIND, Mode.SRINDY)
        for insn in self.graph.insns.values():
            if what == "d" and (insn.mode in dp or insn.name in ("TDC", "PHD", "PEI")):
                return True
            if what == "db" and ((insn.mode in ab and insn.name not in ("JMP", "JSR")) or insn.name == "PHB"):
                return True
        return False


def _touches_ah(s) -> bool:
    if isinstance(s, (CallOut, Exit)):
        return False
    if isinstance(s, Assign) and s.dst == "a" and not s.lo:
        return True
    return any((isinstance(x, Var) and x.name == "a") or (isinstance(x, Part) and x.hi)
               for e in s.exprs() for x in e.walk())


def _keys_mask(keys) -> int:
    m = 0
    for name, bit in LIVE_NAMES:
        if name == "A" and "al" in keys:
            m |= bit
        elif name != "A" and name.lower() in keys:
            m |= bit
    return m


def _keys_text(keys) -> str:
    return " ".join(r.upper() for r in regs_of(keys)) or "nothing"


def _var_order(name: str):
    order = ["a", "x", "y", "negative", "overflow", "zero", "carry"]
    return (order.index(name) if name in order else 10, len(name), name)


def _copy_exit(s: Exit) -> Exit:
    return Exit(s.kind, s.pc, s.pb, s.m8, s.x8, s.note, s.live)


def _walk_stmts(items):
    for s in items:
        yield s
        if isinstance(s, If):
            yield from _walk_stmts(s.then)
            yield from _walk_stmts(s.els)
        elif isinstance(s, Loop):
            yield from _walk_stmts(s.body)


def _comment(lines: list) -> list:
    out = ["/*"]
    for line in lines:
        words = line.split(" ")
        cur = " *"
        for wd in words:
            if len(cur) + 1 + len(wd) > 100:
                out.append(cur)
                cur = " *  "
            cur += " " + wd
        out.append(cur)
    out.append(" */")
    return out


def indent_lines(lines, depth):
    return ["    " * depth + x if not x.endswith(":;") else x for x in lines]


# ---- event handlers: operands by the command's offset ----------------------------------------

SCRIPT_BASE = 0x7F2001


def rebase_script_reads(lifted: dict, entry, base: int):
    """In an event command handler, X and Y are the command's offset (Y on entry)
    plus a constant for a while: script reads through them become
    `script + cmd + k`, which says which operand they are."""
    from .ir import map_exprs
    state_in = {entry: {"x": None, "y": ("cmd", 0)}}
    work = [entry]
    seen_out = {}
    while work:
        node = work.pop()
        st = dict(state_in[node])
        l = lifted[node]
        for s in l.stmts:
            st = _affine_step(st, s)
        seen_out[node] = st
        nexts = []
        if l.fall is not None:
            nexts.append(l.fall)
        for s in l.stmts:
            if isinstance(s, (Branch, Goto)) and isinstance(s.target, tuple) and s.target[0] != "exit":
                nexts.append(s.target)
        for n in nexts:
            if n not in lifted:
                continue
            old = state_in.get(n)
            new = st if old is None else {r: (st[r] if st[r] == old.get(r) else None) for r in ("x", "y")}
            if new != old:
                state_in[n] = new
                work.append(n)
    for node, l in lifted.items():
        st = dict(state_in.get(node, {"x": None, "y": None}))
        new = []
        for s in l.stmts:
            snap = dict(st)
            if isinstance(s, (Store,)) or any(isinstance(x, Load) for e in s.exprs() for x in e.walk()):
                s = map_exprs(s, lambda e: _rebase(e, snap, base))
            new.append(s)
            st = _affine_step(st, s)
        l.stmts = new


def _affine_of(e, st):
    if isinstance(e, Var) and e.name in ("x", "y") and e.w == 16:
        return st.get(e.name)
    if isinstance(e, Bin) and e.op in ("+", "-") and isinstance(e.r, Const):
        inner = _affine_of(e.l, st)
        if inner is not None:
            return (inner[0], inner[1] + (e.r.v if e.op == "+" else -e.r.v))
    return None


def _affine_step(st, s):
    st = dict(st)
    if isinstance(s, Assign) and s.dst in ("x", "y"):
        st[s.dst] = _affine_of(s.e, st)
    elif isinstance(s, (Pull, CallOut)):
        for r in ("x", "y"):
            if r in s.defs():
                st[r] = None
    return st


def _rebase(e, st, base):
    if isinstance(e, Load):
        a = e.addr
        if isinstance(a, Bin) and a.op == "+" and isinstance(a.l, Const) and a.l.v == base and \
                isinstance(a.r, Var) and a.r.name in ("x", "y") and a.r.w == 16:
            aff = st.get(a.r.name)
            if aff is not None and aff[0] == "cmd" and aff[1] >= 0:
                addr = Bin("+", Const(base, 24, "long"), Var("cmd", 16), 24)
                if aff[1]:
                    addr = Bin("+", addr, Const(aff[1], 24), 24)
                return Load(e.w, addr, e.pure)
    kids = e.children()
    if not kids:
        return e
    return e.rebuild(tuple(_rebase(k, st, base) for k in kids))


# ---- command line ----------------------------------------------------------------------------


def expand(patterns, root: Path) -> list:
    out = []
    for p in patterns:
        path = Path(p)
        if not path.is_absolute():
            path = root / p
        out += [Path(x) for x in sorted(glob.glob(str(path)))]
    return out


def entry_widths(an: Analyzer, addr: int, m, x) -> list:
    """The (m8, x8) a draft at addr is made for: those given, else those the
    analysis entered it with, else those it decoded the instruction there with."""
    def fits(w):
        return None not in w and (m is None or w[0] == (m == 8)) and (x is None or w[1] == (x == 8))
    if m is not None and x is not None:
        return [(m == 8, x == 8)]
    got = [k[1] for k in an.contexts if k[0] == addr and fits(k[1])]
    if not got:
        got = [w for w in an.code.get(addr, {}) if fits(w)]
    return sorted(set(got), key=str)


widths_at = entry_widths


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 -m tools.recomp.show", description=__doc__.split("\n")[0])
    ap.add_argument("addr", nargs="+", help="BB:AAAA entry addresses")
    ap.add_argument("--m", type=int, choices=(8, 16))
    ap.add_argument("--x", type=int, choices=(8, 16))
    ap.add_argument("--d", type=lambda v: int(v.replace("$", ""), 16), help="direct page at entry (hex)")
    ap.add_argument("--db", type=lambda v: int(v.replace("$", ""), 16), help="data bank at entry (hex)")
    ap.add_argument("--page", help="a direct page from re/ram.toml whose D isn't fixed, to name offsets by")
    ap.add_argument("--name", help="the draft's function name")
    ap.add_argument("--asm", action="store_true", help="interleave the original instructions as comments")
    ap.add_argument("--no-preamble", action="store_true", help="leave out the includes and helpers")
    ap.add_argument("--rom", type=Path, default=REPO_ROOT / "roms" / "ct-usa.sfc")
    ap.add_argument("--hints", type=Path, default=REPO_ROOT / "re" / "hints.toml")
    ap.add_argument("--ram", type=Path, default=REPO_ROOT / "re" / "ram.toml")
    ap.add_argument("--registry", type=Path, default=REPO_ROOT / "game" / "registry.c")
    ap.add_argument("--coverage", action="append", default=None,
                    help=f"coverage files (globs; default {' '.join(DEFAULT_COVERAGE)})")
    ap.add_argument("--calls", action="append", default=None,
                    help=f"call logs (globs; default {' '.join(DEFAULT_CALLS)})")
    ap.add_argument("--root", type=Path, default=REPO_ROOT,
                    help="where the default coverage and call globs are looked for")
    args = ap.parse_args(argv)
    if not args.rom.exists():
        raise SystemExit(f"{args.rom} not found; stage the ROM with tools/rom.py first")
    rom = Rom(args.rom.read_bytes())
    coverage = expand(args.coverage or DEFAULT_COVERAGE, args.root)
    calls = expand(args.calls or DEFAULT_CALLS, args.root)
    if not coverage or not calls:
        print(f"no coverage or call files under {args.root}: the analysis and liveness will know less "
              "(--root, --coverage, --calls)", file=sys.stderr)
    addrs = [parse(a) for a in args.addr]
    k = Knowledge(rom, args.hints, coverage, calls, args.registry, args.ram, [(a, args.m, args.x) for a in addrs])
    out = [] if args.no_preamble else [PREAMBLE]
    for a in addrs:
        ws = widths_at(k.an, a, args.m, args.x)
        if not ws:
            raise SystemExit(f"{fmt(a)}: no routine the analysis entered there; give --m and --x")
        if len(ws) > 1:
            raise SystemExit(f"{fmt(a)}: entered with several widths "
                             f"({', '.join(f'm{8 if m else 16} x{8 if x else 16}' for m, x in ws)}); "
                             "give --m and --x")
        page = None
        if args.page:
            if k.ram is None or args.page not in k.ram.pages:
                raise SystemExit(f"--page: no page {args.page!r} in {args.ram}")
            page = k.ram.pages[args.page]
        d = Draft(k, a, ws[0][0], ws[0][1], args.d, args.db, page, args.asm, args.name if len(addrs) == 1 else None)
        d.build()
        out.append(d.render())
    sys.stdout.write("\n".join(out))


if __name__ == "__main__":
    main()
