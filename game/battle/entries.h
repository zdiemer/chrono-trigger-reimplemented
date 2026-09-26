/* Battle mode: the registered entries, as the runtime calls them (game/registry.c). */
#ifndef CT_GAME_BATTLE_ENTRIES_H
#define CT_GAME_BATTLE_ENTRIES_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* What the original's routines leave in the carry for its callers, worked out
 * for the bridges that call them: the marking of the first target ($C1:AD09)
 * and of the others ($C1:AD35), from the candidates and the carry it found
 * (battle/targets.c); and a combatant's screen box (battle/objects.c), its
 * last sum's. */
bool battle_first_mark_carry(const GameState *g, bool carry);
bool battle_other_marks_carry(const GameState *g, bool carry);
bool battle_bounds_carry(const GameState *g, unsigned slot);

/* The enemies' minds: battle/random.c, positions.c, targets.c, enemy_scripts.c,
 * enemy_conditions.c, enemy_actions.c, enemy_turn.c, and main_loop.c's
 * beaten enemies. */
/* battle/random.c */
void battle_draw_random(Snes *s);
void battle_draw_random_other(Snes *s);

/* battle/positions.c */
void battle_search_for_target(Snes *s);
void battle_search_nearest_member(Snes *s);
void battle_search_farthest_member(Snes *s);
void battle_search_nearest_other_enemy(Snes *s);
void battle_search_farthest_enemy(Snes *s);
void battle_search_within_reach(Snes *s);
void battle_search_level(Snes *s);
void battle_search_other_below(Snes *s);
void battle_search_other_to_the_right(Snes *s);
void battle_search_walk(Snes *s);
void battle_search_in_lower_half(Snes *s);
void battle_search_in_upper_half(Snes *s);
void battle_search_at_right_edge(Snes *s);
void battle_search_at_left_edge(Snes *s);
void battle_search_nearer_to_third(Snes *s);
void battle_search_within_wide_reach(Snes *s);
void battle_within_long_reach(Snes *s);
void battle_within_search_reach(Snes *s);
void battle_within_far_reach(Snes *s);
void battle_touching(Snes *s);
void battle_within_near_reach(Snes *s);
void battle_grid_stops_combatant(Snes *s);
void battle_box_meets_another(Snes *s);
void battle_direction_between(Snes *s);
void battle_distance_along(Snes *s);

/* battle/targets.c */
void battle_select_party(Snes *s);
void battle_select_nobody(Snes *s);
void battle_select_everyone(Snes *s);
void battle_select_itself(Snes *s);
void battle_select_last_to_act_on_it(Snes *s);
void battle_select_random_member(Snes *s);
void battle_select_nearest_member(Snes *s);
void battle_select_farthest_member(Snes *s);
void battle_select_weakest_member(Snes *s);
void battle_select_members_by_condition(Snes *s);
void battle_select_members_by_status_1(Snes *s);
void battle_select_members_by_status_2(Snes *s);
void battle_select_members_by_status_3(Snes *s);
void battle_select_members_by_status_4(Snes *s);
void battle_select_members_with_02(Snes *s);
void battle_select_members_with_80(Snes *s);
void battle_select_members_with_04(Snes *s);
void battle_select_members_with_04_of_4(Snes *s);
void battle_select_members_with_40_of_4(Snes *s);
void battle_select_members_with_10_of_3(Snes *s);
void battle_select_members_with_08_of_1(Snes *s);
void battle_select_other_enemies(Snes *s);
void battle_select_enemies(Snes *s);
void battle_select_nearest_other_enemy(Snes *s);
void battle_select_farthest_enemy(Snes *s);
void battle_select_weakest_enemy(Snes *s);
void battle_select_other_enemies_by_condition(Snes *s);
void battle_select_enemies_by_condition(Snes *s);
void battle_select_other_enemies_by_status_1(Snes *s);
void battle_select_enemies_by_status_1(Snes *s);
void battle_select_other_enemies_by_status_2(Snes *s);
void battle_select_enemies_by_status_2(Snes *s);
void battle_select_other_enemies_with_02(Snes *s);
void battle_select_other_enemies_with_80(Snes *s);
void battle_select_other_enemies_with_04(Snes *s);
void battle_select_other_enemies_with_40_of_4(Snes *s);
void battle_select_other_enemies_with_02_of_condition(Snes *s);
void battle_select_other_enemies_with_01_of_19(Snes *s);
void battle_select_weakest_other_enemy(Snes *s);
void battle_select_slot_3(Snes *s);
void battle_select_slot_4(Snes *s);
void battle_select_slot_5(Snes *s);
void battle_select_slot_6(Snes *s);
void battle_select_slot_7(Snes *s);
void battle_select_slot_8(Snes *s);
void battle_select_slot_9(Snes *s);
void battle_select_slot_10(Snes *s);
void battle_select_reserve_enemy(Snes *s);
void battle_select_slot_0(Snes *s);
void battle_select_slot_1(Snes *s);
void battle_select_slot_2(Snes *s);
void battle_select_slot_3_again(Snes *s);
void battle_select_slot_4_again(Snes *s);
void battle_select_slot_5_again(Snes *s);
void battle_select_slot_6_again(Snes *s);
void battle_select_strongest_member(Snes *s);
void battle_select_other_enemy_at_random(Snes *s);
void battle_select_from_script(Snes *s);
void battle_select_by(Snes *s);
void battle_clear_candidates_entry(Snes *s);
void battle_mark_first_target_entry(Snes *s);
void battle_mark_other_targets_entry(Snes *s);

/* battle/enemy_scripts.c */
void battle_enemy_scripts_choose(Snes *s);
void battle_enemy_script_choose(Snes *s);
void battle_enemy_condition_held(Snes *s);
void battle_enemy_choose_actions(Snes *s);
void battle_enemy_find_reactions(Snes *s);
void battle_enemy_find_script_place(Snes *s);

/* battle/enemy_conditions.c */
void battle_condition_always(Snes *s);
bool battle_condition_always_ready(Snes *s);
void battle_condition_at_half_hp(Snes *s);
bool battle_condition_at_half_hp_ready(Snes *s);
void battle_condition_with_status(Snes *s);
bool battle_condition_with_status_ready(Snes *s);
void battle_condition_is_enemy(Snes *s);
bool battle_condition_is_enemy_ready(Snes *s);
void battle_condition_enemy_present(Snes *s);
bool battle_condition_enemy_present_ready(Snes *s);
void battle_condition_enemies_at_most(Snes *s);
bool battle_condition_enemies_at_most_ready(Snes *s);
void battle_condition_frames_at_least(Snes *s);
bool battle_condition_frames_at_least_ready(Snes *s);
void battle_condition_count_compared(Snes *s);
bool battle_condition_count_compared_ready(Snes *s);
void battle_condition_at_most_hp(Snes *s);
bool battle_condition_at_most_hp_ready(Snes *s);
void battle_condition_stat_at_least(Snes *s);
bool battle_condition_stat_at_least_ready(Snes *s);
void battle_condition_stat_bits(Snes *s);
bool battle_condition_stat_bits_ready(Snes *s);
void battle_condition_stat_at_most(Snes *s);
bool battle_condition_stat_at_most_ready(Snes *s);
void battle_condition_within_reach(Snes *s);
bool battle_condition_within_reach_ready(Snes *s);
void battle_condition_level_with(Snes *s);
bool battle_condition_level_with_ready(Snes *s);
void battle_condition_stands_beside(Snes *s);
bool battle_condition_stands_beside_ready(Snes *s);
void battle_condition_could_walk_to(Snes *s);
bool battle_condition_could_walk_to_ready(Snes *s);
void battle_condition_stands_in(Snes *s);
bool battle_condition_stands_in_ready(Snes *s);
void battle_condition_acted_on_by(Snes *s);
bool battle_condition_acted_on_by_ready(Snes *s);
void battle_condition_acted_on_by_this(Snes *s);
bool battle_condition_acted_on_by_this_ready(Snes *s);
void battle_condition_acted_bit(Snes *s);
bool battle_condition_acted_bit_ready(Snes *s);
void battle_condition_acted_on_by_member(Snes *s);
bool battle_condition_acted_on_by_member_ready(Snes *s);
void battle_condition_acted_with_elements(Snes *s);
bool battle_condition_acted_with_elements_ready(Snes *s);
void battle_condition_chance(Snes *s);
bool battle_condition_chance_ready(Snes *s);
void battle_condition_stat_is(Snes *s);
bool battle_condition_stat_is_ready(Snes *s);
void battle_condition_other_kind_present(Snes *s);
bool battle_condition_other_kind_present_ready(Snes *s);
void battle_condition_members_at_most(Snes *s);
bool battle_condition_members_at_most_ready(Snes *s);
void battle_condition_is_party_member(Snes *s);
bool battle_condition_is_party_member_ready(Snes *s);
void battle_condition_three_on_out(Snes *s);
bool battle_condition_three_on_out_ready(Snes *s);
void battle_condition_chooses_but_fails(Snes *s);
bool battle_condition_chooses_but_fails_ready(Snes *s);
void battle_condition_within_wide_reach(Snes *s);
bool battle_condition_within_wide_reach_ready(Snes *s);
void battle_condition_was_beaten(Snes *s);
bool battle_condition_was_beaten_ready(Snes *s);
void battle_condition_stands_apart(Snes *s);
bool battle_condition_stands_apart_ready(Snes *s);
void battle_condition_turn_wait(Snes *s);
bool battle_condition_turn_wait_ready(Snes *s);

/* battle/enemy_actions.c */
void battle_enemy_choose_pose(Snes *s);
bool battle_enemy_choose_pose_ready(Snes *s);
void battle_enemy_choose_aim(Snes *s);
bool battle_enemy_choose_aim_ready(Snes *s);
void battle_enemy_draw_one_of_four(Snes *s);
bool battle_enemy_draw_one_of_four_ready(Snes *s);
void battle_enemy_forgive_block(Snes *s);
bool battle_enemy_forgive_block_ready(Snes *s);
void battle_enemy_note_number(Snes *s);
bool battle_enemy_note_number_ready(Snes *s);
void battle_enemy_need_beaten(Snes *s);
bool battle_enemy_need_beaten_ready(Snes *s);
void battle_enemy_record_action(Snes *s);
void battle_enemy_note_actor_bits(Snes *s);
void battle_enemy_pose_kind(Snes *s);

/* battle/enemy_turn.c */
void battle_enemy_takes_turn(Snes *s);
void battle_enemy_tech_body(Snes *s);
bool battle_enemy_tech_body_ready(Snes *s);
void battle_start_action_showing(Snes *s);
/* battle/effect_work.c: an enemy's action's effect worked out, each kind of
 * effect as the party's code runs it, and the notes of the enemy's action. */
void battle_work_out_enemy_tech(Snes *s);
bool battle_work_out_enemy_tech_ready(Snes *s);
void battle_work_out_enemy_attack(Snes *s);
bool battle_work_out_enemy_attack_ready(Snes *s);
void battle_effect_restore(Snes *s);
bool battle_effect_restore_ready(Snes *s);
void battle_effect_cure(Snes *s);
bool battle_effect_cure_ready(Snes *s);
void battle_effect_inflict(Snes *s);
bool battle_effect_inflict_ready(Snes *s);
void battle_effect_hit(Snes *s);
bool battle_effect_hit_ready(Snes *s);
void battle_effect_drain(Snes *s);
bool battle_effect_drain_ready(Snes *s);
void battle_effect_restore_showing_hp(Snes *s);
bool battle_effect_restore_showing_hp_ready(Snes *s);
void battle_effect_hit_repeatedly(Snes *s);
bool battle_effect_hit_repeatedly_ready(Snes *s);
void battle_effect_item_restore(Snes *s);
bool battle_effect_item_restore_ready(Snes *s);
void battle_effect_item_cure(Snes *s);
bool battle_effect_item_cure_ready(Snes *s);
void battle_effect_item_status(Snes *s);
bool battle_effect_item_status_ready(Snes *s);
void battle_effect_lose_condition_bit(Snes *s);
bool battle_effect_lose_condition_bit_ready(Snes *s);
void battle_note_enemy_tech(Snes *s);
void battle_note_enemy_attack(Snes *s);
void battle_enemy_pose_turn(Snes *s);
bool battle_enemy_pose_turn_ready(Snes *s);
void battle_enemy_attack_turn(Snes *s);
bool battle_enemy_attack_turn_ready(Snes *s);
void battle_enemy_tech_turn(Snes *s);
bool battle_enemy_tech_turn_ready(Snes *s);
void battle_enemy_no_turn(Snes *s);
bool battle_enemy_no_turn_ready(Snes *s);
void battle_enemy_choose_again_turn(Snes *s);
bool battle_enemy_choose_again_turn_ready(Snes *s);
void battle_enemy_change_into_turn(Snes *s);
bool battle_enemy_change_into_turn_ready(Snes *s);
void battle_enemy_leave_turn(Snes *s);
bool battle_enemy_leave_turn_ready(Snes *s);
void battle_enemy_die_turn(Snes *s);
bool battle_enemy_die_turn_ready(Snes *s);
void battle_enemy_set_stat_turn(Snes *s);
bool battle_enemy_set_stat_turn_ready(Snes *s);
void battle_enemy_add_to_stat_turn(Snes *s);
bool battle_enemy_add_to_stat_turn_ready(Snes *s);
void battle_enemy_flags_turn(Snes *s);
bool battle_enemy_flags_turn_ready(Snes *s);
void battle_enemy_message_turn(Snes *s);
bool battle_enemy_message_turn_ready(Snes *s);
void battle_enemy_bring_back_turn(Snes *s);
bool battle_enemy_bring_back_turn_ready(Snes *s);
void battle_enemy_set_stats_turn(Snes *s);
bool battle_enemy_set_stats_turn_ready(Snes *s);
void battle_enemy_set_stats_then_tech_turn(Snes *s);
bool battle_enemy_set_stats_then_tech_turn_ready(Snes *s);
void battle_enemy_add_to_turn_wait_turn(Snes *s);
bool battle_enemy_add_to_turn_wait_turn_ready(Snes *s);
void battle_enemy_add_to_stats_turn(Snes *s);
bool battle_enemy_add_to_stats_turn_ready(Snes *s);
void battle_enemy_add_to_stats_then_tech_turn(Snes *s);
bool battle_enemy_add_to_stats_then_tech_turn_ready(Snes *s);
void battle_enemy_call_in_turn(Snes *s);
bool battle_enemy_call_in_turn_ready(Snes *s);

/* battle/main_loop.c */
void battle_finish_beaten_enemies(Snes *s);

/* their guards: each entry above that carries out a script's command has
 * its own, which knows the command (name_ready) */
typedef enum {
    TAKES_NOTHING,       /* nothing of the script at the place */
    TAKES_CONDITION,     /* a condition the entry carries out, and its block */
    TAKES_HELD,          /* the actions after the condition that held */
    TAKES_ACTION,        /* an action the entry chooses or carries out */
    TAKES_SELECTOR,      /* the selector after the place */
    TAKES_SELECTOR_IN_A, /* the selector in A */
    TAKES_ACTIONS,       /* the actions the choosing is at */
} ScriptUse;
bool battle_script_entry_ready(Snes *s, ScriptUse use, uint8_t command);
bool battle_enemy_script_ready(Snes *s);
bool battle_enemy_first_block_ready(Snes *s);
bool battle_enemy_held_ready(Snes *s);
bool battle_script_selector_ready(Snes *s);
bool battle_selector_in_a_ready(Snes *s);
uint8_t battle_selector_in_a(const Snes *s);
bool battle_enemy_actions_ready(Snes *s);
bool battle_enemy_turn_ready(Snes *s);
bool battle_candidates_ready(Snes *s);
bool battle_enemy_choosing_ready(Snes *s);
bool battle_enemies_choose_ready(Snes *s);
bool battle_search_ready(Snes *s);
bool battle_reach_test_ready(Snes *s);
bool battle_combatant_in_work_ready(Snes *s);

/* battle/wait.c */
bool battle_frame_wait_is_idle(Snes *s);
void battle_wait_for_frame_loop(Snes *s);
void battle_wait_vblank_end_loop(Snes *s);
void battle_wait_hblank_loop(Snes *s);
void battle_wait_line_loop(Snes *s);
void battle_wait_for_nmi_loop(Snes *s);
void battle_wait_window_slide_loop(Snes *s);
void battle_irq_wait_hblank_loop(Snes *s);
void battle_wait_graphics_loaded_loop(Snes *s);

/* battle/battlefield.c, menu_window.c, damage.c, reset.c */
void battle_mark_object_grid(Snes *s);
void battle_reset_window_frames(Snes *s);
void battle_clear_damage(Snes *s);
void battle_clear_numbers(Snes *s);
void battle_reset_object_state(Snes *s);

/* battle/display.c */
void battle_setup_display(Snes *s);
void battle_setup_window_hdma(Snes *s);
void battle_load_graphics(Snes *s);
void battle_nmi_alternate(Snes *s);

/* battle/scroll.c */
void battle_update_scroll_dispatch(Snes *s);
void battle_update_scroll_tables(Snes *s);
void battle_update_scroll_tables_bg3_moving(Snes *s);
void battle_update_screen_shake(Snes *s);

/* battle/uploads.c */
void battle_upload_palette(Snes *s);
void battle_vram_transfer(Snes *s);
void battle_flush_vram_transfer(Snes *s);
void battle_upload_alternating_vram(Snes *s);
void battle_upload_tile_rows(Snes *s);
void battle_upload_vram(Snes *s);
void battle_request_window_rows(Snes *s);
void battle_request_saved_window_rows(Snes *s);
void battle_request_lower_window_rows(Snes *s);

/* battle/stats.c */
void battle_update_low_hp_flag(Snes *s);
void battle_update_status_flags(Snes *s);
void battle_update_all_combatants(Snes *s);
void battle_update_combatant(Snes *s);

/* battle/tiles.c */
void battle_decode_tile_row(Snes *s);
bool battle_decodes_tile_row(Snes *s);

/* battle/animation.c */
void battle_step_combatant_animations(Snes *s);
bool battle_steps_animations(Snes *s);
void battle_choose_status_pose(Snes *s);
bool battle_poses_combatant(Snes *s);
void battle_step_enemy_effects(Snes *s);

/* battle/timers.c */
void battle_tick_timers(Snes *s);
void battle_dispatch_expired_timers(Snes *s);
/* battle/status_timers.c: the timed statuses' handlers, $C1:8876-$C1:8C08. */
void battle_stop_timer_ran_out(Snes *s);
bool battle_stop_timer_ran_out_ready(Snes *s);
void battle_poison_timer_ran_out(Snes *s);
bool battle_poison_timer_ran_out_ready(Snes *s);
void battle_slow_timer_ran_out(Snes *s);
bool battle_slow_timer_ran_out_ready(Snes *s);
void battle_haste_timer_ran_out(Snes *s);
bool battle_haste_timer_ran_out_ready(Snes *s);
void battle_evasion_timer_ran_out(Snes *s);
bool battle_evasion_timer_ran_out_ready(Snes *s);
void battle_shield_timer_ran_out(Snes *s);
bool battle_shield_timer_ran_out_ready(Snes *s);
void battle_timer_6_ran_out(Snes *s);
bool battle_timer_6_ran_out_ready(Snes *s);
void battle_timer_7_ran_out(Snes *s);
bool battle_timer_7_ran_out_ready(Snes *s);
void battle_hp_loss_timer_ran_out(Snes *s);
bool battle_hp_loss_timer_ran_out_ready(Snes *s);
void battle_mp_regain_timer_ran_out(Snes *s);
bool battle_mp_regain_timer_ran_out_ready(Snes *s);
void battle_barrier_timer_ran_out(Snes *s);
bool battle_barrier_timer_ran_out_ready(Snes *s);
void battle_timer_11_ran_out(Snes *s);
bool battle_timer_11_ran_out_ready(Snes *s);

/* battle/effect_animation.c */
void battle_animate_effects(Snes *s);

/* battle/effect_paths.c */
void battle_step_effect_slots(Snes *s);
void battle_step_orbits(Snes *s);
void battle_place_formation(Snes *s);
void battle_points_are_close(Snes *s);
void battle_sine_scale(Snes *s);
void battle_multiply_scale(Snes *s);
void battle_chase_angle(Snes *s);
void battle_effect_combatant_line(Snes *s);
void battle_effect_combatant_jump(Snes *s);
void battle_effect_combatant_spiral(Snes *s);
void battle_effect_combatant_orbit(Snes *s);
void battle_effect_combatant_jump_to_target(Snes *s);
void battle_effect_combatant_orbit_flat(Snes *s);
void battle_effect_flicker(Snes *s);
void battle_effect_flicker_step(Snes *s);
void battle_effect_combatant_chase(Snes *s);
void battle_effect_anchor_line(Snes *s);
void battle_effect_anchor_jump(Snes *s);
void battle_effect_anchor_spiral(Snes *s);
void battle_effect_anchor_orbit(Snes *s);
void battle_effect_anchor_jump_to_target(Snes *s);
void battle_effect_anchor_orbit_facing(Snes *s);
void battle_effect_anchor_chase(Snes *s);

/* battle/objects.c */
void battle_update_object_bounds(Snes *s);
void battle_update_all_object_bounds(Snes *s);
bool battle_names_bounds_object(Snes *s);
void battle_find_due_animation(Snes *s);
bool battle_scheduler_in_place(Snes *s);
void battle_update_animation_positions(Snes *s);

/* battle/frame.c */
void battle_frame_dispatch(Snes *s);
void battle_frame_work(Snes *s);
void battle_step_window_slide(Snes *s);
void battle_step_gradient_phase(Snes *s);
void battle_clear_oam_buffer(Snes *s);
void battle_frame_update(Snes *s);
void battle_frame_wait_tail(Snes *s);
void battle_scroll_map_layers(Snes *s);

/* battle/nmi.c */
void battle_nmi(Snes *s);
void battle_restore_math_operands(Snes *s);
void battle_apply_alternate_frame(Snes *s);
void battle_irq(Snes *s);
void battle_irq_after_hblank(Snes *s);

/* battle/scripts.c */
void battle_run_script_channel(Snes *s);
void battle_run_scripts(Snes *s);
bool battle_script_channel_in_y(Snes *s);

/* battle/draw_objects.c */
void battle_draw_objects(Snes *s);
void battle_draw_shadow(Snes *s);
void battle_draw_afterimages(Snes *s);
void battle_draw_combatant(Snes *s);
void battle_draw_afterimage(Snes *s);
void battle_copy_frame_tiles(Snes *s);
void battle_draw_in_front(Snes *s);
void battle_draw_marker(Snes *s);
void battle_draw_numbers(Snes *s);
void battle_draw_behind(Snes *s);
void battle_draw_effect(Snes *s);
bool battle_draw_pass_ready(Snes *s);
bool battle_drawing_combatant(Snes *s);
bool battle_drawing_combatant_frame(Snes *s);
bool battle_drawing_combatant_in_x(Snes *s);
bool battle_drawing_combatant_x_and_cell(Snes *s);
bool battle_drawing_effect_record(Snes *s);
bool battle_tile_copy_in_buffers(Snes *s);

/* battle/object_motion.c */
void battle_raise_numbers(Snes *s);
void battle_step_scripted_moves(Snes *s);
void battle_update_anchors(Snes *s);
void battle_record_trails(Snes *s);
void battle_cycle_party_colors(Snes *s);

/* battle/sprites.c */
void battle_build_object_graphics(Snes *s);
void battle_build_object_graphics_checked(Snes *s);
bool battle_builds_known_object(Snes *s);
void battle_step_object_palettes(Snes *s);
void battle_index_effect_animations(Snes *s);
void battle_build_bitmap_shapes(Snes *s);

/* battle/window.c, battle/status.c, battle/digits.c, battle/menu_window.c,
 * battle/numbers.c: the windows and the numbers, and the guard they run under */
bool battle_window_ready(Snes *s);
bool battle_window_member_ready(Snes *s);
bool battle_window_ready_a_clear(Snes *s);
bool battle_menu_window_ready(Snes *s);
void battle_draw_window(Snes *s);
void battle_draw_gauge(Snes *s);
void battle_draw_party_status(Snes *s);
void battle_number_to_digits(Snes *s);
void battle_number_to_2_digits(Snes *s);
void battle_blank_leading_zeros(Snes *s);
void battle_draw_menu_window(Snes *s);
void battle_place_menu_cursors(Snes *s);
void battle_update_combo_members(Snes *s);
void battle_script_build_number_sprites(Snes *s);

/* battle/arith.c, battle/buttons.c */
void battle_multiply_8x8(Snes *s);
void battle_multiply_8x16(Snes *s);
void battle_divide(Snes *s);
void battle_update_button_repeat(Snes *s);
void battle_multiply_16x16(Snes *s);
bool battle_divide_is_exact(Snes *s);

#endif
