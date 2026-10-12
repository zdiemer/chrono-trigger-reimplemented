/* Battle: waiting for the frame (battle/wait.c). */
#ifndef CT_GAME_BATTLE_WAIT_H
#define CT_GAME_BATTLE_WAIT_H

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* Wait out a frame of a paused fight, until the vblank handler has finished
 * it (battle/sound.c's pause). */
void battle_sit_out_a_paused_frame(GameState *g, Platform *pf);

void battle_wait_for_frame(Snes *s);
void battle_wait_for_a_last_frame(Snes *s);
void battle_wait_for_the_handler(Snes *s);
void battle_wait_for_the_next_frame(Snes *s);
void battle_wait_some_frames(Snes *s);
void battle_wait_for_the_small_handler(Snes *s);
void battle_wait_window_slide_loop(Snes *s);
void battle_wait_graphics_loaded_loop(Snes *s);
void battle_irq_wait_hblank_loop(Snes *s);

#endif
