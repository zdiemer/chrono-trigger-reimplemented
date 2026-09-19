/* Scenes the game decompresses to $7E:3000 and runs from RAM (the attract
 * sequence and title screen, the jet bike race), and the bank C3 routines those
 * programs call. The attract sequence's wait loops are in game/attract/. */
#ifndef CT_GAME_SCENE_H
#define CT_GAME_SCENE_H

#include <stdbool.h>

#include "snes/system.h"

/* scene/sprites.c */
void scene_load_sprite_list(Snes *s);
void scene_load_sprite_list_clipped(Snes *s);
void scene_add_sprite(Snes *s);
void scene_add_large_sprite(Snes *s);

/* scene/bitmap.c */
void scene_scale_bitmap_row(Snes *s);
void scene_scale_bitmap(Snes *s);

/* scene/sequence.c */
void scene_run_sequence(Snes *s);
void scene_script_wait_sound_port2(Snes *s);
bool scene_sequence_bank_is_scripts(Snes *s);

/* scene/circle.c */
void scene_draw_circle(Snes *s);

/* scene/matrix_code.c */
void scene_write_matrix_code(Snes *s);

/* scene/objects.c (bank C6) */
void scene_object_read_mode(Snes *s);
void scene_object_clear_7f0062(Snes *s);
void scene_object_set_7f01ed(Snes *s);
void scene_object_clear_7f01ed(Snes *s);
void scene_object_advance_line_tables(Snes *s);
void scene_object_step_28(Snes *s);
void scene_object_step_28_down(Snes *s);
void scene_object_add_velocity(Snes *s);
void scene_object_drift(Snes *s);
void scene_epoch_outside_sides(Snes *s);
void scene_epoch_in_area(Snes *s);
bool scene_epoch_on_world_map(Snes *s);
void scene_object_load_graphics_2(Snes *s);
void scene_object_load_c3_stream_1(Snes *s);
void scene_object_load_graphics_13(Snes *s);
void scene_object_load_c3_stream_2(Snes *s);
void scene_object_window1_lines(Snes *s);
void scene_object_window2_lines(Snes *s);
void scene_object_bg1_scroll_lines(Snes *s);
bool scene_object_bank_reaches_io(Snes *s);

/* scene/math.c */
void scene_multiply_signed(Snes *s);
void scene_find_divisor(Snes *s);
bool scene_find_divisor_on_divider_page(Snes *s);

/* scene/nmi.c (scene_frame_work is in scene/display.h) */
void scene_nmi(Snes *s);
void scene_nmi_while_working(Snes *s);
void scene_line_irq_ack(Snes *s);
void scene_boot_nmi(Snes *s);
void scene_boot_line_irq(Snes *s);
void scene_boot_line_irq_wait_hblank_end_loop(Snes *s);
void scene_boot_line_irq_wait_hblank_loop(Snes *s);
void scene_boot_line_irq_blank(Snes *s);

/* scene/boot.c */
void scene_boot(Snes *s);
bool scene_boot_bank_reaches_io(Snes *s);
void scene_copy_data_to_7e(Snes *s);
void scene_copy_data_to_7f(Snes *s);

/* scene/program.c (RAM code; guarded) */
bool scene_divide_present(Snes *s);
void scene_divide(Snes *s);
bool scene_divide_8_8_present(Snes *s);
void scene_divide_8_8(Snes *s);
bool scene_multiply_present(Snes *s);
void scene_multiply(Snes *s);
bool scene_shift_c00_words_present(Snes *s);
void scene_shift_c00_words(Snes *s);
bool scene_run_tasks_950_present(Snes *s);
void scene_run_tasks_950(Snes *s);
bool scene_run_tasks_940_present(Snes *s);
void scene_run_tasks_940(Snes *s);
bool scene_wait_apu_port3_present(Snes *s);
void scene_wait_apu_port3_loop(Snes *s);
bool scene_wait_39_clear_present(Snes *s);
void scene_wait_39_clear_loop(Snes *s);

#endif
