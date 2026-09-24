/* Battle mode: the registered entries, as the runtime calls them (game/registry.c). */
#ifndef CT_GAME_BATTLE_ENTRIES_H
#define CT_GAME_BATTLE_ENTRIES_H

#include "snes/system.h"

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

/* battle/buffers.c */
void battle_clear_oam_buffer(Snes *s);
void battle_mark_object_grid(Snes *s);
void battle_reset_window_frames(Snes *s);
void battle_clear_damage(Snes *s);
void battle_clear_numbers(Snes *s);
void battle_reset_object_state(Snes *s);

/* battle/display.c */
void battle_update_scroll_tables(Snes *s);
void battle_update_scroll_tables_bg3_moving(Snes *s);
void battle_upload_palette(Snes *s);
void battle_setup_display(Snes *s);
void battle_load_graphics(Snes *s);
void battle_upload_tile_rows(Snes *s);
void battle_nmi_alternate(Snes *s);

/* battle/stats.c */
void battle_update_low_hp_flag(Snes *s);
void battle_update_status_flags(Snes *s);
void battle_update_all_combatants(Snes *s);
void battle_select_status_pose(Snes *s);
void battle_update_combatant(Snes *s);

/* battle/tiles.c */
void battle_decode_tile_row(Snes *s);

/* battle/timers.c */
void battle_tick_timers(Snes *s);
void battle_dispatch_expired_timers(Snes *s);
void battle_step_slot_effects(Snes *s);

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
void battle_find_due_animation(Snes *s);
void battle_update_animation_positions(Snes *s);

/* battle/frame.c */
void battle_frame_dispatch(Snes *s);
void battle_flush_vram_transfer(Snes *s);
void battle_step_window_slide(Snes *s);
void battle_update_scroll_dispatch(Snes *s);
void battle_step_cdc9(Snes *s);
void battle_frame_work(Snes *s);
void battle_upload_vram(Snes *s);
void battle_request_window_rows(Snes *s);
void battle_request_saved_window_rows(Snes *s);
void battle_request_lower_window_rows(Snes *s);
void battle_frame_update(Snes *s);
void battle_vram_transfer(Snes *s);
void battle_upload_alternating_vram(Snes *s);
void battle_update_screen_shake(Snes *s);
void battle_setup_window_hdma(Snes *s);
void battle_call_c00002(Snes *s);
void battle_frame_wait_tail(Snes *s);

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
void battle_step_object_palettes(Snes *s);
void battle_index_record_groups(Snes *s);
void battle_build_bitmap_sprites(Snes *s);

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
