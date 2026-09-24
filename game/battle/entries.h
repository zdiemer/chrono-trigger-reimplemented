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
void battle_reset_0b40_table(Snes *s);
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
void battle_update_combo_members(Snes *s);
void battle_update_combatant(Snes *s);

/* battle/tiles.c */
void battle_decode_tile_row(Snes *s);

/* battle/timers.c */
void battle_tick_timers(Snes *s);
void battle_dispatch_expired_timers(Snes *s);
void battle_step_slot_delays(Snes *s);
void battle_step_slot_effects(Snes *s);

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
void battle_step_object_effect(Snes *s);
void battle_draw_sprite_parts(Snes *s);
void battle_draw_afterimage(Snes *s);
void battle_copy_sprite_tiles(Snes *s);
void battle_draw_object_underlays(Snes *s);
void battle_draw_object_marker(Snes *s);
void battle_copy_prepared_sprites(Snes *s);
void battle_draw_object_overlays(Snes *s);
void battle_draw_effect_sprites(Snes *s);

/* battle/object_motion.c */
void battle_step_prepared_sprites(Snes *s);
void battle_step_object_paths(Snes *s);
void battle_update_anchor_points(Snes *s);
void battle_record_object_trails(Snes *s);
void battle_cycle_party_palettes(Snes *s);

/* battle/sprites.c */
void battle_build_object_graphics(Snes *s);
void battle_build_object_graphics_checked(Snes *s);
void battle_step_object_palettes(Snes *s);
void battle_index_record_groups(Snes *s);
void battle_build_bitmap_sprites(Snes *s);

/* battle/gauges.c */
void battle_draw_gauge(Snes *s);
void battle_number_to_digits(Snes *s);
void battle_number_to_2_digits(Snes *s);
void battle_blank_leading_zeros(Snes *s);
void battle_draw_party_status(Snes *s);

/* battle/arith.c, battle/buttons.c */
void battle_multiply_8x8(Snes *s);
void battle_multiply_8x16(Snes *s);
void battle_divide(Snes *s);
void battle_update_button_repeat(Snes *s);
void battle_multiply_16x16(Snes *s);
bool battle_divide_is_exact(Snes *s);

#endif
