/* Battle: the fight's start's machine side -- each piece's registered entry,
 * taking what the original's callers hand it in the registers and the
 * direct page's cells, and the guards that turn away what the game never
 * hands it (battle/fight_start.c says what the pieces are). */
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

// $FD:B2DE, $FD:B33F

// $FD:B22E

/* An enemy in X, and the same in the cell the original finds its block by
 * ($02). */
enum { ENEMY_CELL = 0x02 };

/* It leaves in A the count of the record's last part, its three bytes, which
 * its caller's next pieces find there. */
enum { LAST_PART_BYTES = 3 };
