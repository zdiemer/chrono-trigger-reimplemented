/* Where the command scripts (menu/script.h) meet the original's machine
 * (menu/script_bridge.c): the interpreter's registered entry, one entry per
 * command for the callers that reach a handler by its address (the
 * original's own interpreter, where the port hands a command back to it, and
 * ct_fuzz, which exercises each alone), and their guards.
 *
 * Every handler has the same calling convention in the original: entered
 * through the table at $C2:0F91 with 16-bit A and index registers, A and X
 * the opcode doubled and the carry clear, the command at the script pointer
 * menu_object_script; it returns A = what the interpreter does next (a
 * length, a branch, or 0: the carry set to end the object, clear to wait),
 * with the zero flag from it. It also leaves X and the carry, which the
 * original's interpreter hands its caller when the command stops the object:
 * the entries work them out (menu/script_bridge.c); Y and the sign and
 * overflow flags are dead, and declared so at every entry
 * (game/registry.c). */
#ifndef CT_GAME_MENU_SCRIPT_BRIDGE_H
#define CT_GAME_MENU_SCRIPT_BRIDGE_H

#include "snes/system.h"

/* The commands' entries, by opcode, in the order of the table at $C2:0F91:
 * X(name, opcode). */
#define MENU_SCRIPT_ENTRIES(X) \
    X(menu_object_cmd_reset_state, 0x00) \
    X(menu_object_cmd_set_sprite_palette, 0x01) \
    X(menu_object_cmd_set_sprite_priority, 0x02) \
    X(menu_object_cmd_start_sprite_object, 0x03) \
    X(menu_object_cmd_load_palette_row, 0x04) \
    X(menu_object_cmd_change_location, 0x05) \
    X(menu_object_cmd_wait_frame, 0x06) \
    X(menu_object_cmd_write_table_byte, 0x07) \
    X(menu_object_cmd_set_scene_script, 0x08) \
    X(menu_object_cmd_start_object, 0x09) \
    X(menu_object_cmd_clear_slot_byte, 0x0A) \
    X(menu_object_cmd_increment_slot_byte, 0x0B) \
    X(menu_object_cmd_decrement_slot_byte, 0x0C) \
    X(menu_object_cmd_set_slot_byte, 0x0D) \
    X(menu_object_cmd_set_slot_bits, 0x0E) \
    X(menu_object_cmd_clear_slot_bits, 0x0F) \
    X(menu_object_cmd_clear_cell, 0x10) \
    X(menu_object_cmd_increment_cell, 0x11) \
    X(menu_object_cmd_decrement_cell, 0x12) \
    X(menu_object_cmd_set_cell, 0x13) \
    X(menu_object_cmd_set_cell_bits, 0x14) \
    X(menu_object_cmd_clear_cell_bits, 0x15) \
    X(menu_object_cmd_store_slot_byte, 0x16) \
    X(menu_object_cmd_load_slot_byte, 0x17) \
    X(menu_object_cmd_copy_slot_byte, 0x18) \
    X(menu_object_cmd_copy_cell, 0x19) \
    X(menu_object_cmd_jump, 0x1A) \
    X(menu_object_cmd_loop_slot_byte, 0x1B) \
    X(menu_object_cmd_branch_if_slot_byte_zero, 0x1C) \
    X(menu_object_cmd_branch_if_slot_byte_set, 0x1D) \
    X(menu_object_cmd_branch_if_slot_byte_not, 0x1E) \
    X(menu_object_cmd_branch_if_slot_byte_is, 0x1F) \
    X(menu_object_cmd_branch_if_slot_bits_set, 0x20) \
    X(menu_object_cmd_branch_if_slot_bits_clear, 0x21) \
    X(menu_object_cmd_branch_if_cell_zero, 0x22) \
    X(menu_object_cmd_branch_if_cell_set, 0x23) \
    X(menu_object_cmd_branch_if_cell_not, 0x24) \
    X(menu_object_cmd_branch_if_cell_is, 0x25) \
    X(menu_object_cmd_branch_if_cell_bits_set, 0x26) \
    X(menu_object_cmd_branch_if_cell_bits_clear, 0x27) \
    X(menu_object_cmd_fade_out, 0x28) \
    X(menu_object_cmd_fade_in, 0x29) \
    X(menu_object_cmd_mosaic_on, 0x2A) \
    X(menu_object_cmd_mosaic_off, 0x2B) \
    X(menu_object_cmd_set_position, 0x2C) \
    X(menu_object_cmd_nothing, 0x2D) \
    X(menu_object_cmd_set_velocity_x, 0x2E) \
    X(menu_object_cmd_set_velocity_y, 0x2F) \
    X(menu_object_cmd_set_sprite_script, 0x30) \
    X(menu_object_cmd_animate_for, 0x31) \
    X(menu_object_cmd_scroll_map_for, 0x32) \
    X(menu_object_cmd_queue_vram_upload, 0x33) \
    X(menu_object_cmd_call_routine, 0x34) \
    X(menu_object_cmd_start_handler, 0x35) \
    X(menu_object_cmd_call, 0x36) \
    X(menu_object_cmd_return, 0x37) \
    X(menu_object_cmd_wait_for, 0x38) \
    X(menu_object_cmd_animate_wait_for, 0x39) \
    X(menu_object_cmd_stop_after_age, 0x3A) \
    X(menu_object_cmd_sound_18, 0x3B) \
    X(menu_object_cmd_sound_19, 0x3C) \
    X(menu_object_cmd_play_song_unless_flag, 0x3D) \
    X(menu_object_cmd_redraw_map, 0x3E) \
    X(menu_object_cmd_move_x_to, 0x3F) \
    X(menu_object_cmd_move_y_to, 0x40) \
    X(menu_object_cmd_wait_frame_again, 0x41) \
    X(menu_object_cmd_start_handler_high, 0x42) \
    X(menu_object_cmd_start_script, 0x43) \
    X(menu_object_cmd_mark_table_entry, 0x44) \
    X(menu_object_cmd_unmark_table_entry, 0x45) \
    X(menu_object_cmd_add_to_slot_byte, 0x46) \
    X(menu_object_cmd_subtract_from_slot_byte, 0x47) \
    X(menu_object_cmd_add_to_cell, 0x48) \
    X(menu_object_cmd_subtract_from_cell, 0x49) \
    X(menu_object_cmd_play_song, 0x4A) \
    X(menu_object_cmd_sound_command, 0x4B) \
    X(menu_object_cmd_branch_if_cell_below, 0x4C) \
    X(menu_object_cmd_branch_if_cell_at_least, 0x4D) \
    X(menu_object_cmd_call_routine_long, 0x4E) \
    X(menu_object_cmd_copy_tilemap_rows, 0x4F) \
    X(menu_object_cmd_copy_tilemap_block, 0x50) \
    X(menu_object_cmd_scroll_map_plane_for, 0x51) \
    X(menu_object_cmd_end, 0x52)

#define MENU_SCRIPT_DECLARE_ENTRY(name, opcode) \
    void name(Snes *s);                         \
    bool name##_accepts(Snes *s);
MENU_SCRIPT_ENTRIES(MENU_SCRIPT_DECLARE_ENTRY)
#undef MENU_SCRIPT_DECLARE_ENTRY

/* The interpreter ($C2:0F63), run as a slot's handler, and its guard. */
void menu_run_object_script(Snes *s);
bool menu_object_script_runs(Snes *s);

#endif
