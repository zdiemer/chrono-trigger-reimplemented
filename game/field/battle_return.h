/* Field mode: the field put back after a battle (field/battle_return.c). */
#ifndef CT_GAME_FIELD_BATTLE_RETURN_H
#define CT_GAME_FIELD_BATTLE_RETURN_H

#include <stdbool.h>

#include "snes/system.h"

void field_return_from_battle(Snes *s);
bool field_battle_return_can_run(Snes *s);
void field_hold_party_if_defeated(Snes *s);

#endif
