"""Object command scripts of bank C2, parsed from the ROM (docs/MATCHING.md, MD4, C3).

The interpreter at $C2:0F63 runs an object's script from the long pointer at
slot +$07 (bank +$09): it fetches the command byte at [$58], calls the handler
from the 83-word table at $C2:0F91 (`JSR ($0F91,X)`), and adds the A the handler
returns to $58 (0 stops for this frame, carry set with 0 frees the object).
The command table below is read from the handlers, one by one (the handler
address is given for each): the operand bytes each reads through [$58],Y, the
A values each of its RTS paths returns, and what it writes to $58 or passes
on. Written from the code, not from the scripts. The names are the ports'
(game/menu/object_script.c, whose entries are `menu_object_cmd_` and the name).

Where scripts come from (the roots), all enumerated from the code:

- the static loads before `JSR $04D9`/`JSR $04FB` (start an object running a
  script: A = bank, X = address) and the stores of a constant address into slot
  +$07/+$09 (`$C2:43B9`, `$C2:477A`), found in the analysis by `code_args`;
- the eight scenes `$C2:2C6E-2C90` loads: scene i (`$0100` = `$1F0`+i, record
  i of the eight at `$C6:FD10`, fields +$15 and +$16 hold i) decompresses stream
  i of the table at `$C6:FFE0` to `$7F:0400` and starts it there (`$C2:2C8B`), and
  stream i of `$C6:FFC0` into the tables at `$7E:7E00`, `$7E:8300`, `$7E:8180`
  and `$7E:8480` (`$C2:2A71-2B60`); the words at `$7E:8480` are script addresses
  in bank `$7F` that `$C2:2420`, `$C2:33AD` and `$C2:33DA` start;
- the commands that name scripts: `$09` and `$43` (address, bank), `$08` (an
  address in bank `$7F` that `$C2:3772`/`$C2:3DA5` start), `$1A` (goto), `$36`
  (call), and the relative branches.

The parse follows every successor from every root. Commands `$34` (JSR to the
word operand through `JMP ($005B)` at `$C2:1819`), `$4E` (JSL to the long operand
through `JML [$005B]` at `$C2:1836`), `$35` and `$42` (spawn an object whose
handler word is the operand, run by `JMP ($0050)` at `$C2:0553`) give the code
addresses. Commands that write through an operand address or slot offset are
checked not to write a slot's handler word or script pointer, or $50-$5D.
"""

from dataclasses import dataclass, field

from .lz import decompress
from .memmap import bus_to_offset, fmt

# Kinds: "next" (always the length), "wait" (0 or the length: the command
# repeats until it moves on; with `rel`, the offset of a frame count the
# handler loads into slot +$0A whenever +$0A is 0, so that a count of 0 never
# moves on), "branch" (the length, or the signed byte operand
# at `rel` added to the command's address), "halt" (always 0), "end" (0 with
# carry: frees the object), "goto" (address = word operand + 1 in the same
# bank), "call" (address = word operand; the return address, +3, is saved in
# slot +5), "return" (continues at slot +5).
# Extra fields: "jsr", "jsl", "handler", "script" (word at 1, bank at 3),
# "script7f" (word at 1, bank $7F), "sub" (byte index at 1 into $C2:1A70),
# "slotwrite" (byte at `off` is a slot offset written), "abswrite" (word at
# `off` is an absolute address written in bank $7E).


@dataclass(frozen=True)
class Command:
    handler: int
    length: int
    kind: str = "next"
    rel: int = 0
    extra: tuple = ()
    name: str = ""          # what the command does, from its port in game/menu/object_script.c


C = Command
COMMANDS = {
    0x00: C(0x1037, 1, name="reset_state"),                                 # clears slot fields +$05-$3F, returns 1
    0x01: C(0x1091, 2, name="set_sprite_palette"),                          # ORA byte into +$0F
    0x02: C(0x10A8, 2, name="set_sprite_priority"),
    0x03: C(0x10BF, 10, name="start_sprite_object"),                        # spawns $1CF5, which reads bytes 1-9 of this command
    0x04: C(0x10CD, 6, name="load_palette_row"),                            # bytes 1-5 into +$32-$37, spawns $1DD4
    0x05: C(0x1100, 5, name="change_location"),
    0x06: C(0x1169, 1, "halt", name="wait_frame"),                          # TDC; RTS
    0x07: C(0x116B, 5, name="write_table_byte"),
    0x08: C(0x11B2, 4, extra=(("script7f", 1),), name="set_scene_script"),  # word 1 into $1B3B/$1B3D/$1B3F/$1B41
    0x09: C(0x11F6, 4, extra=(("script", 1),), name="start_object"),        # JSR $04D9, X = word 1, A = byte 3
    0x0A: C(0x120C, 2, extra=(("slotwrite", 1),), name="clear_slot_byte"),
    0x0B: C(0x121D, 2, extra=(("slotwrite", 1),), name="increment_slot_byte"),
    0x0C: C(0x1230, 2, extra=(("slotwrite", 1),), name="decrement_slot_byte"),
    0x0D: C(0x1243, 3, extra=(("slotwrite", 1),), name="set_slot_byte"),
    0x0E: C(0x1257, 3, extra=(("slotwrite", 1),), name="set_slot_bits"),
    0x0F: C(0x126D, 3, extra=(("slotwrite", 1),), name="clear_slot_bits"),
    0x10: C(0x1285, 3, extra=(("abswrite", 1),), name="clear_cell"),
    0x11: C(0x1296, 3, extra=(("abswrite", 1),), name="increment_cell"),
    0x12: C(0x12A7, 3, extra=(("abswrite", 1),), name="decrement_cell"),
    0x13: C(0x12B8, 4, extra=(("abswrite", 1),), name="set_cell"),
    0x14: C(0x12CE, 4, extra=(("abswrite", 1),), name="set_cell_bits"),
    0x15: C(0x12E7, 4, extra=(("abswrite", 1),), name="clear_cell_bits"),
    0x16: C(0x1302, 4, extra=(("abswrite", 2),), name="store_slot_byte"),   # slot byte (offset 1) to the address at 2
    0x17: C(0x131C, 4, extra=(("slotwrite", 1),), name="load_slot_byte"),   # the byte at the address at 2 to a slot offset
    0x18: C(0x1336, 3, extra=(("slotwrite", 1),), name="copy_slot_byte"),
    0x19: C(0x1350, 5, extra=(("abswrite", 1),), name="copy_cell"),         # copies the byte at the address at 3 to the one at 1
    0x1A: C(0x136A, 3, "goto", name="jump"),                                # $58 = word 1, returns 1
    0x1B: C(0x1375, 3, "branch", 2, name="loop_slot_byte"),                 # decrement a slot byte, branch while not zero
    0x1C: C(0x1398, 3, "branch", 2, name="branch_if_slot_byte_zero"),
    0x1D: C(0x13B8, 3, "branch", 2, name="branch_if_slot_byte_set"),
    0x1E: C(0x13D8, 4, "branch", 3, name="branch_if_slot_byte_not"),
    0x1F: C(0x13FC, 4, "branch", 3, name="branch_if_slot_byte_is"),
    0x20: C(0x1420, 4, "branch", 3, name="branch_if_slot_bits_set"),
    0x21: C(0x1444, 4, "branch", 3, name="branch_if_slot_bits_clear"),
    0x22: C(0x1469, 4, "branch", 3, name="branch_if_cell_zero"),
    0x23: C(0x148B, 4, "branch", 3, name="branch_if_cell_set"),
    0x24: C(0x14AD, 5, "branch", 4, name="branch_if_cell_not"),
    0x25: C(0x14D2, 5, "branch", 4, name="branch_if_cell_is"),
    0x26: C(0x14F7, 5, "branch", 4, name="branch_if_cell_bits_set"),
    0x27: C(0x151C, 5, "branch", 4, name="branch_if_cell_bits_clear"),
    0x28: C(0x158B, 2, name="fade_out"),                                    # spawns $20A2
    0x29: C(0x1599, 2, name="fade_in"),                                     # spawns $2105
    0x2A: C(0x15A7, 3, name="mosaic_on"),                                   # spawns $21F8
    0x2B: C(0x15B5, 3, name="mosaic_off"),                                  # spawns $2194
    0x2C: C(0x15C3, 5, name="set_position"),
    0x2D: C(0x15DF, 2, name="nothing"),
    0x2E: C(0x15E3, 5, name="set_velocity_x"),
    0x2F: C(0x15F9, 5, name="set_velocity_y"),
    0x30: C(0x160F, 2, name="set_sprite_script"),                           # sprite script from $C3:E000 by byte 1
    0x31: C(0x161D, 2, "wait", 1, name="animate_for"),                      # count at 1 into +$0A; a count of 0 waits forever
    0x32: C(0x164B, 2, "wait", 1, name="scroll_map_for"),
    0x33: C(0x17D2, 8, name="queue_vram_upload"),
    0x34: C(0x1807, 3, extra=(("jsr", 1),), name="call_routine"),           # $5B = word 1, JSR $1819 (JMP ($005B)), m8 x16
    0x35: C(0x1839, 3, extra=(("handler", 1),), name="start_handler"),      # JSR $048A with X = word 1
    0x36: C(0x1848, 3, "call", name="call"),                                # +5 = $58 + 3, $58 = word 1 - 1, returns 1
    0x37: C(0x185F, 1, "return", name="return"),                            # $58 = +5 - 1, returns 1
    0x38: C(0x186B, 2, "wait", 1, name="wait_for"),
    0x39: C(0x188E, 2, "wait", 1, name="animate_wait_for"),
    0x3A: C(0x18B4, 3, "wait", name="stop_after_age"),                      # until the frame counter +3 reaches word 1
    0x3B: C(0x18C7, 3, name="sound_18"),
    0x3C: C(0x18D0, 3, name="sound_19"),
    0x3D: C(0x18F1, 2, name="play_song_unless_flag"),                       # continues into $4A's handler when $7F:01ED is 0
    0x3E: C(0x1947, 2, name="redraw_map"),
    0x3F: C(0x1959, 5, "wait", name="move_x_to"),
    0x40: C(0x19C2, 5, "wait", name="move_y_to"),
    0x41: C(0x1A2B, 1, "halt", name="wait_frame_again"),                    # TDC; RTS
    0x42: C(0x1A43, 3, extra=(("handler", 1),), name="start_handler_high"), # JSR $04CD with X = word 1
    0x43: C(0x1A2D, 4, extra=(("script", 1),), name="start_script"),        # JSR $04FB, X = word 1, A = byte 3
    0x44: C(0x1A52, 3, extra=(("sub", 1),), name="mark_table_entry"),       # JSR ($1A70,X), X = byte 1 * 2
    0x45: C(0x1AAC, 3, extra=(("sub", 1),), name="unmark_table_entry"),
    0x46: C(0x1ACA, 3, extra=(("slotwrite", 1),), name="add_to_slot_byte"),
    0x47: C(0x1AE1, 3, extra=(("slotwrite", 1),), name="subtract_from_slot_byte"),
    0x48: C(0x1AFB, 4, extra=(("abswrite", 1),), name="add_to_cell"),
    0x49: C(0x1B15, 4, extra=(("abswrite", 1),), name="subtract_from_cell"),
    0x4A: C(0x18F9, 2, name="play_song"),
    0x4B: C(0x191F, 5, name="sound_command"),
    0x4C: C(0x1541, 5, "branch", 4, name="branch_if_cell_below"),
    0x4D: C(0x1566, 5, "branch", 4, name="branch_if_cell_at_least"),
    0x4E: C(0x181C, 4, extra=(("jsl", 1),), name="call_routine_long"),      # $5B-$5D = long 1, JSL $1836 (JML [$005B]), m8 x16
    0x4F: C(0x1B2F, 9, name="copy_tilemap_rows"),
    0x50: C(0x1BE0, 5, name="copy_tilemap_block"),
    0x51: C(0x16FA, 3, "wait", 2, name="scroll_map_plane_for"),
    0x52: C(0x1C81, 1, "end", name="end"),                                  # TDC; SEC; RTS
}
del C

TABLE = 0xC20F91
SUB_TABLE = 0xC21A70         # the $44/$45 sub-dispatch, 3 words
SUB_TABLE_COUNT = 3
SCENE_RECORDS = 0xC6FD00     # 8 words: record addresses in bank C6
SCENE_COUNT = 8
SCENE_TABLES = 0xC6FFC0      # 24-bit stream pointers: the tables stream of scene i
SCENE_SCRIPTS = 0xC6FFE0     # the script stream of scene i, run at $7F:0400
SCENE_BASE = 0x7F0400
# Slot fields a script may not write: the handler word (+0/+1), the saved return
# address (+5/+6) and the script pointer (+7-+9); and the interpreter's cells.
GUARDED_SLOT_OFFSETS = {0, 1, 5, 6, 7, 8, 9}
GUARDED_DP = range(0x50, 0x5E)
SLOTS = range(0x0B30, 0x1B30)


@dataclass
class Scene:
    index: int
    image: bytes                  # the script stream's output, run at $7F:0400
    script_stream: int
    table_stream: int
    words: list                   # the $7E:8480 word table
    records: list = field(default_factory=list)   # the $7E:7E00, $7E:8300 and $7E:8180 records


@dataclass
class Parse:
    commands: dict = field(default_factory=dict)        # (space, addr) -> (opcode, length)
    jsr: dict = field(default_factory=dict)             # target -> {command addresses}
    jsl: dict = field(default_factory=dict)
    handlers: dict = field(default_factory=dict)        # handler word -> {command addresses}
    subs: set = field(default_factory=set)              # $44/$45 index operands
    flags: set = field(default_factory=set)             # (space, table, record) that $44 flags
    roots: dict = field(default_factory=dict)           # (space, addr) -> why
    problems: list = field(default_factory=list)
    scenes: list = field(default_factory=list)


def _u16(b, i):
    return b[i] | b[i + 1] << 8


def load_scenes(rom) -> tuple[list, list]:
    """The eight scenes' script images and word tables (see the module doc)."""
    scenes, problems = [], []
    for i in range(SCENE_COUNT):
        rec = 0xC60000 | rom.u16(SCENE_RECORDS + 2 * i)
        t_idx, s_idx = rom.read(rec + 0x15, 1)[0], rom.read(rec + 0x16, 1)[0]
        tab = rom.u24(SCENE_TABLES + 3 * t_idx)
        scr = rom.u24(SCENE_SCRIPTS + 3 * s_idx)
        rt = decompress(rom.data, bus_to_offset(tab)) if bus_to_offset(tab) is not None else None
        rs = decompress(rom.data, bus_to_offset(scr)) if bus_to_offset(scr) is not None else None
        if rt is None or rs is None:
            problems.append(f"scene {i}: no stream at {fmt(tab)} or {fmt(scr)}")
            continue
        t = rt[0]
        x = 0
        recs = []
        for size in (7, 3, 3):          # $7E:7E00, $7E:8300, $7E:8180: a count and records
            recs.append([t[x + 1 + size * k:x + 1 + size * (k + 1)] for k in range(t[x])])
            x += 1 + size * t[x]
        n = t[x]
        words = [_u16(t, x + 1 + 2 * k) for k in range(n)]
        if x + 1 + 2 * n != len(t):
            problems.append(f"scene {i}: the tables stream at {fmt(tab)} has {len(t) - x - 1 - 2 * n} bytes "
                            "after its word table")
        scenes.append(Scene(i, rs[0], scr, tab, words, records=recs))
    return scenes, problems


class ObjectScripts:
    """Parses every object script reachable from the roots. Spaces: "C2" for
    scripts in ROM (any bank), or a scene index for bank $7F."""

    def __init__(self, rom):
        self.rom = rom
        self.scenes, problems = load_scenes(rom)
        self.result = Parse(problems=list(problems), scenes=self.scenes)

    def read(self, space, addr: int, n: int):
        if space == "rom":
            return self.rom.read(addr, n)
        img = self.scenes[space].image
        i = addr - SCENE_BASE
        if addr >> 16 != 0x7F or i < 0 or i + n > len(img):
            return None
        return img[i:i + n]

    def spaces_for(self, bank: int, where: str) -> list:
        if bank == 0x7F:
            return [s.index for s in self.scenes]
        if bus_to_offset(bank << 16 | 0x8000) is None:
            self.result.problems.append(f"{where}: script in bank {bank:02X}, neither ROM nor $7F")
            return []
        return ["rom"]

    def parse(self, code_roots: list) -> Parse:
        """code_roots: [(address, why)] of scripts the code starts."""
        res = self.result
        work = []

        def root(space, addr, why):
            key = (space, addr)
            if key not in res.roots:
                res.roots[key] = why
                work.append(key)

        for addr, why in code_roots:
            for sp in self.spaces_for(addr >> 16, why):
                root(sp, addr, why)
        for s in self.scenes:
            root(s.index, SCENE_BASE, f"scene {s.index} start ($C2:2C8B)")
            for k, w in enumerate(s.words):
                # every word inside the image is a root (the used ones are checked in load_scenes)
                if SCENE_BASE <= 0x7F0000 | w < SCENE_BASE + len(s.image):
                    root(s.index, 0x7F0000 | w, f"scene {s.index} word {k}")
        seen = set()
        while work:
            space, addr = work.pop()
            while (space, addr) not in seen:
                seen.add((space, addr))
                where = f"{fmt(addr)}" + ("" if space == "rom" else f" (scene {space})")
                op = self.read(space, addr, 1)
                if op is None:
                    res.problems.append(f"{where}: script runs outside its data")
                    break
                cmd = COMMANDS.get(op[0])
                if cmd is None:
                    res.problems.append(f"{where}: command {op[0]:02X} isn't in the table")
                    break
                raw = self.read(space, addr, cmd.length)
                if raw is None:
                    res.problems.append(f"{where}: command {op[0]:02X} runs outside its data")
                    break
                res.commands[(space, addr)] = (op[0], cmd.length)
                bank = addr & 0xFF0000
                for kind, o in cmd.extra:
                    w = _u16(raw, o) if kind != "sub" and kind != "slotwrite" else raw[o]
                    if kind == "jsr":
                        res.jsr.setdefault(0xC20000 | w, set()).add((space, addr))
                    elif kind == "jsl":
                        res.jsl.setdefault(w | raw[o + 2] << 16, set()).add((space, addr))
                    elif kind == "handler":
                        res.handlers.setdefault(0xC20000 | w, set()).add((space, addr))
                    elif kind == "script":
                        b = raw[o + 2]
                        for sp in ([space] if b == 0x7F and space != "rom" else self.spaces_for(b, where)):
                            root(sp, b << 16 | w, f"command {op[0]:02X} at {where}")
                    elif kind == "script7f":
                        for sp in ([space] if space != "rom" else self.spaces_for(0x7F, where)):
                            root(sp, 0x7F0000 | w, f"command 08 at {where}")
                    elif kind == "sub":
                        res.subs.add(w)
                        if op[0] == 0x44:
                            res.flags.add((space, w, raw[2]))
                    elif kind == "slotwrite" and w in GUARDED_SLOT_OFFSETS:
                        res.problems.append(f"{where}: command {op[0]:02X} writes slot offset {w:02X}")
                    elif kind == "abswrite" and (w in GUARDED_DP or (w in SLOTS and (w - 0x0B30) % 0x40
                                                                        in GUARDED_SLOT_OFFSETS)):
                        res.problems.append(f"{where}: command {op[0]:02X} writes {w:04X}")
                k = cmd.kind
                if k == "branch":
                    rel = raw[cmd.rel] - (0x100 if raw[cmd.rel] & 0x80 else 0)
                    work.append((space, bank | (addr + rel) & 0xFFFF))
                if k == "goto":
                    addr = bank | (_u16(raw, 1) + 1) & 0xFFFF
                elif k == "call":
                    work.append((space, bank | _u16(raw, 1)))
                    addr = bank | (addr + 3) & 0xFFFF
                elif k in ("halt", "end", "return") or (k == "wait" and cmd.rel and raw[cmd.rel] == 0):
                    break
                else:
                    addr = bank | (addr + cmd.length) & 0xFFFF
        self.check_scene_words()
        return res

    def check_scene_words(self):
        """The word indices the code can use must name scripts in the image.
        $C2:32CD-331C selects records ($1B32, $1B34) only among those with bit 7
        of their first byte set, which the stream sets or command $44 sets at run
        time (byte 1 picks the table, byte 2 the record: $C2:1A76-1A9B); then
        $C2:2402-2420 takes a $7E:7E00 record's byte +5 when its word at +3 has
        $1FF in its low 9 bits, and $C2:626E a $7E:8300 record's byte +2."""
        res = self.result
        for s in self.scenes:
            flagged = {(t, k) for sp, t, k in res.flags if sp in ("rom", s.index)}
            used = set()
            for t, size_recs in ((0, s.records[0]), (1, s.records[1])):
                for k, r in enumerate(size_recs):
                    if not (r[0] & 0x80 or (t, k) in flagged):
                        continue
                    if t == 0 and _u16(r, 3) & 0x1FF == 0x1FF:
                        used.add(r[5])
                    elif t == 1:
                        used.add(r[2])
            s.used = used
            for k in sorted(used):
                if k >= len(s.words) or not SCENE_BASE <= 0x7F0000 | s.words[k] < SCENE_BASE + len(s.image):
                    res.problems.append(f"scene {s.index}: word index {k} is used but isn't a script in the image")


__all__ = ["COMMANDS", "ObjectScripts", "Parse", "load_scenes"]
