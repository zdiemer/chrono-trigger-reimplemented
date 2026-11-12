/* Battle: whether the port takes the fight's start (battle/fight_start.c
 * says what its pieces are). */
#include "battle/fight.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/setup.h"
#include "vars.h"

bool battle_fight_can_start(const GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t character = battle_combatant_fighting(g, member);
        if (character != COMBATANT_NONE && character >= SETUP_CHARACTERS)
            return false;
    }
    return true;
}

