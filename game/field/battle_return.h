/* Field mode: the field put back after a battle (field/battle_return.c). */
#ifndef CT_GAME_FIELD_BATTLE_RETURN_H
#define CT_GAME_FIELD_BATTLE_RETURN_H

#include <stdbool.h>

#include "platform/platform.h"
#include <stdint.h>
#include "state.h"

/* The field taken up again after a battle ($C0:0283), as C: on its map, a
 * frame at a time, or the game over, or back at the place put aside. False
 * where it went no further. */
bool field_taken_up_after_battle(GameState *g, Platform *pf);

/* Continuations of the field's held frame waits. */

#endif
