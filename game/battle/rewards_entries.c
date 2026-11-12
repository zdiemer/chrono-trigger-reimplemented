/* Battle: the rewards' machine side -- each piece's registered entry, taking
 * what the original's callers hand it in the registers and in the cells the
 * original keeps its work in, and the guards that turn away what the game
 * never hands it (battle/rewards_levels.c says what the pieces are). */
#include "battle/rewards.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/inventory.h"
#include "battle/rewards_data.h"
#include "battle/rewards_work.h"
#include "battle/setup.h"

bool grower_at(uint16_t place, Grower *who)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (place == grower_place(grower_member(member))) {
            *who = grower_member(member);
            return true;
        }
    for (uint8_t character = 0; character < CHARACTER_RECORDS_COUNT; character++)
        if (place == grower_place(grower_in_reserve(character))) {
            *who = grower_in_reserve(character);
            return true;
        }
    return false;
}

/* What the original leaves in A as the experience's share is done: the count
 * of the characters it went through, and the carry of its last test of it,
 * set. */
enum { CHARACTERS_GONE_THROUGH = SETUP_CHARACTERS };

/* ---- What the guards ask ----------------------------------------------------------------- */

/* A record whose character the growth tables have. */
static bool grows(const GameState *g, Grower who) { return grower_character(g, who) < SETUP_CHARACTERS; }

/* The record the level-up works on, where the original keeps its place. */

/* The member the rewards note for: a party member, or the reserve. */

/* The members fighting: characters the tables have, each with fewer single
 * techs known than it has, or all learned (its techs stay in its eight places
 * of the messages' list). */

static bool members_ready(const GameState *g)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        if (battle_combatant_fighting(g, member) == COMBATANT_NONE)
            continue;
        uint8_t character = battle_stats_block_byte(g, member, STAT_ID);
        if (character >= SETUP_CHARACTERS)
            return false;
        bool all = battle_stats_block_byte(g, member, STAT_NOTES) & STAT_NOTE_TECHS_ALL_LEARNED;
        if (!all && party_single_techs_known(g, character) >= character_techs(g, character).count)
            return false;
    }
    return true;
}

/* The characters in reserve: characters the tables have. */
static bool reserve_ready(const GameState *g)
{
    for (uint8_t character = 0; character < SETUP_CHARACTERS; character++)
        if (character_in_reserve(g, character) && !grows(g, grower_in_reserve(character)))
            return false;
    return true;
}

// $C1:F93E
bool battle_experience_can_be_shared(const GameState *g) { return members_ready(g) && reserve_ready(g); }
