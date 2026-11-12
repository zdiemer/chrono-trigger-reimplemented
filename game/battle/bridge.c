/* Validate combatant identifiers and stat-record boundaries. */
#include "battle/bridge.h"
#include "battle/battle.h"

bool battle_names_combatant(uint16_t slot) { return slot < BATTLE_COMBATANTS; }
bool battle_names_stat_block(uint16_t offset)
{
    return offset % BATTLE_STAT_BLOCK_BYTES == 0 && battle_names_combatant(battle_slot_of_stat_block(offset));
}
