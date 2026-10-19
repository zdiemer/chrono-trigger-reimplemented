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
/* ... and the area search (battle/area_search.c), for the area rules' shape
 * it searched, the side it searched, and the combatants it was placed on and
 * reached toward. */
bool battle_area_search_carry(uint8_t shape, bool among_party, uint8_t centre, uint8_t toward);
bool battle_other_marks_carry(const GameState *g, bool carry);
bool battle_bounds_carry(const GameState *g, unsigned slot);
/* battle/area_search.c: the noted area searched, and the battle's step 7
 * that does it. */
void battle_search_the_noted_area(Snes *s);
void battle_find_the_noted_area(Snes *s);

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
void battle_effect_steal(Snes *s);
bool battle_effect_steal_ready(Snes *s);
/* The carry a kind's work hands back: set after the note of what acted on the
 * target, clear after the restoring that shows the actor's HP, the losing of
 * a condition's bit and the kinds that do nothing (whose jump shifts the
 * kind's number, clearing it). */
bool battle_kind_leaves_carry(uint8_t kind);
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
/* battle/aim.c: whom a tech or an item aims at, and its call to the area
 * search. */
void battle_aim_action(Snes *s);
bool battle_aim_action_ready(Snes *s);
void battle_search_area(Snes *s);
/* battle/member_turn.c: a party member's chosen action carried out, its
 * attack, the end of its turn, and the battle's step for a called-off
 * action. */
void battle_carry_out_member_action(Snes *s);
bool battle_carry_out_member_action_ready(Snes *s);
void battle_member_attacks(Snes *s);
void battle_member_attacks_from_bank_fd(Snes *s);
bool battle_member_attacks_ready(Snes *s);
void battle_end_member_turn(Snes *s);
bool battle_end_member_turn_ready(Snes *s);
void battle_member_called_off_step(Snes *s);
void battle_members_wait_after_tech(Snes *s);
bool battle_members_wait_after_tech_ready(Snes *s);
/* battle/party_work.c: a party member's tech, or its attack, worked out, and
 * the item it uses. battle/inventory.c: an item added to the inventory. */
void battle_work_out_member_tech(Snes *s);
bool battle_work_out_member_tech_ready(Snes *s);
void battle_use_item(Snes *s);
bool battle_use_item_ready(Snes *s);
void battle_add_to_inventory(Snes *s);
bool battle_add_to_inventory_ready(Snes *s);
void battle_inventory_holds_entry(Snes *s);
bool battle_inventory_holds_ready(Snes *s);
void battle_inventory_take_entry(Snes *s);
bool battle_inventory_take_ready(Snes *s);
void battle_gold_covers_entry(Snes *s);
void battle_give_gold_entry(Snes *s);
void battle_take_gold_entry(Snes *s);
void battle_party_request(Snes *s);
bool battle_party_request_ready(Snes *s);
void battle_build_item_list(Snes *s);
void battle_list_item(Snes *s);
bool battle_list_item_ready(Snes *s);
void battle_pass_empty_place(Snes *s);
/* battle/member_stats.c */
void battle_set_up_member_stats(Snes *s);
bool battle_set_up_member_stats_ready(Snes *s);
void battle_apply_stat_boost(Snes *s);
bool battle_apply_stat_boost_ready(Snes *s);
void battle_add_affinities(Snes *s);
bool battle_add_affinities_ready(Snes *s);
void battle_item_record_place(Snes *s);
bool battle_item_record_place_ready(Snes *s);
void battle_copy_equipment_records(Snes *s);
bool battle_copy_equipment_records_ready(Snes *s);
/* battle/tech_records.c */
void battle_set_up_triple_tech(Snes *s);
bool battle_set_up_triple_tech_ready(Snes *s);
void battle_set_double_tech_cost(Snes *s);
bool battle_set_double_tech_cost_ready(Snes *s);
void battle_set_triple_tech_cost(Snes *s);
bool battle_set_triple_tech_cost_ready(Snes *s);
bool battle_set_up_member_techs_ready(Snes *s);
void battle_set_up_single_techs_entry(Snes *s);
void battle_set_up_double_techs_entry(Snes *s);
void battle_blank_tech_records_entry(Snes *s);
/* battle/fight_entries.c */
bool battle_start_fight_ready(Snes *s);
void battle_start_fight_entry(Snes *s);
void fight_load_demo_party_entry(Snes *s);
void fight_start_demo_characters_entry(Snes *s);
bool fight_list_combatants_ready(Snes *s);
void fight_list_combatants_entry(Snes *s);
bool fight_load_enemy_ready(Snes *s);
void fight_load_enemy_entry(Snes *s);
bool fight_clear_enemy_block_ready(Snes *s);
void fight_clear_enemy_block_entry(Snes *s);
void fight_note_enemy_families_entry(Snes *s);
bool fight_member_in_y_ready(Snes *s);
void fight_take_member_record_entry(Snes *s);
bool fight_aylas_fists_ready(Snes *s);
void fight_aylas_fists_entry(Snes *s);
bool fight_set_up_affinities_ready(Snes *s);
void fight_set_up_affinities_entry(Snes *s);
void fight_note_accessories_entry(Snes *s);
void fight_note_weapons_entry(Snes *s);
void fight_clear_timers_entry(Snes *s);
void fight_set_timer_lengths_entry(Snes *s);
bool fight_set_first_turns_ready(Snes *s);
void fight_set_first_turns_entry(Snes *s);
void fight_apply_accessory_effects_entry(Snes *s);
bool fight_raise_critical_rate_ready(Snes *s);
void fight_raise_critical_rate_entry(Snes *s);
void fight_clear_turn_state_entry(Snes *s);
void fight_apply_member_bonuses_entry(Snes *s);
void fight_number_places_entry(Snes *s);
void fight_ward_off_confusion_entry(Snes *s);

/* battle/scripts.c: the script commands whose operations are C, each at its
 * handler's address */
bool script_end_ready(Snes *s);
bool script_halt_ready(Snes *s);
bool script_call_ready(Snes *s);
bool script_return_from_call_ready(Snes *s);
bool script_wait_frames_ready(Snes *s);
bool script_return_at_the_top_ready(Snes *s);
bool script_wait_for_variable_ready(Snes *s);
bool script_wait_for_mark_a_ready(Snes *s);
bool script_wait_for_mark_b_ready(Snes *s);
bool script_end_unless_listed_ready(Snes *s);
bool script_set_variable_ready(Snes *s);
bool script_set_mark_a_ready(Snes *s);
bool script_set_mark_b_ready(Snes *s);
bool script_count_variable_up_ready(Snes *s);
bool script_count_mark_a_up_ready(Snes *s);
bool script_count_mark_b_up_ready(Snes *s);
bool script_count_variable_down_ready(Snes *s);
bool script_count_mark_a_down_ready(Snes *s);
bool script_count_mark_b_down_ready(Snes *s);
bool script_add_to_variable_ready(Snes *s);
bool script_add_to_mark_a_ready(Snes *s);
bool script_add_variable_to_mark_a_ready(Snes *s);
bool script_copy_variable_ready(Snes *s);
bool script_store_mark_a_ready(Snes *s);
bool script_add_held_ready(Snes *s);
bool script_swap_with_mark_a_ready(Snes *s);
bool script_swap_with_mark_a_point_x_ready(Snes *s);
bool script_swap_with_mark_a_point_y_ready(Snes *s);
bool script_skip_ready(Snes *s);
bool script_play_animation_ready(Snes *s);
bool script_play_and_wait_shown_ready(Snes *s);
bool script_play_and_wait_ready(Snes *s);
bool script_own_speed_ready(Snes *s);
bool script_set_speed_ready(Snes *s);
bool script_play_and_wait_marked_ready(Snes *s);
bool script_drawn_on_top_ready(Snes *s);
bool script_drawn_on_top_off_ready(Snes *s);
bool script_hidden_ready(Snes *s);
bool script_hidden_off_ready(Snes *s);
bool script_sprite_back_in_place_ready(Snes *s);
bool script_bring_back_ready(Snes *s);
bool script_withdrawn_off_ready(Snes *s);
bool script_withdrawn_ready(Snes *s);
bool script_trail_ready(Snes *s);
bool script_trail_off_ready(Snes *s);
bool script_glide_sprite_ready(Snes *s);
bool script_glide_sprite_home_ready(Snes *s);
bool script_shadow_ready(Snes *s);
bool script_shadow_off_ready(Snes *s);
bool script_attribute_bits_ready(Snes *s);
bool script_attribute_bits_back_ready(Snes *s);
bool script_anchor_follows_ready(Snes *s);
bool script_anchor_stays_ready(Snes *s);
bool script_palettes_paused_ready(Snes *s);
bool script_palettes_paused_off_ready(Snes *s);
bool script_palette_animation_ready(Snes *s);
bool script_palette_animation_off_ready(Snes *s);
bool script_color_cycles_off_ready(Snes *s);
bool script_all_effects_shown_ready(Snes *s);
bool script_all_effects_shown_off_ready(Snes *s);
bool script_effect_shown_ready(Snes *s);
bool script_effect_shown_off_ready(Snes *s);
bool script_effect_drawn_with_ready(Snes *s);
bool script_effect_drawn_with_behind_ready(Snes *s);
bool script_palette_frame_ready(Snes *s);
bool script_flash_with_ready(Snes *s);
bool script_flash_off_ready(Snes *s);
bool script_rotate_colors_ready(Snes *s);
bool script_blink_ready(Snes *s);
/* battle/script_walk.c */
void battle_find_the_way(Snes *s);
bool script_line_to_coordinates_ready(Snes *s);
bool script_line_to_the_point_ready(Snes *s);
bool script_line_to_a_place_ready(Snes *s);
bool script_chase_to_coordinates_ready(Snes *s);
bool script_chase_to_the_point_ready(Snes *s);
bool script_chase_to_a_place_ready(Snes *s);
bool script_walk_to_coordinates_ready(Snes *s);
bool script_walk_to_the_point_ready(Snes *s);
bool script_walk_to_a_place_ready(Snes *s);
bool script_put_at_coordinates_ready(Snes *s);
bool script_put_at_the_point_ready(Snes *s);
bool script_put_at_a_place_ready(Snes *s);
bool script_point_at_place_ready(Snes *s);
bool script_slot_at_place_ready(Snes *s);
bool script_point_off_place_ready(Snes *s);
bool script_point_at_the_mover_ready(Snes *s);
bool script_show_numbers_ready(Snes *s);
bool script_face_ready(Snes *s);
bool script_take_heading_ready(Snes *s);
bool script_heading_between_ready(Snes *s);
bool script_turn_heading_ready(Snes *s);
struct ScriptCommand;
bool script_play_sound_bridge(Snes *s, const struct ScriptCommand *command);
bool script_play_sound_at_bridge(Snes *s, const struct ScriptCommand *command);
bool script_weapon_sound_bridge(Snes *s, const struct ScriptCommand *command);
bool script_play_sound_ready(Snes *s);
bool script_play_sound_at_ready(Snes *s);
bool script_weapon_swing_sound_ready(Snes *s);
bool script_weapon_hit_sound_ready(Snes *s);
bool script_screen_effect_ready(Snes *s);
bool script_slot_follows_place_ready(Snes *s);
bool script_set_heading_ready(Snes *s);
bool script_jump_to_a_place_ready(Snes *s);
bool script_jump_to_the_point_ready(Snes *s);
bool script_jump_down_to_a_place_ready(Snes *s);
bool script_jump_down_to_the_point_ready(Snes *s);
bool script_spiral_out_ready(Snes *s);
bool script_spiral_for_ready(Snes *s);
bool script_spiral_for_stopping_ready(Snes *s);
bool script_orbit_a_place_ready(Snes *s);
bool script_orbit_the_point_ready(Snes *s);
bool script_orbit_flat_a_place_ready(Snes *s);
bool script_orbit_flat_the_point_ready(Snes *s);
bool script_aim_throw_ready(Snes *s);
bool script_throw_rise_ready(Snes *s);
bool script_throw_waits_before_ready(Snes *s);
bool script_throw_waits_after_ready(Snes *s);
bool script_throw_moves_ready(Snes *s);
bool script_point_along_ready(Snes *s);
bool script_flicker_ready(Snes *s);
void battle_script_command_entry(Snes *s);

/* battle/rewards_entries.c */
void battle_turn_experience_to_gold_entry(Snes *s);
bool battle_share_experience_ready(Snes *s);
void battle_share_experience_entry(Snes *s);
bool rewards_gain_experience_ready(Snes *s);
void rewards_gain_experience_entry(Snes *s);
bool rewards_level_up_ready(Snes *s);
void rewards_level_up_entry(Snes *s);
bool rewards_reach_level_ready(Snes *s);
void rewards_reach_level_entry(Snes *s);
bool rewards_grow_hp_and_mp_ready(Snes *s);
void rewards_grow_hp_and_mp_entry(Snes *s);
bool rewards_share_with_reserve_ready(Snes *s);
void rewards_share_with_reserve_entry(Snes *s);
bool rewards_learn_techs_ready(Snes *s);
void rewards_learn_techs_entry(Snes *s);
void rewards_sort_combination_members_entry(Snes *s);
bool rewards_count_tech_points_ready(Snes *s);
void rewards_count_tech_points_entry(Snes *s);
bool rewards_reach_next_tech_ready(Snes *s);
void rewards_reach_next_tech_entry(Snes *s);
bool rewards_learn_single_tech_ready(Snes *s);
void rewards_learn_single_tech_entry(Snes *s);
bool rewards_learn_combination_techs_ready(Snes *s);
void rewards_learn_combination_techs_entry(Snes *s);
void battle_put_items_back_entry(Snes *s);
bool battle_return_chosen_item_ready(Snes *s);
void battle_return_chosen_item_entry(Snes *s);
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

/* battle/wait.c: battle/wait.h; battle/message.c: battle/message.h */

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
void battle_load_line_arrangement(Snes *s);
void battle_step_ripple(Snes *s);
void battle_nmi_alternate(Snes *s);
void battle_slide_split_in(Snes *s);
void battle_slide_split_out(Snes *s);

/* battle/frame.c */
void battle_keep_animations_off_the_cycle_entry(Snes *s);

/* battle/scroll.c */
void battle_update_scroll_dispatch(Snes *s);
void battle_update_scroll_tables(Snes *s);
void battle_update_scroll_tables_bg3_moving(Snes *s);
void battle_update_scroll_tables_line_wave(Snes *s);
void battle_update_scroll_tables_finer_line_wave(Snes *s);
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

/* battle/battlefield_change.c */
void battle_change_battlefield(Snes *s);
void battle_load_battlefield(Snes *s);
void battle_fade_old_battlefield_layer(Snes *s);
void battle_decode_battlefield_tile_set(Snes *s);
void battle_clear_battlefield_map(Snes *s);
void battle_build_battlefield_map(Snes *s);
void battle_load_effect_tile_rows(Snes *s);
void battle_load_second_effect_tile_rows(Snes *s);
bool battle_changed_battlefield_fits(Snes *s);
bool battle_battlefield_fits(Snes *s);
bool battle_battlefield_map_fits(Snes *s);

/* battle/beaten_enemies.c */
void battle_see_to_the_beaten_enemies(Snes *s);
void battle_see_off_the_beaten_enemies(Snes *s);
void battle_see_off_an_enemy(Snes *s);
void battle_dissolve_an_enemy(Snes *s);
void battle_dissolve_a_step(Snes *s);
void battle_change_battlefield_as_an_enemy_leaves(Snes *s);
bool battle_seeing_off_the_beaten(Snes *s);
bool battle_seeing_off(Snes *s);
bool battle_dissolving(Snes *s);

/* battle/battlefield_restore.c */
void battle_put_the_battlefield_back(Snes *s);
void battle_take_the_start_settings_long(Snes *s);
void battle_take_back_the_start_screen(Snes *s);
void battle_fade_battlefield_layer_back_in(Snes *s);
void battle_has_a_layer_of_its_own(Snes *s);
void battle_restore_kept_colors_entry(Snes *s);

/* battle/closing.c */
void battle_close_the_display(Snes *s);
void battle_open_the_window(Snes *s);
void battle_close_the_window(Snes *s);
void battle_fade_the_music_out(Snes *s);
void battle_play_out_the_boss_end(Snes *s);
void battle_let_the_screen_settle(Snes *s);
void battle_change_to_the_battlefield(Snes *s);

/* battle/animation.c */
void battle_step_combatant_animations(Snes *s);
bool battle_steps_animations(Snes *s);
void battle_choose_status_pose(Snes *s);
bool battle_poses_combatant(Snes *s);
void battle_step_enemy_effects(Snes *s);

/* battle/rewards_messages.c */
void battle_show_the_rewards(Snes *s);
bool battle_rewards_can_be_shown(Snes *s);

/* battle/battle_end.c: the battle's end's bookkeeping */
void battle_clear_party_statuses_entry(Snes *s);
void battle_stand_party_up_entry(Snes *s);
void battle_keep_party_records_entry(Snes *s);
void battle_take_enemies_away_entry(Snes *s);
void battle_keep_standing_enemies_entry(Snes *s);
void battle_count_enemies_out_entry(Snes *s);
void battle_note_beaten_enemies_entry(Snes *s);

/* battle/party_turns.c */
void battle_member_status_turn(Snes *s);
bool battle_member_status_turn_ready(Snes *s);
void battle_cancel_if_unable(Snes *s);
bool battle_cancel_if_unable_ready(Snes *s);
void battle_party_counters(Snes *s);
bool battle_party_counters_ready(Snes *s);
void battle_call_off_waiting_combination(Snes *s);
void battle_call_off_waiting_member(Snes *s);
bool battle_call_off_waiting_member_ready(Snes *s);
void battle_call_off_combination_entry(Snes *s);
void battle_see_to_knocked_out_enemies(Snes *s);
bool battle_see_to_knocked_out_enemies_ready(Snes *s);
void battle_demo_party_ready_entry(Snes *s);
void battle_demo_next_turn_entry(Snes *s);
void battle_party_stands_down(Snes *s);

/* battle/enemy_movement.c: each movement's routine, $C1:3772-$C1:4021 */
void battle_enemy_waits_for_target(Snes *s);
bool battle_enemy_waits_for_target_ready(Snes *s);
void battle_enemy_approaches(Snes *s);
bool battle_enemy_approaches_ready(Snes *s);
void battle_enemy_charges(Snes *s);
bool battle_enemy_charges_ready(Snes *s);
void battle_enemy_lines_up(Snes *s);
bool battle_enemy_lines_up_ready(Snes *s);
void battle_enemy_keeps_distance(Snes *s);
bool battle_enemy_keeps_distance_ready(Snes *s);
void battle_enemy_circles(Snes *s);
bool battle_enemy_circles_ready(Snes *s);
void battle_enemy_weaves(Snes *s);
bool battle_enemy_weaves_ready(Snes *s);
void battle_enemy_marches(Snes *s);
bool battle_enemy_marches_ready(Snes *s);
void battle_enemy_makes_for_the_middle(Snes *s);
bool battle_enemy_makes_for_the_middle_ready(Snes *s);

/* battle/timers.c */
void battle_tick_timers(Snes *s);
void battle_dispatch_expired_timers(Snes *s);
/* battle/status_timers.c: the timed statuses' handlers, $C1:8876-$C1:8C08,
 * and the showing of a status's number, $C1:895B. */
void battle_show_status_number(Snes *s);
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

/* battle/from_field.c */
void battle_start_from_field(Snes *s);
void battle_return_to_field(Snes *s);
bool battle_return_ready(Snes *s);

/* battle/opening.c */
void battle_party_faces_the_enemies(Snes *s);
bool battle_opening_ready(Snes *s);

/* battle/effect_animation.c */
void battle_animate_effects(Snes *s);
void battle_take_effect_frame(Snes *s);
bool battle_names_stepping_effect(Snes *s);

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
bool battle_menu_window_ready_a_clear(Snes *s);
void battle_draw_window(Snes *s);
void battle_draw_gauge(Snes *s);
void battle_draw_party_status(Snes *s);
void battle_number_to_digits(Snes *s);
void battle_number_to_2_digits(Snes *s);
void battle_blank_leading_zeros(Snes *s);
void battle_draw_menu_window(Snes *s);
void battle_place_menu_cursors(Snes *s);
void battle_update_combo_members(Snes *s);

/* battle/menu_input.c, menu_lists.c, name_tiles.c, target_choice.c and
 * menu_bridge.c: the player choosing (battle/choosing.h). */
void battle_take_menu_input(Snes *s);
bool battle_take_menu_input_ready(Snes *s);
void battle_frame_member_choosing_entry(Snes *s);
void battle_note_menu_stat_block_entry(Snes *s);
bool battle_note_menu_stat_block_ready(Snes *s);
void battle_lay_out_tech_list_entry(Snes *s);
bool battle_tech_list_ready(Snes *s);
void battle_mark_tech_rows_entry(Snes *s);
bool battle_tech_rows_ready(Snes *s);
void battle_lay_out_item_list_entry(Snes *s);
bool battle_item_list_ready(Snes *s);
void battle_convert_name(Snes *s);
void battle_choose_targets_entry(Snes *s);
bool battle_choose_targets_ready(Snes *s);

/* battle/action_showing.c, battle/action_animation.c, battle/frame_step.c:
 * an action shown, its animation loaded, and the battle's own frame */
void battle_show_action(Snes *s);
bool battle_show_action_ready(Snes *s);
void battle_forget_poses_entry(Snes *s);
void battle_list_the_showing_entry(Snes *s);
void battle_find_the_members_falling_entry(Snes *s);
void battle_load_action_animation(Snes *s);
void battle_load_attack_animation(Snes *s);
void battle_load_tech_animation(Snes *s);
void battle_load_item_animation(Snes *s);
void battle_attack_up_close_entry(Snes *s);
bool battle_animation_loading_ready(Snes *s);
bool battle_attack_animation_ready(Snes *s);
bool battle_attack_up_close_ready(Snes *s);
void battle_frame_step(Snes *s);
void battle_wait_step(Snes *s);
/* $C1:0003: a battle step by its number (battle/steps.c), and its guard. */
void battle_run_a_step(Snes *s);
bool battle_step_is_known(Snes *s);
void battle_next_member_takes_the_menu_entry(Snes *s);
bool battle_menu_queue_ready(Snes *s);
bool battle_frame_step_ready(Snes *s);

/* battle/pose_looks.c: what a pose shows, and the poses' round */
void battle_go_round_the_poses_entry(Snes *s);
bool battle_poses_round_ready(Snes *s);
void battle_show_pose_look_entry(Snes *s);
bool battle_pose_look_ready(Snes *s);

/* battle/damage.c: the hits landing, and a felled enemy's spoils */
void battle_apply_damage_entry(Snes *s);
void battle_drop_missed_targets_entry(Snes *s);
void battle_take_the_spoils(Snes *s);
bool battle_spoils_ready(Snes *s);

/* battle/arith.c, battle/buttons.c */
void battle_multiply_8x8(Snes *s);
void battle_multiply_8x16(Snes *s);
void battle_divide(Snes *s);
void battle_update_button_repeat(Snes *s);
void battle_multiply_16x16(Snes *s);
bool battle_divide_is_exact(Snes *s);

#endif
