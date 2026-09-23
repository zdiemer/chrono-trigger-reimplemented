/* Event scripts: the bytecode that drives map objects, cutscenes and
 * dialog. The protocol a command handler works to is in event/script.h; the
 * translation to the original's registers, and the guards, in event/bridge.h.
 * These are the registered entries (game/registry.c): the engine's two, and
 * one per command handler, at the handler's address in the table at
 * $C0:5D6E. tools/disasm/eventscript.py has the command set. */
#ifndef CT_GAME_EVENT_H
#define CT_GAME_EVENT_H

#include <stdbool.h>

#include "snes/system.h"

/* event/engine.c */
void event_run_init_scripts(Snes *s);
void event_run_object_script(Snes *s);
bool event_has_objects(Snes *s);
bool event_runs_an_object(Snes *s);

/* event/bridge.c: the guards every command runs under (event/bridge.h) */
bool event_machine_as_expected(Snes *s);
bool event_takes_object(Snes *s);     /* operand 1 names an object */
bool event_takes_member(Snes *s);     /* operand 1 names a party member whose object is one */
bool event_calls_member(Snes *s);     /* the same, or a member the command checks for first */

/* event/animation.c: the running object's animation */
void event_cmd_loop_animation(Snes *s);
void event_cmd_loop_animation_0(Snes *s);
void event_cmd_loop_animation_1(Snes *s);
void event_cmd_reset_animation(Snes *s);
void event_cmd_play_animation(Snes *s);
void event_cmd_play_animation_for(Snes *s);
void event_cmd_static_frame(Snes *s);

/* event/arithmetic.c: script variables */
void event_cmd_set_animation_line_limit(Snes *s);
void event_cmd_var_set(Snes *s);
void event_cmd_var_set16(Snes *s);
void event_cmd_var_copy(Snes *s);
void event_cmd_var_copy16(Snes *s);
void event_cmd_var_add(Snes *s);
void event_cmd_var_add_var(Snes *s);
void event_cmd_var_add_var16(Snes *s);
void event_cmd_var_subtract(Snes *s);
void event_cmd_var_subtract16(Snes *s);
void event_cmd_var_subtract_var(Snes *s);
void event_cmd_var_set_bit(Snes *s);
void event_cmd_var_clear_bit(Snes *s);
void event_cmd_var_and(Snes *s);
void event_cmd_var_or(Snes *s);
void event_cmd_var_xor(Snes *s);
void event_cmd_var_shift_right(Snes *s);
void event_cmd_var_increment(Snes *s);
void event_cmd_var_increment16(Snes *s);
void event_cmd_var_decrement(Snes *s);
void event_cmd_var_set_1(Snes *s);
void event_cmd_var_set16_1(Snes *s);
void event_cmd_var_clear(Snes *s);
void event_cmd_var_random(Snes *s);
bool event_sets_a_bit(Snes *s);
bool event_clears_a_bit(Snes *s);

/* event/calls.c: calls between objects' scripts */
void event_cmd_return(Snes *s);
void event_cmd_call(Snes *s);
void event_cmd_call_when_accepted(Snes *s);
void event_cmd_call_and_wait(Snes *s);
void event_cmd_call_party_member(Snes *s);
void event_cmd_call_party_member_when_accepted(Snes *s);
void event_cmd_call_party_member_and_wait(Snes *s);
void event_cmd_refuse_calls(Snes *s);
void event_cmd_accept_calls(Snes *s);
void event_cmd_disable_object(Snes *s);
void event_cmd_make_object_uncallable(Snes *s);
void event_cmd_make_object_callable(Snes *s);
void event_cmd_set_blocking(Snes *s);
void event_cmd_set_arrival_flags(Snes *s);
bool event_return_finds_a_level(Snes *s);

/* event/dialog.c: dialog */
void event_cmd_set_dialog_table(Snes *s);
void event_cmd_dialog(Snes *s);
void event_cmd_dialog_top(Snes *s);
void event_cmd_dialog_bottom(Snes *s);
void event_cmd_choice(Snes *s);
void event_cmd_choice_top(Snes *s);
void event_cmd_choice_bottom(Snes *s);
bool event_dialog_has_strings(Snes *s);

/* event/facing.c: which way objects face */
void event_cmd_face_object(Snes *s);
void event_cmd_face_party_member(Snes *s);
void event_cmd_face_0(Snes *s);
void event_cmd_face_1(Snes *s);
void event_cmd_face_2(Snes *s);
void event_cmd_face_3(Snes *s);
void event_cmd_face_operand(Snes *s);
void event_cmd_face_variable(Snes *s);
void event_cmd_object_face_0(Snes *s);
void event_cmd_object_face_1(Snes *s);
void event_cmd_object_face_2(Snes *s);
void event_cmd_object_face_3(Snes *s);

/* event/flow.c: jumps, conditions and waits */
void event_cmd_run_state_routine_once(Snes *s);
void event_cmd_jump_forward(Snes *s);
void event_cmd_jump_back(Snes *s);
void event_cmd_if_var_const(Snes *s);
void event_cmd_if_var_const16(Snes *s);
void event_cmd_if_var_var(Snes *s);
void event_cmd_if_var_var16(Snes *s);
void event_cmd_if_memory(Snes *s);
void event_cmd_if_memory0_below(Snes *s);
void event_cmd_if_result(Snes *s);
void event_cmd_if_object_in_view(Snes *s);
void event_cmd_if_object_on_screen(Snes *s);
void event_cmd_wait(Snes *s);
void event_cmd_wait_4(Snes *s);
void event_cmd_wait_8(Snes *s);
void event_cmd_wait_16(Snes *s);
void event_cmd_wait_32(Snes *s);
void event_cmd_yield(Snes *s);
void event_cmd_halt(Snes *s);
bool event_compares_bytes(Snes *s);
bool event_compares_words(Snes *s);
bool event_compares_words_var(Snes *s);
bool event_compares_memory(Snes *s);

/* event/input.c: conditions on the controller */
void event_cmd_if_any_input(Snes *s);
void event_cmd_if_mapped_held_bit1(Snes *s);
void event_cmd_if_mapped_held_bit7(Snes *s);
void event_cmd_if_a_held(Snes *s);
void event_cmd_if_b_held(Snes *s);
void event_cmd_if_x_held(Snes *s);
void event_cmd_if_y_held(Snes *s);
void event_cmd_if_l_held(Snes *s);
void event_cmd_if_r_held(Snes *s);
void event_cmd_if_mapped_pressed_bit1(Snes *s);
void event_cmd_if_mapped_pressed_bit7(Snes *s);
void event_cmd_if_a_pressed(Snes *s);
void event_cmd_if_b_pressed(Snes *s);
void event_cmd_if_x_pressed(Snes *s);
void event_cmd_if_y_pressed(Snes *s);
void event_cmd_if_l_pressed(Snes *s);
void event_cmd_if_r_pressed(Snes *s);

/* event/locations.c: locations, battles and the party */
void event_cmd_change_location_now(Snes *s);
void event_cmd_change_location_now_with_1e(Snes *s);
void event_cmd_party_routine_0a(Snes *s);
void event_cmd_remove_from_party(Snes *s);
void event_cmd_change_location(Snes *s);
void event_cmd_change_location_from_variables(Snes *s);
void event_cmd_store_location(Snes *s);
void event_cmd_set_location(Snes *s);
void event_cmd_set_location_with_1e(Snes *s);
void event_cmd_start_battle(Snes *s);
bool event_party_objects_are_objects(Snes *s);

/* event/memory.c: memory */
void event_cmd_var_from_pointer(Snes *s);
void event_cmd_var_from_pointer16(Snes *s);
void event_cmd_poke(Snes *s);
void event_cmd_poke16(Snes *s);
void event_cmd_poke_variable(Snes *s);
void event_cmd_poke_variable16(Snes *s);
void event_cmd_copy_data(Snes *s);
void event_cmd_var_from_memory(Snes *s);
void event_cmd_var_from_memory16(Snes *s);
void event_cmd_var_from_memory0(Snes *s);
void event_cmd_memory_set(Snes *s);
void event_cmd_memory_set_from_var(Snes *s);
void event_cmd_memory_set_from_var16(Snes *s);
void event_cmd_memory0_set(Snes *s);
void event_cmd_memory_set_bit(Snes *s);
void event_cmd_memory_clear_bit(Snes *s);

/* event/movement.c: moving objects */
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

/* event/properties.c: object properties */
void event_cmd_set_speed(Snes *s);
void event_cmd_set_speed_from_variable(Snes *s);
void event_cmd_set_solidity(Snes *s);
void event_cmd_set_step_count(Snes *s);
void event_cmd_set_tile_position(Snes *s);
void event_cmd_set_tile_position_from_variables(Snes *s);
void event_cmd_set_position(Snes *s);
void event_cmd_set_sprite_priority(Snes *s);
void event_cmd_show_sprite(Snes *s);
void event_cmd_hide_sprite(Snes *s);
void event_cmd_set_no_sprite(Snes *s);
void event_cmd_show_object_sprite(Snes *s);
void event_cmd_hide_object_sprite(Snes *s);

/* event/screen.c: the screen, and the party routines */
void event_cmd_copy_tiles(Snes *s);
void event_cmd_party_routine_6_7(Snes *s);
void event_cmd_party_routine_6(Snes *s);
void event_cmd_party_routine_7(Snes *s);
void event_cmd_set_player_control(Snes *s);
void event_cmd_brightness(Snes *s);
void event_cmd_wait_brightness(Snes *s);
void event_cmd_wait_color(Snes *s);
void event_cmd_fixed_color(Snes *s);
void event_cmd_set_bc(Snes *s);
void event_cmd_queue_tile_copy(Snes *s);
void event_cmd_set_layer_extra_speed(Snes *s);
void event_cmd_scroll_screen(Snes *s);

/* event/sound.c: sound */
void event_cmd_play_sound(Snes *s);
void event_cmd_play_song(Snes *s);
void event_cmd_sound_81_83(Snes *s);
void event_cmd_sound_command(Snes *s);
void event_cmd_wait_apu_port1(Snes *s);
void event_cmd_wait_apu_port3(Snes *s);
bool event_sound_bank_is_system(Snes *s);

/* event/variables.c: reading into variables */
void event_cmd_set_result_from_variable(Snes *s);
void event_cmd_set_result_from_memory(Snes *s);
void event_cmd_get_party_leader(Snes *s);
void event_cmd_get_object_tile(Snes *s);
void event_cmd_get_party_member_tile(Snes *s);
void event_cmd_get_object_facing(Snes *s);
void event_cmd_get_party_member_facing(Snes *s);

#endif
