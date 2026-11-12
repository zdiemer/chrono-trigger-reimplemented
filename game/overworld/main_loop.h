/* The overworld's main loop (game/overworld/main_loop.c), a pass a frame.
 * Each pass ends waiting for the next frame, or where the overworld leaves
 * for the field, or at the halt. */
#ifndef CT_GAME_OVERWORLD_MAIN_LOOP_H
#define CT_GAME_OVERWORLD_MAIN_LOOP_H

#include "platform/platform.h"
#include <stdbool.h>
#include <stdint.h>
#include "state.h"

/* The world map as the program runs it: the sprite-object mode started
 * ($C2:0000), the map set up, and the loop's passes until the party leaves
 * for the field (the location to enter set), or a step the port refuses
 * stops the program. */
typedef enum { OVERWORLD_LEFT, OVERWORLD_WENT_NO_FURTHER } OverworldEnd;
OverworldEnd overworld_runs(GameState *g, Platform *pf);

#endif
