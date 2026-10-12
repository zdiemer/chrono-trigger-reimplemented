/* Battle: waiting for the frame (battle/wait.c). */
#ifndef CT_GAME_BATTLE_WAIT_H
#define CT_GAME_BATTLE_WAIT_H

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* Wait until the vblank handler has finished a frame, as a wait of the
 * battle's own whose loop is `loop` (PF_LOOP). */
void battle_wait_for_the_handler_to_finish(GameState *g, Platform *pf, PfOrigin loop);

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
