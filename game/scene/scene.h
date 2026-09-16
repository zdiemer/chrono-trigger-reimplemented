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

/* scene/bitmap.c */
void scene_scale_bitmap_row(Snes *s);

/* scene/math.c */
void scene_multiply_signed(Snes *s);

/* scene/nmi.c */
void scene_nmi(Snes *s);
bool scene_run_frame_work(Snes *s);

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
