/* Battle mode: ordinary operations and checked adapters for the legacy driver. */
#ifndef CT_GAME_BATTLE_ENTRIES_H
#define CT_GAME_BATTLE_ENTRIES_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
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
/* battle/positions.c */
void battle_search_for_target(Snes *s);
/* battle/targets.c */
/* battle/enemy_scripts.c */
/* battle/enemy_conditions.c */
/* battle/enemy_actions.c */
/* battle/enemy_turn.c */
void battle_enemy_takes_turn_entry(Snes *s);
/* An enemy's turn ($C1:8CF9), as C. */
bool battle_enemy_takes_turn(GameState *g, Platform *pf);
/* Its guard's state half: the enemy acting one of the six, no member coming
 * back, its actions in the table; and the action shown ($C1:AC57: the
 * battle's step 4 and its hits landing), as C. */
bool battle_enemy_turn_fits(const GameState *g);
bool battle_action_shown(GameState *g, Platform *pf);
void battle_enemy_tech_body(Snes *s);
bool battle_enemy_tech_body_ready(Snes *s);
void battle_start_action_showing(Snes *s);
/* battle/effect_work.c: an enemy's action's effect worked out, each kind of
 * effect as the party's code runs it, and the notes of the enemy's action. */
void battle_work_out_enemy_tech(Snes *s);
bool battle_work_out_enemy_tech_ready(Snes *s);
void battle_work_out_enemy_attack(Snes *s);
/* An enemy's tech's or attack's effect worked out on its targets ($C1:D7C4,
 * $C1:D8D1), as C, and their guards' state half: combatants named, and an
 * effect the port works out. */
bool battle_enemy_action_worked_out(const GameState *g, bool tech);
void battle_work_out_enemy_action(GameState *g, Platform *pf, bool tech);
bool battle_work_out_enemy_attack_ready(Snes *s);
/* The carry a kind's work hands back: set after the note of what acted on the
 * target, clear after the restoring that shows the actor's HP, the losing of
 * a condition's bit and the kinds that do nothing (whose jump shifts the
 * kind's number, clearing it). */
bool battle_kind_leaves_carry(uint8_t kind);
/* battle/aim.c: whom a tech or an item aims at, and its call to the area
 * search. */
void battle_aim_action_entry(Snes *s);
/* The action aimed ($C1:C1DD), from the carry its caller leaves, as C. */
bool battle_aim_action(GameState *g, Platform *pf, bool carry);
bool battle_aim_action_ready(Snes *s);
/* battle/member_turn.c: a party member's chosen action carried out, its
 * attack, the end of its turn, and the battle's step for a called-off
 * action. */
void battle_carry_out_member_action_entry(Snes *s);
/* A member's chosen action carried out ($C1:B967), as C. */
bool battle_carry_out_member_action(GameState *g, Platform *pf);
bool battle_carry_out_member_action_ready(Snes *s);
void battle_member_attacks_entry(Snes *s);
void battle_member_attacks_from_bank_fd(Snes *s);
/* A member's attack on the action's first target ($C1:BFAA), as C. */
bool battle_member_attacks(GameState *g, Platform *pf);
bool battle_member_attacks_ready(Snes *s);
void battle_end_member_turn_entry(Snes *s);
/* A member's turn its status took, ended ($C1:BCE1), as C. */
bool battle_end_member_turn(GameState *g);
bool battle_end_member_turn_ready(Snes *s);
void battle_member_called_off_step(Snes *s);
/* battle/party_work.c: a party member's tech, or its attack, worked out, and
 * the item it uses. battle/inventory.c: an item added to the inventory. */
void battle_work_out_member_tech_entry(Snes *s);
/* A member's tech worked out ($C1:BF79), and an item used ($C1:BF46), as C. */
bool battle_work_out_member_tech(GameState *g, Platform *pf);
bool battle_use_item(GameState *g, Platform *pf);
bool battle_work_out_member_tech_ready(Snes *s);
void battle_use_item_entry(Snes *s);
bool battle_use_item_ready(Snes *s);
void battle_add_to_inventory(Snes *s);
bool battle_add_to_inventory_ready(Snes *s);
void battle_give_gold_entry(Snes *s);
/* battle/member_stats.c */
/* battle/tech_records.c */
/* battle/fight_entries.c */
bool battle_start_fight_ready(Snes *s);
void battle_start_fight_entry(Snes *s);
bool fight_load_enemy_ready(Snes *s);
void fight_load_enemy_entry(Snes *s);
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
void battle_put_items_back_entry(Snes *s);
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
void battle_call_enemies_in_entry(Snes *s);
bool battle_call_enemies_in_ready(Snes *s);

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
uint8_t battle_selector_in_a(const Snes *s);
bool battle_enemy_turn_ready(Snes *s);
bool battle_search_ready(Snes *s);

/* battle/wait.c: battle/wait.h; battle/message.c: battle/message.h */

/* battle/battlefield.c, menu_window.c, damage.c, reset.c */
/* battle/display.c */
void battle_setup_display(Snes *s);
void battle_nmi_alternate(Snes *s);
/* battle/frame.c */
/* battle/scroll.c */
/* battle/uploads.c */
void battle_upload_palette(Snes *s);
void battle_request_window_rows(Snes *s);
void battle_request_saved_window_rows(Snes *s);
/* battle/stats.c */
void battle_update_all_combatants_entry(Snes *s);
/* Every combatant updated for the turn ($C1:B223), as C: false where a
 * member's coming back went no further. */
bool battle_update_all_combatants(GameState *g);
void battle_update_combatant(Snes *s);

/* battle/tiles.c */

/* battle/battlefield_change.c */
void battle_change_battlefield_entry(Snes *s);
/* The battlefield changed ($CD:0018), and an effect's rows of the
 * battlefield's tiles loaded, the first set or the second ($CD:0015,
 * $CD:002A), as C. */
bool battle_change_battlefield(GameState *g, Platform *pf, uint16_t battlefield);
bool battle_load_effect_tile_rows(GameState *g, Platform *pf, uint8_t first_row_set, bool second);
void battle_load_battlefield(Snes *s);
void battle_load_effect_tile_rows_entry(Snes *s);
void battle_load_second_effect_tile_rows_entry(Snes *s);
bool battle_changed_battlefield_fits(Snes *s);
bool battle_battlefield_fits(Snes *s);

/* battle/beaten_enemies.c */
void battle_see_to_the_beaten_enemies_entry(Snes *s);
void battle_see_off_the_beaten_enemies(Snes *s);
void battle_see_off_an_enemy(Snes *s);
void battle_dissolve_an_enemy(Snes *s);
void battle_change_battlefield_as_an_enemy_leaves(Snes *s);
bool battle_seeing_off_the_beaten(Snes *s);
bool battle_seeing_off(Snes *s);
bool battle_dissolving(Snes *s);

/* battle/battlefield_restore.c */
void battle_put_the_battlefield_back_entry(Snes *s);
/* battle/closing.c */
void battle_close_the_display(Snes *s);
void battle_open_the_window(Snes *s);
void battle_close_the_window_entry(Snes *s);
void battle_fade_the_music_out(Snes *s);
void battle_play_out_the_boss_end(Snes *s);
void battle_let_the_screen_settle(Snes *s);
void battle_change_to_the_battlefield(Snes *s);

/* battle/animation.c */
bool battle_steps_animations(Snes *s);
void battle_step_enemy_effects(Snes *s);

/* battle/rewards_messages.c */
void battle_show_the_rewards_entry(Snes *s);
/* The rewards' messages ($FD:AD17), as C; and the experience's share's guard's
 * state half ($C1:F93E). */
bool battle_show_the_rewards(GameState *g, Platform *pf);
bool battle_experience_can_be_shared(const GameState *g);
bool battle_rewards_can_be_shown(Snes *s);

/* battle/battle_end.c: the battle's end's bookkeeping */
/* battle/party_turns.c */
void battle_member_status_turn_entry(Snes *s);
bool battle_member_status_turn_ready(Snes *s);
void battle_cancel_if_unable_entry(Snes *s);
bool battle_cancel_if_unable_ready(Snes *s);
void battle_party_counters_entry(Snes *s);
bool battle_party_counters_ready(Snes *s);
void battle_call_off_waiting_combination_entry(Snes *s);
void battle_call_off_waiting_member_entry(Snes *s);
bool battle_call_off_waiting_member_ready(Snes *s);
void battle_see_to_knocked_out_enemies_entry(Snes *s);
bool battle_see_to_knocked_out_enemies_ready(Snes *s);
void battle_party_stands_down_entry(Snes *s);

/* battle/enemy_movement.c: each movement's routine, $C1:3772-$C1:4021 */
/* battle/timers.c */
void battle_tick_timers(Snes *s);
void battle_dispatch_expired_timers(Snes *s);
/* battle/status_timers.c: the timed statuses' handlers, $C1:8876-$C1:8C08,
 * and the showing of a status's number, $C1:895B. */
void battle_show_status_number_entry(Snes *s);
/* A status's number, or the running away, shown as an action of its own
 * ($C1:895B), as C. */
bool battle_show_status_number(GameState *g, Platform *pf, uint8_t showing);
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
void battle_set_up(Snes *s);
void battle_play_out_the_defeat_end(Snes *s);
void battle_set_up_after_the_graphics(Snes *s);
bool battle_combatants_graphics_known(Snes *s);
void battle_start_the_frame_work(GameState *g);
void battle_set_the_screen_up_for_the_field(Snes *s);

/* battle/effect_animation.c */

/* battle/effect_paths.c */
void battle_step_effect_slots_entry(Snes *s);
/* The frame's effects' paths ($CF:EFAF), the kinds of path the game gives
 * (its guard's state half), and the draw pass ($CF:E83A, with someone on the
 * battlefield; false where nobody is). */
void battle_step_effect_slots(GameState *g);
bool battle_effect_paths_known(const GameState *g);
bool battle_objects_drawn(GameState *g);
void battle_place_formation_entry(Snes *s);
void battle_place_formation(GameState *g);
/* battle/objects.c */
void battle_update_all_object_bounds(Snes *s);
void battle_update_animation_positions_entry(Snes *s);
/* The places the scripts follow worked out ($C1:4212), as C. */
bool battle_update_animation_positions(GameState *g, Platform *pf);

/* battle/frame.c */
void battle_frame_dispatch(Snes *s);
void battle_frame_work(Snes *s);
void battle_step_window_slide(Snes *s);
void battle_frame_update(Snes *s);
void battle_frame_wait_tail(Snes *s);
void battle_scroll_map_layers(Snes *s);
/* The map's layers that scroll by themselves, a frame's worth ($CD:0C89). */
void battle_scroll_the_map_layers(GameState *g);
enum { BATTLE_SCROLL_MAP_LAYERS_ENTRY = 0xCD0C89 };

/* battle/nmi.c */
void battle_nmi(Snes *s);
void battle_irq(Snes *s);

/* battle/scripts.c */
void battle_run_script_channel(Snes *s);
void battle_run_scripts_entry(Snes *s);
/* The animation scripts' channels run for a frame ($C1:4BBE), as C. */
bool battle_run_scripts(GameState *g, Platform *pf);
bool battle_script_channel_in_y(Snes *s);

/* battle/draw_objects.c */
void battle_draw_objects(Snes *s);
bool battle_draw_pass_ready(Snes *s);

/* battle/object_motion.c */
void battle_raise_numbers_entry(Snes *s);
void battle_raise_numbers(GameState *g);
void battle_step_scripted_moves_entry(Snes *s);
void battle_step_scripted_moves(GameState *g);
void battle_update_anchors_entry(Snes *s);
void battle_update_anchors(GameState *g);
void battle_record_trails_entry(Snes *s);
void battle_record_trails(GameState *g);
void battle_cycle_party_colors_entry(Snes *s);
void battle_cycle_party_colors(GameState *g);

/* battle/sprites.c */
void battle_build_bitmap_shapes_entry(Snes *s);
/* Up to four of the bitmap shapes, a frame on ($C1:4943), as C. */
bool battle_build_bitmap_shapes(GameState *g, Platform *pf);

/* battle/window.c, battle/status.c, battle/digits.c, battle/menu_window.c,
 * battle/numbers.c: the windows and the numbers, and the guard they run under */
bool battle_window_ready(Snes *s);
bool battle_window_ready_a_clear(Snes *s);
bool battle_menu_window_ready(Snes *s);
bool battle_menu_window_ready_a_clear(Snes *s);
void battle_draw_window(Snes *s);
void battle_draw_menu_window(Snes *s);
void battle_place_menu_cursors(Snes *s);
void battle_update_combo_members_entry(Snes *s);
/* Whose combination techs are called off ($C1:B575), as C. */
bool battle_update_combo_members(GameState *g, Platform *pf);

/* battle/menu_input.c, menu_lists.c, name_tiles.c, target_choice.c and
 * menu_bridge.c: the player choosing (battle/choosing.h). */
void battle_take_menu_input(Snes *s);
bool battle_take_menu_input_ready(Snes *s);

/* battle/action_showing.c, battle/action_animation.c, battle/frame_step.c:
 * an action shown, its animation loaded, and the battle's own frame */
void battle_show_action_entry(Snes *s);
bool battle_show_action_ready(Snes *s);
void battle_load_action_animation_entry(Snes *s);
/* The action's animation loaded ($C1:4310), as C. */
bool battle_load_action_animation(GameState *g, Platform *pf);
void battle_load_attack_animation(Snes *s);
void battle_load_tech_animation(Snes *s);
void battle_load_item_animation(Snes *s);
bool battle_animation_loading_ready(Snes *s);
bool battle_attack_animation_ready(Snes *s);
void battle_frame_step(Snes *s);
void battle_menu_turn_long(Snes *s);
void battle_wait_step(Snes *s);
/* $C1:0003: a battle step by its number (battle/steps.c), and its guard. */
void battle_run_a_step(Snes *s);
void battle_step_returns(Snes *s);
bool battle_step_is_known(Snes *s);
/* The battle's steps 1 and 2 (battle/menu_line.c): a member's turn comes and
 * it joins the line for the menu; a member's action is called off. */
void battle_turn_comes_step(Snes *s);
/* The party's poses (battle/party_poses.c): as a whole, a frame's step,
 * and the battle's steps 8 and 9, the victory and the defeat, that end in
 * them; the poses from a routine that goes on into them. */
void battle_victory_step(Snes *s);
void battle_defeat_step(Snes *s);
bool battle_poses_ready(Snes *s);
bool battle_victory_ready(Snes *s);
bool battle_strike_the_poses(Snes *s);
bool battle_strike_the_poses_as_their_own(Snes *s);
/* The poses the party strikes: the opening's and the victory's by each
 * member taking part in the fight, the defeat's by all three. */
typedef enum { PARTY_POSE_OPENING, PARTY_POSE_VICTORY, PARTY_POSE_DEFEAT } PartyPose;
void battle_give_the_party_poses(GameState *g, PartyPose pose);
void battle_called_off_step(Snes *s);
bool battle_turn_comes_ready(Snes *s);
bool battle_called_off_ready(Snes *s);
bool battle_frame_step_ready(Snes *s);

/* battle/pose_looks.c: what a pose shows, and the poses' round */

/* battle/damage.c: the hits landing, and a felled enemy's spoils */

/* battle/arith.c, battle/buttons.c */
void battle_update_button_repeat_entry(Snes *s);
/* The controller's held buttons turned into the menus', with their repeat
 * ($CD:0340). */
void battle_update_button_repeat(GameState *g);

#endif
