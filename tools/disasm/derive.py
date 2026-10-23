"""Derivations: targets of indirect transfers taken from the data the code
interprets, for the `derive` field of [[indirect]] and [[jumptable]] hints
(docs/MATCHING.md, MD4, C3).

A deriver gets an Analyzer that has traced what it could so far and returns a
Derivation: target sets by name (the part after the dot in the hint's `derive`)
and problems. Analyzer._derive traces the targets and calls the derivers again
until nothing new appears, so arguments found in newly traced code count too.
Each deriver enumerates where its data comes from; a source it can't resolve
that isn't on its list of known dynamic sources, with the reason its values are
covered, is a problem, reported by the analysis and failing the matching
build's closure check.
"""

import itertools
from dataclasses import dataclass, field

from .codeargs import _CLOBBER, _TRANSFER, call_sites, callees, flow, register_values
from .cpu import Mode
from .memmap import bus_to_offset, fmt


@dataclass
class Derivation:
    targets: dict = field(default_factory=dict)      # part -> set of addresses
    problems: list = field(default_factory=list)
    notes: list = field(default_factory=list)         # counts for reports
    detail: object = None


def _stores(an, bank: int | None, names, modes, operands):
    """(pc, insn) of every instruction in the code set storing to one of the operands."""
    for pc, variants in an.code.items():
        if bank is not None and pc >> 16 != bank:
            continue
        insn = next(iter(variants.values()))
        if insn.name in names and insn.mode in modes and insn.operand in operands:
            yield pc, insn


# ---- object scripts (tools/disasm/objscript.py) --------------------------------

OBJ_SCRIPT_STARTERS = (0xC204D9, 0xC204FB)           # A = bank, X = address
OBJ_SPAWNERS = (0xC2048A, 0xC204CD)                  # X = handler word
# Call sites whose arguments aren't constants, and why their values are covered.
OBJ_DYNAMIC = {
    0xC21203: "command $09: its operands are parsed",
    0xC21A3A: "command $43: its operands are parsed",
    0xC2242E: "a scene's $7E:8480 word: every word of every scene is a root",
    0xC233B2: "a scene's $7E:8480 word",
    0xC233DF: "a scene's $7E:8480 word",
    0xC2183F: "command $35: its operands are parsed",
    0xC21A49: "command $42: its operands are parsed",
}
# Every store to slot +$07 in bank C2 (STA $0007,X) whose value isn't a constant, and why.
OBJ_POINTER_STORES = {
    0xC204ED: "sub_C204D9's argument (its call sites are enumerated)",
    0xC2050F: "sub_C204FB's argument",
    0xC20F86: "the interpreter's own position",
    0xC21E64: "the palette handler $C2:1E38 reuses +$07 for a colour index; it never runs a script",
    0xC2377B: "$1B3B, which command $08 writes (its operands are parsed) and $C2:2434/$33B8/$33E5 clear",
    0xC23DAA: "$1B3B,Y: the same cells",
    0xC20B88: "the sprite node list at $7E:B000 (X from $B504), not a slot",
    0xC2B513: "a menu record at $6F, not a slot",
    0xC2EC5C: "a tile map run from $61, not a slot",
}


def _near_store(an, fl, pc, operand, span=8):
    """The instruction storing A to `operand`,X within `span` instructions of pc."""
    for step in (1, -1):
        cur = pc
        for _ in range(span):
            nxt = cur + next(iter(an.code[cur].values())).size if step > 0 else fl.pred.get(cur)
            if nxt is None or nxt not in an.code:
                break
            i2 = next(iter(an.code[nxt].values()))
            if i2.name == "STA" and i2.mode is Mode.ABSX and i2.operand == operand:
                return nxt
            cur = nxt
    return None


def derive_objscript(an, fl) -> Derivation:
    from .objscript import ObjectScripts
    d = Derivation()
    roots = []
    for target in OBJ_SCRIPT_STARTERS:
        for site in call_sites(an, target):
            xs, as_ = register_values(an, site, "X", fl), register_values(an, site, "A", fl)
            if xs and as_:
                roots += [((a & 0xFF) << 16 | x, f"call at {fmt(site)}") for a in sorted(as_) for x in sorted(xs)]
            elif site not in OBJ_DYNAMIC:
                d.problems.append(f"{fmt(site)}: a script start whose arguments aren't constants and isn't listed")
    for pc, _ in _stores(an, 0xC2, ("STA",), (Mode.ABSX,), (0x0007,)):
        if pc in OBJ_POINTER_STORES:
            continue
        words = register_values(an, pc, "A", fl)
        bank_at = _near_store(an, fl, pc, 0x0009)
        banks = register_values(an, bank_at, "A", fl) if bank_at is not None else None
        if not words or not banks:
            d.problems.append(f"{fmt(pc)}: a store to slot +$07 whose address isn't a constant and isn't listed")
            continue
        roots += [((b & 0xFF) << 16 | w & 0xFFFF, f"slot +$07 store at {fmt(pc)}") for b in banks for w in words]
    static_handlers = set()
    for target in OBJ_SPAWNERS:
        for site in call_sites(an, target):
            xs = register_values(an, site, "X", fl)
            if xs:
                static_handlers |= {0xC20000 | x for x in xs}
            elif site not in OBJ_DYNAMIC:
                d.problems.append(f"{fmt(site)}: an object spawn whose handler isn't a constant and isn't listed")
    parse = ObjectScripts(an.rom).parse(roots)
    d.problems += parse.problems
    d.targets["jsr"] = set(parse.jsr)
    d.targets["jsl"] = set(parse.jsl)
    d.targets["handler"] = set(parse.handlers) | static_handlers
    used = sorted({op for op, _ in parse.commands.values()})
    d.targets["commands"] = {0xC20000 | an.rom.u16(0xC20F91 + 2 * op) for op in used}
    if any(i >= 3 for i in parse.subs):
        d.problems.append(f"commands $44/$45 index the 3-entry table at C2:1A70 with {sorted(parse.subs)}")
    d.notes.append(f"{len(parse.roots)} roots, {len(parse.commands)} commands "
                   f"({sum(1 for s, _ in parse.commands if s == 'rom')} in ROM, the rest in {len(parse.scenes)} "
                   f"scenes in bank $7F); $44/$45 indices {sorted(parse.subs)}; {len(static_handlers)} handler "
                   "words loaded by code")
    d.detail = parse
    return d


# ---- sequence scripts of the attract program (tools/disasm/seqscript.py) ------

SEQ_POINTERS = (0x0A20, 0x0B20)                      # the two channels' script pointers
SEQ_TASK_ADDERS = {                                  # routine -> task list (X = handler)
    0x7E395E: "0920", 0xC30CE2: "0920",
    0x7E3974: "0940", 0xC30CF8: "0940",
    # the $0920 runner's exit 7E:39B1, which handlers jump to: TXA, then the
    # pulls, then STA $0920,X replaces the running task with the X they loaded
    0x7E39B1: "0920",
}
# Calls to the adders whose X isn't a constant, and why they are covered.
# The task lists a routine of the program sets whole, with a block move from a
# list in its own image (the Epoch's dive gives the frame a list of tasks for
# each of its views, $7E:7ED1 and $7E:7EE8): an MVN within bank $7E whose
# destination Y is a list's start and whose source X and count A are
# constants; every word it copies is a task.
SEQ_TASK_LIST_STARTS = {0x0920: "0920", 0x0940: "0940"}

SEQ_DYNAMIC_ADDS = {
    0xC30BAE: "sequence command $AA: its operands are parsed",
    0xC30BC7: "sequence command $A8: its operands are parsed",
}
# Stores to a channel pointer in code whose value isn't a constant load: the
# word table the store reads, and its entry count.
SEQ_POINTER_TABLES = {
    # 7E:3F59: LDA $3F66,X with X = (A & 2) (7E:3F5C-3F5F); DB = $7E (the channel
    # runners set it: 7E:374D-374E, 7E:3758-3759)
    0x7E3F63: (0x7E3F66, 2),
    # 7E:7286: LDA $729C,X with X = (A & 6) (7E:7291-7294)
    0x7E7298: (0x7E729C, 4),
}


def program_reader(an, name: str = "attract_program"):
    """Reads a RAM section's own image (not the sections written over it)."""
    sec = next((s for s in an.ram.sections if s.name == name), None)

    def read(addr, n):
        i = addr - sec.base if sec else -1
        return sec.image[i:i + n] if sec and 0 <= i and i + n <= len(sec.image) else None
    return read


def _dp_unchanged(an, fl, pc, entries, limit=256):
    """Whether every path to pc from an entry in `entries` (through branches,
    fall-through and the call sites of intermediate routines) leaves the direct
    page as it was at that entry: no TCD or PLD on the way."""
    seen, work = set(), [pc]
    while work:
        cur = work.pop()
        if cur in seen:
            continue
        seen.add(cur)
        if len(seen) > limit:
            return False
        kinds = an.labels.get(cur, set())
        if cur in entries:
            continue
        if "sub" in kinds:
            sites = set()
            for (e, _), c in an.contexts.items():
                if e == cur:
                    sites |= c.callers
            if not sites:
                return False
            work += [s for s in sites]
            continue
        srcs = ([fl.pred[cur]] if cur in fl.pred else []) + sorted(fl.branches.get(cur, ()))
        if not srcs or cur in fl.joins:
            return False
        for s in srcs:
            if next(iter(an.code[s].values())).name in ("TCD", "PLD"):
                return False
            work.append(s)
    return True


def _always(an, fl, pc, entry, limit=64) -> bool:
    """Whether the instruction at pc runs on every path from `entry` to its
    return: the instructions from the entry to it fall through one to the next
    with nothing branching in or out (calls in between return), and so do those
    from it to an RTS or RTL."""
    cur = pc
    for _ in range(limit):
        if cur == entry:
            break
        if fl.branches.get(cur) or cur in fl.joins or cur not in fl.pred:
            return False
        cur = fl.pred[cur]
        if next(iter(an.code[cur].values())).name in CONDITIONAL:
            return False
    else:
        return False
    cur = pc
    for _ in range(limit):
        insn = next(iter(an.code[cur].values()))
        if insn.name in ("RTS", "RTL"):
            return True
        if insn.name in CONDITIONAL or insn.name in ("JMP", "JML", "BRA", "BRL", "RTI"):
            return False
        cur = insn.next_pc
        if cur not in an.code or fl.branches.get(cur):
            return False
    return False


CONDITIONAL = {"BEQ", "BNE", "BCC", "BCS", "BPL", "BMI", "BVC", "BVS"}


def _pointer_stores(an, fl, pcs, problems, entries=frozenset()):
    """[(pc, pointer, values)] of the stores to a channel pointer among pcs:
    absolute $0A20/$0B20, or $20 in the direct page (pointer None: the running
    channel, when called from a sequence)."""
    out = []
    for pc in pcs:
        insn = next(iter(an.code[pc].values()))
        if insn.name not in ("STA", "STX", "STY", "STZ"):
            continue
        if insn.mode is Mode.ABS and insn.operand in SEQ_POINTERS:
            ptr = insn.operand
        elif insn.mode is Mode.DP and insn.operand in (0x20, 0x21) and entries and _dp_unchanged(an, fl, pc, entries):
            ptr = None
        else:
            continue
        if pc in SEQ_POINTER_TABLES:
            table, n = SEQ_POINTER_TABLES[pc]
            vals = {an.u16(table + 2 * i) for i in range(n)} - {None}
        elif insn.name == "STZ" or insn.operand == 0x21:
            vals = None
        else:
            vals = register_values(an, pc, insn.name[2], fl)
        if not vals:
            problems.append(f"{fmt(pc)}: a store to a sequence pointer whose value isn't known")
            continue
        out.append((pc, ptr, vals))
    return out


def _paired_values(an, fl, pc, regs):
    """The tuples of constant values `regs` hold together before `pc`, one per path
    into it, or None. Where more than one register takes several values, the
    values are paired by the path that sets them (the dive's two lists, $7E:7ED1
    and $7E:7EE8, share one block move with a source and a length each), not
    crossed: crossing them reads a list with the other's length."""
    vals = [register_values(an, pc, r, fl) for r in regs]
    if not all(vals):
        return None
    if sum(len(v) > 1 for v in vals) <= 1:
        return set(itertools.product(*vals))
    cur = pc
    for _ in range(64):
        sources = ([fl.pred[cur]] if cur in fl.pred else []) + sorted(fl.branches.get(cur, ()))
        if len(sources) != 1:
            break
        cur = sources[0]
        name = next(iter(an.code[cur].values())).name
        if any(name.startswith("LD" + r) or name in _CLOBBER[r] or _TRANSFER.get(name, ("",))[0] == r
               for r in regs):
            return None
    else:
        return None
    out = set()
    for src in sources:
        insn = next(iter(an.code[src].values()))
        path = []
        for r in regs:
            if insn.name == "LD" + r and insn.mode in (Mode.IMM_M, Mode.IMM_X):
                path.append({insn.operand})
            else:
                path.append(register_values(an, src, r, fl))
        if not all(path) or any(len(v) > 1 for v in path):
            return None
        out.add(tuple(next(iter(v)) for v in path))
    return out


def derive_seqscript(an, fl) -> Derivation:
    from . import seqscript
    d = Derivation()
    read = program_reader(an)
    sec = next((s for s in an.ram.sections if s.name == "attract_program"), None)
    program = sorted(pc for pc in an.code if sec and sec.base <= pc < sec.end)
    by_entry = {}
    for (e, _), c in an.contexts.items():
        by_entry.setdefault(e, set()).update(c.insns)
    parse, problems = None, []
    for _ in range(16):
        problems = []
        effects, inside = {}, set()
        for target in sorted(parse.calls) if parse else []:
            pcs = set()
            for entry in callees(an, [target]):
                pcs |= by_entry.get(entry, set())
            inside |= pcs
            effects[target] = [(ptr, vals, _always(an, fl, pc, target)) for pc, ptr, vals in
                               _pointer_stores(an, fl, sorted(pcs), problems, frozenset([target]))]
        roots = []
        for pc, ptr, vals in _pointer_stores(an, fl, [p for p in program if p not in inside], problems):
            roots += [(v, ptr, f"store at {fmt(pc)}") for v in sorted(vals)]
        new = seqscript.parse(read, roots, effects)
        done = parse is not None and set(new.roots) == set(parse.roots) and set(new.calls) == set(parse.calls)
        parse = new
        if done:
            break
    d.problems += list(dict.fromkeys(problems + parse.problems))
    tasks = {"0920": set(parse.tasks_0920), "0940": set(parse.tasks_0940)}
    static = 0
    for adder, lst in SEQ_TASK_ADDERS.items():
        for site in call_sites(an, adder):
            xs = register_values(an, site, "X", fl)
            if xs:
                tasks[lst] |= {0x7E0000 | x for x in xs}
                static += len(xs)
            elif site not in SEQ_DYNAMIC_ADDS:
                d.problems.append(f"{fmt(site)}: a task added whose handler isn't a constant and isn't listed")
    for pc in program:
        insn = next(iter(an.code[pc].values()))
        if insn.name != "MVN" or insn.operand != 0x7E7E:
            continue
        ys = register_values(an, pc, "Y", fl)
        if not ys or not ys & set(SEQ_TASK_LIST_STARTS):
            continue
        copies = _paired_values(an, fl, pc, ("X", "A"))
        if not copies or len(ys) != 1:
            d.problems.append(f"{fmt(pc)}: a task list copied whose source or length isn't a constant")
            continue
        lst = SEQ_TASK_LIST_STARTS[next(iter(ys))]
        for x, n in copies:
            words = {an.u16(0x7E0000 | (x + 2 * i)) for i in range((n + 1) // 2)}
            tasks[lst] |= {0x7E0000 | w for w in words if w}
            static += len(words)
    d.targets["call"] = set(parse.calls)
    d.targets["tasks_0920"] = tasks["0920"]
    d.targets["tasks_0940"] = tasks["0940"]
    d.notes.append(f"{len(parse.roots)} roots, {len(parse.commands)} commands; {len(parse.calls)} code calls, "
                   f"{len(tasks['0920'])} and {len(tasks['0940'])} tasks ({static} added by code)")
    d.detail = parse
    return d


# ---- operands written at run time (RAM code) ------------------------------------

CONTROL = {"JSR", "JMP", "JSL", "JML", "BRA", "BRL", "BEQ", "BNE", "BCC", "BCS", "BPL", "BMI", "BVC", "BVS"}


def _operand_stores(an, fl):
    """Stores (STA/STX/STY absolute or long, 16-bit) into the operand bytes of a
    control transfer in a RAM section, as {site: [(store pc, values or None)]}.
    Only stores in the site's own section count: a program's image is only in
    RAM while it runs. Absolute stores are taken to write bank $7E (the sections
    with such transfers are there): another data bank would only add targets."""
    operand_of = {}
    for pc, variants in an.code.items():
        if bus_to_offset(pc) is not None:
            continue
        insn = next(iter(variants.values()))
        if insn.name in CONTROL and insn.size > 1:
            for a in range(pc + 1, pc + insn.size):
                operand_of[a] = pc
    out = {}
    for pc, variants in an.code.items():
        insn = next(iter(variants.values()))
        if insn.name not in ("STA", "STX", "STY") or insn.mode not in (Mode.ABS, Mode.LONG):
            continue
        dest = insn.operand if insn.mode is Mode.LONG else 0x7E0000 | insn.operand
        site = operand_of.get(dest)
        sec = an.ram.section_at(site) if site is not None else None
        # only the program's own code runs while its image is in RAM
        if site is None or sec is None or not sec.base <= pc < sec.end:
            continue
        widths = {w[0] if insn.name == "STA" else w[1] for w in variants}
        reg = insn.name[2]
        vals = None
        if widths == {False} and dest == site + 1:
            vals = register_values(an, pc, reg, fl)
        elif widths == {True} and dest in (site + 1, site + 2):
            # a byte: paired with a store of the other operand byte in the next or
            # previous few instructions, else the other byte stays the image's
            other = site + 3 - (dest - site)
            mine = register_values(an, pc, reg, fl)
            theirs = None
            for step in (1, -1):
                cur = pc
                for _ in range(6):
                    cur = cur + next(iter(an.code[cur].values())).size if step > 0 else fl.pred.get(cur)
                    if cur is None or cur not in an.code:
                        break
                    i2 = next(iter(an.code[cur].values()))
                    d2 = i2.operand if i2.mode is Mode.LONG else 0x7E0000 | i2.operand
                    if i2.name in ("STA", "STX", "STY") and i2.mode in (Mode.ABS, Mode.LONG) and d2 == other:
                        theirs = register_values(an, cur, i2.name[2], fl)
                        break
                if theirs:
                    break
            if theirs is None:
                b = an.read(other, 1)
                theirs = {b[0]} if b else None
            if mine and theirs:
                vals = {(m | th << 8) if dest == site + 1 else (th | m << 8) for m in mine for th in theirs}
        out.setdefault(site, []).append((pc, vals))
    return out


def derive_operands(an, fl) -> Derivation:
    d = Derivation()
    stores = _operand_stores(an, fl)
    wanted = {site for site, spec in an.hints.derive.items() if spec.startswith("operands.")}
    for site, lst in sorted(stores.items()):
        bank = site & 0xFF0000
        part = f"{site:06X}"
        d.targets.setdefault(part, set())
        for pc, vals in lst:
            if not vals:
                d.problems.append(f"{fmt(pc)}: writes the operand of {fmt(site)} with a value that isn't known")
                continue
            d.targets[part] |= {bank | v for v in vals}
        if site not in wanted:
            d.problems.append(f"{fmt(site)}: code writes its operand ({', '.join(fmt(p) for p, _ in lst)}) and no "
                              "derive hint takes the values")
    for site in wanted:
        # the operand the image holds runs until the first store
        insn = next(iter(an.code[site].values())) if site in an.code else None
        d.targets.setdefault(f"{site:06X}", set())
        sec = an.ram.section_at(site)
        image_target = (site & 0xFF0000) | insn.operand if insn is not None else None
        # (an operand outside the program, like a JSR $0000 placeholder, can't be code it runs)
        if (insn is not None and insn.name in ("JSR", "JMP") and insn.mode is Mode.ABS and sec is not None
                and sec.base <= image_target < sec.end):
            d.targets[f"{site:06X}"].add(image_target)
    d.notes.append(f"{len(stores)} transfers in RAM code with operands written at run time, "
                   f"{sum(len(v) for v in stores.values())} stores")
    return d


# ---- window command lists ($C2:ED31) ---------------------------------------------

# $C2:ED31 runs the list at X in bank $FF (DB = $FF): a byte with bit 7 set ends
# it; otherwise the byte is both the index into the handler table at $C2:ED59
# (`TAY : ... : TYX : JSR ($ED59,X)`, so it must be even) and into the operand
# lengths at $FF:BD18, whose value plus one bytes `MVN` copies to $7E:005B,
# leaving X after them.
WIN_RUNNER = 0xC2ED31
WIN_TABLE, WIN_COUNT = 0xC2ED59, 11
WIN_LENGTHS = 0xFFBD18
# Call sites whose X isn't a constant load, and the word table it comes from.
WIN_TABLES = {
    0xC2DFBA: (0xC2DFC5, 5),     # X = the word at $C2:DFC5 + $04CC * 2; the table has 5 words (to $C2:DFCE)
}


def derive_winlist(an, fl) -> Derivation:
    d = Derivation()
    roots = set()
    for site in call_sites(an, WIN_RUNNER):
        if site in WIN_TABLES:
            table, n = WIN_TABLES[site]
            roots |= {0xFF0000 | an.rom.u16(table + 2 * i) for i in range(n)}
            continue
        xs = register_values(an, site, "X", fl)
        if not xs:
            d.problems.append(f"{fmt(site)}: a window command list whose address isn't a constant and isn't listed")
            continue
        roots |= {0xFF0000 | x for x in xs}
    used, lists, commands = set(), 0, 0
    for r in sorted(roots):
        p = r
        lists += 1
        for _ in range(512):
            b = an.rom.read(p, 1)
            if b is None:
                d.problems.append(f"{fmt(r)}: the window list runs outside bank $FF")
                break
            b = b[0]
            if b & 0x80:
                break
            commands += 1
            if b & 1 or b // 2 >= WIN_COUNT:
                d.problems.append(f"{fmt(p)}: window command {b:02X} isn't an entry of the {WIN_COUNT}-word table")
                break
            used.add(b // 2)
            p += 2 + an.rom.read(WIN_LENGTHS + b, 1)[0]
        else:
            d.problems.append(f"{fmt(r)}: the window list doesn't end")
    d.targets["handlers"] = {0xC20000 | an.rom.u16(WIN_TABLE + 2 * i) for i in sorted(used)}
    d.notes.append(f"{lists} lists, {commands} commands, handler entries used {sorted(used)} of {WIN_COUNT}")
    return d


# ---- event scripts (tools/disasm/eventscript.py) ---------------------------------

# $C0:56D4 decompresses entry n of the stream table at $FC:F9F0 to $7F:2000, n
# the word at +8 of the location's 14-byte record at $F6:0000 (512 records).
EVENT_STREAMS, EVENT_STREAM_COUNT = 0xFCF9F0, 513
EVENT_LOCATIONS, EVENT_LOCATION_COUNT, EVENT_LOCATION_SIZE, EVENT_LOCATION_FIELD = 0xF60000, 512, 14, 8
# Entries some location names that the decompressor rejects (the resource hint
# skips them): locations $1C0, $1C4-$1C7 and $1CD-$1CF, which no location command
# ($DC-$E1) of any event script names. Were one entered, the decompressor would
# make something of those bytes; that no path enters one is assumed, not shown.
EVENT_UNLOADABLE = frozenset((231, 232, 233, 234, 235, 320, 321, 322))
# The built-in runs of commands at $C0:5D37-5D6D, which $C0:5CEC copies to
# $7F:3700-3785 after every load (their addresses are the immediates at
# $C0:5CEE, $C0:5CFC, $C0:5D0A, $C0:5D18, $C0:5D26), entered through $1180 =
# $16FF (event command $DA, and $C0:5B4C, $C2:2E38, $C2:2E87), $171F/$173F/$175F
# ($D9) and $177F ($C0:324F, in $C0:3154, which command $B0 runs).
EVENT_BUILTIN = (0xC05D37, 0xC05D6D, (0xC05D37, 0xC05D50, 0xC05D68))
# Dispatches on an operand: site -> (opcode, operand key, table, entries).
# $12/$14/$16 and $13/$15 double the 8-bit operator with ASL A (so bit 7 is
# lost; $16 masks it first) and call through the 8-bit or the 16-bit table;
# $FF's modes from $90 index $C0:400E with (mode - $90) * 2.
EVENT_SITES = {
    0xC06307: (0x12, "op", 0xC06467, 16),
    0xC0633D: (0x13, "op", 0xC06477, 8),
    0xC06398: (0x14, "op", 0xC06467, 16),
    0xC063D7: (0x15, "op", 0xC06477, 8),
    0xC06433: (0x16, "op", 0xC06467, 16),
    0xC04009: (0xFF, "mode", 0xC0400E, 20),
}
EVENT_PARTS = {0xC06307: "if_var_const", 0xC0633D: "if_var_const16", 0xC06398: "if_var_var",
               0xC063D7: "if_var_var16", 0xC06433: "if_memory", 0xC04009: "mode"}


def event_scripts(rom):
    """[(where, Script)] of every event script the code can load, and problems."""
    from . import eventscript as ev
    from .lz import decompress
    out, problems = [], []
    ids = set()
    for k in range(EVENT_LOCATION_COUNT):
        ids.add(rom.u16(EVENT_LOCATIONS + EVENT_LOCATION_SIZE * k + EVENT_LOCATION_FIELD))
    seen = set()
    for n in sorted(ids):
        if n >= EVENT_STREAM_COUNT:
            problems.append(f"a location names event script {n}, past the {EVENT_STREAM_COUNT}-entry table")
            continue
        at = rom.u24(EVENT_STREAMS + 3 * n)
        off = bus_to_offset(at)
        got = decompress(rom.data, off) if off is not None else None
        if got is None:
            if n not in EVENT_UNLOADABLE:
                problems.append(f"event script {n} at {fmt(at)} isn't a stream and isn't listed")
            continue
        if at in seen:
            continue
        seen.add(at)
        try:
            out.append((f"script {n} ({fmt(at)})", ev.parse(got[0])))
        except ev.ScriptError as exc:
            problems.append(f"event script {n} at {fmt(at)}: {exc}")
    start, end, entries = EVENT_BUILTIN
    blob = rom.read(start, end - start + 1)
    out.append((f"{fmt(start)}-{fmt(end)}", ev.parse_commands(blob, [e - start for e in entries])))
    for where, sc in out:
        problems += [f"{where}: {p}" for p in sc.problems]
    return out, problems


def derive_eventscript(an, fl) -> Derivation:
    d = Derivation()
    # The scripts depend on the ROM alone: parse them once per ROM, not per round.
    cached = getattr(an.rom, "_event_scripts", None)
    if cached is None:
        cached = an.rom._event_scripts = event_scripts(an.rom)
    scripts, problems = cached
    d.problems = list(problems)
    for site, (op, key, table, count) in EVENT_SITES.items():
        values = set()
        for _, sc in scripts:
            values |= sc.operands.get((op, key), set())
        if op == 0xFF:
            index = {v - 0x90 for v in values if v >= 0x90}
        else:
            index = {v & 0x7F for v in values}           # (v << 1) & $FF, halved
        bad = sorted(i for i in index if i >= count)
        if bad:
            d.problems.append(f"{fmt(site)}: event command {op:02X} indexes the {count}-entry table at "
                              f"{fmt(table)} with {bad}")
        d.targets[EVENT_PARTS[site]] = {(table & 0xFF0000) | an.rom.u16(table + 2 * i) for i in index if i < count}
        d.notes.append(f"{fmt(site)}: entries {sorted(index)} of {count}")
    reached = sum(sum(sc.commands.values()) for _, sc in scripts)
    d.notes.insert(0, f"{len(scripts) - 1} event scripts and the built-in commands, "
                      f"{sum(len(sc.commands) for _, sc in scripts)} commands ({reached} bytes)")
    d.detail = scripts
    return d


def derive_battleanim(an, fl) -> Derivation:
    from .battlescript import derive_animation
    return derive_animation(an, fl)


def derive_enemyai(an, fl) -> Derivation:
    from .battlescript import derive_enemy
    return derive_enemy(an, fl)


DERIVERS = {
    "eventscript": derive_eventscript,
    "battleanim": derive_battleanim,
    "enemyai": derive_enemyai,
    "objscript": derive_objscript,
    "seqscript": derive_seqscript,
    "operands": derive_operands,
    "winlist": derive_winlist,
}


def derive_all(an) -> dict:
    """site -> targets for every `derive` hint, running each deriver once."""
    results = {}
    out = {}
    fl = None
    for site, spec in sorted(an.hints.derive.items()):
        name, _, part = spec.partition(".")
        if name not in DERIVERS:
            raise ValueError(f"derive {spec!r} at {fmt(site)}: no deriver {name!r}")
        if name not in results:
            fl = fl or flow(an)
            results[name] = DERIVERS[name](an, fl)
        res = results[name]
        if part not in res.targets:
            raise ValueError(f"derive {spec!r} at {fmt(site)}: {name} has no part {part!r}")
        out[site] = res.targets[part]
    an.derivations.update(results)
    return out


__all__ = ["DERIVERS", "Derivation", "derive_all"]
