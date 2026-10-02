/* Event scripts: the bytecode that drives map objects, cutscenes and dialog.
 * The language -- its operations and the table of opcodes -- is in
 * event/script.h; the engine that runs it in event/engine.c; the
 * translation to the original's machine, and the guards, in event/bridge.h.
 *
 * These are the registered entries (game/registry.c): the engine's two, and
 * one for each command the language's table has a row for, at its handler's
 * address in the original's table ($C0:5D6E), each named for its operation
 * and guarded by its row (<name>_accepts). tools/disasm/eventscript.py has
 * the command set. */
#ifndef CT_GAME_EVENT_H
#define CT_GAME_EVENT_H

#include <stdbool.h>

#include "snes/system.h"

/* The engine's entries on the machine (event/bridge.c) */
void event_run_init_scripts(Snes *s);
void event_run_object_script(Snes *s);
bool event_has_objects(Snes *s);
bool event_runs_an_object(Snes *s);

/* The commands, by their rows (event/entries.c): entry name, opcode. The
 * operations' entries run the command by its row; the bridges' entries are the
 * bridges themselves. */
#define EVENT_ENTRIES(X) \
    X(event_cmd_loop_animation, 0xAA) \
    X(event_cmd_play_animation, 0xAB) \
    X(event_cmd_hold_frame, 0xAC) \
    X(event_cmd_stop_animating, 0xAE) \
    X(event_cmd_loop_animation_0, 0xB3) \
    X(event_cmd_loop_animation_1, 0xB4) \
    X(event_cmd_play_animation_for, 0xB7) \
    X(event_cmd_return_from_function, 0x00) \
    X(event_cmd_call_function, 0x02) \
    X(event_cmd_call_when_taken, 0x03) \
    X(event_cmd_call_and_wait, 0x04) \
    X(event_cmd_call_function_member, 0x05) \
    X(event_cmd_call_when_taken_member, 0x06) \
    X(event_cmd_call_and_wait_member, 0x07) \
    X(event_cmd_take_no_calls, 0x08) \
    X(event_cmd_take_calls, 0x09) \
    X(event_cmd_disable_object, 0x0A) \
    X(event_cmd_make_unreachable, 0x0B) \
    X(event_cmd_make_reachable, 0x0C) \
    X(event_cmd_set_result, 0x19) \
    X(event_cmd_if_answer, 0x1A) \
    X(event_cmd_set_result_story, 0x1C) \
    X(event_cmd_use_text, 0xB8) \
    X(event_cmd_say, 0xBB) \
    X(event_cmd_ask, 0xC0) \
    X(event_cmd_say_top, 0xC1) \
    X(event_cmd_say_bottom, 0xC2) \
    X(event_cmd_ask_top, 0xC3) \
    X(event_cmd_ask_bottom, 0xC4) \
    X(event_cmd_face_0, 0x0F) \
    X(event_cmd_face_1, 0x17) \
    X(event_cmd_face_2, 0x1B) \
    X(event_cmd_face_3, 0x1D) \
    X(event_cmd_face_object_0, 0x1E) \
    X(event_cmd_face_object_1, 0x1F) \
    X(event_cmd_note_facing, 0x23) \
    X(event_cmd_note_facing_member, 0x24) \
    X(event_cmd_face_object_2, 0x25) \
    X(event_cmd_face_object_3, 0x26) \
    X(event_cmd_face, 0xA6) \
    X(event_cmd_face_variable, 0xA7) \
    X(event_cmd_jump, 0x10) \
    X(event_cmd_jump_back, 0x11) \
    X(event_cmd_if_compares, 0x12) \
    X(event_cmd_if_compares_word, 0x13) \
    X(event_cmd_if_compares_variable, 0x14) \
    X(event_cmd_if_compares_variable_word, 0x15) \
    X(event_cmd_if_story, 0x16) \
    X(event_cmd_if_story_before, 0x18) \
    X(event_cmd_if_in_view, 0x27) \
    X(event_cmd_if_well_on_screen, 0x28) \
    X(event_cmd_wait_frames, 0xAD) \
    X(event_cmd_wait_for_next_frame, 0xB1) \
    X(event_cmd_stop_here, 0xB2) \
    X(event_cmd_wait_frames_4, 0xB9) \
    X(event_cmd_wait_frames_8, 0xBA) \
    X(event_cmd_wait_frames_16, 0xBC) \
    X(event_cmd_wait_frames_32, 0xBD) \
    X(event_cmd_if_assignment_held_any, 0x2D) \
    X(event_cmd_if_dash_held, 0x30) \
    X(event_cmd_if_confirm_held, 0x31) \
    X(event_cmd_if_held_a, 0x34) \
    X(event_cmd_if_held_b, 0x35) \
    X(event_cmd_if_held_x, 0x36) \
    X(event_cmd_if_held_y, 0x37) \
    X(event_cmd_if_held_l, 0x38) \
    X(event_cmd_if_held_r, 0x39) \
    X(event_cmd_if_pressed_dash, 0x3B) \
    X(event_cmd_if_pressed_confirm, 0x3C) \
    X(event_cmd_if_pressed_a, 0x3F) \
    X(event_cmd_if_pressed_b, 0x40) \
    X(event_cmd_if_pressed_x, 0x41) \
    X(event_cmd_if_pressed_y, 0x42) \
    X(event_cmd_if_pressed_l, 0x43) \
    X(event_cmd_if_pressed_r, 0x44) \
    X(event_cmd_fight, 0xD8) \
    X(event_cmd_set_location_put_aside, 0xDC) \
    X(event_cmd_set_location, 0xDD) \
    X(event_cmd_set_location_raising_1e, 0xDE) \
    X(event_cmd_go_to_location, 0xE0) \
    X(event_cmd_go_to_worked_out_location, 0xE2) \
    X(event_cmd_peek, 0x48) \
    X(event_cmd_peek_word, 0x49) \
    X(event_cmd_poke, 0x4A) \
    X(event_cmd_poke_word, 0x4B) \
    X(event_cmd_poke_variable, 0x4C) \
    X(event_cmd_poke_variable_word, 0x4D) \
    X(event_cmd_launch, 0x7B) \
    X(event_cmd_note_leader, 0x20) \
    X(event_cmd_set_blocked_by, 0x0D) \
    X(event_cmd_set_arrival, 0x0E) \
    X(event_cmd_note_tile, 0x21) \
    X(event_cmd_note_tile_member, 0x22) \
    X(event_cmd_set_sprite_shown_object, 0x7C) \
    X(event_cmd_set_sprite_hidden_object, 0x7D) \
    X(event_cmd_set_sprite_none, 0x7E) \
    X(event_cmd_set_solidity, 0x84) \
    X(event_cmd_set_move_length, 0x87) \
    X(event_cmd_set_speed, 0x89) \
    X(event_cmd_set_speed_variable, 0x8A) \
    X(event_cmd_stand_on_tile, 0x8B) \
    X(event_cmd_stand_on_tile_variables, 0x8C) \
    X(event_cmd_place, 0x8D) \
    X(event_cmd_set_layering, 0x8E) \
    X(event_cmd_set_sprite_shown, 0x90) \
    X(event_cmd_set_sprite_hidden, 0x91) \
    X(event_cmd_set_animation_line_limit, 0x47) \
    X(event_cmd_set_player_control, 0xE3) \
    X(event_cmd_copy_tiles, 0xE4) \
    X(event_cmd_queue_tile_copy, 0xE5) \
    X(event_cmd_drift_layers, 0xE6) \
    X(event_cmd_scroll_camera, 0xE7) \
    X(event_cmd_fade, 0xF0) \
    X(event_cmd_tint, 0xF1) \
    X(event_cmd_wait_for_screen_fade, 0xF2) \
    X(event_cmd_wait_for_screen_tint, 0xF3) \
    X(event_cmd_shake_screen, 0xF4)     X(event_cmd_layer_work_bg2_left, 0x2A)     X(event_cmd_layer_work_bg2_right, 0x2B)     X(event_cmd_layer_work_credits, 0x32)     X(event_cmd_place_bg3, 0x2C)     X(event_cmd_place_the_credits, 0x2F)     X(event_cmd_move_searchlight, 0xFE) \
    X(event_cmd_wait_for_music_fade, 0xED) \
    X(event_cmd_wait_for_music_to_end, 0xEE) \
    X(event_cmd_record, 0x56) \
    X(event_cmd_record_variable, 0x58) \
    X(event_cmd_record_variable_word, 0x59) \
    X(event_cmd_record_progress, 0x5A) \
    X(event_cmd_mark_story_flag_raised, 0x65) \
    X(event_cmd_mark_story_flag_lowered, 0x66) \
    X(event_cmd_assign, 0x4F) \
    X(event_cmd_assign_word, 0x50) \
    X(event_cmd_assign_variable, 0x51) \
    X(event_cmd_assign_variable_word, 0x52) \
    X(event_cmd_assign_story, 0x53) \
    X(event_cmd_assign_story_word, 0x54) \
    X(event_cmd_assign_story_progress, 0x55) \
    X(event_cmd_add, 0x5B) \
    X(event_cmd_add_variable, 0x5D) \
    X(event_cmd_add_variable_word, 0x5E) \
    X(event_cmd_subtract, 0x5F) \
    X(event_cmd_subtract_word, 0x60) \
    X(event_cmd_subtract_variable, 0x61) \
    X(event_cmd_set_flags_by_number, 0x63) \
    X(event_cmd_keep_only_flags_by_number, 0x64) \
    X(event_cmd_keep_only_flags, 0x67) \
    X(event_cmd_set_flags, 0x69) \
    X(event_cmd_toggle_flags, 0x6B) \
    X(event_cmd_halve, 0x6F) \
    X(event_cmd_add_1, 0x71) \
    X(event_cmd_add_word_1, 0x72) \
    X(event_cmd_subtract_1, 0x73) \
    X(event_cmd_assign_1, 0x75) \
    X(event_cmd_assign_word_1, 0x76) \
    X(event_cmd_assign_0, 0x77) \
    X(event_cmd_roll, 0x7F) \
    X(event_cmd_face_toward_object, 0xA8) \
    X(event_cmd_face_toward_member, 0xA9) \
    X(event_cmd_jump_to_tile, 0x7A) \
    X(event_cmd_move_toward_party_member_until_on_screen, 0x8F) \
    X(event_cmd_move_in_direction, 0x92) \
    X(event_cmd_move_toward_object, 0x94) \
    X(event_cmd_move_toward_party_member, 0x95) \
    X(event_cmd_move_to_tile, 0x96) \
    X(event_cmd_move_to_tile_from_variables, 0x97) \
    X(event_cmd_move_toward_object_counted, 0x98) \
    X(event_cmd_move_toward_party_member_counted, 0x99) \
    X(event_cmd_move_to_tile_counted, 0x9A) \
    X(event_cmd_move_in_direction_keep_facing, 0x9C) \
    X(event_cmd_move_in_direction_from_variables_keep_facing, 0x9D) \
    X(event_cmd_move_toward_object_keep_facing, 0x9E) \
    X(event_cmd_move_toward_party_member_keep_facing, 0x9F) \
    X(event_cmd_move_to_tile_keep_facing, 0xA0) \
    X(event_cmd_move_to_tile_from_variables_keep_facing, 0xA1) \
    X(event_cmd_walk_with_the_party, 0xB0) \
    X(event_cmd_walk_with_the_party_once, 0xAF) \
    X(event_cmd_take_formation, 0xD9) \
    X(event_cmd_gather_party, 0xDA) \
    X(event_cmd_follow_object, 0xB5) \
    X(event_cmd_follow_party_member, 0xB6)

#define EVENT_BRIDGES(X) \
    X(event_cmd_go_to_location_now_raising_1e, 0xDF) \
    X(event_cmd_go_to_location_now, 0xE1) \
    X(event_cmd_copy_data, 0x4E) \
    X(event_cmd_party_routine_0a, 0xD5) \
    X(event_cmd_take_out_of_party, 0xD6) \
    X(event_cmd_party_routine_6_7, 0xF8) \
    X(event_cmd_party_routine_6, 0xF9) \
    X(event_cmd_party_routine_7, 0xFA) \
    X(event_cmd_play_effect, 0xE8) \
    X(event_cmd_play_song, 0xEA) \
    X(event_cmd_fade_music, 0xEB) \
    X(event_cmd_send_sound_command, 0xEC)     X(event_cmd_choose_credits, 0x29)     X(event_cmd_screen_command, 0xFF)

#define EVENT_DECLARE_ENTRY(name, opcode) \
    void name(Snes *s);                  \
    bool name##_accepts(Snes *s);
EVENT_ENTRIES(EVENT_DECLARE_ENTRY)
EVENT_BRIDGES(EVENT_DECLARE_ENTRY)

/* Event $FF's call of the menus' party routine 8, which its dispatch reaches
 * through its own table (event/screen.c), with the command's guard. */
void event_screen_party_routine(Snes *s);
bool event_screen_party_routine_accepts(Snes *s);

#endif
