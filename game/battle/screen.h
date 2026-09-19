/* Battle: the screen effects in bank D1, shared between their files.
 *
 * Most of them are the bodies of the battle's screen script commands: two
 * script channels ($CA32, 16 bytes each) that the frame work runs, whose
 * commands from $80 up dispatch through $CD:16B5 to handlers in bank CD, many
 * of which call a routine here. When a handler runs, the script pointer
 * ($40-$42) points at its first operand, and the channel moves it one byte on
 * afterwards: a command with one operand leaves it where it is, one with three
 * moves it two, and one with none moves it back a byte. $43 holds the channel
 * (0 or $10).
 *
 * Everything here runs with the battle's direct page ($0000) and data bank
 * ($7E), which the entries' guard checks, so work RAM is addressed by its long
 * address. */
#ifndef CT_GAME_BATTLE_SCREEN_H
#define CT_GAME_BATTLE_SCREEN_H

#include "snes/system.h"

/* The guard of every entry here: D = $0000 and DB = $7E, as all of their
 * callers have them. */
bool battle_screen_page(Snes *s);
/* $D1:F91D and $D1:F972 run with the data bank at the registers (0). */
bool battle_quad_page(Snes *s);

/* screen_palette.c */
void battle_screen_swap_palette_rows(Snes *s);
void battle_screen_whiten_palette(Snes *s);
void battle_screen_whiten_high_rows(Snes *s);
void battle_screen_move_rows_up(Snes *s);
void battle_screen_move_rows_down(Snes *s);
void battle_step_hp_gradient(Snes *s);
void battle_load_effect_graphics(Snes *s);
void battle_pause_palette_animation(Snes *s);
void battle_find_gradient_phase(Snes *s);
void battle_gradient_row_rotation(Snes *s);
void battle_write_gradient_row(Snes *s);
void battle_save_palette_kinds(Snes *s);

/* screen_window.c */
void battle_screen_open_band(Snes *s);
void battle_screen_wipe_from_left(Snes *s);
void battle_screen_wave_windows(Snes *s);
void battle_screen_wave_counted(Snes *s);
void battle_screen_wave_framed(Snes *s);
void battle_screen_add_line_deltas(Snes *s);
void battle_screen_copy_window1_to_window2(Snes *s);
void battle_screen_copy_window1_inverted(Snes *s);
void battle_screen_move_column_band(Snes *s);
void battle_screen_draw_column_band(Snes *s);
void battle_screen_fill_windows(Snes *s);
void battle_screen_draw_beam(Snes *s);
void battle_wave_w1_from_table(Snes *s);
void battle_wave_w1(Snes *s);
void battle_wave_w2(Snes *s);
void battle_fill_windows_empty(Snes *s);
void battle_fill_window1_empty(Snes *s);
void battle_fill_window2_empty(Snes *s);
void battle_fill_window1_full(Snes *s);
void battle_sort_quad_corners(Snes *s);
void battle_quad_edge_slope(Snes *s);
void battle_quad_edge_slopes(Snes *s);
void battle_draw_window_quad(Snes *s);

/* screen_motion.c */
void battle_screen_random_byte(Snes *s);
void battle_screen_jitter(Snes *s);
void battle_screen_swing(Snes *s);
void battle_screen_save_positions(Snes *s);
void battle_set_screen_effect(Snes *s);
void battle_stop_shake(Snes *s);
void battle_start_shake(Snes *s);
void battle_set_palette_preset(Snes *s);

/* screen_setup.c */
void battle_build_fade_tables(Snes *s);

#endif
