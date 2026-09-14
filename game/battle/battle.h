/* Battle mode. */
#ifndef CT_GAME_BATTLE_H
#define CT_GAME_BATTLE_H

#include "snes/system.h"

/* battle/wait.c */
bool battle_frame_wait_is_idle(Snes *s);
void battle_wait_for_frame_loop(Snes *s);
void battle_wait_vblank_end_loop(Snes *s);
void battle_wait_hblank_loop(Snes *s);
void battle_wait_line_loop(Snes *s);
void battle_wait_for_nmi_loop(Snes *s);
void battle_wait_ca1f_loop(Snes *s);
void battle_irq_wait_hblank_loop(Snes *s);

/* battle/buffers.c */
void battle_clear_oam_buffer(Snes *s);
void battle_mark_object_grid(Snes *s);
void battle_reset_0b40_table(Snes *s);
void battle_clear_b328_table(Snes *s);

/* battle/display.c */
void battle_update_scroll_tables(Snes *s);
void battle_update_scroll_tables_bg3_moving(Snes *s);

/* battle/timers.c */
void battle_tick_timers(Snes *s);
void battle_dispatch_expired_timers(Snes *s);
void battle_step_slot_delays(Snes *s);
void battle_step_effect_slots(Snes *s);

/* battle/objects.c */
void battle_update_object_bounds(Snes *s);
void battle_update_all_object_bounds(Snes *s);
void battle_find_due_animation(Snes *s);

/* battle/frame.c */
void battle_frame_dispatch(Snes *s);
void battle_flush_vram_transfer(Snes *s);
void battle_step_window_slide(Snes *s);
void battle_update_scroll_dispatch(Snes *s);
void battle_step_cdc9(Snes *s);
void battle_frame_work(Snes *s);

/* battle/nmi.c */
void battle_nmi(Snes *s);
void battle_restore_math_operands(Snes *s);
void battle_irq_update_ca22(Snes *s);
void battle_irq(Snes *s);
void battle_irq_after_hblank(Snes *s);

/* battle/sprites.c */
void battle_draw_objects(Snes *s);
void battle_draw_shadow(Snes *s);
void battle_step_object_effect(Snes *s);
void battle_draw_sprite_parts(Snes *s);
void battle_copy_sprite_tiles(Snes *s);
void battle_draw_object_underlays(Snes *s);
void battle_draw_object_overlays(Snes *s);
void battle_step_prepared_sprites(Snes *s);
void battle_step_object_paths(Snes *s);
void battle_update_anchor_points(Snes *s);
void battle_record_object_trails(Snes *s);
void battle_cycle_party_palettes(Snes *s);

#endif
