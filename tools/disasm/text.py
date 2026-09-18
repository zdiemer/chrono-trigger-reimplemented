"""The game's string encodings, as codecs with a round trip (docs/MATCHING.md, MD2).

No cartridge contents are stored here: the tables below are what the readers
do with a byte, worked out from the code at the addresses given.

Each shape a reader interprets is its own format:

- ``dialog``: the decoder at `$C2:57DF` (set up) and `$C2:5823` (a step),
  reached through `$C2:0003`/`$C2:0009` by the field dialog (`$C0:20F2`) and
  by the menus (`$C2:F905`/`$C2:F90C`). A string is read through the long
  pointer at `$0231` (`$C2:58B2`): `$A0-$FF` is a glyph, `$21-$9F` a
  dictionary entry, `$00-$20` a control code through the table at `$C2:5903`.
- ``dialog-dictionary``: the 128 substrings those `$21-$9F` bytes name, read
  at `$C2:58DD` through the 16-bit pointers at `$DE:FA00` (bank `$DE` from the
  immediate at `$C2:58E8`); each is a length byte and that many glyph bytes,
  drawn by `$C2:5BF5`.
- ``menu``: the menu text drawer `$C2:EF65`/`$C2:EF7E` (game/menu/text.c):
  `$10-$FF` is a tile, `$01-$0F` a control code through the table at
  `$C2:EFD4`, `$00` ends the string (as does the tile limit in `$87`).
- ``names``: fixed-width name records, drawn by the menu drawer with a tile
  limit (item names at `$C2:BDD9`) and by the dialog decoder's name codes,
  which stop at the first `$EF` (`$C2:5D23`, `$C2:5D34`).

Readable form. A glyph with a character is that character; any other byte is
``[$XX]``. Control codes are ``[name]`` or ``[name $op ...]``, operands in hex
at their width. A dictionary entry is its expansion in braces, ``{...}``: the
expansions are distinct, so the braces say which entry without an index, and
the text reads as the words it stands for. Dialog line and page codes are
followed by a newline, which the encoder ignores.
"""

from dataclasses import dataclass
import re


class TextError(ValueError):
    pass


# The font order at $FF:2060 (dialog, 12x12 glyphs, 24 bytes each, the right
# four columns from $FF:3860) and the menu font agree on $A0-$FF: the digits
# $D4-$DD are what $C2:F20B and $C2:5C3E add to a digit, and the name records
# both readers draw are one byte string. $EF is the dialog's space (the name
# readers stop at it) and $FF the menus' blank tile ($C2:EF97, $C2:F08D);
# $F4-$FE are blank glyphs nothing uses, so they keep hex escapes.
GLYPHS = {}
for _i in range(26):
    GLYPHS[0xA0 + _i] = chr(ord("A") + _i)
    GLYPHS[0xBA + _i] = chr(ord("a") + _i)
for _i in range(10):
    GLYPHS[0xD4 + _i] = chr(ord("0") + _i)
GLYPHS.update({
    0xDE: "!", 0xDF: "?", 0xE0: "/", 0xE1: "\u201c", 0xE2: "\u201d", 0xE3: ":", 0xE4: "&",
    0xE5: "(", 0xE6: ")", 0xE7: "'", 0xE8: ".", 0xE9: ",", 0xEA: "=", 0xEB: "-", 0xEC: "+",
    0xED: "%", 0xEE: "\u266a", 0xEF: " ", 0xF0: "\u2665", 0xF1: "\u2026", 0xF2: "\u221e",
    0xF3: "#", 0xFF: "_",
})
CODES = {c: b for b, c in GLYPHS.items()}
assert len(CODES) == len(GLYPHS)


@dataclass(frozen=True)
class Code:
    name: str
    operands: tuple = ()     # operand widths in bytes, little endian
    newline: bool = False    # the readable form breaks the line after it


def _codes(table: dict) -> dict:
    return {b: (c if isinstance(c, Code) else Code(c)) for b, c in table.items()}


# $C2:5903, indexed by the byte ($00-$20). The handlers:
#   $01, $02  $C2:5B06: the next byte is the low half of a 16-bit glyph code.
#   $03       $C2:5945: an operand byte into $0216; $C0:21A8 waits that many
#             times 15 frames, and sends 0 to $C0:21D8, the exit the
#             terminator takes ($C0:21D3): the field dialog ends there.
#   $04-$0C   $C2:594F: $0215 = the code. $C2:5851 starts a new line at x 8
#             (odd codes) or 20 (even); $C0:2126 then clears the box first for
#             $09-$0C, waits for a key ($34 = $FFFF) for $07, $08, $0B, $0C,
#             and waits when the box is full for $05-$08. $04 is the state
#             the decoder starts in, and draws nothing.
#   $0D-$0F   $C2:5953/5985/59C8: a number, 8, 16 or 24 bits, read through the
#             argument pointer at $023D, not from the string.
#   $10       $C2:5B14: a bare RTS.
#   $11       $C2:5A26: the name of the character whose index is the next
#             argument byte ($023D), through $C2:5FD8.
#   $12       $C2:5A4C: an operand byte: 0 and 1 draw the 11-byte name record
#             at $CC:15C4 (skipping an icon $2F) or $CC:6500 whose index is
#             the next argument byte; 8 and up the dictionary entry operand
#             + $17 (the only way to entry $7F).
#   $13-$19   $C2:5B15: the character names at $7E:2C23 + 6n ($C2:5FD8).
#   $1A       $C2:5B3B: the name at $7E:2C23 again, through its own handler.
#   $1B-$1D   $C2:5B70: the party member in slot n ($7E:2980), by name.
#   $1E       $C2:5B56: the five glyphs at $C2:6146.
#   $1F       $C2:5B9B: the name of the item whose id is at $7F:0200, from
#             the record at $CC:0B5E + 11n after its icon byte.
#   $20       $C2:5BD6: the name at $7E:2C4D (slot 7 of $C2:5FD8's layout).
DIALOG_CODES = _codes({
    0x03: Code("delay", (1,)),
    0x05: Code("line", newline=True), 0x06: Code("line indent", newline=True),
    0x07: Code("line key", newline=True), 0x08: Code("line key indent", newline=True),
    0x09: Code("page", newline=True), 0x0A: Code("page indent", newline=True),
    0x0B: Code("page key", newline=True), 0x0C: Code("page key indent", newline=True),
    0x0D: "num8", 0x0E: "num16", 0x0F: "num24",
    0x11: "pcname", 0x12: Code("ext", (1,)),
    0x13: "crono", 0x14: "marle", 0x15: "lucca", 0x16: "robo", 0x17: "frog", 0x18: "ayla",
    0x19: "magus", 0x1A: "crono2", 0x1B: "member1", 0x1C: "member2", 0x1D: "member3",
    0x1E: "nadia", 0x1F: "item", 0x20: "epoch",
})
DIALOG_WIDE = (0x01, 0x02)      # prefixes of a 16-bit glyph code
DIALOG_DICT = range(0x21, 0xA0)
DIALOG_END = "end"              # $03 $00: the field dialog's other exit
DICTIONARY_ENTRIES = 128        # $DE:FA00: 256 bytes of pointers, then the entries

# $C2:EFD4, indexed by the byte ($01-$0F); game/menu/text.c has the handlers.
MENU_CODES = _codes({
    0x01: Code("line", newline=True),     # $C2:EFF4
    0x02: Code("at", (2,)),               # $C2:F005: position, plus $61
    0x03: Code("number", (2, 2)),         # $C2:F011: address, format ($C2:F114)
    0x04: Code("string", (3,)),           # $C2:F022: a substring at a long address
    0x05: Code("name", (2,)),             # $C2:F036: 5 tiles from $7E:operand
    0x06: Code("palette", (2,)),          # $C2:F0A2: palette bits from $7E:operand
    0x07: Code("tile", (2,)),             # $C2:F071: the word at $7E:operand
    0x08: Code("blanks", (1,)),           # $C2:F08D
    0x09: Code("mode", (1,)),             # $C2:F0B8: $88
    0x0A: Code("attr", (1,)),             # $C2:F068: $7E
    0x0B: Code("number2", (2, 2)),        # $C2:F0C1: address, format ($C2:F227)
    0x0C: Code("limited", (1, 2)),        # $C2:F0D1: limit index, $7E address
    # $0D-$0F point at the RTS $C2:EFD3.
})

_TOKEN = re.compile(r"\[([^\]]*)\]|\{([^}]*)\}|(\r?\n)|(.)", re.S)


def _hex_operands(vals, widths) -> str:
    return "".join(f" ${v:0{2 * w}X}" for v, w in zip(vals, widths))


def _glyph(b: int) -> str:
    return GLYPHS.get(b) or f"[${b:02X}]"


def _parse_token(body: str):
    """'name $op ...' or '$XX' -> (name or None, [(value, digits)])."""
    words = body.split()
    names = []
    while words and not words[0].startswith("$"):
        names.append(words.pop(0))
    ops = []
    for w in words:
        if not re.fullmatch(r"\$[0-9A-Fa-f]+", w):
            raise TextError(f"bad operand {w!r} in [{body}]")
        ops.append((int(w[1:], 16), len(w) - 1))
    return (" ".join(names) or None), ops


def _operand_bytes(ops, widths, body) -> bytes:
    if len(ops) != len(widths):
        raise TextError(f"[{body}] takes {len(widths)} operand(s)")
    out = bytearray()
    for (v, _), w in zip(ops, widths):
        if v >= 1 << (8 * w):
            raise TextError(f"operand of [{body}] doesn't fit {w} byte(s)")
        out += v.to_bytes(w, "little")
    return bytes(out)


# ---- glyph strings (dictionary entries, names) ------------------------------

def decode_glyphs(data: bytes) -> str:
    """Bytes as $C2:5BF5 draws them: each a glyph, except $01/$02 and the byte
    after, a 16-bit glyph code."""
    out, i = [], 0
    while i < len(data):
        b = data[i]
        if b in DIALOG_WIDE:
            if i + 1 >= len(data):
                raise TextError("a 16-bit glyph code is cut off")
            out.append(f"[${b:02X}{data[i + 1]:02X}]")
            i += 2
        else:
            out.append(_glyph(b))
            i += 1
    return "".join(out)


def encode_glyphs(text: str) -> bytes:
    out = bytearray()
    for m in _TOKEN.finditer(text):
        tok, brace, nl, ch = m.groups()
        if ch is not None:
            if ch not in CODES:
                raise TextError(f"no glyph for {ch!r}")
            out.append(CODES[ch])
        elif tok is not None:
            name, ops = _parse_token(tok)
            if name is not None or len(ops) != 1 or ops[0][1] not in (2, 4):
                raise TextError(f"[{tok}] isn't a glyph")
            v, digits = ops[0]
            if digits == 4:
                if v >> 8 not in DIALOG_WIDE:
                    raise TextError(f"[{tok}]: a 16-bit glyph code starts with $01 or $02")
                out += bytes((v >> 8, v & 0xFF))
            else:
                if v in DIALOG_WIDE:
                    raise TextError(f"[{tok}] is the prefix of a 16-bit glyph code")
                out.append(v)
        else:
            raise TextError("braces and newlines don't belong in a glyph string")
    return bytes(out)


# ---- the dictionary -----------------------------------------------------------

@dataclass(frozen=True)
class Dictionary:
    entries: tuple          # glyph bytes, by index (byte - $21)
    order: tuple            # the indices in the order the entries are stored

    def index(self) -> dict:
        return {e: i for i, e in enumerate(self.entries)}


def decode_dictionary(blob: bytes, base: int, count: int = DICTIONARY_ENTRIES) -> Dictionary:
    """`count` bank-relative pointers at bank offset `base`, then the entries
    they point at, each a length byte and its glyphs, stored end to end in
    some order and consuming the rest of the region exactly."""
    table = 2 * count
    if len(blob) < table:
        raise TextError("truncated dictionary pointer table")
    ptrs = [blob[2 * i] | blob[2 * i + 1] << 8 for i in range(count)]
    order = sorted(range(count), key=lambda i: ptrs[i])
    at = base + table
    entries = [b""] * count
    for i in order:
        if ptrs[i] != at:
            raise TextError(f"dictionary entry {i} isn't where the previous one ends")
        o = at - base
        if o >= len(blob) or o + 1 + blob[o] > len(blob):
            raise TextError(f"dictionary entry {i} runs past the region")
        entries[i] = bytes(blob[o + 1:o + 1 + blob[o]])
        at += 1 + blob[o]
    if at - base != len(blob):
        raise TextError("the dictionary entries don't consume the region")
    if len(set(entries)) != count:
        raise TextError("two dictionary entries expand to the same glyphs")
    return Dictionary(tuple(entries), tuple(order))


def encode_dictionary(d: Dictionary, base: int) -> bytes:
    count = len(d.entries)
    if sorted(d.order) != list(range(count)):
        raise TextError("the storage order must list every entry once")
    ptrs = [0] * count
    body = bytearray()
    at = base + 2 * count
    for i in d.order:
        e = d.entries[i]
        if not 0 < len(e) < 256:
            raise TextError(f"dictionary entry {i} must hold 1-255 bytes")
        ptrs[i] = at + len(body)
        body.append(len(e))
        body += e
    if at + len(body) > 0x10000:
        raise TextError("the dictionary runs past the bank")
    return b"".join(p.to_bytes(2, "little") for p in ptrs) + bytes(body)


def dictionary_text(d: Dictionary) -> str:
    """One entry per line in storage order: its index (a string names entry n
    with the byte $21 + n; entry $7F only `[ext $68]` reaches), then its
    expansion."""
    lines = []
    for i in d.order:
        lines.append(f"{i:02X} {{{decode_glyphs(d.entries[i])}}}")
    return "\n".join(lines) + "\n"


def parse_dictionary_text(text: str) -> Dictionary:
    got = {}
    order = []
    for n, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        m = re.fullmatch(r"([0-9A-Fa-f]{2}) \{(.*)\}", line)
        if not m:
            raise TextError(f"dictionary line {n}: expected 'NN {{glyphs}}'")
        i = int(m.group(1), 16)
        if not 0 <= i < DICTIONARY_ENTRIES or i in got:
            raise TextError(f"dictionary line {n}: bad or repeated index {i:02X}")
        got[i] = encode_glyphs(m.group(2))
        order.append(i)
    if len(got) != DICTIONARY_ENTRIES:
        raise TextError(f"the dictionary needs {DICTIONARY_ENTRIES} entries, has {len(got)}")
    return Dictionary(tuple(got[i] for i in range(DICTIONARY_ENTRIES)), tuple(order))


# ---- dialog -------------------------------------------------------------------

def dialog_length(data: bytes, off: int, limit: int | None = None) -> int | None:
    """Bytes from `off` through the token that ends the string: the terminator
    $00 or `[end]` ($03 $00, whose operand is the first zero byte); None if
    `limit` (exclusive) comes first."""
    end = len(data) if limit is None else limit
    i = off
    while i < end:
        b = data[i]
        if b == 0:
            return i + 1 - off
        if b in DIALOG_WIDE or b in (0x03, 0x12):
            if b == 0x03 and i + 1 < end and data[i + 1] == 0:
                return i + 2 - off
            i += 2
        else:
            i += 1
    return None


def decode_dialog(blob: bytes, d: Dictionary, terminated: bool = True) -> str:
    """A string region: tokens up to the terminator, which is left implicit.
    $03 $00 is `[end]`, and a region ending in it has no terminator after it;
    `[end]` elsewhere stays where it is. With `terminated` false the region is
    a fragment that runs into the next string and has no end of its own."""
    out = []
    i = 0
    n = len(blob)
    while i < n:
        b = blob[i]
        if b == 0:
            if not terminated or i != n - 1:
                raise TextError("a terminator inside the region")
            return "".join(out)
        if b in DIALOG_WIDE or b in (0x03, 0x12):
            if i + 1 >= n:
                raise TextError(f"code ${b:02X} is cut off by the region's end")
            op = blob[i + 1]
            if b in DIALOG_WIDE:
                out.append(f"[${b:02X}{op:02X}]")
            elif b == 0x03 and op == 0:
                out.append(f"[{DIALOG_END}]")
                if terminated and i + 2 == n:
                    return "".join(out)
                if terminated and i + 3 == n and blob[i + 2] == 0:
                    raise TextError("[end] and then a terminator: the region holds two strings")
            else:
                c = DIALOG_CODES[b]
                out.append(f"[{c.name}{_hex_operands((op,), c.operands)}]")
            i += 2
            continue
        if b in DIALOG_DICT:
            out.append("{" + decode_glyphs(d.entries[b - 0x21]) + "}")
        elif b >= 0xA0:
            out.append(_glyph(b))
        elif b in DIALOG_CODES:
            c = DIALOG_CODES[b]
            out.append(f"[{c.name}]" + ("\n" if c.newline else ""))
        else:
            out.append(f"[${b:02X}]")
        i += 1
    if terminated:
        raise TextError("no terminator")
    return "".join(out)


_DIALOG_BY_NAME = {c.name: (b, c) for b, c in DIALOG_CODES.items()}


def encode_dialog(text: str, d: Dictionary, terminated: bool = True) -> bytes:
    index = d.index()
    out = bytearray()
    last_end = False            # the last token was $03 $00
    for m in _TOKEN.finditer(text):
        tok, brace, nl, ch = m.groups()
        if nl is not None:
            continue
        last_end = False
        if ch is not None:
            if ch not in CODES:
                raise TextError(f"no glyph for {ch!r}")
            out.append(CODES[ch])
        elif brace is not None:
            i = index.get(encode_glyphs(brace))
            if i is None or 0x21 + i not in DIALOG_DICT:
                raise TextError(f"{{{brace}}} isn't a dictionary entry a byte can name")
            out.append(0x21 + i)
        else:
            name, ops = _parse_token(tok)
            if name is None:
                if len(ops) != 1:
                    raise TextError(f"[{tok}]: one hex value")
                v, digits = ops[0]
                if digits == 4:
                    if v >> 8 not in DIALOG_WIDE:
                        raise TextError(f"[{tok}]: a 16-bit glyph code starts with $01 or $02")
                    out += bytes((v >> 8, v & 0xFF))
                elif digits == 2 and v != 0 and v not in DIALOG_WIDE and v not in (0x03, 0x12):
                    out.append(v)
                else:
                    raise TextError(f"[{tok}] isn't a byte that stands alone")
            elif name == DIALOG_END and not ops:
                out += b"\x03\x00"
                last_end = True
            elif name in _DIALOG_BY_NAME:
                b, c = _DIALOG_BY_NAME[name]
                out.append(b)
                out += _operand_bytes(ops, c.operands, tok)
                last_end = b == 0x03 and out[-1] == 0
            else:
                raise TextError(f"unknown code [{tok}]")
    if terminated and not last_end:
        out.append(0)
    return bytes(out)


# ---- menu strings ------------------------------------------------------------

_MENU_BY_NAME = {c.name: (b, c) for b, c in MENU_CODES.items()}


def _decode_menu_one(blob: bytes, i: int, stop: int, tiles_left: int | None):
    """Tokens from i until the terminator (consumed), `stop`, or the tile
    limit. Returns (text, next offset, terminated)."""
    out = []
    while i < stop:
        b = blob[i]
        if b == 0:
            return "".join(out), i + 1, True
        if b < 0x10:
            c = MENU_CODES.get(b)
            if c is None:
                out.append(f"[${b:02X}]")
                i += 1
                continue
            w = sum(c.operands)
            if i + 1 + w > stop:
                raise TextError(f"menu code ${b:02X} is cut off")
            vals, o = [], i + 1
            for width in c.operands:
                vals.append(int.from_bytes(blob[o:o + width], "little"))
                o += width
            out.append(f"[{c.name}{_hex_operands(vals, c.operands)}]" + ("\n" if c.newline else ""))
            i = o
            continue
        out.append(_glyph(b))
        i += 1
        if tiles_left is not None:
            tiles_left -= 1
            if tiles_left == 0:
                return "".join(out), i, False
    return "".join(out), i, False


def _encode_menu_one(text: str) -> bytes:
    out = bytearray()
    for m in _TOKEN.finditer(text):
        tok, brace, nl, ch = m.groups()
        if nl is not None:
            continue
        if ch is not None:
            if ch not in CODES:
                raise TextError(f"no tile for {ch!r}")
            out.append(CODES[ch])
        elif brace is not None:
            raise TextError("menu strings have no dictionary")
        else:
            name, ops = _parse_token(tok)
            if name is None:
                if len(ops) != 1 or ops[0][1] != 2 or ops[0][0] == 0 or (ops[0][0] in MENU_CODES):
                    raise TextError(f"[{tok}] isn't a byte that stands alone")
                out.append(ops[0][0])
            elif name in _MENU_BY_NAME:
                b, c = _MENU_BY_NAME[name]
                out.append(b)
                out += _operand_bytes(ops, c.operands, tok)
            else:
                raise TextError(f"unknown menu code [{tok}]")
    return bytes(out)


def decode_menu(blob: bytes, width: int | None = None) -> str:
    """One terminated string, or with `width` a run of records of that many
    bytes, each ending at the tile limit or at a terminator that fills the
    record out; one record per line."""
    if width is None:
        text, i, term = _decode_menu_one(blob, 0, len(blob), None)
        if not term or i != len(blob):
            raise TextError("a menu string region is one string and its terminator")
        return text
    if width <= 0 or len(blob) % width:
        raise TextError("menu records don't tile the region")
    lines = []
    for o in range(0, len(blob), width):
        text, i, term = _decode_menu_one(blob, o, o + width, width)
        if i != o + width:
            raise TextError("a menu record ends before its width")
        lines.append(text.replace("\n", "") + ("[$00]" if term else ""))
    return "\n".join(lines) + "\n"


def encode_menu(text: str, width: int | None = None) -> bytes:
    if width is None:
        return _encode_menu_one(text) + b"\0"
    out = bytearray()
    for line in text.splitlines():
        term = line.endswith("[$00]")
        rec = _encode_menu_one(line[:-5] if term else line) + (b"\0" if term else b"")
        if len(rec) != width:
            raise TextError(f"menu record {line!r} isn't {width} bytes")
        out += rec
    return bytes(out)


# ---- name records ------------------------------------------------------------

NAME_PAD = 0xEF


def decode_names(blob: bytes, width: int) -> str:
    """Records of `width` tiles, one per line, with the trailing $EF padding
    dropped (the dialog decoder's name codes stop at the first $EF)."""
    if width <= 0 or len(blob) % width:
        raise TextError("name records don't tile the region")
    lines = []
    for o in range(0, len(blob), width):
        rec = blob[o:o + width].rstrip(bytes((NAME_PAD,)))
        lines.append("".join(_glyph(b) for b in rec))
    return "\n".join(lines) + "\n"


def encode_names(text: str, width: int) -> bytes:
    out = bytearray()
    for line in text.split("\n")[:-1] if text.endswith("\n") else text.split("\n"):
        rec = bytearray()
        for m in _TOKEN.finditer(line):
            tok, brace, nl, ch = m.groups()
            if ch is not None:
                if ch not in CODES:
                    raise TextError(f"no tile for {ch!r}")
                rec.append(CODES[ch])
            elif tok is not None:
                name, ops = _parse_token(tok)
                if name is not None or len(ops) != 1 or ops[0][1] != 2:
                    raise TextError(f"[{tok}] isn't a tile")
                rec.append(ops[0][0])
            else:
                raise TextError("a name record is tiles only")
        if len(rec) > width:
            raise TextError(f"name {line!r} is longer than {width} tiles")
        out += rec + bytes((NAME_PAD,)) * (width - len(rec))
    return bytes(out)


# ---- formats, as the matching build uses them ---------------------------------

FORMATS = ("dialog", "dialog-dictionary", "menu", "names")


def region_params(region) -> dict:
    """A [[data]] region's parameters, plus the bank offset it starts at (the
    dictionary's pointers are relative to its bank)."""
    return dict(region.params, base=region.start & 0xFFFF)


def dictionary_context(regions, data: bytes, offset) -> dict:
    """{"dictionary": Dictionary} from the region of format
    "dialog-dictionary" among `regions`, decoded from `data` (`offset` maps a
    bus address to an index into it); {} if there is none."""
    for r in regions:
        if r.type == "text" and r.params.get("format") == "dialog-dictionary":
            s, e = offset(r.start), offset(r.end) + 1
            return {"dictionary": decode_dictionary(data[s:e], r.start & 0xFFFF)}
    return {}


def decode(fmt: str, blob: bytes, params: dict, context: dict) -> str:
    """The readable form of a region of format `fmt`. `context` carries the
    dictionary (key "dictionary") for dialog."""
    if fmt == "dialog":
        return decode_dialog(blob, _dictionary(context), params.get("terminated", True))
    if fmt == "dialog-dictionary":
        return dictionary_text(decode_dictionary(blob, params["base"]))
    if fmt == "menu":
        return decode_menu(blob, params.get("width"))
    if fmt == "names":
        return decode_names(blob, params["width"])
    raise TextError(f"unknown text format {fmt!r}")


def encode(fmt: str, text: str, params: dict, context: dict) -> bytes:
    if fmt == "dialog":
        return encode_dialog(text, _dictionary(context), params.get("terminated", True))
    if fmt == "dialog-dictionary":
        return encode_dictionary(parse_dictionary_text(text), params["base"])
    if fmt == "menu":
        return encode_menu(text, params.get("width"))
    if fmt == "names":
        return encode_names(text, params["width"])
    raise TextError(f"unknown text format {fmt!r}")


def _dictionary(context: dict) -> Dictionary:
    d = context.get("dictionary")
    if d is None:
        raise TextError("dialog needs the dictionary region (format = \"dialog-dictionary\")")
    return d
