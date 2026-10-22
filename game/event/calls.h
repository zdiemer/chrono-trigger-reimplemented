/* Handing an object a script to run, from outside its own script: another
 * object's call (event/calls.c), the player talking to it or the leader
 * walking into it (event/contact.c), and the party's own scripts
 * (event/party.c). An object runs at a priority level, 0 the most urgent,
 * and keeps a place in its script at each level it has left something
 * unfinished at, to come back to when what interrupted it returns. */
#ifndef CT_GAME_EVENT_CALLS_H
#define CT_GAME_EVENT_CALLS_H

#include <stdbool.h>
#include <stdint.h>

#include "field/object.h"
#include "state.h"

/* The levels a script can run at, the most urgent first: its places hold
 * eight. */
enum { EVENT_LEVELS = 8 };

/* Where one of an object's functions starts in the map's script. */
uint16_t object_function_start(const GameState *g, FieldObject object, uint8_t function);

/* An object started over forgets the calls it was interrupted in, at every
 * level, and runs at the least urgent. */
void object_forget_its_calls(GameState *g, FieldObject object);

/* Whether calls reach the object: it doesn't refuse them for now, and it is
 * reachable (field/object.h). */
bool object_takes_calls(const GameState *g, FieldObject object);

/* Whether the object would take a call at `level` now: it takes calls, calls
 * reach it, and the call is more urgent than what it is doing. */
bool object_takes_a_call_now(const GameState *g, FieldObject object, uint8_t level);

/* The object drops what it is doing -- keeping its place at its level to
 * come back to -- and runs the script from `start` at `level`, forgetting
 * the move it was making. */
void object_run_script_from(GameState *g, FieldObject object, uint8_t level, uint16_t start);

#endif
