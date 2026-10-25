/* The overworld's main loop (game/overworld/main_loop.c), on the original's
 * machine, a frame at a time: from its head ($C2:23CF), and from where the
 * original comes back from the menus ($C2:255A). Each ends back at the head
 * for the next frame, or where the overworld leaves for the field, or hands
 * over to the original. */
#ifndef CT_GAME_OVERWORLD_MAIN_LOOP_H
#define CT_GAME_OVERWORLD_MAIN_LOOP_H

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* The world map as the program runs it, with no machine: the sprite-object
 * mode started ($C2:0000), the map set up, and the loop's passes until the
 * party leaves for the field (the location to enter set), or a step only the
 * machine runs yet stops the program. */
typedef enum { OVERWORLD_LEFT, OVERWORLD_WENT_NO_FURTHER } OverworldEnd;
OverworldEnd overworld_runs(GameState *g, Platform *pf);

void overworld_run_a_frame(Snes *s);
void overworld_come_back_from_the_menus(Snes *s);

/* The halt ($C2:250D): the game stopped for good, a pass at a time, each
 * coming back to itself. */
void overworld_halt_entry(Snes *s);

/* Putting the overworld aside while a screen runs, and bringing it back
 * ($C2:2E23, $C2:2E72), as the original's own routines: the loop does them
 * as C. */
void overworld_put_aside_entry(Snes *s);
void overworld_bring_back_entry(Snes *s);

#endif
