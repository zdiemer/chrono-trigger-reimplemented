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
#include "port/machine.h"

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

/* ---- Experience and levels --------------------------------------------------------------- */

// $FD:B201
void battle_turn_experience_to_gold_entry(Snes *s)
{
    battle_turn_experience_to_gold(state_of(s));
    m_rtl(s);
}

// $C1:F93E
bool battle_experience_can_be_shared(const GameState *g) { return members_ready(g) && reserve_ready(g); }

bool battle_share_experience_ready(Snes *s)
{
    vars_peek(true);
    bool ready = battle_experience_can_be_shared(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && ready;
}

void battle_share_experience_entry(Snes *s)
{
    battle_share_experience(state_of(s));
    s->cpu.a = CHARACTERS_GONE_THROUGH;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C1:F908
/* The record's place in X. */

/* The record the original keeps the place of, and whose level it notes for a
 * member or the reserve. */

// $C1:F623

/* The pot is the experience won's cell. */

// $C1:F7B9

// $C1:F871

/* The carry says whether the MP reached its most, where it stops. */

// $C1:F9EB

/* ---- Techs ------------------------------------------------------------------------------- */

// $C1:F205

/* It comes back with the carry set, from its count of the combinations. */

// $C1:F2DB

/* The member whose stat block is at the offset the original keeps, and the
 * character learning; where the pieces note techs, the rewards note for that
 * member too. */

/* Where the next single tech is noted, a byte as the pieces count it, and the
 * places the member can still learn stay in the messages' list. */

// $C1:F39A

/* It comes back with A clear. */

// $C1:F45F

// $C1:F4DF
/* With the places of the character's count of techs known and of its techs'
 * record in the cells, as the tech points' count leaves them; A's high byte
 * clear, as the count leaves it (it comes into the index of the count). */

/* The combination's learned byte's place, in the first cell. */

// $C1:F533
/* A combination's techs, as the search for the members' record leaves them;
 * the double techs it can teach fit the messages' list. */

/* ---- Items ------------------------------------------------------------------------------- */

// $C1:B4E9

void battle_put_items_back_entry(Snes *s)
{
    battle_put_items_back(state_of(s));
    m_rts(s);
}
