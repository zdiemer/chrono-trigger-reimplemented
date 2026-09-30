/* Battle: the rewards' machine side -- each piece's registered entry, taking
 * what the original's callers hand it in the registers and in the cells the
 * original keeps its work in, and the guards that turn away what the game
 * never hands it (battle/rewards_levels.c says what the pieces are). */
#include "battle/rewards.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/inventory.h"
#include "battle/rewards_data.h"
#include "battle/setup.h"
#include "port/machine.h"
#include "ram.h"
#include "vars.h"

static uint16_t index_x(const Snes *s) { return s->cpu.x; }

/* What the original leaves in A as the experience's share is done: the count
 * of the characters it went through, and the carry of its last test of it,
 * set. */
enum { CHARACTERS_GONE_THROUGH = SETUP_CHARACTERS };

/* The cells the pieces take their work from (battle/rewards_techs.c). */
enum { MEMBER_BLOCK_AT = 0x00, KNOWN_AT = 0x02, TECHS_AT = 0x04, RETURNING_FOR_AT = 0x14 };

static uint16_t cell_word(Snes *s, unsigned at)
{
    vars_peek(true);
    uint16_t v = battle_cell_word(state_of(s), at);
    vars_peek(false);
    return v;
}

/* ---- What the guards ask ----------------------------------------------------------------- */

/* A record whose character the growth tables have. */
static bool grows(const GameState *g, Grower who)
{
    uint8_t character = who.in_reserve ? character_records_record_byte(g, who.who, STAT_ID) : battle_stats_block_byte(g, who.who, STAT_ID);
    return character < SETUP_CHARACTERS;
}

/* The record the level-up works on, where the original keeps its place. */
static bool growing(const GameState *g, Grower *who) { return grower_at(battle_growth_record_places(g, 0), who); }

/* The member the rewards note for: a party member, or the reserve. */
static bool noting(const GameState *g, unsigned most) { return battle_rewards_member(g) <= most; }

enum { OUT_OF_FIGHT = 0x80, NO_SLOT = 0x80, JOINED_FIRST = 0x80 };

/* The members still fighting: characters the tables have, none knocked out
 * (the level-ups would go on for ever), and each with fewer single techs
 * known than it has, or all learned. */
static bool members_ready(const GameState *g)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t fighting = battle_combatant_fighting(g, member);
        if (fighting == COMBATANT_NONE)
            continue;
        uint8_t character = battle_stats_block_byte(g, member, STAT_ID);
        if (character >= SETUP_CHARACTERS)
            return false;
        if (!(fighting & OUT_OF_FIGHT) && (battle_stats_block_byte(g, member, STAT_CONDITION) & CONDITION_KNOCKED_OUT))
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
    uint8_t joined = party_characters_joined(g);
    for (uint8_t character = 0; character < SETUP_CHARACTERS; character++, joined = (uint8_t)(joined << 1))
        if ((joined & JOINED_FIRST) && (battle_character_slot(g, character) & NO_SLOT)
            && !grows(g, grower_in_reserve(character)))
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
bool battle_share_experience_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ready = members_ready(g) && reserve_ready(g);
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
bool rewards_gain_experience_ready(Snes *s)
{
    Grower who;
    return battle_on_page(s) && grower_at(index_x(s), &who);
}

void rewards_gain_experience_entry(Snes *s)
{
    GameState *g = state_of(s);
    Grower who;
    grower_at(index_x(s), &who);
    rewards_gain_experience(g, who, battle_experience_won(g));
    m_rts(s);
}

/* The record the original keeps the place of, and whose level it notes for a
 * member or the reserve. */
static bool level_up_ready(Snes *s, bool with_tables)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    Grower who;
    bool ready = growing(g, &who) && noting(g, REWARDS_RESERVE) && (!with_tables || grows(g, who));
    vars_peek(false);
    return battle_on_page(s) && ready;
}

static Grower grower_kept(const GameState *g)
{
    Grower who = grower_member(0);
    growing(g, &who);
    return who;
}

// $C1:F623
bool rewards_level_up_ready(Snes *s) { return level_up_ready(s, true); }

void rewards_level_up_entry(Snes *s)
{
    GameState *g = state_of(s);
    rewards_level_up(g, grower_kept(g), (uint8_t)battle_rewards_member(g));
    m_rts(s);
}

// $C1:F7B9
bool rewards_reach_level_ready(Snes *s) { return level_up_ready(s, false); }

void rewards_reach_level_entry(Snes *s)
{
    GameState *g = state_of(s);
    rewards_reach_level(g, grower_kept(g), (uint8_t)battle_rewards_member(g));
    m_rts(s);
}

// $C1:F871
enum { TOP_LEVEL = 99 };

bool rewards_grow_hp_and_mp_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    Grower who;
    bool ready = growing(g, &who) && grows(g, who)
                 && (who.in_reserve ? character_records_record_byte(g, who.who, STAT_LEVEL) : battle_stats_block_byte(g, who.who, STAT_LEVEL)) <= TOP_LEVEL;
    vars_peek(false);
    return battle_on_page(s) && ready;
}

/* The carry says whether the MP reached its most. */
void rewards_grow_hp_and_mp_entry(Snes *s)
{
    GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_C, rewards_grow_hp_and_mp(g, grower_kept(g)));
    m_rts(s);
}

// $C1:F9EB
bool rewards_share_with_reserve_ready(Snes *s)
{
    vars_peek(true);
    bool ready = reserve_ready(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && ready;
}

void rewards_share_with_reserve_entry(Snes *s)
{
    rewards_share_with_reserve(state_of(s));
    s->cpu.a = CHARACTERS_GONE_THROUGH;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* ---- Techs ------------------------------------------------------------------------------- */

// $C1:F205
bool rewards_learn_techs_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ready = members_ready(g);
    vars_peek(false);
    return battle_on_page(s) && ready;
}

/* It comes back with the carry set, from its count of the combinations. */
void rewards_learn_techs_entry(Snes *s)
{
    rewards_learn_techs(state_of(s));
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C1:F2DB
void rewards_sort_combination_members_entry(Snes *s)
{
    rewards_sort_combination_members(state_of(s));
    m_rts(s);
}

/* The member whose stat block is at the offset the original keeps, the one
 * the rewards note for; and the character learning. */
static bool learner_ready(Snes *s, uint8_t *member)
{
    uint16_t block = cell_word(s, MEMBER_BLOCK_AT);
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t character = battle_tech_learner(g);
    bool ready = block % BATTLE_STAT_BLOCK_BYTES == 0 && block / BATTLE_STAT_BLOCK_BYTES < BATTLE_PARTY_SIZE
                 && battle_rewards_member(g) == block / BATTLE_STAT_BLOCK_BYTES && character < SETUP_CHARACTERS;
    vars_peek(false);
    *member = (uint8_t)(block / BATTLE_STAT_BLOCK_BYTES);
    return battle_on_page(s) && ready;
}

/* The techs it learns stay in the member's eight places of the messages'
 * list. */
enum { PLACES_A_MEMBER = 8 };

static bool techs_fit(Snes *s, uint8_t member)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t character = battle_tech_learner(g);
    unsigned known = party_single_techs_known(g, character), count = character_techs(g, character).count;
    bool all = battle_stats_block_byte(g, member, STAT_NOTES) & STAT_NOTE_TECHS_ALL_LEARNED;
    unsigned at = battle_rewards_tech_at(g), most = (unsigned)(member + 1) * PLACES_A_MEMBER;
    bool fit = all || (known < count && at + (count - known) <= most && at >= (unsigned)member * PLACES_A_MEMBER);
    vars_peek(false);
    return fit;
}

// $C1:F39A
bool rewards_count_tech_points_ready(Snes *s)
{
    uint8_t member;
    return learner_ready(s, &member) && techs_fit(s, member);
}

/* It comes back with A clear. */
void rewards_count_tech_points_entry(Snes *s)
{
    GameState *g = state_of(s);
    rewards_count_tech_points(g, (uint8_t)(battle_cell_word(g, MEMBER_BLOCK_AT) / BATTLE_STAT_BLOCK_BYTES), battle_tech_learner(g));
    s->cpu.a = 0;
    m_rts(s);
}

// $C1:F45F
bool rewards_reach_next_tech_ready(Snes *s)
{
    uint8_t member;
    return learner_ready(s, &member);
}

void rewards_reach_next_tech_entry(Snes *s)
{
    GameState *g = state_of(s);
    rewards_reach_next_tech(g, (uint8_t)(battle_cell_word(g, MEMBER_BLOCK_AT) / BATTLE_STAT_BLOCK_BYTES), battle_tech_learner(g),
                            battle_tech_points_won_low(g));
    m_rts(s);
}

// $C1:F4DF
/* With the places of the character's count of techs known and of its techs'
 * record in the cells, as the tech points' count leaves them. */
bool rewards_learn_single_tech_ready(Snes *s)
{
    uint8_t member;
    if (!learner_ready(s, &member))
        return false;
    uint16_t known = cell_word(s, KNOWN_AT), techs = cell_word(s, TECHS_AT);
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t character = battle_tech_learner(g);
    unsigned at = battle_rewards_tech_at(g);
    vars_peek(false);
    return known == (uint16_t)(RAM_PARTY_SINGLE_TECHS_KNOWN + character) && techs == character_techs_place(character)
           && at < BATTLE_REWARDS_TECHS_COUNT;
}

void rewards_learn_single_tech_entry(Snes *s)
{
    GameState *g = state_of(s);
    rewards_learn_single_tech(g, (uint8_t)(battle_cell_word(g, MEMBER_BLOCK_AT) / BATTLE_STAT_BLOCK_BYTES), battle_tech_learner(g),
                              battle_learner_first_tech(g));
    m_rts(s);
}

/* The combination's learned byte's place, in the first cell. */
static bool combination_learned_at(uint16_t place, uint8_t *record)
{
    unsigned bytes = PARTY_DOUBLE_TECHS_LEARNED_COUNT + PARTY_TRIPLE_TECHS_LEARNED_COUNT;
    *record = (uint8_t)(place - (uint16_t)RAM_PARTY_DOUBLE_TECHS_LEARNED);
    return place >= (uint16_t)RAM_PARTY_DOUBLE_TECHS_LEARNED && place < (uint16_t)RAM_PARTY_DOUBLE_TECHS_LEARNED + bytes;
}

// $C1:F533
/* A combination's techs, as the search for the members' record leaves them;
 * the double techs it can teach fit the messages' list. */
bool rewards_learn_combination_techs_ready(Snes *s)
{
    uint8_t record;
    bool learned_place = combination_learned_at(cell_word(s, MEMBER_BLOCK_AT), &record);
    vars_peek(true);
    const GameState *g = state_of(s);
    unsigned count = battle_combination_techs_left(g);
    bool fits = count != 0 && battle_rewards_double_techs_listed(g) + count <= BATTLE_REWARDS_DOUBLE_TECHS_COUNT;
    bool ready = learned_place && fits && noting(g, BATTLE_PARTY_SIZE - 1);
    vars_peek(false);
    return battle_on_page(s) && ready;
}

void rewards_learn_combination_techs_entry(Snes *s)
{
    GameState *g = state_of(s);
    Combination c;
    combination_learned_at(battle_cell_word(g, MEMBER_BLOCK_AT), &c.record);
    c.characters = battle_combination_members(g);
    c.first = battle_combination_tech(g);
    c.count = battle_combination_techs_left(g);
    c.asks = battle_cell_word(g, TECHS_AT);
    rewards_learn_combination_techs(g, c, (uint8_t)battle_rewards_member(g));
    m_rts(s);
}

/* ---- Items ------------------------------------------------------------------------------- */

// $C1:B4E9
void battle_put_items_back_entry(Snes *s)
{
    battle_put_items_back(state_of(s));
    m_rts(s);
}

// $C1:B6D1
/* The member in the cell its callers leave it in; X comes back as it was. */
bool battle_return_chosen_item_ready(Snes *s)
{
    vars_peek(true);
    uint8_t member = battle_low_scratch(state_of(s), RETURNING_FOR_AT);
    vars_peek(false);
    return battle_on_page(s) && member < BATTLE_PARTY_SIZE;
}

void battle_return_chosen_item_entry(Snes *s)
{
    GameState *g = state_of(s);
    battle_return_chosen_item(g, battle_low_scratch(g, RETURNING_FOR_AT));
    m_rts(s);
}
