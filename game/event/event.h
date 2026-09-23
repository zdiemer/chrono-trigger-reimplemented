/* Event scripts: the bytecode that drives map objects, cutscenes and dialog.
 * The language -- its operations and the table of opcodes -- is in
 * event/script.h; the engine that runs it in event/engine.c; the
 * translation to the original's machine, and the guards, in event/bridge.h.
 *
 * These are the registered entries (game/registry.c): the engine's two, and
 * one for each handler of the original's table at $C0:5D6E that the port
 * has, at its address. A command with a row in the language's table has a
 * generic entry that runs it by its row (EVENT_ENTRIES below, and its guard,
 * <name>_accepts); the rest are bridges, written out, which call out to the
 * original's code. tools/disasm/eventscript.py has the command set. */
#ifndef CT_GAME_EVENT_H
#define CT_GAME_EVENT_H

#include <stdbool.h>

#include "snes/system.h"

/* event/engine.c */
void event_run_init_scripts(Snes *s);
void event_run_object_script(Snes *s);
bool event_has_objects(Snes *s);
bool event_runs_an_object(Snes *s);

/* event/bridge.c: the bridges' guards */
bool event_machine_as_expected(Snes *s);
bool event_takes_object(Snes *s);     /* operand 1 names an object */
bool event_takes_member(Snes *s);     /* operand 1 names a party member whose object is one */
bool event_party_objects_are_objects(Snes *s);
bool event_sound_bank_is_system(Snes *s);

/* The commands run by their rows: entry name, opcode. */
#define EVENT_ENTRIES(X)     X(event_cmd_loop_animation, 0xAA) \
    X(event_cmd_play_animation, 0xAB) \
    X(event_cmd_static_frame, 0xAC) \
    X(event_cmd_reset_animation, 0xAE) \
    X(event_cmd_loop_animation_0, 0xB3) \
    X(event_cmd_loop_animation_1, 0xB4) \
    X(event_cmd_play_animation_for, 0xB7) \
    X(event_cmd_return, 0x00) \
    X(event_cmd_call, 0x02) \
    X(event_cmd_call_when_accepted, 0x03) \
    X(event_cmd_call_and_wait, 0x04) \
    X(event_cmd_call_party_member, 0x05) \
    X(event_cmd_call_party_member_when_accepted, 0x06) \
    X(event_cmd_call_party_member_and_wait, 0x07) \
    X(event_cmd_refuse_calls, 0x08) \
    X(event_cmd_accept_calls, 0x09) \
    X(event_cmd_disable_object, 0x0A) \
    X(event_cmd_make_object_uncallable, 0x0B) \
    X(event_cmd_make_object_callable, 0x0C) \
    X(event_cmd_set_result_from_variable, 0x19) \
    X(event_cmd_if_result, 0x1A) \
    X(event_cmd_set_result_from_memory, 0x1C) \
    X(event_cmd_set_dialog_table, 0xB8) \
    X(event_cmd_dialog, 0xBB) \
    X(event_cmd_choice, 0xC0) \
    X(event_cmd_dialog_top, 0xC1) \
    X(event_cmd_dialog_bottom, 0xC2) \
    X(event_cmd_choice_top, 0xC3) \
    X(event_cmd_choice_bottom, 0xC4) \
    X(event_cmd_face_0, 0x0F) \
    X(event_cmd_face_1, 0x17) \
    X(event_cmd_face_2, 0x1B) \
    X(event_cmd_face_3, 0x1D) \
    X(event_cmd_object_face_0, 0x1E) \
    X(event_cmd_object_face_1, 0x1F) \
    X(event_cmd_get_object_facing, 0x23) \
    X(event_cmd_get_party_member_facing, 0x24) \
    X(event_cmd_object_face_2, 0x25) \
    X(event_cmd_object_face_3, 0x26) \
    X(event_cmd_face_operand, 0xA6) \
    X(event_cmd_face_variable, 0xA7) \
    X(event_cmd_jump_forward, 0x10) \
    X(event_cmd_jump_back, 0x11) \
    X(event_cmd_if_var_const, 0x12) \
    X(event_cmd_if_var_const16, 0x13) \
    X(event_cmd_if_var_var, 0x14) \
    X(event_cmd_if_var_var16, 0x15) \
    X(event_cmd_if_memory, 0x16) \
    X(event_cmd_if_memory0_below, 0x18) \
    X(event_cmd_if_object_in_view, 0x27) \
    X(event_cmd_if_object_on_screen, 0x28) \
    X(event_cmd_wait, 0xAD) \
    X(event_cmd_yield, 0xB1) \
    X(event_cmd_halt, 0xB2) \
    X(event_cmd_wait_4, 0xB9) \
    X(event_cmd_wait_8, 0xBA) \
    X(event_cmd_wait_16, 0xBC) \
    X(event_cmd_wait_32, 0xBD) \
    X(event_cmd_if_any_input, 0x2D) \
    X(event_cmd_if_mapped_held_bit1, 0x30) \
    X(event_cmd_if_mapped_held_bit7, 0x31) \
    X(event_cmd_if_a_held, 0x34) \
    X(event_cmd_if_b_held, 0x35) \
    X(event_cmd_if_x_held, 0x36) \
    X(event_cmd_if_y_held, 0x37) \
    X(event_cmd_if_l_held, 0x38) \
    X(event_cmd_if_r_held, 0x39) \
    X(event_cmd_if_mapped_pressed_bit1, 0x3B) \
    X(event_cmd_if_mapped_pressed_bit7, 0x3C) \
    X(event_cmd_if_a_pressed, 0x3F) \
    X(event_cmd_if_b_pressed, 0x40) \
    X(event_cmd_if_x_pressed, 0x41) \
    X(event_cmd_if_y_pressed, 0x42) \
    X(event_cmd_if_l_pressed, 0x43) \
    X(event_cmd_if_r_pressed, 0x44) \
    X(event_cmd_start_battle, 0xD8) \
    X(event_cmd_store_location, 0xDC) \
    X(event_cmd_set_location, 0xDD) \
    X(event_cmd_set_location_with_1e, 0xDE) \
    X(event_cmd_change_location, 0xE0) \
    X(event_cmd_change_location_from_variables, 0xE2) \
    X(event_cmd_var_from_pointer, 0x48) \
    X(event_cmd_var_from_pointer16, 0x49) \
    X(event_cmd_poke, 0x4A) \
    X(event_cmd_poke16, 0x4B) \
    X(event_cmd_poke_variable, 0x4C) \
    X(event_cmd_poke_variable16, 0x4D) \
    X(event_cmd_get_party_leader, 0x20) \
    X(event_cmd_set_blocking, 0x0D) \
    X(event_cmd_set_arrival_flags, 0x0E) \
    X(event_cmd_get_object_tile, 0x21) \
    X(event_cmd_get_party_member_tile, 0x22) \
    X(event_cmd_show_object_sprite, 0x7C) \
    X(event_cmd_hide_object_sprite, 0x7D) \
    X(event_cmd_set_no_sprite, 0x7E) \
    X(event_cmd_set_solidity, 0x84) \
    X(event_cmd_set_step_count, 0x87) \
    X(event_cmd_set_speed, 0x89) \
    X(event_cmd_set_speed_from_variable, 0x8A) \
    X(event_cmd_set_tile_position, 0x8B) \
    X(event_cmd_set_tile_position_from_variables, 0x8C) \
    X(event_cmd_set_position, 0x8D) \
    X(event_cmd_set_sprite_priority, 0x8E) \
    X(event_cmd_show_sprite, 0x90) \
    X(event_cmd_hide_sprite, 0x91) \
    X(event_cmd_set_animation_line_limit, 0x47) \
    X(event_cmd_set_player_control, 0xE3) \
    X(event_cmd_queue_tile_copy, 0xE5) \
    X(event_cmd_set_layer_extra_speed, 0xE6) \
    X(event_cmd_scroll_screen, 0xE7) \
    X(event_cmd_brightness, 0xF0) \
    X(event_cmd_fixed_color, 0xF1) \
    X(event_cmd_wait_brightness, 0xF2) \
    X(event_cmd_wait_color, 0xF3) \
    X(event_cmd_set_bc, 0xF4) \
    X(event_cmd_wait_apu_port1, 0xED) \
    X(event_cmd_wait_apu_port3, 0xEE) \
    X(event_cmd_memory_set, 0x56) \
    X(event_cmd_memory_set_from_var, 0x58) \
    X(event_cmd_memory_set_from_var16, 0x59) \
    X(event_cmd_memory0_set, 0x5A) \
    X(event_cmd_memory_set_bit, 0x65) \
    X(event_cmd_memory_clear_bit, 0x66) \
    X(event_cmd_var_set, 0x4F) \
    X(event_cmd_var_set16, 0x50) \
    X(event_cmd_var_copy, 0x51) \
    X(event_cmd_var_copy16, 0x52) \
    X(event_cmd_var_from_memory, 0x53) \
    X(event_cmd_var_from_memory16, 0x54) \
    X(event_cmd_var_from_memory0, 0x55) \
    X(event_cmd_var_add, 0x5B) \
    X(event_cmd_var_add_var, 0x5D) \
    X(event_cmd_var_add_var16, 0x5E) \
    X(event_cmd_var_subtract, 0x5F) \
    X(event_cmd_var_subtract16, 0x60) \
    X(event_cmd_var_subtract_var, 0x61) \
    X(event_cmd_var_set_bit, 0x63) \
    X(event_cmd_var_clear_bit, 0x64) \
    X(event_cmd_var_and, 0x67) \
    X(event_cmd_var_or, 0x69) \
    X(event_cmd_var_xor, 0x6B) \
    X(event_cmd_var_shift_right, 0x6F) \
    X(event_cmd_var_increment, 0x71) \
    X(event_cmd_var_increment16, 0x72) \
    X(event_cmd_var_decrement, 0x73) \
    X(event_cmd_var_set_1, 0x75) \
    X(event_cmd_var_set16_1, 0x76) \
    X(event_cmd_var_clear, 0x77) \
    X(event_cmd_var_random, 0x7F)

#define EVENT_DECLARE_ENTRY(name, opcode)     void name(Snes *s);                      bool name##_accepts(Snes *s);
EVENT_ENTRIES(EVENT_DECLARE_ENTRY)

/* The bridges, by file. */

/* event/facing.c */
void event_cmd_face_object(Snes *s);
void event_cmd_face_party_member(Snes *s);

/* event/flow.c */
void event_cmd_run_state_routine_once(Snes *s);

/* event/locations.c */
void event_cmd_change_location_now(Snes *s);
void event_cmd_change_location_now_with_1e(Snes *s);

/* event/memory.c */
void event_cmd_copy_data(Snes *s);

/* event/movement.c */
void event_cmd_move_to_tile(Snes *s);
void event_cmd_move_to_tile_from_variables(Snes *s);
void event_cmd_move_to_tile_keep_facing(Snes *s);
void event_cmd_move_to_tile_from_variables_keep_facing(Snes *s);
void event_cmd_move_to_tile_counted(Snes *s);
void event_cmd_move_toward_object(Snes *s);
void event_cmd_move_toward_party_member(Snes *s);
void event_cmd_move_toward_object_keep_facing(Snes *s);
void event_cmd_move_toward_party_member_keep_facing(Snes *s);
void event_cmd_move_toward_object_counted(Snes *s);
void event_cmd_move_toward_party_member_counted(Snes *s);
void event_cmd_move_toward_party_member_until_on_screen(Snes *s);
void event_cmd_move_in_direction(Snes *s);
void event_cmd_move_in_direction_keep_facing(Snes *s);
void event_cmd_move_by_variables(Snes *s);
void event_cmd_move_with_velocity(Snes *s);
void event_cmd_move_to_tile_with_velocity(Snes *s);
void event_cmd_run_state_routine(Snes *s);
void event_cmd_follow_object(Snes *s);
void event_cmd_follow_party_member(Snes *s);

/* event/party.c */
void event_cmd_party_routine_0a(Snes *s);
void event_cmd_party_routine_6_7(Snes *s);
void event_cmd_party_routine_6(Snes *s);
void event_cmd_party_routine_7(Snes *s);
void event_cmd_remove_from_party(Snes *s);

/* event/screen.c */
void event_cmd_copy_tiles(Snes *s);

/* event/sound.c */
void event_cmd_play_sound(Snes *s);
void event_cmd_play_song(Snes *s);
void event_cmd_sound_81_83(Snes *s);
void event_cmd_sound_command(Snes *s);

#endif
