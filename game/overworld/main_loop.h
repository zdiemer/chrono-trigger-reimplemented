/* The overworld's main loop (game/overworld/main_loop.c), on the original's
 * machine, a frame at a time: from its head ($C2:23CF), and from where the
 * original comes back from the menus ($C2:255A). Each ends back at the head
 * for the next frame, or where the overworld leaves for the field, or hands
 * over to the original. */
#ifndef CT_GAME_OVERWORLD_MAIN_LOOP_H
#define CT_GAME_OVERWORLD_MAIN_LOOP_H

#include "snes/system.h"

void overworld_run_a_frame(Snes *s);
void overworld_come_back_from_the_menus(Snes *s);

/* Putting the overworld aside while a screen runs, and bringing it back
 * ($C2:2E23, $C2:2E72), as the original's own routines: the loop does them
 * as C. */
void overworld_put_aside_entry(Snes *s);
void overworld_bring_back_entry(Snes *s);

#endif
