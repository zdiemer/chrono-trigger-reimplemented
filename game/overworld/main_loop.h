/* The overworld's main loop (game/overworld/main_loop.c): as the program runs
 * it, and on the original's machine in the shipping mode, a frame at a time
 * as its held wait's continuation. Each pass ends waiting for the next frame,
 * or where the overworld leaves for the field, or at the halt. */
#ifndef CT_GAME_OVERWORLD_MAIN_LOOP_H
#define CT_GAME_OVERWORLD_MAIN_LOOP_H

#include "platform/platform.h"
#include <stdbool.h>
#include <stdint.h>
#include "state.h"

/* The world map as the program runs it, with no machine: the sprite-object
 * mode started ($C2:0000), the map set up, and the loop's passes until the
 * party leaves for the field (the location to enter set), or a step only the
 * machine runs yet stops the program. */
typedef enum { OVERWORLD_LEFT, OVERWORLD_WENT_NO_FURTHER } OverworldEnd;
OverworldEnd overworld_runs(GameState *g, Platform *pf);

/* The loop's contract on the machine: the mode's page with the data bank on
 * low RAM, an 8-bit accumulator, 16-bit index registers and binary
 * arithmetic (overworld_main_loop_runs, the page and bank alone). */

/* The loop's wait for the next frame, a held wait (holds.h,
 * HOLD_OVERWORLD_FRAME): the port starting it stops in the wait's loop
 * ($C2:0348), and the runtime goes on with overworld_next_frame once the
 * vblank handler has counted the frame; overworld_waits_for_the_frame is a
 * saveable C loop before it. The guards say which applies, each where the
 * wait returns to the loop's pass, which the mode's other waits that share
 * its code don't. */

/* The halt ($C2:250D): the game stopped for good, a pass at a time, each
 * coming back to itself, a held wait of its own (HOLD_OVERWORLD_HALT). */

/* Saveable phases around the menu services' counted waits. */

#endif
