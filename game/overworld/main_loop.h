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

#endif
