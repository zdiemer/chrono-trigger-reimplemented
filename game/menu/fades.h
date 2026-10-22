/* The menus' fades (menu/fades.c): the screen brought up out of black as a
 * screen opens, in a task of its own, and taken down to black as the menus
 * close, in the task that closes them. */
#ifndef CT_GAME_MENU_FADES_H
#define CT_GAME_MENU_FADES_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* Start the fade in: its task runs from the next frame, while the screen
 * that started it goes on. */
void menu_start_the_fade_in(GameState *g);

/* Fade the screen out to black, a step a frame, waiting through it, as the
 * original's routine for it does when the JSR at `site` calls it; false where
 * the port stops (menu/bridge.h, menu_yield). Its last frame's wait is the
 * task switch's ($C2:821E) as if from `site`, with every register as the
 * original leaves them (tools/dontcare_check.py reads it so). */
enum { MENU_FADE_S_LAST_WAIT_READS_ALL = 0 };
bool menu_fade_out_from(Snes *s, uint32_t site);

void menu_fade_the_screen_out(Snes *s);
void menu_start_the_fade_in_entry(Snes *s);
void menu_fade_in_task(Snes *s);

#endif
