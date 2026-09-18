"""The battle's two script formats, parsed from the ROM and assembled back
(docs/MATCHING.md, "The battle script formats"). No cartridge contents here:
the command tables below are read from the interpreters, handler by handler.

**Animation scripts.** `$C1:4BBE` runs up to sixteen channels a frame; channel
Y's 24-bit script pointer is `$5DBD + 4Y`. `$C1:4C9E` fetches the command byte,
leaves the channel where it is when the byte is `$DB` or more (`$C1:4CC9`),
and otherwise calls the handler from the word table `$C1:7A6B`
(`JSR ($7A6B,X)` at `$C1:4CD4`); a handler moves the pointer on by the A it
passes to `$C1:75BB`, or not at all to repeat next frame. Lengths below are
what each handler passes on its every path (some keep the length in `$8E`
and pass it once their wait is over). Channels are started by five loaders,
each an animation id through a table of header offsets:

    loader     header table   bank
    $C1:4401   $CD:5EC6       $CE
    $C1:450A   $CD:5FF0       $CD
    $C1:45EC   $CD:5EF0       $CE
    $C1:46C6   $CD:61F0       $CD
    $C1:47A4   $CD:63F0       $CE

A header is two channel masks, big-endian words (bit 15 is channel 0), then
one script pointer (a word in the header's bank) per set bit of the first mask
and then of the second. `$A0B3` (bit 6 of `$AE94`, `$C1:4099`) picks the mask:
clear, the first mask's channels start and the second's pointers are never
read; set, the first's are skipped and the second's start. Command `$1E` calls
a subroutine through the word table at `$CE:0000` (bank `$CE` whatever bank
the caller is in; the return address is kept at `$A569 + 4Y`), `$1F` returns
and `$21` returns when `$5D9F` is zero.

**Enemy scripts.** `$CC:8B08` holds a word per enemy (bank `$CC`, `$C1:AFD6`,
`$C1:B48C`). A script is two sections, each ending with `$FF`: the attack
section, which `$C1:8C09` evaluates for each enemy's turn, and the reaction
section, which `$C1:B488` finds by scanning for the first `$FF` and
`$C1:8461` evaluates after the party acts. A section is blocks; a block is
conditions, four bytes each (the byte dispatched through `$C1:B80D`, three
operands), `$FE`, then actions, `$FE`. The evaluators read the first
condition and, unless byte 4 is `$FE`, the second (`$C1:AFE9`), and skip a
failing block by scanning for `$FE` twice (`$C1:B054`, `$C1:B066`). An
action is a byte below `$80` (`$C1:8CE3` stops at any other) dispatched
through `$C1:B85F` when chosen and `$C1:B88D` when run, and its length is the
byte at `$FD:BA4A + action` (`$C1:8E68`): action `$04` there is `$FF`, but it
is one byte, a random choice among the four actions after it (`$C1:98C5`).
"""

import bisect
import re
from dataclasses import dataclass, field

from .memmap import fmt

FORMATS = ("battle-animation", "enemy-ai")


class ScriptError(ValueError):
    """Bytes a format rejects, or text that doesn't encode."""


# ---- animation scripts ------------------------------------------------------

ANIM_TABLE = 0xC17A6B
ANIM_COUNT = 0xDB                    # bytes from $DB up stop before the dispatch
PLACES = 0xC179D3                    # $C1:762E: JSR ($79D3,X), X = operand * 2 (8-bit ASL)
PLACES_COUNT = 72                    # layout: up to the $C1:7A63 table
DIRECTIONS = 0xC179A1                # $C1:75CC: JSR ($79A1,X)
DIRECTIONS_COUNT = 25                # layout: up to the $79D3 table
CALLS = 0xCE0000                     # command $1E's subroutine table
CALL_COUNT = 78                      # the words before its $FFFF fill
# (table, entries, bank of its headers and scripts, loader). Counts are the
# layout (up to the next table); FFFF entries are empty.
HEADER_TABLES = (
    (0xCD5EC6, 21, 0xCE, 0xC1442C),
    (0xCD5EF0, 128, 0xCE, 0xC14617),
    (0xCD5FF0, 256, 0xCD, 0xC14535),
    (0xCD61F0, 256, 0xCD, 0xC146EF),
    (0xCD63F0, 96, 0xCE, 0xC147CB),
)
# Entries that don't name a header: the unused ids of $CD:5FF0 (145-159) and
# $CD:61F0 (194-255) repeat one word, which in each case points into script
# bytes ($CD:95C4 is a command, $CD:EE23 the table after the scripts); their
# "masks" would start channels at unrelated bytes.
NOT_HEADERS = {0xCD5FF0: 0xCD95C4, 0xCD61F0: 0xCDEE23}


@dataclass(frozen=True)
class Anim:
    name: str
    handler: int
    length: int | None                # None: command $80, (byte 1 & 15) + 1
    kind: str = "next"                # next, end, halt, call, return, return-if, end-if
    places: tuple = ()                # operand offsets that index $C1:79D3
    directions: tuple = ()            # operand offsets that index $C1:79A1


def _anim_table():
    t = {}

    def put(ops, name, handler, length, kind="next", places=(), directions=(), alias=True):
        for i, op in enumerate(ops):
            nm = name if (i == 0 or not alias) else f"{name}_{op:02X}"
            t[op] = Anim(nm, handler, length, kind, places, directions)

    put([0x00], "end", 0x4CF0, 1, "end")                     # STZ $5DAC,X: the channel stops
    # $C1:4CF7, the handler of every table entry nothing else has: the channel
    # stays on it (it counts $9876 up each frame, or with $A3D1 set passes 0).
    stays = [0x01, 0x2F, 0x4B, 0x4C, *range(0x56, 0x5D), 0x7E, 0x7F, *range(0x86, 0x98), 0x9E, 0x9F,
             0xA0, 0xA1, 0xA3, 0xA6, 0xA7, *range(0xAA, 0xC0), *range(0xC6, 0xD0)]
    put(stays, "halt", 0x4CF7, 1, "halt")
    put([0x02], "anim", 0x4D04, 2)                           # sprite animation byte 1 ($A1E8 from $1C99)
    put([0x03], "anim_wait", 0x4E22, 2)                      # the same, then waits for $A124 to clear
    put([0x04, 0x05, 0x06], "anim_wait_b", 0x4EA2, 2)        # the same through $A150
    put([0x07], "speed_default", 0x4F1A, 1)                  # $A324/$A2FE from $98BF/$973F
    for op in range(0x08, 0x10):                             # $A2FE/$A324 from $CC:F5BC/$CC:F4BC + op - 8
        t[op] = Anim(f"speed_{op - 8}", 0x4F54, 1)
    # Four families that set a destination and wait for the move: x and y
    # operands, the saved $5D9E/$5D9F, or a place (the $79D3 table).
    put([0x10], "move_xy", 0x4FBF, 3)
    put([0x11], "move_saved", 0x4FD1, 1)
    put([0x12], "move_place", 0x4FE3, 2, places=(1,))
    put([0x13], "move_b_xy", 0x51EE, 4)                      # byte 1: $82 ($FF: $A5AA)
    put([0x14], "move_b_saved", 0x5205, 2)
    put([0x15], "move_b_place", 0x521C, 3, places=(2,))
    put([0x16], "move_c_xy", 0x5409, 3)
    put([0x17], "move_c_saved", 0x541B, 1)
    put([0x18], "move_c_place", 0x542D, 2, places=(1,))
    put([0x19], "move_d_xy", 0x5611, 3)
    put([0x1A], "move_d_saved", 0x566C, 1)
    put([0x1B], "move_d_place", 0x567D, 2, places=(1,))
    put([0x1C], "cmd_1C", 0x5694, 2)                         # $A437 from byte 1
    put([0x1D], "cmd_1D", 0x56BB, 2)                         # $A437 + byte 1 = $FF
    put([0x1E], "call", 0x56CC, 2, "call")
    put([0x1F], "return", 0x56FB, 1, "return")
    put([0x20], "wait", 0x5714, 2)                           # byte 1 frames ($5DFD + channel)
    put([0x21], "return_unless_y", 0x5722, 1, "return-if")   # returns when $5D9F is 0
    put([0x22], "wait_var", 0x572C, 3)                       # until $5D80 + byte 1 = byte 2
    put([0x23], "wait_a", 0x5743, 2)                         # until $5D9C = byte 1
    put([0x24], "wait_b", 0x5756, 2)                         # until $5D9D = byte 1
    put([0x25], "end_if_absent", 0x5769, 2, "end-if")        # ends when $A018 + byte 1 is negative
    put([0x26], "anim_count", 0x577D, 3)                     # animation byte 1, $A150 = byte 2
    put([0x27], "cmd_27", 0x5806, 1)
    put([0x28], "cmd_28", 0x5840, 1)
    put([0x29], "cmd_29", 0x5844, 1)
    put([0x2A], "cmd_2A", 0x5879, 1)
    put([0x2B], "cmd_2B", 0x587D, 1)
    put([0x2C], "cmd_2C", 0x58AB, 1)
    put([0x2D], "cmd_2D", 0x58DC, 1)                         # $CFFF = 1
    put([0x2E], "cmd_2E", 0x58E4, 1)                         # $CFFF = 0
    # The script variables: $5D80 + n, and the registers a ($5D9C), b ($5D9D),
    # x ($5D9E) and y ($5D9F).
    put([0x30], "set_var", 0x58EC, 3)
    put([0x31], "set_a", 0x58FE, 2)
    put([0x32], "set_b", 0x590C, 2)
    put([0x33], "load_place", 0x591A, 2, places=(1,))        # x, y = place byte 1
    put([0x34], "inc_var", 0x5931, 2)
    put([0x35], "inc_a", 0x5940, 1)
    put([0x36], "inc_b", 0x594B, 1)
    put([0x37], "dec_var", 0x5956, 2)
    put([0x38], "dec_a", 0x5965, 1)
    put([0x39], "dec_b", 0x5970, 1)
    put([0x3A], "add_var", 0x597B, 3)
    put([0x3B], "add_a", 0x5991, 2)
    put([0x3C], "add_a_var", 0x59A3, 2)
    put([0x3D, 0x3E, 0x3F, 0x40, 0x45, 0x46], "save_place", 0x59B9, 2, places=(1,))   # to $5DA0 by the byte
    put([0x41], "copy_var", 0x59E4, 3)
    put([0x42], "store_a", 0x59FA, 2)
    put([0x43], "load_place_offset", 0x5A0C, 4, places=(1,))
    put([0x44], "load_actor_xy", 0x5A3E, 1)
    put([0x47, 0x48, 0x81, 0x82, 0x83, 0x84], "keep_place", 0x623C, 2, places=(1,))  # $A4E8 = byte + 1, read at $C1:4218
    put([0x49], "add_var_clamped", 0x5A70, 3)
    put([0x4A, 0x4D], "swap_a_b", 0x5A99, 1)
    put([0x4E], "swap_a_x", 0x5AAF, 1)
    put([0x4F], "swap_a_y", 0x5AC5, 1)
    put([0x50, 0x51, 0x52, 0x53, 0x54, 0x55], "numbers", 0x5ADB, 1)   # game/battle/numbers.c
    put([0x5D], "cmd_5D", 0x5CF1, 1)
    put([0x5E], "cmd_5E", 0x5D12, 1)
    put([0x5F], "skip", 0x5D18, 2)                           # passes 2, reads nothing
    put([0x60], "cmd_60", 0x5D1D, 2)
    put([0x61, 0x62, 0x63, 0x64], "cmd_61", 0x5D93, 4)
    put([0x65, 0x66, 0x67, 0x68], "cmd_65", 0x5DC1, 1)
    put([0x69], "cmd_69", 0x5DD3, 2)
    put([0x6A], "cmd_6A", 0x5E87, 1)
    put([0x6B], "cmd_6B", 0x5EDF, 2)
    put([0x6C], "cmd_6C", 0x5F13, 3)
    put([0x6D], "cmd_6D", 0x5F9A, 1)
    put([0x6E], "cmd_6E", 0x5FA5, 1)
    put([0x6F], "cmd_6F", 0x5FB3, 1)
    put([0x70], "cmd_70", 0x5FC1, 1)
    put([0x71], "cmd_71", 0x5FD3, 1)
    put([0x72], "face", 0x5FDE, 2, directions=(1,))          # $A2B4 through the $79A1 table
    put([0x73], "cmd_73", 0x6059, 2)
    put([0x74], "cmd_74", 0x605F, 2)
    put([0x75], "cmd_75", 0x6093, 2)
    put([0x76], "cmd_76", 0x60E6, 3, places=(1, 2))
    put([0x77], "cmd_77", 0x6154, 2)
    put([0x78, 0x79], "sound", 0x619E, 2)                    # $1E01 = byte 1, JSL $C7:0004
    put([0x7A, 0x7B], "sound_at", 0x61C1, 3, places=(2,))
    put([0x7C], "sound_c", 0x61EB, 1)                        # the byte from $CD:6D4C + $A4E7
    put([0x7D], "sound_d", 0x61F5, 1)                        # from $CD:6DAC + $A4E7
    put([0x80], "copy_53", 0x621B, None)                     # byte 1 & 15 bytes (from byte 1) to $53, JSL $CD:001B
    put([0x85], "cmd_85", 0x6258, 2)
    put([0x98, 0x9C], "cmd_98", 0x6299, 3, places=(2,))
    put([0x99, 0x9D], "cmd_99", 0x672E, 2)
    put([0x9A], "cmd_9A", 0x6743, 3, places=(2,))
    put([0x9B], "cmd_9B", 0x674A, 2)
    put([0xA2], "cmd_A2", 0x6751, 1)
    put([0xA4], "cmd_A4", 0x691B, 3)
    put([0xA5], "cmd_A5", 0x69BA, 1)
    put([0xA8], "cmd_A8", 0x69C3, 2)
    put([0xA9], "cmd_A9", 0x6AF0, 2)
    put([0xC0], "cmd_C0", 0x6CF3, 5, places=(1,))
    put([0xC1], "cmd_C1", 0x6F31, 4)
    put([0xC2], "cmd_C2", 0x6F53, 5, places=(1,))
    put([0xC3], "cmd_C3", 0x6F75, 4)
    put([0xC4], "cmd_C4", 0x6F9A, 3)
    put([0xC5], "cmd_C5", 0x6FEF, 2)
    put([0xD0], "cmd_D0", 0x6FFC, 1)
    put([0xD1], "cmd_D1", 0x702B, 1)
    put([0xD2], "cmd_D2", 0x705A, 2, places=(1,))
    put([0xD3], "cmd_D3", 0x720E, 1)
    put([0xD4], "cmd_D4", 0x7349, 1)
    put([0xD5], "cmd_D5", 0x73AD, 1)
    put([0xD6], "cmd_D6", 0x7364, 1)
    put([0xD7], "cmd_D7", 0x740B, 3, places=(2,))
    put([0xD8], "cmd_D8", 0x74B4, 4)
    put([0xD9], "cmd_D9", 0x753A, 2)                         # $99B5 = byte 1
    # $C1:7579 reads byte 1 but passes 1: the byte after it is also the next command.
    put([0xDA], "cmd_DA", 0x7579, 1)
    for op in range(ANIM_COUNT, 0x100):                      # $C1:4CC9: not dispatched, the channel stays
        t[op] = Anim(f"stop_{op:02X}", 0, 1, "halt")
    return t


ANIM = _anim_table()
ANIM_BY_NAME = {c.name: op for op, c in ANIM.items()}


def anim_length(blob, i: int) -> int | None:
    """Length of the command at blob[i], or None if it runs past the end."""
    c = ANIM[blob[i]]
    n = c.length if c.length is not None else ((blob[i + 1] & 0x0F) + 1 if i + 1 < len(blob) else None)
    return n if n is not None and i + n <= len(blob) else None


def _index(i: int) -> int:
    """The table offset `TAX` sees after an 8-bit `ASL A` with B = 0."""
    return (i * 2) & 0xFF


@dataclass
class AnimParse:
    commands: dict = field(default_factory=dict)       # address -> (opcode, length)
    headers: dict = field(default_factory=dict)        # address -> (mask1, mask2)
    roots: dict = field(default_factory=dict)          # address -> why
    calls: set = field(default_factory=set)            # command $1E operands
    places: set = field(default_factory=set)           # operands indexing $C1:79D3
    directions: set = field(default_factory=set)       # operands indexing $C1:79A1
    returns: dict = field(default_factory=dict)        # subroutine address -> can return
    problems: list = field(default_factory=list)
    skipped: int = 0                                   # header table entries that name no header


def _popcount(v):
    return bin(v).count("1")


def header_size(rom_read, addr: int) -> tuple | None:
    raw = rom_read(addr, 4)
    if raw is None:
        return None
    m1, m2 = raw[0] << 8 | raw[1], raw[2] << 8 | raw[3]
    return m1, m2, 4 + 2 * (_popcount(m1) + _popcount(m2))


def parse_animation(rom) -> AnimParse:
    """Every script the five loaders and the call table can start."""
    res = AnimParse()
    for table, count, bank, _ in HEADER_TABLES:
        for i in range(count):
            w = rom.u16(table + 2 * i)
            if w == 0xFFFF or NOT_HEADERS.get(table) == bank << 16 | w:
                res.skipped += 1
                continue
            h = bank << 16 | w
            m1, m2, size = header_size(rom.read, h)
            res.headers[h] = (m1, m2)
            for j in range((size - 4) // 2):
                res.roots.setdefault(bank << 16 | rom.u16(h + 4 + 2 * j), f"{fmt(table)} entry {i}")
    for i in range(CALL_COUNT):
        res.roots.setdefault(0xCE0000 | rom.u16(CALLS + 2 * i), f"call table entry {i}")
    # A call continues after itself only if the subroutine can return, which
    # depends on the subroutines it calls: walk until nothing changes.
    subs = {0xCE0000 | rom.u16(CALLS + 2 * i) for i in range(CALL_COUNT)}
    res.returns = {s: False for s in subs}
    while True:
        before = dict(res.returns)
        res.commands.clear()
        res.problems.clear()
        res.calls.clear()
        res.places.clear()
        res.directions.clear()
        for s in subs:
            res.returns[s] = _walk(rom, [s], res, record=False)
        _walk(rom, list(res.roots), res, record=True)
        if res.returns == before:
            break
    return res


def _walk(rom, starts, res: AnimParse, record: bool) -> bool:
    """Follow commands from `starts`; True if a return is reachable."""
    work, seen, returns = list(starts), set(), False
    while work:
        a = work.pop()
        while a not in seen:
            seen.add(a)
            op = rom.read(a, 1)
            if op is None:
                res.problems.append(f"{fmt(a)}: script runs outside the ROM")
                break
            c = ANIM[op[0]]
            if c.length is None:
                b = rom.read(a + 1, 1)[0]
                n = (b & 0x0F) + 1
                if not b & 0x0F:
                    res.problems.append(f"{fmt(a)}: command 80 copies 256 bytes")
            else:
                n = c.length
            raw = rom.read(a, n)
            if record:
                res.commands[a] = (op[0], n)
                res.places.update(raw[o] for o in c.places)
                res.directions.update(raw[o] for o in c.directions)
            if c.kind in ("return", "return-if"):
                returns = True
            if c.kind == "call":
                if raw[1] >= CALL_COUNT:
                    res.problems.append(f"{fmt(a)}: call {raw[1]:02X} is past the {CALL_COUNT}-entry table")
                    break
                if record:
                    res.calls.add(raw[1])
                target = 0xCE0000 | rom.u16(CALLS + _index(raw[1]))
                if not res.returns.get(target, False):
                    break
            if c.kind in ("end", "halt", "return"):
                break
            a = (a & 0xFF0000) | ((a + n) & 0xFFFF)
    return returns


def place_targets(rom, used) -> set:
    return {0xC10000 | rom.u16(PLACES + _index(i)) for i in used}


def direction_targets(rom, used) -> set:
    return {0xC10000 | rom.u16(DIRECTIONS + _index(i)) for i in used}


@dataclass
class AnimContext:
    headers: set = field(default_factory=set)          # header addresses
    starts: set = field(default_factory=set)           # command starts the parse reaches
    tables: dict = field(default_factory=dict)         # address -> word count (the call table)


def animation_context(rom) -> AnimContext:
    p = parse_animation(rom)
    return AnimContext(set(p.headers), set(p.commands), {CALLS: CALL_COUNT})


def _label(addr: int) -> str:
    return f"L_{addr:06X}"


_LABEL = re.compile(r"L_([0-9A-F]{6})$")


def _ref(addr: int, lo: int, hi: int) -> str:
    return _label(addr) if lo <= addr < hi else f"${addr & 0xFFFF:04X}"


def decode_animation(blob: bytes, start: int, ctx: AnimContext) -> str:
    """The readable form: one line per header, call table or command, a label
    where a pointer lands, `db` for bytes that don't decode at a boundary the
    parse agrees with."""
    end = start + len(blob)
    bank = start & 0xFF0000
    targets = set()
    items = []                                           # (address, size, text)
    o = 0
    barriers = sorted(a for a in ctx.headers | set(ctx.tables) | ctx.starts if start <= a < end)

    def next_barrier(a):
        k = bisect.bisect_right(barriers, a)
        return barriers[k] if k < len(barriers) else end

    while o < len(blob):
        a = start + o
        if a in ctx.tables:
            n = ctx.tables[a]
            if o + 2 * n > len(blob):
                raise ScriptError(f"{fmt(a)}: the call table runs past the region")
            words = [bank | blob[o + 2 * i] | blob[o + 2 * i + 1] << 8 for i in range(n)]
            targets.update(words)
            items.append((a, 2 * n, ("calls", words)))
            o += 2 * n
            continue
        if a in ctx.headers:
            if o + 4 > len(blob):
                raise ScriptError(f"{fmt(a)}: header runs past the region")
            m1, m2 = blob[o] << 8 | blob[o + 1], blob[o + 2] << 8 | blob[o + 3]
            k = _popcount(m1) + _popcount(m2)
            if o + 4 + 2 * k > len(blob):
                raise ScriptError(f"{fmt(a)}: header runs past the region")
            words = [bank | blob[o + 4 + 2 * i] | blob[o + 5 + 2 * i] << 8 for i in range(k)]
            targets.update(words)
            items.append((a, 4 + 2 * k, ("header", m1, m2, words)))
            o += 4 + 2 * k
            continue
        n = anim_length(blob, o)
        stop = next_barrier(a) - start
        if n is None or o + n > stop:
            # runs past the region, or into a header or a command the parse
            # starts elsewhere: bytes, up to that boundary
            items.append((a, stop - o, ("db", blob[o:stop])))
            o = stop
            continue
        items.append((a, n, ("cmd", blob[o:o + n])))
        o += n
    lines = []
    starts_at = {a for a, _, _ in items}
    bad = sorted(t for t in targets if start <= t < end and t not in starts_at)
    if bad:
        raise ScriptError(f"pointer into the middle of a line at {fmt(bad[0])}")
    for a, size, it in items:
        if a in targets:
            lines.append(f"{_label(a)}:")
        kind = it[0]
        if kind == "calls":
            words = it[1]
            for i in range(0, len(words), 8):
                lines.append("\tcalls " + " ".join(_ref(w, start, end) for w in words[i:i + 8]))
        elif kind == "header":
            _, m1, m2, words = it
            lines.append(f"\theader ${m1:04X} ${m2:04X}" + "".join(" " + _ref(w, start, end) for w in words))
        elif kind == "db":
            raw = it[1]
            for i in range(0, len(raw), 16):
                lines.append("\tdb " + " ".join(f"${b:02X}" for b in raw[i:i + 16]))
        else:
            raw = it[1]
            lines.append(f"\t{ANIM[raw[0]].name}" + "".join(f" ${b:02X}" for b in raw[1:]))
    return "\n".join(lines) + "\n"


def _tokens(text: str):
    for n, line in enumerate(text.split("\n"), 1):
        line = line.split(";", 1)[0].strip()
        if line:
            yield n, line


def _byte(tok: str, where: str) -> int:
    if not re.fullmatch(r"\$[0-9A-Fa-f]{1,2}", tok):
        raise ScriptError(f"{where}: {tok!r} isn't a byte ($XX)")
    return int(tok[1:], 16)


def _assemble(text: str, start: int, line_size, emit) -> bytes:
    """Two passes over `text`: sizes and labels, then bytes. line_size(words,
    where) -> size; emit(words, where, resolve) -> bytes."""
    labels, pos = {}, start
    parsed = []
    for n, line in _tokens(text):
        where = f"line {n}"
        if line.endswith(":"):
            name = line[:-1]
            m = _LABEL.fullmatch(name)
            if not m or int(m.group(1), 16) != pos:
                raise ScriptError(f"{where}: label {name} isn't at its address ({fmt(pos)})")
            labels[name] = pos
            continue
        words = line.split()
        parsed.append((where, words))
        pos += line_size(words, where)

    def resolve(tok, where):
        if tok in labels:
            return labels[tok]
        if re.fullmatch(r"\$[0-9A-Fa-f]{1,4}", tok):
            return int(tok[1:], 16)
        raise ScriptError(f"{where}: {tok!r} isn't a label of this region or a word ($XXXX)")

    out = bytearray()
    for where, words in parsed:
        out += emit(words, where, resolve)
    return bytes(out)


def encode_animation(text: str, start: int) -> bytes:
    def size(words, where):
        op = words[0]
        if op == "db":
            return len(words) - 1
        if op == "calls":
            return 2 * (len(words) - 1)
        if op == "header":
            return 4 + 2 * (len(words) - 3)
        if op not in ANIM_BY_NAME:
            raise ScriptError(f"{where}: no command {op!r}")
        return 1 + len(words) - 1

    def emit(words, where, resolve):
        op, args = words[0], words[1:]
        if op == "db":
            return bytes(_byte(t, where) for t in args)
        if op == "calls":
            return b"".join((resolve(t, where) & 0xFFFF).to_bytes(2, "little") for t in args)
        if op == "header":
            if len(args) < 2:
                raise ScriptError(f"{where}: header needs two masks")
            m = [resolve(t, where) for t in args[:2]]
            if _popcount(m[0]) + _popcount(m[1]) != len(args) - 2:
                raise ScriptError(f"{where}: the masks have {_popcount(m[0]) + _popcount(m[1])} channels, "
                                  f"not {len(args) - 2}")
            return bytes((m[0] >> 8, m[0] & 0xFF, m[1] >> 8, m[1] & 0xFF)) + b"".join(
                (resolve(t, where) & 0xFFFF).to_bytes(2, "little") for t in args[2:])
        code = ANIM_BY_NAME[op]
        c = ANIM[code]
        raw = bytes([code]) + bytes(_byte(t, where) for t in args)
        want = c.length if c.length is not None else ((raw[1] & 0x0F) + 1 if len(raw) > 1 else None)
        if want != len(raw):
            raise ScriptError(f"{where}: {op} takes {(want or 2) - 1} operand bytes, not {len(args)}")
        return raw

    return _assemble(text, start, size, emit)


# ---- enemy scripts ----------------------------------------------------------

ENEMY_POINTERS = 0xCC8B08
ENEMY_COUNT = 256
CONDITIONS = 0xC1B80D                # JSR ($B80D,X), X = byte * 2 (8-bit ASL)
CONDITION_COUNT = 41                 # layout: up to the $C1:B85F table
ACTION_LENGTHS = 0xFDBA4A            # one byte per action, $C1:8E68
ACTION_COUNT = 23                    # $C1:B85F and $C1:B88D, up to the next table
END_BLOCK, END_SECTION = 0xFE, 0xFF


@dataclass(frozen=True)
class Cond:
    name: str
    handler: int


# Condition names say what the handler tests where that is read; the rest
# keep their number. Byte 1 is a target selector (read through $C1:AC14,
# dispatched through $C1:B8BB) in 01, 03, 08-0F, 1C, 1D, 1F and 22; byte 2 is in
# 18 and 23-28.
_COND_NAMES = {
    0x00: "always",
    0x01: "hp_half",         # a target at or below half its maximum HP ($0003, $0005)
    0x02: "status",          # bytes 2, 3: an offset into the stat block and a mask all set
    0x03: "enemy_is",        # a target whose enemy id ($AEFF) is byte 2
    0x04: "present",         # enemy id byte 2 in the battle ($AF02) or not, by byte 3
    0x05: "enemies_at_most", # at most byte 1 enemies left
    0x06: "counter_at_least",  # the 24-bit $96F1 at least bytes 1-3
    0x07: "turns",           # $B320 for this enemy against byte 1, by byte 2
    0x08: "hp_at_most",      # a target's HP at most the word at 2
}
CONDITION_HANDLERS = (
    0x8EA7, 0x8EAB, 0x8F11, 0x8F87, 0x8FDA, 0x9013, 0x9045, 0x9082, 0x90BE, 0x9130, 0x918E, 0x91F9, 0x925D,
    0x92A3, 0x9314, 0x938D, 0x93E6, 0x942A, 0x9474, 0x94D2, 0x9514, 0x959A, 0x95D6, 0x95DA, 0x95FA, 0x9652,
    0x9656, 0x96A5, 0x96D4, 0x9728, 0x975C, 0x9765, 0x97AB, 0x97C0, 0x97D5, 0x95FA, 0x95FA, 0x95FA, 0x95FA,
    0x95FA, 0x95FA,
)
COND = {i: Cond(_COND_NAMES.get(i, f"{i:02X}"), h) for i, h in enumerate(CONDITION_HANDLERS)}


@dataclass(frozen=True)
class Action:
    name: str
    choose: int                      # handler in $C1:B85F
    run: int                         # handler in $C1:B88D


_ACTION_NAMES = {
    0x01: "attack",       # byte 1 the attack, byte 2 a target selector
    0x02: "tech",         # byte 1 the technique; targets from bytes 2 and 3
    0x04: "random",       # one of the next four actions ($C1:98C5)
    0x09: "vanish",       # the enemy leaves ($AEFF = $FF), animation byte 1
    0x0A: "die",          # HP 0 and leaves; byte 2 a message
    0x0B: "set_stat",     # stat block offset 1 = byte 2 (or ORed, byte 3); message byte 4
    0x0C: "add_stat",     # offset 1 += byte 2 (bit 7: subtract), clamped; message byte 3
    0x0F: "message",      # message byte 1 ($CD:0033)
    0x11: "set_stats",    # four (offset, value) pairs, message byte 9
    0x13: "add_counter",  # target byte 1, $B158 += byte 2 (bit 7: subtract)
    0x14: "add_stats",    # four (offset, amount) pairs, message byte 9
}
ACTION_CHOOSE = (0x9810, 0x983A, 0x983A, 0x98C4, 0x98C5, 0x9960, 0x9961, 0x9962, 0x9966, 0x9967, 0x9978,
                 0x9979, 0x997D, 0x997E, 0x997F, 0x9980, 0x9981, 0x99B4, 0x99B4, 0x99B4, 0x99B4, 0x99B4, 0x9981)
ACTION_RUN = (0x99B8, 0x99BE, 0x9A39, 0x9B46, 0x9B47, 0x9B48, 0x9B8C, 0x9B8D, 0x9C6E, 0x9C6F, 0x9CB3, 0x9D1B,
              0x9D72, 0x9DCE, 0x9E62, 0x9E63, 0x9E78, 0x9F5A, 0x9FD2, 0xA14E, 0xA188, 0xA20B, 0xA396)
ACTIONS = {i: Action(_ACTION_NAMES.get(i, f"act_{i:02X}"), ACTION_CHOOSE[i], ACTION_RUN[i])
           for i in range(ACTION_COUNT)}
ACTION_BY_NAME = {a.name: i for i, a in ACTIONS.items()}
COND_BY_NAME = {f"if_{c.name}": i for i, c in COND.items()}
if len(COND_BY_NAME) != CONDITION_COUNT:
    raise AssertionError("condition names must be distinct")


# The lengths at $FD:BA4A, which the executor adds to move to the next action
# (parse_enemy checks the ROM's table against them). $03 is 0 and $05 $FF: no
# typed script has either. $04 is $FF there too, but one byte here: $C1:98C5
# steps over it to one of the four actions after it.
ACTION_SIZES = {0x00: 4, 0x01: 4, 0x02: 6, 0x04: 1, 0x06: 4, 0x07: 4, 0x08: 4, 0x09: 2, 0x0A: 3, 0x0B: 5,
                0x0C: 4, 0x0D: 3, 0x0E: 2, 0x0F: 2, 0x10: 4, 0x11: 10, 0x12: 16, 0x13: 3, 0x14: 10, 0x15: 16,
                0x16: 12}
ACTION_TABLE_BYTES = {**ACTION_SIZES, 0x03: 0x00, 0x04: 0xFF, 0x05: 0xFF}


@dataclass
class EnemyParse:
    sections: list = field(default_factory=list)       # (start, end) in storage order
    conditions: dict = field(default_factory=dict)      # condition byte the evaluators dispatch -> {why}
    actions: set = field(default_factory=set)
    problems: list = field(default_factory=list)
    notes: list = field(default_factory=list)
    end: int = 0


def _blocks(blob, o):
    """Parse sections from blob[o] as the grammar has them: (end offset,
    [(start, end)]) up to the first byte that breaks it. A block's actions
    may end at the $FF that ends the section instead of their own $FE: the
    dispatcher stops at any byte of $80 and up."""
    sections = []
    while o < len(blob):
        s = o
        try:
            while blob[o] != END_SECTION:
                while blob[o] != END_BLOCK:
                    if blob[o] >= CONDITION_COUNT or o + 4 > len(blob):
                        raise IndexError
                    o += 4
                o += 1
                while blob[o] not in (END_BLOCK, END_SECTION):
                    n = ACTION_SIZES.get(blob[o])
                    if n is None or o + n > len(blob):
                        raise IndexError
                    o += n
                if blob[o] == END_BLOCK:
                    o += 1
            o += 1
        except IndexError:
            return s, sections
        sections.append((s, o))
    return o, sections


def parse_enemy(rom) -> EnemyParse:
    """The sections in storage order after the pointer table, and the
    condition bytes the evaluators dispatch when they read each enemy's
    script as the code does."""
    res = EnemyParse()
    table = rom.read(ACTION_LENGTHS, ACTION_COUNT)
    for i, n in enumerate(table):
        if ACTION_TABLE_BYTES.get(i) != n:
            res.problems.append(f"action {i:02X}: length {n} at {fmt(ACTION_LENGTHS + i)}, not the known "
                                f"{ACTION_TABLE_BYTES.get(i)}")
    base = ENEMY_POINTERS + 2 * ENEMY_COUNT
    data = rom.read(base, 0x10000 - (base & 0xFFFF))
    end, sections = _blocks(data, 0)
    res.sections = [(base + s, base + e) for s, e in sections]
    res.end = base + end
    for s, e in sections:
        o = s
        while data[o] != END_SECTION:
            while data[o] != END_BLOCK:
                o += 4
            o += 1
            while data[o] not in (END_BLOCK, END_SECTION):
                res.actions.add(data[o])
                o += ACTION_SIZES[data[o]]
            if data[o] == END_BLOCK:
                o += 1
    starts = {s for s, _ in res.sections}

    def byte(a):
        b = rom.read(a, 1)
        if b is None or not base <= a < res.end:
            raise ScriptError(f"{fmt(a)} is outside the sections")
        return b[0]

    def evaluate(a, why):
        """Blocks from a as $C1:AFE6 (attack) and $C1:8706 (reaction) walk
        them: the first condition, the second unless byte 4 is $FE, then to
        the $FE after the next $FE (the scans start one byte on), until a
        block starts with $FF or has condition $00 alone, which always
        passes ($C1:8EA7 sets no failure in $AF24), so nothing after it is
        evaluated."""
        try:
            while True:
                res.conditions.setdefault(byte(a), set()).add(why)
                if byte(a + 4) != END_BLOCK:
                    res.conditions.setdefault(byte(a + 4), set()).add(why)
                elif byte(a) == 0x00:
                    return
                a += 1
                while byte(a) != END_BLOCK:
                    a += 1
                a += 1
                while byte(a) != END_BLOCK:
                    a += 1
                a += 1
                if byte(a) == END_SECTION:
                    return
        except ScriptError as exc:
            res.problems.append(f"{why}: {exc}")

    for i in range(ENEMY_COUNT):
        p = 0xCC0000 | rom.u16(ENEMY_POINTERS + 2 * i)
        if not base <= p < res.end:
            res.problems.append(f"enemy {i}: script at {fmt(p)} is outside the sections")
            continue
        if p not in starts:
            res.notes.append(f"enemy {i}: its script starts at {fmt(p)}, inside a section")
        evaluate(p, f"enemy {i} attack")
        r = p
        while r < res.end and rom.read(r, 1)[0] != END_SECTION:     # $C1:B49A
            r += 1
        evaluate(r + 1, f"enemy {i} reaction")
    return res


def condition_targets(rom, used) -> set:
    return {0xC10000 | rom.u16(CONDITIONS + _index(c)) for c in used}


def decode_enemy(blob: bytes, start: int, count: int) -> str:
    """The pointer table (`count` words) and the sections after it, one line
    per condition, action and terminator: `then` and `end` are the $FE after a
    block's conditions and its actions, `done` the $FF after a section."""
    end = start + len(blob)
    if len(blob) < 2 * count:
        raise ScriptError("the pointer table runs past the region")
    ptrs = [(start & 0xFF0000) | blob[2 * i] | blob[2 * i + 1] << 8 for i in range(count)]
    body = []                                    # (address, line), so labels go where pointers land
    o = 2 * count

    def add(a, text):
        body.append((a, text))

    while o < len(blob):
        if blob[o] == END_SECTION:
            add(start + o, "done")
            o += 1
            continue
        # conditions
        while o < len(blob) and blob[o] != END_BLOCK:
            if blob[o] >= CONDITION_COUNT or o + 4 > len(blob):
                raise ScriptError(f"{fmt(start + o)}: condition {blob[o]:02X} isn't in the table")
            add(start + o, f"if_{COND[blob[o]].name}" + "".join(f" ${b:02X}" for b in blob[o + 1:o + 4]))
            o += 4
        if o >= len(blob):
            raise ScriptError("conditions run past the region")
        add(start + o, "then")
        o += 1
        while o < len(blob) and blob[o] not in (END_BLOCK, END_SECTION):
            b = blob[o]
            n = ACTION_SIZES.get(b)
            if n is None or o + n > len(blob):
                raise ScriptError(f"{fmt(start + o)}: action {b:02X} has no length")
            add(start + o, ACTIONS[b].name + "".join(f" ${x:02X}" for x in blob[o + 1:o + n]))
            o += n
        if o < len(blob) and blob[o] == END_BLOCK:
            add(start + o, "end")
            o += 1
    if body and body[-1][1] != "done":
        raise ScriptError("the last section doesn't end ($FF) in the region")
    at = {a for a, _ in body}
    bad = sorted(p for p in ptrs if start <= p < end and p not in at)
    if bad:
        raise ScriptError(f"pointer into the middle of a line at {fmt(bad[0])}")
    lines = ["\tscripts " + " ".join(_ref(p, start, end) for p in ptrs[i:i + 8]) for i in range(0, count, 8)]
    targets = set(ptrs)
    for a, text in body:
        if a in targets:
            lines.append(f"{_label(a)}:")
        lines.append("\t" + text)
    return "\n".join(lines) + "\n"


def encode_enemy(text: str, start: int) -> bytes:
    def size(words, where):
        w = words[0]
        if w == "scripts":
            return 2 * (len(words) - 1)
        if w in ("then", "end", "done"):
            return 1
        if w in COND_BY_NAME:
            return 4
        if w in ACTION_BY_NAME:
            return len(words)
        raise ScriptError(f"{where}: no condition or action {w!r}")

    def emit(words, where, resolve):
        w, args = words[0], words[1:]
        if w == "scripts":
            return b"".join((resolve(t, where) & 0xFFFF).to_bytes(2, "little") for t in args)
        if w in ("then", "end"):
            return bytes([END_BLOCK])
        if w == "done":
            return bytes([END_SECTION])
        if w in COND_BY_NAME:
            if len(args) != 3:
                raise ScriptError(f"{where}: a condition takes three operand bytes")
            return bytes([COND_BY_NAME[w]] + [_byte(t, where) for t in args])
        code = ACTION_BY_NAME[w]
        if ACTION_SIZES.get(code) != len(words):
            raise ScriptError(f"{where}: {w} takes {ACTION_SIZES.get(code, 1) - 1} operand bytes")
        return bytes([code] + [_byte(t, where) for t in args])

    return _assemble(text, start, size, emit)


# ---- the codec entry points (tools/disasm/dataformats.py, tools/matching) ----

def context(data: bytes) -> dict:
    """What decoding needs from the whole ROM: where the animation headers
    and the call table are, and the action lengths."""
    from .memmap import Rom
    return {"battle-animation": animation_context(Rom(data))}


def decode(form: str, blob: bytes, region, ctx: dict) -> str:
    if form == "battle-animation":
        if "battle-animation" not in ctx:
            raise ScriptError("no animation context")
        return decode_animation(blob, region.start, ctx["battle-animation"])
    if form == "enemy-ai":
        return decode_enemy(blob, region.start, region.params.get("count", ENEMY_COUNT))
    raise ScriptError(f"no format {form!r}")


def encode(form: str, text: str, region) -> bytes:
    if form == "battle-animation":
        return encode_animation(text, region.start)
    if form == "enemy-ai":
        return encode_enemy(text, region.start)
    raise ScriptError(f"no format {form!r}")


__all__ = ["ANIM", "ACTIONS", "COND", "FORMATS", "ScriptError", "context", "decode", "encode",
           "parse_animation", "parse_enemy"]
