/* Field mode: the field put back after a battle (field/battle_return.c). */
#ifndef CT_GAME_FIELD_BATTLE_RETURN_H
#define CT_GAME_FIELD_BATTLE_RETURN_H

#include <stdbool.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

void field_return_from_battle(Snes *s);
bool field_battle_return_can_run(Snes *s);
void field_hold_party_if_defeated(Snes *s);

/* The field taken up again after a battle ($C0:0283), as C: on its map, a
 * frame at a time, or the game over, or back at the place put aside. False
 * where it went no further. */
bool field_taken_up_after_battle(GameState *g, Platform *pf);

#endif
