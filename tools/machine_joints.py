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

The second count is of what `ct_native` links: a subsystem whose machine side
is a file of its own leaves it out of the native program (CMakeLists.txt's
`CT_NATIVE_GAME_SOURCES`, as `registry.c` is), so its joints there are the
gate's, not the product's.
"""

import argparse
import ast
import collections
import functools
import operator
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "game"

# (name, pattern, what retires it)
JOINTS = [
    ("registered entries", None, "ordinary functions their callers call"),
    ("held waits' continuations", None, "restartable C loops replace their repeated dispatch"),
    ("returns through the emulated stack", r"\bm_rt[sli]\(", "the entry wrappers deleted"),
    ("stops at the original's addresses", r"\bm_stop_at(?:_own)?\(|\bm_hold_at\(|\bnative_\w*dont_care_at_stop\(",
     "the next step called as C"),
    ("call-outs to entries by address", r"\bcall_out\w*\(|\bnative_(?:yield_|jump_)?call_dc\(",
     "the callee called as C"),
    ("siblings found in the registry", r"\bnative_hand_entry\(", "the sibling called as C"),
    ("time tags that take the machine", r"\bNATIVE_TIME_AS\(", "a tag with no machine"),
    ("register escapes", r"\bvid_snes_\w+\(|\bau_voices_snes_register\(", "a service call"),
    ("video service calls", r"\bvid_(?!snes_)\w+\(", "inventory, including inline value helpers"),
    ("video calls without host backend", None, "the named service implemented on the host"),
    ("cartridge reads by address", None, "named resource accessors serving the store's bytes"),
    ("scaffolding writes", r"\bORIGINAL_EFFECT\(", "its subsystem retiring the gate"),
    ("functions that take the machine", None, "the game's state and plain values as parameters"),
]

ENTRY = re.compile(r"0x[0-9A-Fa-f]{6}")


NATIVE_LEAVES_OUT = re.compile(r"list\(REMOVE_ITEM CT_NATIVE_GAME_SOURCES ([^)]*)\)")


def native_left_out():
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    return {f for m in NATIVE_LEAVES_OUT.finditer(text) for f in m.group(1).split()}


def sources():
    return sorted(p for p in GAME.rglob("*") if p.suffix in (".c", ".h"))


def missing_video_backends():
    """Public service declarations without a definition in the host backend.

    Inline encoders are already portable, and the explicitly SNES escapes
    have their own row. This measures implementation presence, not pixels.
    """
    header = (ROOT / "runtime/platform/video.h").read_text(encoding="utf-8")
    declared = set(re.findall(r"PF_API\s+\w+\s+(vid_(?!snes_)\w+)\(", header))
    native = "\n".join(p.read_text(encoding="utf-8")
                       for p in (ROOT / "runtime/platform/native").glob("*.c"))
    native = re.sub(r"/\*.*?\*/|//[^\n]*", "", native, flags=re.S)
    implemented = set(re.findall(r"\b(vid_\w+)\([^;{}]*\)\s*\{", native))
    return declared - implemented


def registry_entries():
    """The addresses in the registry's table (game_natives), not in what
    follows it (the retired subsystems' uncompared cells)."""
    text = (GAME / "registry.c").read_text(encoding="utf-8")
    start = text.index("game_natives[] = {")
    return len(ENTRY.findall(text[start:text.index("\n};", start)]))


def restartable_loops(source):
    """Functions with an explicit C loop and a saveable polling boundary.

    A caller merely naming a hold, or a wrapper calling one pass, has not
    replaced repeated dispatch. Count only the two runtime checkpoint APIs,
    not comments, declarations, or a call in a neighbouring function.
    """
    source = re.sub(r'"(?:\\.|[^"\\])*"', '""', without_comments(source))
    result = set()
    for m in DEFINITION.finditer(source):
        depth = 1
        end = m.end()
        while end < len(source) and depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        body = source[m.end():end - 1]
        name = re.search(r'(\w+)\s*\(', m[0])[1]
        if (not depth and
                re.search(r'\b(?:for|while)\s*\(', body) and
                re.search(r'\bnative_(?:poll_held|idle_held)\s*\(', body)):
            result.add(name)
    return result


def held_wait_counts(registry, loops):
    """Remaining dispatch rows and retained C-loop compatibility rows.

    `.loop` names the restartable idle loop; non-idle loops (warp) are the
    entry function itself. Metadata alone is insufficient: source must have
    the loop and checkpoint. The idle alias beside warp still counts.
    """
    registry = without_comments(registry)
    start = registry.index("game_held_waits[] = {")
    block = registry[start:registry.index("\n};", start)]
    rows = re.findall(r'\{([^{}]*\.addr\s*=[^{}]*)\}', block)
    converted = 0
    for row in rows:
        loop = re.search(r'\.loop\s*=\s*(\w+)\b', row)
        entry = re.search(r'\.fn\s*=\s*(\w+)\b', row)
        fn = loop or entry
        enabled = not loop or re.search(r'\.idle\s*=\s*true\b', row)
        converted += bool(enabled and fn and fn[1] in loops)
    return len(rows) - converted, converted


def held_wait_rows():
    loops = set().union(*(restartable_loops(p.read_text(encoding="utf-8")) for p in sources()))
    return held_wait_counts((GAME / "registry.c").read_text(encoding="utf-8"), loops)[0]


def count(per_file):
    totals = collections.Counter()
    by_file = collections.defaultdict(collections.Counter)
    missing_video = missing_video_backends()
    for path in sources():
        if path.name == "registry.c":
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        n = cartridge_reads(path, text)
        if n:
            name = "cartridge reads by address"
            totals[name] += n
            by_file[path.relative_to(ROOT).as_posix()][name] += n
        n = machine_functions(text)
        if n:
            name = "functions that take the machine"
            totals[name] += n
            by_file[path.relative_to(ROOT).as_posix()][name] += n
        missing = sum(len(re.findall(r"\b" + name + r"\(", text)) for name in missing_video)
        if missing:
            name = "video calls without host backend"
            totals[name] += missing
            by_file[path.relative_to(ROOT).as_posix()][name] += missing
        for name, pattern, _ in JOINTS:
            if pattern is None:
                continue
            n = len(re.findall(pattern, text))
            if n:
                totals[name] += n
                by_file[path.relative_to(ROOT).as_posix()][name] += n
    totals["registered entries"] = registry_entries()
    totals["held waits' continuations"] = held_wait_rows()
    by_file["game/registry.c"]["held waits' continuations"] = totals["held waits' continuations"]
    return totals, by_file


# A function definition: a return type and name at the start of a line, its
# parameters, and the body's opening brace.
DEFINITION = re.compile(r"^[A-Za-z_][\w \t*]*?\b\w+\s*\(([^;{}]*)\)\s*\{", re.M)


def machine_functions(text):
    """Function definitions with a `Snes *` parameter: each can run only on an
    emulated SNES, however its body reads (CLAUDE.md, "Keep the machine out of
    the logic")."""
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    return sum(1 for m in DEFINITION.finditer(text) if re.search(r"\bSnes\s*\*", m.group(1)))


def without_comments(text):
    # Keep strings intact here: quoted includes are needed by constant lookup.
    return re.sub(r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|/\*.*?\*/|//[^\n]*''',
                  lambda m: m[0] if m[0][0] in "\"'" else re.sub(r"[^\n]", " ", m[0]),
                  text, flags=re.S)


def expression_at(text, start):
    """One argument/initializer, with nested parentheses, brackets and braces."""
    depth = 0
    for end in range(start, len(text)):
        c = text[end]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if not depth:
                return text[start:end].strip(), end
            depth -= 1
        elif c in ",;" and not depth:
            return text[start:end].strip(), end
    return text[start:].strip(), len(text)


@functools.lru_cache(maxsize=None)
def header_text(path):
    return path.read_text(encoding="utf-8")


@functools.lru_cache(maxsize=None)
def source_definitions(source):
    source = without_comments(source).replace("\\\n", "")
    includes = re.findall(r'^\s*#\s*include\s*["<]([^">]+)[">]', source, re.M)
    entries = []
    for m in re.finditer(r'^\s*#\s*define\s+(\w+)(\([^\n]*?\))?[^\S\n]+([^\n]+)', source, re.M):
        parameters = tuple(p.strip() for p in m[2][1:-1].split(",")) if m[2] else None
        entries.append((m[1], parameters, m[3]))
    # An undef/redefinition is not a single constant for this source audit.
    for name in re.findall(r'^\s*#\s*undef\s+(\w+)', source, re.M):
        entries.extend(((name, None, ""), (name, None, "unknown")))
    for m in re.finditer(r'\benum\s*(?:\w+\s*)?\{([^}]+)\}', source):
        previous = "-1"
        for row in m[1].split(","):
            parts = row.strip().split("=", 1)
            name = parts[0].strip()
            if not re.fullmatch(r"\w+", name):
                continue
            value = parts[1] if len(parts) == 2 else f"({previous}) + 1"
            entries.append((name, None, value))
            previous = name
    return includes, entries


def constants_in(path, text):
    """Object/function macros and enums, including transitive quoted headers.

    Conflicting definitions remain unknown. We do not guess which conditional
    compilation branch or local scope applies to an address expression.
    """
    definitions = {}
    visited = set()

    def add(name, parameters, value):
        entry = (parameters, value.strip())
        if name in definitions and definitions[name] != entry:
            definitions[name] = None
        else:
            definitions[name] = entry

    def visit(file, source):
        file = file.resolve()
        if file in visited:
            return
        visited.add(file)
        includes, entries = source_definitions(source)
        for include in includes:
            for parent in (file.parent, GAME, ROOT / "runtime", ROOT):
                header = (parent / include).resolve()
                if header.is_file():
                    visit(header, header_text(header))
                    break
        for entry in entries:
            add(*entry)

    visit(path, text)
    return definitions


INTEGER_OPS = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
               ast.BitOr: operator.or_, ast.BitAnd: operator.and_, ast.BitXor: operator.xor,
               ast.LShift: operator.lshift, ast.RShift: operator.rshift}


def constant_value(expression, definitions, seen=frozenset()):
    """A small integer-expression evaluator; unsupported C is unknown, never RAM.

    Casts must be evaluated, not dropped: a low-word cast can turn a cartridge
    constant into a RAM offset. Only integer constants can be narrowed here.
    """
    casts = {}
    while True:
        m = re.search(r'\(\s*(u?int(?:8|16|32)_t|unsigned|int)\s*\)\s*', expression)
        if not m:
            break
        start = m.end()
        if start < len(expression) and expression[start] == "(":
            inner, end = expression_at(expression, start + 1)
            end += 1
        else:
            atom = re.match(r'(?:0[xX][\da-fA-F]+|\d+)[uUlL]*\b|[A-Za-z_]\w*', expression[start:])
            if not atom:
                return None
            inner, end = atom[0], start + atom.end()
        value = constant_value(inner, definitions, seen)
        if value is None:
            return None
        bits = re.search(r'(8|16|32)', m[1])
        if bits:
            width = int(bits[1])
            value &= (1 << width) - 1
            if m[1].startswith("int") and value & (1 << (width - 1)):
                value -= 1 << width
        key = f"__cast{len(casts)}"
        casts[key] = value
        expression = expression[:m.start()] + key + expression[end:]
    expression = re.sub(r'\b(0[xX][\da-fA-F]+|\d+)[uUlL]+\b', r'\1', expression)

    def evaluate(node):
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.Name):
            if node.id in casts:
                return casts[node.id]
            definition = definitions.get(node.id)
            if not definition or definition[0] is not None or node.id in seen:
                raise ValueError
            return required(constant_value(definition[1], definitions, seen | {node.id}))
        if isinstance(node, ast.BinOp) and type(node.op) in INTEGER_OPS:
            left, right = evaluate(node.left), evaluate(node.right)
            if isinstance(node.op, (ast.LShift, ast.RShift)) and not 0 <= right <= 32:
                raise ValueError
            return INTEGER_OPS[type(node.op)](left, right)
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub, ast.Invert)):
            value = evaluate(node.operand)
            return -value if isinstance(node.op, ast.USub) else ~value if isinstance(node.op, ast.Invert) else value
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
            name = node.func.id
            definition = definitions.get(name)
            if not definition or definition[0] is None or name in seen or node.keywords:
                raise ValueError
            parameters, body = definition
            if len(parameters) != len(node.args):
                raise ValueError
            local = dict(definitions)
            for parameter, argument in zip(parameters, node.args):
                local[parameter] = (None, str(evaluate(argument)))
            return required(constant_value(body, local, seen | {name}))
        raise ValueError

    def required(value):
        if value is None:
            raise ValueError
        return value

    try:
        return evaluate(ast.parse(expression.strip(), mode="eval").body)
    except (SyntaxError, ValueError, TypeError, RecursionError):
        return None


def work_ram_address(value):
    if value is None or not 0 <= value <= 0xFFFFFF:
        return False
    bank, offset = value >> 16, value & 0xFFFF
    return bank in (0x7E, 0x7F) or (bank < 0x40 and offset < 0x2000)


# Exact (file, operation, expression) -> audit evidence. No wildcard variable
# names: an unresolved source in a new caller must appear in the inventory.
# Leave unproved expressions counted rather than treating a RAM-looking name
# or a fixed bank ORed with an unbounded value as proof.
AUDITED_RAM_SOURCES = {}


def cartridge_sources(path, text):
    """Conservative candidates, with line, operation and source expression.

    Named data seams may resolve store addresses. Machine reads inside them
    still count. Elsewhere every unknown is a finding. Generic wram_read
    escapes also take bus addresses, so their unresolved operands count too.
    Source field assignments are conservatively included even when the
    descriptor's eventual consumer may use a named accessor. PF_AT is metadata.
    This is deliberately not a whole-program C type/range analysis.
    """
    named_data = path.name.endswith("_data.h") or path.name in (
        "packed_graphics.h", "assets.c", "decompress.c", "rom.h")
    definitions = constants_in(path, text)
    source = without_comments(text)
    source = re.sub(r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*' ''',
                    lambda m: re.sub(r"[^\n]", " ", m[0]), source, flags=re.X)
    operations = r'm_read\w*|rom_read\w*|rom_data\w*|wram_read(?:8|16|24)|PF_GAME'
    aliases = {}
    for name in definitions:
        target, visited = name, set()
        while target not in visited:
            visited.add(target)
            definition = definitions.get(target)
            if not definition or definition[0] is not None or not re.fullmatch(r'\w+', definition[1]):
                break
            target = definition[1]
            if re.fullmatch(operations, target):
                aliases[name] = target
                break
    calls = r'\b(' + operations + ''.join('|' + re.escape(name) for name in aliases) + r')\s*\('
    candidates = []
    for m in re.finditer(calls, source):
        kind = aliases.get(m[1], m[1])
        if named_data and not kind.startswith("m_read"):
            continue
        start = m.end()
        if kind != "PF_GAME" and not kind.startswith("rom_data"):
            _, start = expression_at(source, start)
            start += 1
        expression, _ = expression_at(source, start)
        # A typed parameter is a declaration, not a read of an address.
        if re.fullmatch(r'(?:const\s+)?u?int(?:8|16|32)_t\s+\w+', expression):
            continue
        candidates.append((m.start(), kind, expression))
    if not named_data:
        for m in re.finditer(r'(?:\.|->)source\s*=(?!=)', source):
            expression, _ = expression_at(source, m.end())
            candidates.append((m.start(), ".source", expression))
    relative = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.as_posix()
    for at, kind, expression in sorted(candidates):
        key = (relative, kind, re.sub(r"\s+", "", expression))
        if work_ram_address(constant_value(expression, definitions)) or key in AUDITED_RAM_SOURCES:
            continue
        yield source.count("\n", 0, at) + 1, kind, expression


def cartridge_reads(path, text):
    return sum(1 for _ in cartridge_sources(path, text))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--files", action="store_true", help="per file, most joints first")
    ap.add_argument("--cartridge-sources", action="store_true", help="unproved cartridge sources, with expressions")
    args = ap.parse_args()
    if args.cartridge_sources:
        for path in sources():
            for line, kind, expression in cartridge_sources(path, path.read_text(encoding="utf-8")):
                print(f"{path.relative_to(ROOT).as_posix()}:{line}: {kind}: {expression}")
        return
    totals, by_file = count(args.files)
    if args.files:
        for path, c in sorted(by_file.items(), key=lambda kv: -sum(kv[1].values())):
            print(f"{sum(c.values()):6d}  {path}: " + ", ".join(f"{n} {k}" for k, n in c.most_common()))
        return
    left_out = native_left_out()
    native = collections.Counter()
    for path, c in by_file.items():
        if path not in left_out:
            native.update(c)
    print("in game/, and in what ct_native links of it (not " + ", ".join(sorted(left_out)) + "):")
    for name, _, retires in JOINTS:
        print(f"{totals[name]:6d} {native[name]:6d}  {name}  (goes when: {retires})")


if __name__ == "__main__":
    main()
