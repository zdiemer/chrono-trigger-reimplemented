"""Event scripts: the field's event bytecode, parsed and re-encoded (docs/MATCHING.md,
"MD2 as built: the event scripts").

Where they are. The word at +8 of the location's record (`$F6:0008 + 14 *
location`) selects an entry of the 513-entry stream table at `$FC:F9F0`;
`$C0:56D4` decompresses that stream to `$7F:2000` (the event buffer). Every
script is therefore an `lz` stream, and `parse` works on the decompressed
bytes. After each load `$C0:5CEC` copies three short runs of commands from
`$C0:5D37-5D6D` to `$7F:3700-3785`, which commands `$D9`, `$DA` and
`$C0:3154` start objects on; `parse_commands` reads such a run (no header,
entered where the code says).

The buffer. Byte 0 is the object count n. Then, from `$7F:2001`, each object
has 16 words: the script offsets of its functions, relative to `$7F:2001`
(`$C0:5709` takes word 0 of each as the object's starting position `$1180,X`;
the call commands `$02`-`$07` take word k, `$C0:5FB6` and the rest of
game/event/calls.c). The commands follow the 32 * n bytes of the table. So an
offset o in the script is byte o + 1 of the decompressed stream.

The interpreter. `$C0:5900` (map setup: each object runs function 0 up to its
first `$00`, and resumes after it every frame), `$C0:5AA3` (per frame, up to
the count in `$68`), `$C0:595C` (object 0's function 1 at map setup, up to its
`$00`) and `$C0:597D` (one object's function 0, as at map setup) fetch the
byte at `$7F:2001,X` and dispatch through the 256-word table at `$C0:5D6E`
with Y = the command's offset; a handler returns
X = the next command's offset and carry set to keep going this frame. The
operand layouts below are read from the handlers, one by one: 185 of them are
ported (game/event/, each validated against the original call by call), the
rest are read from their code at the handler address given. The table carries
what a command reads and where it leaves X, nothing from outside documentation
and nothing from the scripts.

Kinds of flow: "next" (continue after the command, possibly after waiting on
it), "cond" (continue after the command when the condition holds, otherwise at
the jump byte's offset plus its value), "goto" (`$10`: operand offset plus the
byte), "back" (`$11`: operand offset minus the byte), "return" (`$00`: resume
at a saved position; the init pass's `$00` also continues after itself),
"stop" (the handler never moves X on, or leaves the interpreter: halts, mode
changes, location changes), "invalid" (the 32 table entries at `$C0:5F6E`,
which blank the screen and hang).

Operand kinds: "byte", "word", "long" (constants), "object" (an object slot,
2 x the object's index), "member" (a party member index), "var" (a variable
index: `$7F:0200 + 2 * index`), "jump" (a forward distance, from the jump
byte), "back" (a backward distance), "cmp" (a comparison operator, the tables
at `$C0:6467`/`$C0:6477`), "cmpmem" (the same with bit 7 selecting `$7F:0100`),
"data" (bytes copied out of the script; their count is the preceding length
word less 2).

The readable form (`decode`/`encode`): an `objects n` line, one line per
object naming its 16 functions, then one line per command, `name key=value
...`, with each branch target and function start that falls on a command given
a label `Lxxxx` (its script offset; `END` for the byte after the last).
Operators are written by name, and the length words before copied data are
left out and rebuilt from the data. Bytes no path reaches are decoded in a line
where they form whole commands, after an `; unreached` comment, and kept as
`.db` lines otherwise; a branch into the middle of a command keeps its
distance as a number. A run of commands (`decode_commands`) has a `commands`
line and an `entries` line instead of the objects. Nothing in this module
holds script contents; the scripts are ROM data.
"""

from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# The command table

CMP_NAMES = ("eq", "ne", "gt", "lt", "ge", "le", "and", "or")


@dataclass(frozen=True)
class Command:
    name: str
    handler: int                 # low word in bank $C0
    operands: tuple = ()         # ((key, kind), ...) after the command byte
    flow: str = "next"


SIZES = {"byte": 1, "word": 2, "long": 3, "object": 1, "member": 1, "var": 1, "jump": 1, "back": 1,
         "cmp": 1, "cmp16": 1, "cmpmem": 1}

B, W, L_, O, M, V, J = "byte", "word", "long", "object", "member", "var", "jump"


def C(name, handler, *operands, flow="next"):
    return Command(name, handler, tuple(operands), flow)


def _k(key, kind):
    return (key, kind)


# Variable-length commands: their operand list depends on the bytes (see
# `shape`). The table gives the fixed part; `VARIABLE` names them.
VARIABLE = {0x2E, 0x4E, 0x88, 0xF1, 0xFF}

COMMANDS = {
    0x00: C("return", 0x5F74, flow="return"),
    0x02: C("call", 0x5FB6, _k("object", O), _k("call", B)),
    0x03: C("call_when_accepted", 0x607E, _k("object", O), _k("call", B)),
    0x04: C("call_and_wait", 0x6109, _k("object", O), _k("call", B)),
    0x05: C("call_party_member", 0x61E9, _k("member", M), _k("call", B)),
    0x06: C("call_party_member_when_accepted", 0x61FE, _k("member", M), _k("call", B)),
    0x07: C("call_party_member_and_wait", 0x6214, _k("member", M), _k("call", B)),
    0x08: C("refuse_calls", 0x6240),
    0x09: C("accept_calls", 0x624B),
    0x0A: C("disable_object", 0x6254, _k("object", O)),
    0x0B: C("make_object_uncallable", 0x6269, _k("object", O)),
    0x0C: C("make_object_callable", 0x6282, _k("object", O)),
    0x0D: C("set_blocking", 0x6295, _k("value", B)),
    0x0E: C("set_arrival_flags", 0x62A4, _k("flags", B)),
    0x0F: C("face_0", 0x5535),
    0x10: C("jump_forward", 0x62B5, _k("to", J), flow="goto"),
    0x11: C("jump_back", 0x62CB, _k("to", "back"), flow="back"),
    0x12: C("if_var_const", 0x62DE, _k("var", V), _k("value", B), _k("op", "cmp"), _k("else", J), flow="cond"),
    0x13: C("if_var_const16", 0x6313, _k("var", V), _k("value", W), _k("op", "cmp16"), _k("else", J),
            flow="cond"),
    0x14: C("if_var_var", 0x6361, _k("var", V), _k("var2", V), _k("op", "cmp"), _k("else", J), flow="cond"),
    0x15: C("if_var_var16", 0x63A4, _k("var", V), _k("var2", V), _k("op", "cmp16"), _k("else", J),
            flow="cond"),
    0x16: C("if_memory", 0x63E6, _k("addr", B), _k("value", B), _k("op", "cmpmem"), _k("else", J),
            flow="cond"),
    0x17: C("face_1", 0x5539),
    0x18: C("if_memory0_below", 0x6442, _k("limit", B), _k("else", J), flow="cond"),
    0x19: C("set_result_from_variable", 0x653B, _k("var", V)),
    0x1A: C("if_result", 0x6568, _k("value", B), _k("else", J), flow="cond"),
    0x1B: C("face_2", 0x553D),
    0x1C: C("set_result_from_memory", 0x654E, _k("addr", W)),
    0x1D: C("face_3", 0x5541),
    0x1E: C("object_face_0", 0x556C, _k("object", O)),
    0x1F: C("object_face_1", 0x5579, _k("object", O)),
    0x20: C("get_party_leader", 0x658F, _k("var", V)),
    0x21: C("get_object_tile", 0x65AA, _k("object", O), _k("x", V), _k("y", V)),
    0x22: C("get_party_member_tile", 0x65EB, _k("member", M), _k("x", V), _k("y", V)),
    0x23: C("get_object_facing", 0x65F8, _k("object", O), _k("var", V)),
    0x24: C("get_party_member_facing", 0x661F, _k("member", M), _k("var", V)),
    0x25: C("object_face_2", 0x5586, _k("object", O)),
    0x26: C("object_face_3", 0x5593, _k("object", O)),
    0x27: C("if_object_in_view", 0x662C, _k("object", O), _k("else", J), flow="cond"),
    0x28: C("if_object_on_screen", 0x6655, _k("object", O), _k("else", J), flow="cond"),
    # $C0:3E84: sets $1D35/$1D36 bit 2, runs $C0:6E27 and $C0:75A0; bit 7 of the
    # operand picks $0BE9 = 2 over 1, its low bits an entry of the $80-ended lists
    # at $FD:A000, whose offset goes to $0BE1.
    0x29: C("select_fd_a000_list", 0x3E84, _k("entry", B)),
    0x2A: C("set_54_bit2", 0x3ED6),                      # TSB $54 #$04; ends the turn
    0x2B: C("set_54_bit3", 0x3EDE),
    0x2C: C("set_1d8f_1d91", 0x3EE6, _k("a", B), _k("b", B)),  # each x 8; clears $1D3F/$1D40/$1D92
    0x2D: C("if_any_input", 0x66A5, _k("else", J), flow="cond"),
    0x2E: C("palette_slot", 0x4A4E, _k("mode", B)),     # variable, see shape
    0x2F: C("set_0be3_0be4", 0x3F12, _k("a", B), _k("b", B)),
    0x30: C("if_mapped_held_bit1", 0x66B2, _k("else", J), flow="cond"),
    0x31: C("if_mapped_held_bit7", 0x66BC, _k("else", J), flow="cond"),
    0x32: C("set_54_bit4", 0x3EE2),
    # $C0:3711: $0F81,X = (operand * 2) & $0E, and the object's 24-byte palette
    # from bank $E4 ($1400,X) copied to $7E:2100 and $7E:2300 at that index.
    0x33: C("load_object_palette", 0x3711, _k("palette", B)),
    0x34: C("if_a_held", 0x66C6, _k("else", J), flow="cond"),
    0x35: C("if_b_held", 0x66E5, _k("else", J), flow="cond"),
    0x36: C("if_x_held", 0x66EF, _k("else", J), flow="cond"),
    0x37: C("if_y_held", 0x66F9, _k("else", J), flow="cond"),
    0x38: C("if_l_held", 0x6705, _k("else", J), flow="cond"),
    0x39: C("if_r_held", 0x670F, _k("else", J), flow="cond"),
    0x3B: C("if_mapped_pressed_bit1", 0x6719, _k("else", J), flow="cond"),
    0x3C: C("if_mapped_pressed_bit7", 0x6724, _k("else", J), flow="cond"),
    0x3F: C("if_a_pressed", 0x6732, _k("else", J), flow="cond"),
    0x40: C("if_b_pressed", 0x6740, _k("else", J), flow="cond"),
    0x41: C("if_x_pressed", 0x674E, _k("else", J), flow="cond"),
    0x42: C("if_y_pressed", 0x675C, _k("else", J), flow="cond"),
    0x43: C("if_l_pressed", 0x676A, _k("else", J), flow="cond"),
    0x44: C("if_r_pressed", 0x6778, _k("else", J), flow="cond"),
    0x47: C("set_animation_line_limit", 0x6788, _k("value", B)),
    0x48: C("var_from_pointer", 0x6792, _k("addr", L_), _k("var", V)),
    0x49: C("var_from_pointer16", 0x67C7, _k("addr", L_), _k("var", V)),
    0x4A: C("poke", 0x67D7, _k("addr", L_), _k("value", B)),
    0x4B: C("poke16", 0x67FB, _k("addr", L_), _k("value", W)),
    0x4C: C("poke_variable", 0x680C, _k("addr", L_), _k("var", V)),
    0x4D: C("poke_variable16", 0x6829, _k("addr", L_), _k("var", V)),
    0x4E: C("copy_data", 0x6846, _k("dest", W), _k("bank", B)),   # then the length word and data
    0x4F: C("var_set", 0x68A5, _k("value", B), _k("var", V)),
    0x50: C("var_set16", 0x68C6, _k("value", W), _k("var", V)),
    0x51: C("var_copy", 0x68E9, _k("from", V), _k("var", V)),
    0x52: C("var_copy16", 0x6913, _k("from", V), _k("var", V)),
    0x53: C("var_from_memory", 0x693D, _k("addr", W), _k("var", V)),
    0x54: C("var_from_memory16", 0x6964, _k("addr", W), _k("var", V)),
    0x55: C("var_from_memory0", 0x698B, _k("var", V)),
    0x56: C("memory_set", 0x69A6, _k("value", B), _k("addr", W)),
    # $C0:41E4 and the six after it: this object becomes character n ($8D + n =
    # its slot); if n is in the party ($7E:2980-2982) it takes that member's
    # place ($C0:426A), otherwise it is disabled with $1101,X = n.
    0x57: C("load_character_0", 0x41E4),
    0x58: C("memory_set_from_var", 0x69C4, _k("var", V), _k("addr", W)),
    0x59: C("memory_set_from_var16", 0x69EB, _k("var", V), _k("addr", W)),
    0x5A: C("memory0_set", 0x6A12, _k("value", B)),
    0x5B: C("var_add", 0x6A1F, _k("value", B), _k("var", V)),
    0x5C: C("load_character_1", 0x41EC),
    0x5D: C("var_add_var", 0x6A45, _k("from", V), _k("var", V)),
    0x5E: C("var_add_var16", 0x6A74, _k("from", V), _k("var", V)),
    0x5F: C("var_subtract", 0x6AA3, _k("value", B), _k("var", V)),
    0x60: C("var_subtract16", 0x6AC9, _k("value", W), _k("var", V)),
    0x61: C("var_subtract_var", 0x6AF1, _k("from", V), _k("var", V)),
    0x62: C("load_character_2", 0x41F4),
    0x63: C("var_set_bit", 0x6B20, _k("bit", B), _k("var", V)),
    0x64: C("var_clear_bit", 0x6B4B, _k("bit", B), _k("var", V)),
    0x65: C("memory_set_bit", 0x6B76, _k("bit", B), _k("addr", B)),
    0x66: C("memory_clear_bit", 0x6BA4, _k("bit", B), _k("addr", B)),
    0x67: C("var_and", 0x6BD2, _k("value", B), _k("var", V)),
    0x68: C("load_character_4", 0x41FC),
    0x69: C("var_or", 0x6BF7, _k("value", B), _k("var", V)),
    0x6A: C("load_character_3", 0x4204),
    0x6B: C("var_xor", 0x6C1C, _k("value", B), _k("var", V)),
    0x6C: C("load_character_5", 0x420C),
    0x6D: C("load_character_6", 0x4214),
    0x6F: C("var_shift_right", 0x6C41, _k("count", B), _k("var", V)),
    0x71: C("var_increment", 0x6C69, _k("var", V)),
    0x72: C("var_increment16", 0x6C85, _k("var", V)),
    0x73: C("var_decrement", 0x6CA1, _k("var", V)),
    0x75: C("var_set_1", 0x6CBD, _k("var", V)),
    0x76: C("var_set16_1", 0x6CD6, _k("var", V)),
    0x77: C("var_clear", 0x6CF0, _k("var", V)),
    0x7A: C("move_to_tile_with_velocity", 0x4D06, _k("x", B), _k("y", B), _k("height", B)),
    0x7B: C("move_with_velocity", 0x4E25, _k("dx", B), _k("dy", B), _k("height", B), _k("steps", B)),
    0x7C: C("show_object_sprite", 0x4CEC, _k("object", O)),
    0x7D: C("hide_object_sprite", 0x4CF9, _k("object", O)),
    0x7E: C("set_no_sprite", 0x4CE6),
    0x7F: C("var_random", 0x6D09, _k("var", V)),
    # $C0:421E: as $57-$6D with the character from the operand.
    0x80: C("load_character", 0x421E, _k("character", B)),
    # $C0:4476: this object shows character n's sprite (record n of $E4:F000,
    # through $C0:448D) without joining the party.
    0x81: C("load_character_sprite", 0x4476, _k("character", B)),
    # $C0:4590 / $C0:46DF: this object's sprite from record n of $E4:F023 (5-byte
    # records) or $E4:F600 (10-byte battle records, $C0:4557 first); $83's second
    # operand: bits 0-6 + 1 index $0B88, bit 7 sets the state to 6.
    0x82: C("load_npc", 0x4590, _k("sprite", B)),
    0x83: C("load_enemy", 0x46DF, _k("enemy", B), _k("slot", B)),
    0x84: C("set_solidity", 0x4867, _k("value", B)),
    0x87: C("set_step_count", 0x4876, _k("steps", B)),
    0x88: C("object_palette", 0x4892, _k("mode", B)),    # variable, see shape
    0x89: C("set_speed", 0x4B49, _k("value", B)),
    0x8A: C("set_speed_from_variable", 0x4B58, _k("var", V)),
    0x8B: C("set_tile_position", 0x4B74, _k("x", B), _k("y", B)),
    0x8C: C("set_tile_position_from_variables", 0x4BC3, _k("x", V), _k("y", V)),
    0x8D: C("set_position", 0x4C27, _k("x", W), _k("y", W)),
    0x8E: C("set_sprite_priority", 0x4C74, _k("mode", B)),
    0x8F: C("move_toward_party_member_until_on_screen", 0x5429, _k("member", M)),
    0x90: C("show_sprite", 0x4CD5),
    0x91: C("hide_sprite", 0x4CE0),
    0x92: C("move_in_direction", 0x4E73, _k("direction", B), _k("steps", B)),
    0x94: C("move_toward_object", 0x5215, _k("object", O)),
    0x95: C("move_toward_party_member", 0x5410, _k("member", M)),
    0x96: C("move_to_tile", 0x4F61, _k("x", B), _k("y", B)),
    0x97: C("move_to_tile_from_variables", 0x5084, _k("x", V), _k("y", V)),
    0x98: C("move_toward_object_counted", 0x534A, _k("object", O), _k("steps", B)),
    0x99: C("move_toward_party_member_counted", 0x550E, _k("member", M), _k("steps", B)),
    0x9A: C("move_to_tile_counted", 0x4FEA, _k("x", B), _k("y", B), _k("steps", B)),
    0x9C: C("move_in_direction_keep_facing", 0x4EC7, _k("direction", B), _k("steps", B)),
    0x9D: C("move_by_variables", 0x4F0A, _k("direction", V), _k("steps", V)),
    0x9E: C("move_toward_object_keep_facing", 0x52C0, _k("object", O)),
    0x9F: C("move_toward_party_member_keep_facing", 0x54F5, _k("member", M)),
    0xA0: C("move_to_tile_keep_facing", 0x5129, _k("x", B), _k("y", B)),
    0xA1: C("move_to_tile_from_variables_keep_facing", 0x5191, _k("x", V), _k("y", V)),
    0xA6: C("face_operand", 0x5545, _k("direction", B)),
    0xA7: C("face_variable", 0x5557, _k("var", V)),
    0xA8: C("face_object", 0x55A0, _k("object", O)),
    0xA9: C("face_party_member", 0x5605, _k("member", M)),
    0xAA: C("loop_animation", 0x2E67, _k("animation", B)),
    0xAB: C("play_animation", 0x2E9A, _k("animation", B)),
    0xAC: C("static_frame", 0x2F71, _k("frame", B)),
    0xAD: C("wait", 0x2F96, _k("frames", B)),
    0xAE: C("reset_animation", 0x2E8F),
    0xAF: C("run_state_routine_once", 0x2FFD),
    0xB0: C("run_state_routine", 0x3002, flow="stop"),
    0xB1: C("yield", 0x353F),
    0xB2: C("halt", 0x3543, flow="stop"),
    0xB3: C("loop_animation_0", 0x2E87),
    0xB4: C("loop_animation_1", 0x2E8B),
    0xB5: C("follow_object", 0x3546, _k("object", O), flow="stop"),
    0xB6: C("follow_party_member", 0x354F, _k("member", M), flow="stop"),
    0xB7: C("play_animation_for", 0x2F01, _k("animation", B), _k("frames", B)),
    0xB8: C("set_dialog_table", 0x3557, _k("table", L_)),
    0xB9: C("wait_4", 0x2FC6),
    0xBA: C("wait_8", 0x2FCA),
    0xBB: C("dialog", 0x3570, _k("string", B)),
    0xBC: C("wait_16", 0x2FD2),
    0xBD: C("wait_32", 0x2FCE),
    0xC0: C("choice", 0x3643, _k("string", B), _k("choices", B)),
    0xC1: C("dialog_top", 0x35B5, _k("string", B)),
    0xC2: C("dialog_bottom", 0x35FC, _k("string", B)),
    0xC3: C("choice_top", 0x36AA, _k("string", B), _k("choices", B)),
    0xC4: C("choice_bottom", 0x36DD, _k("string", B), _k("choices", B)),
    # $C1:8003 (A = function, Y = argument) over the inventory at $7E:2400
    # (items) and $7E:2500 (their counts), and the gold: 0 whether an item is
    # held, 1 add one, 2 remove one, 3 whether the gold reaches the word, 4 add
    # gold, 5 take it.
    0xC7: C("add_item_from_variable", 0x37C7, _k("var", V)),
    # $C0:3757: start the request in $25 (bit 6 of $17), wait for bit 6 of $18.
    0xC8: C("request_25", 0x3757, _k("value", B)),
    0xC9: C("if_item", 0x3780, _k("item", B), _k("else", J), flow="cond"),
    0xCA: C("add_item", 0x37B1, _k("item", B)),
    0xCB: C("remove_item", 0x37E3, _k("item", B)),
    0xCC: C("if_gold", 0x3827, _k("gold", W), _k("else", J), flow="cond"),
    0xCD: C("add_gold", 0x385B, _k("gold", W)),
    0xCE: C("remove_gold", 0x3874, _k("gold", W)),
    # $C2:8004 (A = function, Y = the character): 3 a test (nonzero continues),
    # 4 and 5 party changes; 0 a test (zero continues), 1 adds to the active party
    # ($C0:3919), 2 removes ($C0:394E).
    0xCF: C("if_character_3", 0x388D, _k("character", B), _k("else", J), flow="cond"),
    0xD0: C("party_routine_4", 0x38BE, _k("character", B)),
    0xD1: C("party_routine_5", 0x38D3, _k("character", B)),
    0xD2: C("if_character_0", 0x38E8, _k("character", B), _k("else", J), flow="cond"),
    0xD3: C("add_to_party", 0x3919, _k("character", B)),
    0xD4: C("remove_from_active_party", 0x394E, _k("character", B)),
    0xD5: C("party_routine_0a", 0x3A68, _k("y", B), _k("x", B)),
    0xD6: C("remove_from_party", 0x3A07, _k("character", B)),
    # $C0:37F9: $C2:8004 function $0C with X = the operand; the result to a variable.
    0xD7: C("party_query_0c", 0x37F9, _k("value", B), _k("var", V)),
    0xD8: C("start_battle", 0x3A83, _k("a", B), _k("b", B)),
    # $C0:326C: point the three party members' scripts at the moves built in the
    # buffer at $7F:3720, $7F:3740, $7F:3760 (tile pairs, bit 7 of each marking
    # the axis), and wait until they are done.
    0xD9: C("move_party", 0x326C, _k("x1", B), _k("y1", B), _k("x2", B), _k("y2", B), _k("x3", B),
            _k("y3", B)),
    # $C0:345A: the second and third members run the code at $7F:3700 until done.
    0xDA: C("party_follow", 0x345A),
    0xDC: C("store_location", 0x3AF1, _k("location", W), _k("position", W)),
    0xDD: C("set_location", 0x3B1B, _k("location", W), _k("position", W)),
    0xDE: C("set_location_with_1e", 0x3B45, _k("location", W), _k("position", W)),
    0xDF: C("change_location_now_with_1e", 0x3B98, _k("location", W), _k("position", W), flow="stop"),
    0xE0: C("change_location", 0x3AB7, _k("location", W), _k("position", W), flow="stop"),
    0xE1: C("change_location_now", 0x3B4B, _k("location", W), _k("position", W), flow="stop"),
    0xE2: C("change_location_from_variables", 0x3B9E, _k("location", V), _k("a", V), _k("b", V),
            _k("c", V), flow="stop"),
    0xE3: C("set_player_control", 0x3C05, _k("value", B)),
    0xE4: C("copy_tiles", 0x3D97, _k("a", W), _k("b", W), _k("c", W), _k("d", B)),
    0xE5: C("queue_tile_copy", 0x3DC5, _k("a", W), _k("b", W), _k("c", W), _k("d", B)),
    0xE6: C("set_layer_extra_speed", 0x3DF4, _k("value", W), _k("layers", B), _k("e", B)),
    0xE7: C("scroll_screen", 0x3E30, _k("x", B), _k("y", B)),
    0xE8: C("play_sound", 0x3C11, _k("effect", B)),
    0xEA: C("play_song", 0x3C2F, _k("song", B)),
    0xEB: C("sound_81_83", 0x3C4C, _k("a", B), _k("b", B)),
    0xEC: C("sound_command", 0x3C98, _k("command", B), _k("a", B), _k("b", B)),
    0xED: C("wait_apu_port1", 0x3CBC),
    0xEE: C("wait_apu_port3", 0x3CC5),
    0xF0: C("brightness", 0x3CD0, _k("value", B)),
    0xF1: C("fixed_color", 0x3D07, _k("color", B)),     # variable: a rate unless the color is 0
    0xF2: C("wait_brightness", 0x3CFD),
    0xF3: C("wait_color", 0x3D7A),
    0xF4: C("shake_screen", 0x3D84, _k("shaking", B)),
    0xF8: C("party_routine_6_7", 0x3E5C),
    0xF9: C("party_routine_6", 0x3E6E),
    0xFA: C("party_routine_7", 0x3E75),
    # $C0:3F26: $F4 and $7F:1CE8 = the operand, then eight byte pairs: each
    # pair's first byte goes to $7F:1CE9 (as a word's high byte) and the
    # difference to the second, divided by the operand, to $7F:1CF9; $39 = 6.
    # Ends the turn.
    0xFE: C("set_1ce8_table", 0x3F26, _k("divisor", B),
            *[_k(f"{c}{i}", B) for i in range(8) for c in "ab"]),
    0xFF: C("mode", 0x3FAF, _k("mode", B)),             # variable, see shape
}
del C

# Formats: "event" on an `lz` region (a whole buffer, compressed), and
# "event-commands" on a `script` region (a run of commands with no header, its
# `entries` given by the hint).
LZ_FORMATS = ("event",)
FORMATS = ("event", "event-commands")

INVALID_HANDLER = 0x5F6E     # blank the screen and hang (C0:2E1E)
TABLE = 0xC05D6E
MODE_TABLE = 0xC0400E        # $FF with a mode of $90 and up: JSR ($400E,X), X = (mode - $90) * 2
# $FF's modes from $90: operand bytes after the mode byte, or None for a mode
# that doesn't return ($96, $C0:4181) and "invalid" for $A0-$A3 (C0:5F6E).
FF_MODES = {0x90: 3, 0x91: 0, 0x92: 0, 0x93: 0, 0x94: 0, 0x95: 0, 0x96: None, 0x97: 3, 0x98: 0,
            0x99: 0, 0x9A: 0, 0x9B: 0, 0x9C: 0, 0x9D: 0, 0x9E: 0, 0x9F: 0}


class ScriptError(ValueError):
    pass


def shape(buf: bytes, p: int):
    """(operands, flow) of the command at buffer position p: operands as
    [(key, kind, size)], sizes adding up to the command's length less one.
    Raises ScriptError when the command isn't in the table or runs off the end."""
    if p >= len(buf):
        raise ScriptError("runs off the end")
    op = buf[p]
    cmd = COMMANDS.get(op)
    if cmd is None:
        raise ScriptError(f"command {op:02X} is invalid (C0:5F6E)")
    fields = [(k, kind, SIZES[kind]) for k, kind in cmd.operands]
    flow = cmd.flow

    def at(i):
        if p + i >= len(buf):
            raise ScriptError(f"command {op:02X} runs off the end")
        return buf[p + i]

    if op in VARIABLE:
        sub = at(1)
        if op == 0x2E:                       # $C0:4A4E
            if sub & 0xF0 in (0x40, 0x50):
                fields += [("a", "byte", 1), ("b", "byte", 1), ("c", "byte", 1), ("d", "byte", 1)]
            elif sub & 0xF0 == 0x80:
                fields += [("index", "byte", 1), ("length", "word", 2)]
                fields.append(("data", "data", (at(3) | at(4) << 8) - 2))
        elif op == 0x88:                     # $C0:4892
            if sub in (0x20, 0x30):
                fields += [("a", "byte", 1), ("b", "byte", 1)]
            elif sub & 0xF0 in (0x40, 0x50):
                fields += [("a", "byte", 1), ("b", "byte", 1), ("c", "byte", 1)]
            elif sub & 0xF0 == 0x80:
                fields.append(("length", "word", 2))
                fields.append(("data", "data", (at(2) | at(3) << 8) - 2))
        elif op == 0x4E:                     # $C0:6846
            fields.append(("length", "word", 2))
            fields.append(("data", "data", (at(4) | at(5) << 8) - 2))
        elif op == 0xF1:                     # $C0:3D07: a zero color takes no rate
            if sub:
                fields.append(("rate", "byte", 1))
        elif op == 0xFF:                     # $C0:3FAF
            if sub < 0x90:
                flow = "stop"                # JML to a mode loader with a fresh stack
            elif sub not in FF_MODES:
                raise ScriptError(f"command FF mode {sub:02X} is outside the table at C0:400E")
            elif FF_MODES[sub] is None:
                flow = "stop"
            else:
                fields += [(f"p{i}", "byte", 1) for i in range(FF_MODES[sub])]
    if any(kind == "data" and size < 0 for _, kind, size in fields):
        raise ScriptError(f"command {op:02X}: a length word below 2")
    if p + 1 + sum(f[2] for f in fields) > len(buf):
        raise ScriptError(f"command {op:02X} runs off the end")
    return fields, flow


def length(buf: bytes, p: int) -> int:
    fields, _ = shape(buf, p)
    return 1 + sum(f[2] for f in fields)


# ---------------------------------------------------------------------------
# Parsing

@dataclass
class Script:
    """A parsed buffer: a decompressed stream (`parse`) or a run of commands
    with no header (`parse_commands`). Positions are buffer indices; labels
    name position - origin, which for a whole buffer is the script offset the
    pointers and X hold (position - 1)."""
    buf: bytes
    count: int                                       # objects (0 for a run of commands)
    functions: list                                  # [object][16] script offsets
    head: int                                        # position of the first command
    origin: int = 1
    entries: tuple = ()                              # a run of commands: its entry positions
    commands: dict = field(default_factory=dict)     # position -> length: reached, listed one after another
    shared: dict = field(default_factory=dict)       # position -> length: reached inside a listed command
    targets: set = field(default_factory=set)        # positions branches and functions name
    operands: dict = field(default_factory=dict)     # (opcode, key) -> values, over every reached command
    uses: dict = field(default_factory=dict)         # opcode -> reached commands
    problems: list = field(default_factory=list)
    at_end: int = 0                                  # function words naming the end of the buffer


def header_size(count: int) -> int:
    return 1 + 32 * count


def parse(buf: bytes) -> Script:
    """Follow every path from every function start (see the module doc).
    Raises ScriptError when the header doesn't fit the buffer; a path that
    meets a byte the table rejects, or leaves the buffer, is a problem."""
    if not buf:
        raise ScriptError("empty buffer")
    count = buf[0]
    head = header_size(count)
    if head > len(buf):
        raise ScriptError(f"{count} objects need {head} bytes of header, the buffer has {len(buf)}")
    functions = [[buf[1 + 32 * k + 2 * f] | buf[2 + 32 * k + 2 * f] << 8 for f in range(16)]
                 for k in range(count)]
    sc = Script(bytes(buf), count, functions, head)
    roots = []
    for k in range(count):
        for f in range(16):
            pos = functions[k][f] + 1
            if pos == len(buf):
                sc.at_end += 1               # nothing there: a function no call may name
                continue
            roots.append((pos, f == 0))
    _walk(sc, roots)
    return sc


def parse_commands(buf: bytes, entries) -> Script:
    """A run of commands with no header, entered at `entries` (positions in
    buf), as the built-in fragments at $C0:5D37 are."""
    sc = Script(bytes(buf), 0, [], 0, 0, tuple(entries))
    _walk(sc, [(p, False) for p in entries])
    return sc


def _walk(sc: Script, roots: list) -> None:
    """Follow every path from roots [(position, init)]: `init` for function
    0, whose first `$00` ends the map setup's pass and is resumed after."""
    buf = sc.buf
    work = list(reversed(roots))
    sc.targets.update(p for p, _ in roots)
    reached = {}                             # position -> length, every reached command
    seen = set()
    while work:
        pos, init = work.pop()
        while (pos, init) not in seen:
            seen.add((pos, init))
            where = f"{pos - sc.origin:04X}"
            if not sc.head <= pos < len(buf):
                sc.problems.append(f"{where}: outside the commands")
                break
            try:
                fields, flow = shape(buf, pos)
            except ScriptError as exc:
                sc.problems.append(f"{where}: {exc}")
                break
            n = 1 + sum(f[2] for f in fields)
            if pos not in reached:
                reached[pos] = n
                sc.uses[buf[pos]] = sc.uses.get(buf[pos], 0) + 1
                i = pos + 1
                for key, kind, size in fields:
                    if kind != "data":
                        sc.operands.setdefault((buf[pos], key), set()).add(int.from_bytes(buf[i:i + size], "little"))
                    i += size
            if flow in ("cond", "goto", "back"):
                i = pos + 1
                for key, kind, size in fields:
                    if kind in ("jump", "back"):
                        t = i + buf[i] if kind == "jump" else i - buf[i]
                        sc.targets.add(t)
                        work.append((t, init))
                    i += size
            if flow in ("stop", "goto", "back"):
                break
            if flow == "return":
                if not init:
                    break
                init = False                 # the init pass ends here; every frame resumes after it
            pos += n
    sc.problems = list(dict.fromkeys(sc.problems))
    # Listed: the reached commands in address order, a command that starts
    # inside the one before it (a jump into an operand) set aside as shared.
    end = 0
    for pos in sorted(reached):
        if pos < end:
            sc.shared[pos] = reached[pos]
        else:
            sc.commands[pos] = reached[pos]
            end = pos + reached[pos]


# ---------------------------------------------------------------------------
# The readable form

DERIVED_LENGTH = (0x2E, 0x4E, 0x88)       # a length word the encoder rebuilds from the data after it


def _value(kind: str, v: int, size: int) -> str:
    if kind == "cmp" and v < 8:
        return CMP_NAMES[v]
    if kind == "cmp16" and v < 7:
        return CMP_NAMES[v]
    if kind == "cmpmem" and v & 0x7F < 8:
        return CMP_NAMES[v & 0x7F] + (".hi" if v & 0x80 else "")
    return f"${v:0{2 * size}X}"


def _line(buf: bytes, pos: int, fields, starts, label) -> str:
    parts = [COMMANDS[buf[pos]].name]
    i = pos + 1
    for key, kind, size in fields:
        if kind == "data":
            parts.append(f"{key}={buf[i:i + size].hex().upper()}")
        elif key == "length" and buf[pos] in DERIVED_LENGTH:
            pass
        elif kind in ("jump", "back"):
            t = i + buf[i] if kind == "jump" else i - buf[i]
            parts.append(f"{key}={label(t)}" if t in starts else f"{key}=${buf[i]:02X}")
        else:
            parts.append(f"{key}={_value(kind, int.from_bytes(buf[i:i + size], 'little'), size)}")
        i += size
    return "\t" + " ".join(parts)


def _sweep(buf: bytes, start: int, end: int) -> list:
    """[(position, fields)]: start..end decoded in a line as far as whole
    commands fit (bytes no path reaches)."""
    out = []
    pos = start
    while pos < end:
        try:
            fields, _ = shape(buf, pos)
        except ScriptError:
            break
        n = 1 + sum(f[2] for f in fields)
        if pos + n > end:
            break
        out.append((pos, fields))
        pos += n
    return out


def rows(sc: Script) -> list:
    """[(position, fields or None for a raw byte, reached)] covering the
    buffer after the header: the listed commands, and between them whatever
    decodes as whole commands (unreached), the rest raw."""
    buf = sc.buf
    out = []
    pos = sc.head
    listed = sorted(sc.commands)
    k = 0
    while pos < len(buf):
        if k < len(listed) and listed[k] == pos:
            out.append((pos, shape(buf, pos)[0], True))
            pos += sc.commands[pos]
            k += 1
            continue
        stop = listed[k] if k < len(listed) else len(buf)
        for p, f in _sweep(buf, pos, stop):
            out.append((p, f, False))
            pos = p + 1 + sum(x[2] for x in f)
        while pos < stop:
            out.append((pos, None, False))
            pos += 1
    return out


def _render(sc: Script, lines: list) -> str:
    buf = sc.buf

    def label(p):
        return f"L{p - sc.origin:04X}"

    table = rows(sc)
    starts = {p for p, f, _ in table if f is not None}
    for k, fs in enumerate(sc.functions):
        names = [label(o + 1) if o + 1 in starts else "END" if o + 1 == len(buf) else f"${o:04X}" for o in fs]
        lines.append(f"object ${k:02X} " + " ".join(names))
    if sc.entries:
        lines.append("entries " + " ".join(label(p) for p in sc.entries))
    named = {o + 1 for fs in sc.functions for o in fs} | set(sc.entries)
    for p, fields, _ in table:
        i = p + 1
        for _, kind, size in fields or ():
            if kind in ("jump", "back"):
                named.add(i + buf[i] if kind == "jump" else i - buf[i])
            i += size
    named &= starts
    raw = []
    live = True

    def flush():
        for i in range(0, len(raw), 16):
            lines.append("\t.db " + " ".join(f"{b:02X}" for b in raw[i:i + 16]))
        raw.clear()

    for p, fields, reached in table:
        if fields is None:
            raw.append(buf[p])
            continue
        flush()
        if reached != live:
            lines.append("; reached" if reached else "; unreached")
            live = reached
        if p in named:
            lines.append(f"{label(p)}:")
        lines.append(_line(buf, p, fields, starts, label))
    flush()
    return "\n".join(lines) + "\n"


def decode(buf: bytes) -> str:
    """The readable form of a decompressed event buffer (raises ScriptError when
    its header doesn't fit)."""
    sc = parse(buf)
    return _render(sc, [f"; event script: {sc.count} objects, {len(buf)} bytes", f"objects {sc.count}"])


def decode_commands(buf: bytes, entries) -> str:
    """The readable form of a run of commands with no header (`parse_commands`)."""
    sc = parse_commands(buf, entries)
    for p in sc.entries:
        if p not in sc.commands:
            raise ScriptError(f"entry {p:04X} isn't a command")
    return _render(sc, [f"; event commands: {len(buf)} bytes, {len(sc.entries)} entries", "commands"])


NAMES = {c.name: op for op, c in COMMANDS.items()}


def _number(tok: str, where: str) -> int:
    if not tok.startswith("$"):
        raise ScriptError(f"{where}: expected a $hex value, got {tok!r}")
    try:
        return int(tok[1:], 16)
    except ValueError:
        raise ScriptError(f"{where}: bad number {tok!r}") from None


def _hex(tok: str, where: str) -> bytes:
    try:
        return bytes.fromhex(tok)
    except ValueError:
        raise ScriptError(f"{where}: bad data {tok!r}") from None


def _layout(op: int, args: dict, where: str) -> list:
    """The operand fields of a command as written (a variable command's from
    its first operand and its data)."""
    cmd = COMMANDS[op]
    fields = [(k, kind, SIZES[kind]) for k, kind in cmd.operands]
    if op not in VARIABLE:
        return fields
    first = _number(args.get(fields[0][0], "$00"), where) & 0xFF
    n = (len(_hex(args.get("data", ""), where)) + 2).to_bytes(2, "little")
    probe = bytearray([op, first, 0, 0, 0, 0])
    if op == 0x4E:
        probe[4:6] = n
    elif op == 0x88:
        probe[2:4] = n
    elif op == 0x2E:
        probe[3:5] = n
    probe += bytes(0x10000)
    return shape(bytes(probe), 0)[0]


def encode(text: str) -> bytes:
    """The bytes of a readable form as `decode` writes it: labels resolved in a
    second pass, the length words of the data commands rebuilt from the data."""
    count = None                     # objects; -1 for a run of commands (`decode_commands`)
    objects = []                     # ([16 tokens], where)
    entries = []                     # (label, where)
    items = []                       # ("raw", bytes, where) or ("cmd", (op, fields, args), where)
    labels = {}
    pos = 0
    for n, line in enumerate(text.splitlines(), 1):
        where = f"line {n}"
        s = line.split(";", 1)[0].strip()
        if not s:
            continue
        if count is None and s.startswith("objects "):
            try:
                count = int(s.split()[1])
            except ValueError:
                raise ScriptError(f"{where}: bad object count") from None
            if not 0 <= count <= 0xFF:
                raise ScriptError(f"{where}: {count} objects don't fit a byte")
            pos = header_size(count)
            continue
        if count is None and s == "commands":
            count = -1
            continue
        if count is not None and count >= 0 and s.startswith("object "):
            toks = s.split()[2:]
            if len(toks) != 16:
                raise ScriptError(f"{where}: an object names 16 functions")
            objects.append((toks, where))
            continue
        if count == -1 and s.startswith("entries"):
            entries += [(t, where) for t in s.split()[1:]]
            continue
        if count is None:
            raise ScriptError(f"{where}: before the objects (or commands) line")
        if s.endswith(":"):
            labels[s[:-1]] = pos
            continue
        if s.startswith(".db"):
            b = _hex("".join(s.split()[1:]), where)
            items.append(("raw", b, where))
            pos += len(b)
            continue
        toks = s.split()
        op = NAMES.get(toks[0])
        if op is None:
            raise ScriptError(f"{where}: unknown command {toks[0]!r}")
        args = {}
        for t in toks[1:]:
            k, eq, v = t.partition("=")
            if not eq:
                raise ScriptError(f"{where}: operand {t!r} isn't key=value")
            args[k] = v
        fields = _layout(op, args, where)
        extra = set(args) - {k for k, _, _ in fields}
        if extra:
            raise ScriptError(f"{where}: {toks[0]} takes no {', '.join(sorted(extra))}")
        items.append(("cmd", (op, fields, args), where))
        pos += 1 + sum(f[2] for f in fields)
    if count is None:
        raise ScriptError("no objects (or commands) line")
    if count >= 0 and len(objects) != count:
        raise ScriptError(f"{count} objects declared, {len(objects)} given")
    labels["END"] = pos
    for tok, where in entries:
        if tok not in labels:
            raise ScriptError(f"{where}: entry {tok} isn't a label")

    def target(tok, where):
        if tok in labels:
            return labels[tok]
        if tok.startswith("L") or tok == "END":
            raise ScriptError(f"{where}: undefined label {tok}")
        return None

    out = bytearray([count] if count >= 0 else [])
    for toks, where in objects:
        for t in toks:
            p = target(t, where)
            o = _number(t, where) if p is None else p - 1
            out += (o & 0xFFFF).to_bytes(2, "little")
    for kind, payload, where in items:
        if kind == "raw":
            out += payload
            continue
        op, fields, args = payload
        out.append(op)
        for key, kind_, size in fields:
            i = len(out)
            if kind_ == "data":
                out += _hex(args.get(key, ""), where)
                continue
            if key == "length" and op in DERIVED_LENGTH:
                out += (len(_hex(args.get("data", ""), where)) + 2).to_bytes(2, "little")
                continue
            if key not in args:
                raise ScriptError(f"{where}: {COMMANDS[op].name} needs {key}=")
            tok = args[key]
            if kind_ in ("jump", "back"):
                t = target(tok, where)
                v = _number(tok, where) if t is None else t - i if kind_ == "jump" else i - t
                if not 0 <= v <= 0xFF:
                    raise ScriptError(f"{where}: {tok} is out of reach ({v})")
                out.append(v)
            elif kind_ in ("cmp", "cmp16", "cmpmem") and tok.partition(".")[0] in CMP_NAMES:
                name, _, hi = tok.partition(".")
                out.append(CMP_NAMES.index(name) | (0x80 if hi == "hi" else 0))
            else:
                v = _number(tok, where)
                if v >> (8 * size):
                    raise ScriptError(f"{where}: {key}={tok} doesn't fit {size} bytes")
                out += v.to_bytes(size, "little")
    return bytes(out)


__all__ = ["COMMANDS", "FORMATS", "LZ_FORMATS", "Command", "Script", "ScriptError", "decode", "decode_commands", "encode", "length",
           "parse", "parse_commands", "rows", "shape"]
