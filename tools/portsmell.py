"""The rubric's mechanical items, checked by machine instead of by a reader.

    python3 tools/portsmell.py                       # every finding, by check
    python3 tools/portsmell.py --check               # the ratchet the suite runs
    python3 tools/portsmell.py --only asm-in-comments
    python3 tools/portsmell.py --files               # per file, one line each

A subsystem merges only after a qualitative review against the rubric in
`.claude/skills/review-port/`, which grew from 31 items to over a hundred as the
reviews found new ways for a port to read as a translation. Most of those items
need a reader: whether a name says what the game does, whether a comment is
true, whether a concept sits at the right level. Some do not. An opcode quoted
in a comment, a "presumably", a sibling port called through its ROM address, a
`Trace *` out-parameter and a `typedef enum` nothing takes as a type are all
decidable from the source, and a decidable item does not belong in prose a
reviewer re-reads every chunk: it belongs here, where it is caught before the
port is submitted and costs nothing to check again.

Each check below names the rubric items it stands for, so the skill can point at
this tool instead of spelling the item out, and so a finding here can be read
against the item's own explanation of why it matters. A check earns its place by
being decidable: where the rubric's item needs judgement (is this the right
level? does this name lie?) only the part a machine can settle is taken, and the
rest stays with the reviewer. That is why some items appear here in a narrower
form than the rubric states them.

What this is not: a measure of whether a port reads as hand-written. Every
check can pass on a transliteration -- `tools/portstyle.py` is the measure of
that shape, and the review is the judgement. This tool only keeps the settled
questions from being asked by hand.

`--check` is a ratchet against tools/portsmell_baseline.txt, which records what
each file had when a check was added. A finding in a file with no line is a
failure; more findings than the line allows is a failure; fewer is fine and
reported, and a file that reaches zero must lose its line, so the list empties
as the backlog is rewritten (the shape tools/portstyle.py's baseline uses).
Deliberate exceptions live on the baseline line as a `#` reason.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GAME = ROOT / "game"
BASELINE = ROOT / "tools" / "portsmell_baseline.txt"
RAM_MAP = ROOT / "re" / "ram.toml"

# game/registry.c is the dispatch table: a list of the ROM's addresses is what
# it is for, and the names and addresses in it are not a smell.
REGISTRY = "game/registry.c"


# --- reading source ------------------------------------------------------

def comments(text: str):
    """[(line, text)] for every comment, with string literals already gone."""
    text = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)
    out = []
    for mo in re.finditer(r"/\*.*?\*/|//[^\n]*", text, re.S):
        out.append((text[:mo.start()].count("\n") + 1, mo.group(0)))
    return out


def code(text: str) -> str:
    """The source with comments blanked, newlines kept so lines still count."""
    def blank(mo):
        return "\n" * mo.group(0).count("\n")
    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def lines_of(text: str, pattern: re.Pattern):
    """[(line, the matching line's text)] for a pattern over source."""
    out = []
    for mo in pattern.finditer(text):
        line = text[:mo.start()].count("\n") + 1
        out.append((line, text.splitlines()[line - 1].strip() if line <= len(text.splitlines()) else mo.group(0)))
    return out


# The C function definitions in a file: (name, parameters). The repo writes the
# brace on its own line, so a definition is a line that ends in its parameter
# list. tools/portstyle.py has its own pattern for the same lines: it only counts
# definitions and reads their parameters, so it doesn't capture the whole name.
DEFN = re.compile(r"^(?:static\s+)?(?:[A-Za-z_]\w*[\s*]+)+?(\w+)\s*\(([^;{]*)\)\s*$", re.M)


def definitions(text: str):
    """[(line, name, parameters)] for the functions a file defines."""
    body = code(text)
    return [(body[:mo.start()].count("\n") + 1, mo.group(1), mo.group(2)) for mo in DEFN.finditer(body)]


# --- the checks ----------------------------------------------------------
#
# Each is a function over (path, text): it gets the file's relative path and its
# contents, and returns [(line, what was found)]. Register it in CHECKS with the
# rubric items it stands for and the one-line message a finding prints.

# 65816 mnemonics, as they appear when the assembly is quoted: a mnemonic
# followed by an operand ($, # or a register). Prose that merely names a pair of
# branches ("a BMI/BPL spelled out") has no operand and is not a quotation.
MNEMONICS = (
    "LDA|LDX|LDY|STA|STX|STY|STZ|JSR|JSL|JMP|JML|RTS|RTL|RTI|BRA|BRL|BEQ|BNE|BMI|BPL|BCC|BCS|BVC|BVS|"
    "ADC|SBC|AND|ORA|EOR|CMP|CPX|CPY|INC|INX|INY|DEC|DEX|DEY|ASL|LSR|ROL|ROR|BIT|TSB|TRB|"
    "SEP|REP|SEC|CLC|SEI|CLI|SED|CLD|PHA|PHX|PHY|PHP|PHD|PHB|PHK|PLA|PLX|PLY|PLP|PLD|PLB|"
    "TAX|TAY|TXA|TYA|TXY|TYX|TSX|TXS|TCD|TDC|TCS|TSC|XBA|XCE|MVN|MVP|PEA|PEI|PER|WAI|WDM|NOP|BRK|COP"
)
ASM_QUOTED = re.compile(r"\b(?:%s)\b\s*(?:#?\$[0-9A-Fa-f]|#[0-9A-Za-z]|[AXY]\b)" % MNEMONICS)
# A write to a hardware register, which is what a quoted write sequence is made
# of: the PPU's $21xx and the DMA controller's $42xx and $43xx.
ASM_HARDWARE = re.compile(r"\b(?:%s)\b\s*#?\$(?:21|42|43)[0-9A-Fa-f]{2}\b" % MNEMONICS)


def asm_in_comments(path: str, text: str):
    """Item 15: assembly quoted in a comment, the most blatant form of item 8.

    A single instruction named in prose ("its JSR $09DA", the call site a port
    stands in for) is provenance, which CLAUDE.md asks for. What this finds is a
    quoted *sequence* -- two or more instructions with operands -- or a write to
    a hardware register, which is the write order the platform services exist to
    hide."""
    out = []
    for line, body in comments(text):
        hits = list(ASM_QUOTED.finditer(body))
        hardware = ASM_HARDWARE.search(body)
        if len(hits) < 2 and not hardware:
            continue
        mo = hardware or hits[0]
        out.append((line, body[max(0, mo.start() - 20):mo.end() + 20].replace("\n", " ").strip()))
    return out


# "Admitted ignorance": a comment that says the port doesn't know what it is
# describing. Its fix has an owner -- find the reader, or record it in
# docs/TODO.md -- so it is never allowed to sit in a port's comment.
IGNORANCE = re.compile(r"\b(?:presumably|probably|apparently|unclear|unknown|"
                       r"not sure|no idea|seems to|looks like it|"
                       r"isn'?t ported|not ported yet|whatever reads|"
                       r"for some reason|mysteriously)\b", re.I)


def admitted_ignorance(path: str, text: str):
    """Item 17: a comment admitting it doesn't know what a value is for."""
    out = []
    for line, body in comments(text):
        mo = IGNORANCE.search(body)
        if mo:
            out.append((line, body.replace("\n", " ").strip()[:120]))
    return out


TWIN = re.compile(r"^(.*?)_(\d+)$")


def twin_functions(path: str, text: str):
    """Item 2: `loop_animation`, `loop_animation_0`, `loop_animation_1` -- one
    operation per encoding of its operands, instead of one operation and rows."""
    defns = definitions(text)
    have = {name for _line, name, _params in defns}
    out = []
    for line, name, _params in defns:
        mo = TWIN.match(name)
        if mo and (mo.group(1) in have or mo.group(1) + "_%d" % (int(mo.group(2)) + 1) in have):
            out.append((line, name))
    return out


# Functions that are the CPU's arithmetic unit written out in C. The game's
# arithmetic is `a * b` and `n / d`; a carry, an overflow or a leftover a caller
# reads is derived in the wrapper from those results.
CPU_ARITHMETIC = re.compile(
    r"^(?:adc|sbc|asl|lsr|rol|ror|"
    r"add_(?:with_)?carry|sub_(?:with_)?(?:carry|borrow)|carry_add|borrow_sub|"
    r"rotate_(?:left|right)(?:_\w+)?|shift_(?:left|right)(?:_\w+)?|"
    r"divide_by_(?:rotation|shifting)|multiply_by_(?:shifting|partial_products))$")


def cpu_arithmetic(path: str, text: str):
    """Item 12: the arithmetic unit emulated in the logic."""
    return [(line, name) for line, name, _params in definitions(text) if CPU_ARITHMETIC.match(name)]


# A parameter that exists so the entry wrapper can rebuild a register: a
# `*Trace *` out-parameter, or a name that says who it is for.
TRACE_PARAM = re.compile(r"\b\w*Trace\s*\*|\b\w+_for_wrapper\b|\bbool\s*\*\s*\w*_(?:flag|carry|overflow)\b")


def register_out_params(path: str, text: str):
    """Item 26: a trace or a flag carried out as a parameter so a wrapper can
    rebuild the original's registers. Item 84's form -- result *fields* with
    game-sounding names that only say which registers are defined -- needs a
    reader, and stays with the review."""
    out = []
    for line, name, params in definitions(text):
        mo = TRACE_PARAM.search(params)
        if mo:
            out.append((line, "%s(%s)" % (name, mo.group(0).strip())))
    return out


# The scaffolding switch is a switch, not a value: `ORIGINAL_EFFECTS && x` puts
# it into what a function means.
SWITCH_AS_VALUE = re.compile(r"(?:return|\|\||&&|\?|==|!=)\s*!?\s*ORIGINAL_EFFECTS\b"
                             r"|\bORIGINAL_EFFECTS\s*(?:&&|\|\||\?)")


def scaffolding_switch_as_value(path: str, text: str):
    """Item 23: the scaffolding switch used as a value."""
    return lines_of(code(text), SWITCH_AS_VALUE)


# A guard or a handler that knows itself by where the machine is. Setting the PC
# is how a port says where the machine resumes (an idle loop's exit, a bridge's
# call), which is legitimate; reading it to work out which entry is running, or
# to look an address up in a table, is item 72.
PC = re.compile(r"(?:s->cpu\.pc|c->pc|cpu\.pc)\b")
ASSIGNED = re.compile(r"\s*=[^=]")


def pc_identity(path: str, text: str):
    """Item 72: a guard or handler that knows itself by reading the machine's PC."""
    body = code(text)
    out = []
    for mo in PC.finditer(body):
        if ASSIGNED.match(body[mo.end():]):
            continue            # setting where the machine resumes, not asking where it is
        line = body[:mo.start()].count("\n") + 1
        out.append((line, body.splitlines()[line - 1].strip()))
    return out


# `!((uint16_t)(x - LIMIT) & 0x8000)` is a BMI/BPL spelled out: the sign bit of
# a difference, taken as a comparison.
SIGN_BIT_OF_DIFFERENCE = re.compile(r"\([^()]*[-+][^()]*\)\s*&\s*0x(?:8000|80)\b")


def sign_bit_compare(path: str, text: str):
    """Item 19: branch-shaped compares -- the sign bit of a difference where
    the logic means `<`."""
    return lines_of(code(text), SIGN_BIT_OF_DIFFERENCE)


CALL_OUT = re.compile(r"\bm_(?:jsr|jsl)(?:_dc)?\s*\(\s*\w+\s*,\s*(0x[0-9A-Fa-f]{6})\s*,\s*(0x[0-9A-Fa-f]{6})")
REGISTERED = re.compile(r"\.addr\s*=\s*(0x[0-9A-Fa-f]{6})\s*,\s*\.name\s*=\s*\"(\w+)\"")


def registered_entries() -> dict:
    """{address: entry name} from game/registry.c."""
    text = (ROOT / REGISTRY).read_text(encoding="utf-8")
    return {int(a, 16): name for a, name in REGISTERED.findall(text)}


def sibling_through_machine(path: str, text: str, entries=None):
    """Items 22 and 61: a call-out whose target is one of our own ports, called
    through an emulated JSR where a C call would do."""
    if entries is None:
        entries = registered_entries()
    body = code(text)
    out = []
    for mo in CALL_OUT.finditer(body):
        target = int(mo.group(2), 16)
        if target in entries:
            line = body[:mo.start()].count("\n") + 1
            out.append((line, "%s -> %s (a registered port)" % (mo.group(0).split("(")[0], entries[target])))
    return out


# A path to a source file, as the RAM map's `source` lines and the headers'
# comments cite them.
CITED_PATH = re.compile(r"\b((?:game|runtime|tools|frontend|re)/[\w./-]+\.(?:c|h|py|toml))\b")


def stale_provenance(path: str, text: str):
    """Item 25: a `source` line or a header comment naming a file that is gone."""
    out = []
    for line, body in comments(text) if path.endswith((".c", ".h")) else [(i + 1, l) for i, l in
                                                                         enumerate(text.splitlines())]:
        for cited in CITED_PATH.findall(body):
            if not (ROOT / cited).exists():
                out.append((line, "%s: no such file" % cited))
    return out


# A bridge (`bridge.c`, `script_bridge.c`) is where a port meets the original's
# calling convention, and a leaves file (`leaves.c`, `window_leaves.c`) is where
# it works out the registers it hands back: both are machine-shaped on purpose.
def bridge_or_leaves(path: str) -> bool:
    return path.endswith(("bridge.c", "leaves.c", "bridge.h", "leaves.h"))


def paren_end(text: str, i: int) -> int:
    """The offset of the `)` that closes the `(` at `i`."""
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                return j
    return len(text)


def control_of(head: str):
    """('if'|'else'|'loop'|None, condition) for the text a block or a
    statement starts with: `if (c)`, `else`, `else if (c)`, `for (...)`,
    `while (c)`, `do`."""
    head = head.strip()
    mo = re.match(r"(?:else\s+)?if\s*\(", head)
    if mo:
        return "if", head[mo.end():paren_end(head, mo.end() - 1)]
    if re.match(r"else\b", head):
        return "else", None
    mo = re.match(r"(?:for|while)\s*\(", head)
    if mo:
        return "loop", head[mo.end():paren_end(head, mo.end() - 1)]
    if re.match(r"do\b", head):
        return "loop", None
    return None, None


def blocks_of(body: str):
    """[[open, close, kind, condition, function]] for every brace block of
    source with comments and strings already blanked. An `else` block takes
    the condition of the `if` it follows; a function's body carries the
    parameters it was defined with."""
    blocks, stack, last_if = [], [], {}
    start = i = 0
    while i < len(body):
        ch = body[i]
        if ch == "(":
            i = paren_end(body, i) + 1
            continue
        if ch == "{":
            head = body[start:i]
            kind, cond = control_of(head)
            if kind == "else":
                kind, cond = "if", last_if.get(len(stack))
            params = None
            if not stack:
                mo = re.search(r"(\w+)\s*\(([^;{]*)\)\s*$", head)
                params = (mo.group(1), mo.group(2)) if mo else ("", "")
            stack.append([i, None, kind, cond, params])
            start = i + 1
        elif ch == "}":
            if stack:
                b = stack.pop()
                b[1] = i
                blocks.append(b)
                if b[2] == "if":
                    last_if[len(stack)] = b[3]
            start = i + 1
        elif ch == ";":
            start = i + 1
        i += 1
    return blocks


# What records a flag or a register: a helper named for one (`*_carry*`,
# `*_overflow*`, `*_flags*`, `*_registers*`, `leave_a`, `leave_y_a_byte`) or
# the CPU's flag set directly.
FLAG_NOTE = re.compile(r"\b\w*(?:carry|overflow|flags?|registers?|shifted_out)\w*\s*\("
                       r"|\b(?:battle_)?leave_[axy](?:_\w+)?\s*\(|\bCPU_FLAG_\w+")
EFFECT_CALL = re.compile(r"\bORIGINAL_EFFECT\s*\(")
# A scaffolding helper called plainly, outside ORIGINAL_EFFECT.
LEAVE_FLAG_CALL = re.compile(r"\b(?:battle_)?leave_\w*?(?:carry|overflow|flags?|registers?)\w*\s*\(")
# A condition about the machine or the bridge -- whether a call-out came back,
# a guard refused, a register's value -- rather than about the game.
MACHINE_CONDITION = re.compile(r"\bm_\w+\s*\(|\bs->|\bc->|\bcpu\b|\bORIGINAL_EFFECTS\b"
                               r"|\bstop|\brefus|\bguard|\binterrupt|\bnative|\bcame_back\b", re.I)
MACHINE_PARAM = re.compile(r"\b(?:Snes|Cpu)\s*\*")


def flag_notes_in_control_flow(path: str, text: str):
    """Items 93 and 105: a note of a flag -- `ORIGINAL_EFFECT` of something
    that records a carry, an overflow or a register -- inside a loop's body or
    under an `if` whose condition is the game's, so that the scaffolding runs
    on the original's path through the logic and pins the logic to its order.
    The legitimate form is one leaves helper that works the flag out from the
    final state, after the logic. A helper named `leave_*` is that form and
    is not read; a `leave_*carry*` call outside ORIGINAL_EFFECT counts only in
    a function with no machine parameter, where it can only be scaffolding (in
    a wrapper it is the registers the wrapper hands back, its job). Whether a
    condition is the game's is decided narrowly: one that reads the machine, a
    guard or whether a call-out came back is not."""
    if bridge_or_leaves(path):
        return []
    body = code(text)
    body = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', body)
    body = re.sub(r"'(?:\\.|[^'\\\n])*'", "''", body)
    blocks = blocks_of(body)
    lines = text.splitlines()
    out = []
    calls = [(mo, True) for mo in EFFECT_CALL.finditer(body)] + \
            [(mo, False) for mo in LEAVE_FLAG_CALL.finditer(body)]
    for mo, scaffolding in sorted(calls, key=lambda c: c[0].start()):
        at = mo.start()
        if scaffolding:
            if not FLAG_NOTE.search(body[mo.end():paren_end(body, mo.end() - 1)]):
                continue
        elif re.search(r"ORIGINAL_EFFECT\s*\(\s*$", body[max(0, at - 40):at]):
            continue            # counted as the ORIGINAL_EFFECT it is in
        enclosing = sorted((b for b in blocks if b[0] < at < b[1]), key=lambda b: b[0])
        if not enclosing or enclosing[0][4] is None:
            continue
        function, params = enclosing[0][4]
        if re.match(r"(?:battle_)?leave_", function):
            continue            # a leaves helper deriving the flag: the legitimate form
        if not scaffolding and MACHINE_PARAM.search(params):
            continue
        context = [(b[2], b[3]) for b in enclosing[1:] if b[2]]
        # a brace-less `if (c)` or loop right before the statement
        stmt = max(body.rfind(";", 0, at), body.rfind("{", 0, at), body.rfind("}", 0, at)) + 1
        kind, cond = control_of(body[stmt:at])
        if kind:
            context.append(("if" if kind == "else" else kind, cond))
        why = None
        for kind, cond in reversed(context):
            if kind == "loop":
                why = "in a loop"
                break
            if kind == "if" and cond is not None and not MACHINE_CONDITION.search(cond):
                why = "under if (%s)" % " ".join(cond.split())
                break
        if why:
            line = body[:at].count("\n") + 1
            out.append((line, "%s: %s" % (why, lines[line - 1].strip()[:100])))
    return out


# A return address pushed by hand: a hex constant, or a named one that says it
# is a return, a call site or a JSR's last byte.
PUSH = re.compile(r"\bm_push(?:16|24)\s*\(\s*\w+\s*,")
RETURN_LIKE = re.compile(r"\b0x[0-9A-Fa-f]{3,6}\b|\b[A-Z0-9_]*(?:RETURN|SITE|CALL|JSR|RTS|_TO)[A-Z0-9_]*\b")


def return_address_pushed(path: str, text: str):
    """Items 85 and 101: a return frame for the original's call stack built by
    hand outside a bridge -- `m_push16(s, 0xAC5C)`, `m_push16(s, SITE + JSR_SIZE
    - 1)`. A bridge may push its caller's frame for an inlined subroutine; a
    port elsewhere that does mirrors the ROM's call graph in its own body.
    Pushes of registers (`c->d`, `s->cpu.d`) and of plain locals are a
    wrapper saving state, and are not read."""
    if bridge_or_leaves(path):
        return []
    body = code(text)
    out = []
    for mo in PUSH.finditer(body):
        arg = body[mo.end():paren_end(body, body.index("(", mo.start()))]
        if re.search(r"\bs->|\bc->|\bcpu\b", arg) or not RETURN_LIKE.search(arg):
            continue
        line = body[:mo.start()].count("\n") + 1
        out.append((line, text.splitlines()[line - 1].strip()[:100]))
    return out


TYPEDEF_ENUM = re.compile(r"\}\s*(\w+)\s*;")
ENUM_BLOCK = re.compile(r"\btypedef\s+enum\b[^{]*\{", re.S)


def enum_types_nothing(path: str, text: str, tree=None):
    """Item 33: a `typedef enum` nothing takes as a type -- constants declared
    so a comment can mention them while the parameter stays `uint8_t`."""
    body = code(text)
    out = []
    for mo in ENUM_BLOCK.finditer(body):
        end = body.find("}", mo.end())
        if end < 0:
            continue
        name_mo = TYPEDEF_ENUM.match(body[end:end + 80])
        if not name_mo:
            continue
        name = name_mo.group(1)
        if tree is None:
            tree = tree_text()
        # Uses beyond the typedef itself: as a parameter, a field, a local, a
        # cast or a return type, anywhere in game/ or runtime/.
        uses = len(re.findall(r"\b%s\b" % re.escape(name), tree))
        if uses <= 1:
            out.append((body[:end].count("\n") + 1, "%s: declared, never used as a type" % name))
    return out


_TREE = None


def tree_text() -> str:
    """Every port and runtime source, concatenated: what a name's uses are counted in."""
    global _TREE
    if _TREE is None:
        parts = []
        for base in (GAME, ROOT / "runtime", ROOT / "tests"):
            for p in sorted(base.rglob("*.[ch]")) + sorted(base.rglob("*.py")):
                parts.append(p.read_text(encoding="utf-8", errors="replace"))
        _TREE = "\n".join(parts)
    return _TREE


# name, function, the rubric items it stands for, what a finding means
CHECKS = (
    ("asm-in-comments", asm_in_comments, "15, 8",
     "the assembly quoted in a comment; say what the routine does instead"),
    ("admitted-ignorance", admitted_ignorance, "17",
     "a comment that doesn't know what it describes; find the reader or file it in docs/TODO.md"),
    ("twin-functions", twin_functions, "2",
     "one function per encoding of the operands; make it one operation and several table rows"),
    ("cpu-arithmetic", cpu_arithmetic, "12",
     "the CPU's arithmetic unit written out in C; compute in C and derive the flags in the wrapper"),
    ("register-out-params", register_out_params, "26",
     "a parameter carrying the original's registers out to a wrapper; derive them from the state"),
    ("scaffolding-as-value", scaffolding_switch_as_value, "23",
     "the ORIGINAL_EFFECTS switch used as a value, so it decides what a function means"),
    ("pc-identity", pc_identity, "72",
     "a guard or handler that knows itself by the machine's PC; give each entry its own guard"),
    ("sign-bit-compare", sign_bit_compare, "19",
     "the sign bit of a difference where the logic means a comparison"),
    ("sibling-through-machine", sibling_through_machine, "22, 61",
     "a call-out into one of our own ports; call it as C"),
    ("stale-provenance", stale_provenance, "25",
     "a cited file that no longer exists"),
    ("enum-types-nothing", enum_types_nothing, "33",
     "an enum declared but never used as a type; the type is the vocabulary"),
    ("flag-note-in-logic", flag_notes_in_control_flow, "93, 105",
     "a flag noted inside the logic's loop or game branch; work it out once from the final state"),
    ("return-address-pushed", return_address_pushed, "85, 101",
     "a return frame pushed by hand outside a bridge; register the subroutine or port the call-out"),
)

# Which files each check reads. The RAM map is source for provenance only.
HEADERS_TOO = {"asm-in-comments", "admitted-ignorance", "stale-provenance", "enum-types-nothing",
               "register-out-params", "twin-functions", "cpu-arithmetic"}
# registry.c is the dispatch table: its comments explain each entry's calibrated
# cost pass by pass, which means naming instructions and their cycles.
SKIP_IN_REGISTRY = {"stale-provenance", "twin-functions", "asm-in-comments"}

# game/ram.h and game/vars.h are written by tools/ram_map.py from re/ram.toml, so
# a finding in them is a finding in the map: nothing can be fixed in the file.
GENERATED = re.compile(r"generated by\s+tools/")


def scan(only: str | None = None) -> dict:
    """{(path, check): [(line, what)]} over the ports and the RAM map."""
    entries = registered_entries()
    paths = [p for p in sorted(GAME.rglob("*.c"))] + [p for p in sorted(GAME.rglob("*.h"))]
    found: dict = {}
    for path in paths + [RAM_MAP]:
        rel = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")
        if GENERATED.search("\n".join(text.splitlines()[:6])):
            continue
        for name, fn, _items, _msg in CHECKS:
            if only and name != only:
                continue
            if path == RAM_MAP and name != "stale-provenance":
                continue
            if rel.endswith(".h") and name not in HEADERS_TOO:
                continue
            if rel == REGISTRY and name in SKIP_IN_REGISTRY:
                continue
            hits = fn(rel, text, entries) if fn is sibling_through_machine else fn(rel, text)
            if hits:
                found[(rel, name)] = hits
    return found


# --- the ratchet ---------------------------------------------------------

def read_baseline() -> dict:
    """{(path, check): allowance} from the baseline file."""
    if not BASELINE.exists():
        return {}
    out = {}
    for line in BASELINE.read_text(encoding="utf-8").splitlines():
        line = line.split("#")[0].strip()
        if line:
            path, name, count = line.rsplit(None, 2)
            out[(path, name)] = int(count)
    return out


def write_baseline(found: dict) -> None:
    lines = [
        "# What each port had when a rubric check was added (tools/portsmell.py).",
        "# --check fails on a finding in a file with no line here, and on more findings",
        "# than a line allows. Fewer is fine; a file that reaches zero loses its line, so",
        "# the list empties as the backlog is rewritten. A deliberate exception carries",
        "# its reason as a `#` comment on its line.",
        "",
    ]
    for (path, name), hits in sorted(found.items()):
        lines.append("%-44s %-24s %d" % (path, name, len(hits)))
    BASELINE.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def check(found: dict, out=sys.stdout) -> int:
    base = read_baseline()
    messages = {name: (items, msg) for name, _fn, items, msg in CHECKS}
    findings = []
    for (path, name), hits in sorted(found.items()):
        allowed = base.get((path, name), 0)
        if len(hits) > allowed:
            items, msg = messages[name]
            findings.append("%s: %s, %d finding(s) where %d are allowed (rubric item %s): %s"
                            % (path, name, len(hits), allowed, items, msg))
            for line, what in hits[:4]:
                findings.append("    %s:%d: %s" % (path, line, what))
    for (path, name), allowed in sorted(base.items()):
        if allowed and not found.get((path, name)):
            findings.append("%s: %s is clean now; delete its line from %s."
                            % (path, name, BASELINE.relative_to(ROOT).as_posix()))
    for f in findings:
        print("  " + f, file=out)
    left = sum(base.values())
    print("portsmell: %d finding%s, %d on the backlog across %d file(s)"
          % (len(findings), "" if len(findings) == 1 else "s", left,
             len({p for p, _ in base})), file=out)
    return 1 if findings else 0


def report(found: dict, per_file: bool, only: str | None = None, out=sys.stdout) -> None:
    base = read_baseline()
    if per_file:
        by_path: dict = {}
        for (path, name), hits in found.items():
            by_path.setdefault(path, []).append("%s %d" % (name, len(hits)))
        print("%-44s %s" % ("file", "findings"), file=out)
        for path in sorted(by_path):
            print("%-44s %s" % (path, ", ".join(sorted(by_path[path]))), file=out)
        return
    for name, _fn, items, msg in CHECKS:
        if only and name != only:
            continue
        hits = {p: h for (p, n), h in found.items() if n == name}
        total = sum(len(h) for h in hits.values())
        new = sum(max(0, len(h) - base.get((p, name), 0)) for p, h in hits.items())
        print("\n%s (rubric item %s): %d finding(s) in %d file(s), %d not on the baseline\n  %s"
              % (name, items, total, len(hits), new, msg), file=out)
        for path in sorted(hits):
            for line, what in hits[path]:
                print("    %s:%d: %s" % (path, line, what), file=out)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python3 tools/portsmell.py",
                                 description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="the ratchet: exit 1 on a finding")
    ap.add_argument("--only", help="one check, by name")
    ap.add_argument("--files", action="store_true", help="one line per file instead of per check")
    ap.add_argument("--list", action="store_true", help="the checks and the rubric items they stand for")
    ap.add_argument("--write-baseline", action="store_true",
                    help="record today's findings as the backlog (once, when a check is added)")
    args = ap.parse_args(argv)

    if args.list:
        for name, _fn, items, msg in CHECKS:
            print("%-24s rubric %-8s %s" % (name, items, msg))
        return 0

    found = scan(args.only)
    if args.write_baseline:
        write_baseline(found)
        print("wrote %s" % BASELINE.relative_to(ROOT).as_posix())
        return 0
    if args.check:
        return check(found)
    report(found, args.files, args.only)
    return 0


if __name__ == "__main__":
    sys.exit(main())
