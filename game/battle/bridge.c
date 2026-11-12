/* Whether a number names a combatant. */
#include "battle/bridge.h"
#include "battle/battle.h"

bool battle_names_combatant(uint16_t slot) { return slot < BATTLE_COMBATANTS; }
