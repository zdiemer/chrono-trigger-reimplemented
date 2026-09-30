/* Battle: the experience a won battle gives, and the levels it brings.
 *
 * Every member still fighting gets the whole of the experience the enemies
 * gave (battle_experience_won), added to its own up to 9,999,999, and gains
 * levels with it one at a time: the experience it lacks for its next level is
 * paid off first, and what is left goes on to the next, until none is left or
 * the member is at level 99. A level grows the member's maximum HP and MP by
 * its character's steps and its stats by its character's growth rates. The
 * characters in reserve -- joined, but not in the fight -- get three quarters
 * of it and at most one level. Between the two, the tech points teach the
 * members their techs (battle/rewards_techs.c).
 *
 * The rewards' messages ($FD:AD17, the original's) then show what this noted:
 * the experience and the tech points given (battle_reward_flags), the levels
 * gained (battle_rewards_levels_gained, battle_rewards_noted), the techs
 * learned. A party that wears the accessory that turns the experience into
 * gold gets gold instead, and no levels.
 *
 * The level-up works on a record where the original keeps its place
 * (battle_growth_record_places), a stat block or a character's record, which
 * lay their fields out alike (Grower). */
#include "battle/rewards.h"

#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/combatant.h"
#include "battle/rewards_data.h"
#include "battle/setup.h"
#include "original_effects.h"
#include "ram.h"
#include "vars.h"

/* ---- The record growing ------------------------------------------------------------------ */

uint16_t grower_place(Grower who)
{
    if (who.in_reserve)
        return (uint16_t)(RAM_CHARACTER_RECORDS + who.who * RAM_CHARACTER_RECORDS_STRIDE);
    return (uint16_t)(RAM_BATTLE_STATS + who.who * RAM_BATTLE_STATS_STRIDE);
}

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

static uint8_t record_byte(const GameState *g, Grower who, unsigned at)
{
    return who.in_reserve ? character_records_record_byte(g, who.who, at) : battle_stats_block_byte(g, who.who, at);
}
static void set_record_byte(GameState *g, Grower who, unsigned at, uint8_t v)
{
    if (who.in_reserve)
        set_character_records_record_byte(g, who.who, at, v);
    else
        set_battle_stats_block_byte(g, who.who, at, v);
}
static uint16_t record_word(const GameState *g, Grower who, unsigned at)
{
    return (uint16_t)(record_byte(g, who, at) | record_byte(g, who, at + 1) << 8);
}
static void set_record_word(GameState *g, Grower who, unsigned at, uint16_t v)
{
    set_record_byte(g, who, at, (uint8_t)v);
    set_record_byte(g, who, at + 1, (uint8_t)(v >> 8));
}

static uint8_t character_of(const GameState *g, Grower who) { return record_byte(g, who, STAT_ID); }
static uint8_t level_of(const GameState *g, Grower who) { return record_byte(g, who, STAT_LEVEL); }

/* The level-up works on the record where the original keeps its place, with
 * the other two members' after it, in turn. */
static void note_record_growing(GameState *g, Grower who, uint8_t member)
{
    set_battle_rewards_member(g, member);
    ORIGINAL_EFFECT(set_battle_growth_record_places(g, 0, grower_place(who)));
    if (who.in_reserve)
        return;
    for (unsigned n = 1; n < BATTLE_GROWTH_RECORD_PLACES_COUNT; n++)
        ORIGINAL_EFFECT(set_battle_growth_record_places(g, n, grower_place(grower_member((uint8_t)((member + n) % BATTLE_PARTY_SIZE)))));
}

/* ---- Experience -------------------------------------------------------------------------- */

/* The most experience a record holds, 9,999,999, which the original checks a
 * byte and a word at a time: a high byte past its most is brought down to it,
 * and the low word then only where it is past its own. */
enum { MOST_EXPERIENCE_HIGH = 0x98, MOST_EXPERIENCE_LOW = 0x967F };

// $C1:F908
void rewards_gain_experience(GameState *g, Grower who, uint16_t amount)
{
    uint32_t low = (uint32_t)record_word(g, who, STAT_EXPERIENCE) + amount;
    uint8_t high = record_byte(g, who, STAT_EXPERIENCE + 2);
    if (low > 0xFFFF)
        high++;
    set_record_word(g, who, STAT_EXPERIENCE, (uint16_t)low);
    set_record_byte(g, who, STAT_EXPERIENCE + 2, high);
    if (high < MOST_EXPERIENCE_HIGH)
        return;
    set_record_byte(g, who, STAT_EXPERIENCE + 2, MOST_EXPERIENCE_HIGH);
    if ((uint16_t)low > MOST_EXPERIENCE_LOW)
        set_record_word(g, who, STAT_EXPERIENCE, MOST_EXPERIENCE_LOW);
}

/* ---- A level ----------------------------------------------------------------------------- */

enum { TOP_LEVEL = 99 };

/* What the level-up leaves for the original's code: whether a level was
 * reached (battle_growth_no_level). */
enum { LEVEL_REACHED = 0x00, NO_LEVEL = 0xFF };

/* A level gained is counted for the messages as a word: a count at its most
 * carries into the next byte. */
static void count_level_gained(GameState *g, uint8_t member)
{
    uint8_t count = (uint8_t)(battle_rewards_levels_gained_unchecked(g, member) + 1);
    set_battle_rewards_levels_gained_unchecked(g, member, count);
    if (count == 0)
        set_battle_rewards_levels_gained_unchecked(g, member + 1u, (uint8_t)(battle_rewards_levels_gained_unchecked(g, member + 1u) + 1));
}

enum { NOTED_LEVEL_GAINED = 0x80 };

// $C1:F7B9
/* The experience left to share out pays for the record's next level where it
 * reaches it, or goes towards it. A record knocked out takes none; one at the
 * top level takes it all. True where a level was reached. */
bool rewards_reach_level(GameState *g, Grower who, uint8_t member)
{
    bool reached = false;
    if (!(record_byte(g, who, STAT_CONDITION) & CONDITION_KNOCKED_OUT)) {
        if (level_of(g, who) >= TOP_LEVEL) {
            set_battle_experience_won(g, 0);
            set_battle_reward_flags(g, battle_reward_flags(g) | REWARD_EXPERIENCE);
            ORIGINAL_EFFECT(set_battle_growth_no_level(g, NO_LEVEL));
            return false;
        }
        set_battle_reward_flags(g, battle_reward_flags(g) | REWARD_EXPERIENCE);
        uint16_t won = battle_experience_won(g), left = record_word(g, who, STAT_EXPERIENCE_LEFT);
        if (won >= left) {
            reached = true;
            set_battle_experience_won(g, (uint16_t)(won - left));
            uint8_t level = (uint8_t)(level_of(g, who) + 1);
            set_battle_rewards_noted(g, member, battle_rewards_noted(g, member) | NOTED_LEVEL_GAINED);
            if (level > TOP_LEVEL)
                level = TOP_LEVEL;
            set_record_byte(g, who, STAT_LEVEL, level);
            set_record_word(g, who, STAT_EXPERIENCE_LEFT, experience_for_next_level(g, level));
            count_level_gained(g, member);
        } else {
            set_record_word(g, who, STAT_EXPERIENCE_LEFT, (uint16_t)(left - won));
            set_battle_experience_won(g, 0);
        }
    }
    if (level_of(g, who) >= TOP_LEVEL)
        set_record_word(g, who, STAT_EXPERIENCE_LEFT, 0);
    ORIGINAL_EFFECT(set_battle_growth_no_level(g, reached ? LEVEL_REACHED : NO_LEVEL));
    return reached;
}

/* The most HP and MP a level brings them to. */
enum { MOST_MAX_HP = 999, MOST_MAX_MP = 99 };

/* The first of the character's steps that reaches the level: the gain a
 * level up to there brings. The original counts its place in the steps' bytes
 * in a cell, and the step's in another, which it leaves behind. */
enum { STEP_AT = 0x10, STEP_PLACE_AT = 0x00 };

static uint8_t growth_step_gain(GameState *g, uint8_t character, uint8_t level,
                                GrowthStep (*step)(const GameState *, uint8_t, uint8_t))
{
    uint8_t at = 0;
    while (step(g, character, at).up_to_level < level)
        at = (uint8_t)(at + GROWTH_STEP_BYTES);
    ORIGINAL_EFFECT(battle_leave_word(g, STEP_PLACE_AT, growth_step_place(character, at)));
    ORIGINAL_EFFECT(battle_leave_word(g, STEP_AT, at));
    return step(g, character, at).gain;
}

// $C1:F871
/* A level grows the record's maximum HP and MP by its character's steps.
 * True where the MP reached its most. */
bool rewards_grow_hp_and_mp(GameState *g, Grower who)
{
    uint8_t character = character_of(g, who), level = level_of(g, who);
    uint16_t hp = (uint16_t)(record_word(g, who, STAT_MAX_HP) + growth_step_gain(g, character, level, hp_growth_step));
    set_record_word(g, who, STAT_MAX_HP, hp >= MOST_MAX_HP ? MOST_MAX_HP : hp);
    uint16_t mp = (uint16_t)(record_word(g, who, STAT_MAX_MP) + growth_step_gain(g, character, level, mp_growth_step));
    set_record_word(g, who, STAT_MAX_MP, mp >= MOST_MAX_MP ? MOST_MAX_MP : mp);
    return mp >= MOST_MAX_MP;
}

/* A record's stats, from STAT_BASE_STATS, and their values at the first
 * level, from STAT_FIRST_LEVEL_STATS in the growth rates' order, which is not
 * theirs. Speed doesn't grow. */
typedef enum { POWER, STAMINA, SPEED, MAGIC, HIT, EVADE, MAGIC_DEFENCE, STATS } Stat;

static const struct {
    uint8_t rate; /* its growth rate's place among the character's, and its first-level value's */
    Stat stat;
} stats_grown[] = {
    { 0, POWER }, { 1, STAMINA }, { 3, HIT }, { 4, EVADE }, { 5, MAGIC }, { 6, MAGIC_DEFENCE },
};

enum { MOST_STAT = 99, MOST_SPEED = 50 };

/* In hundredths. */
enum { GROWTH_RATE_SCALE = 100 };

/* A stat is its value at the first level, and its rate for each level past
 * it; its hundredths dropped, and a byte's worth. */
static uint8_t grown_stat(GameState *g, Grower who, uint8_t character, uint8_t rate_at)
{
    uint8_t levels = (uint8_t)(level_of(g, who) - 1);
    uint32_t product = battle_multiply_words(g, stat_growth_rate(g, character, rate_at), levels);
    BattleLongQuotient growth = battle_divide_leftover(g, (uint16_t)product, GROWTH_RATE_SCALE);
    uint8_t first = record_byte(g, who, STAT_FIRST_LEVEL_STATS + rate_at);
    battle_set_wide_math_first(g, first);
    return (uint8_t)(first + (uint8_t)growth.quotient);
}

static void grow_stats(GameState *g, Grower who)
{
    uint8_t character = character_of(g, who);
    /* the original finds the rates by multiplying, and keeps their place */
    battle_leave_multiply(g, character, STAT_GROWTH_RATES);
    ORIGINAL_EFFECT(set_battle_growth_rates_place(g, stat_growth_rates_place(character)));
    for (unsigned n = 0; n < sizeof stats_grown / sizeof stats_grown[0]; n++)
        set_record_byte(g, who, STAT_BASE_STATS + stats_grown[n].stat, grown_stat(g, who, character, stats_grown[n].rate));
    for (unsigned stat = 0; stat < STATS; stat++)
        if (record_byte(g, who, STAT_BASE_STATS + stat) >= MOST_STAT)
            set_record_byte(g, who, STAT_BASE_STATS + stat, MOST_STAT);
    if (record_byte(g, who, STAT_BASE_STATS + SPEED) > MOST_SPEED)
        set_record_byte(g, who, STAT_BASE_STATS + SPEED, MOST_SPEED);
}

// $C1:F623
/* The next level, where the experience left reaches it, and what it grows. */
void rewards_level_up(GameState *g, Grower who, uint8_t member)
{
    if (!rewards_reach_level(g, who, member))
        return;
    rewards_grow_hp_and_mp(g, who);
    grow_stats(g, who);
}

/* ---- Sharing it out ---------------------------------------------------------------------- */

/* A member whose id in the fight has its top bit set is out of it. */
enum { OUT_OF_FIGHT = 0x80 };

// $C1:F93E
void battle_share_experience(GameState *g)
{
    uint16_t won = battle_experience_won(g);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_experience_won(g, won);
        set_battle_rewards_levels_gained(g, member, 0);
        if (battle_combatant_fighting(g, member) & OUT_OF_FIGHT)
            continue;
        Grower who = grower_member(member);
        rewards_gain_experience(g, who, won);
        do {
            note_record_growing(g, who, member);
            rewards_level_up(g, who, member);
        } while (battle_experience_won(g) != 0);
    }
    set_battle_experience_won(g, won);
    rewards_learn_techs(g);
    rewards_share_with_reserve(g);
}

/* A character in reserve takes fifteen twentieths of the experience. */
enum { RESERVE_SHARE = 15, RESERVE_SHARE_OF = 20 };

/* The original counts the characters in a cell as it goes. */
enum { CHARACTER_AT = 0x02 };

/* The characters who have joined, a bit each from Crono's at the top; a
 * character with no slot in the fight has its slot's top bit set. */
enum { JOINED_FIRST = 0x80, NO_SLOT = 0x80 };

static bool in_reserve(const GameState *g, uint8_t joined, uint8_t character)
{
    return (joined & JOINED_FIRST) && (battle_character_slot(g, character) & NO_SLOT);
}

// $C1:F9EB
/* Each character who has joined the party but isn't in the fight gains a
 * share of the experience, and a level at most. */
void rewards_share_with_reserve(GameState *g)
{
    set_battle_rewards_member(g, REWARDS_RESERVE);
    /* the original clears a word, the next byte with it */
    set_battle_status_script_seen(g, 0, 0);
    uint8_t joined = party_characters_joined(g);
    for (uint8_t character = 0; character < SETUP_CHARACTERS; character++, joined = (uint8_t)(joined << 1)) {
        ORIGINAL_EFFECT(set_battle_reserve_bits(g, joined));
        ORIGINAL_EFFECT(battle_leave_word(g, CHARACTER_AT, character));
        if (!in_reserve(g, joined, character))
            continue;
        Grower who = grower_in_reserve(character);
        /* the original finds the record by multiplying */
        battle_leave_multiply(g, RAM_CHARACTER_RECORDS_STRIDE, character);
        ORIGINAL_EFFECT(set_battle_growth_record_places(g, 0, grower_place(who)));
        uint16_t won = battle_experience_won(g);
        uint32_t scaled = battle_multiply_words(g, won, RESERVE_SHARE);
        uint16_t share = (uint16_t)battle_divide_leftover(g, (uint16_t)scaled, RESERVE_SHARE_OF).quotient;
        set_battle_experience_won(g, share);
        rewards_gain_experience(g, who, share);
        rewards_level_up(g, who, REWARDS_RESERVE);
        set_battle_experience_won(g, won);
    }
    ORIGINAL_EFFECT(set_battle_reserve_bits(g, joined));
    ORIGINAL_EFFECT(battle_leave_word(g, CHARACTER_AT, SETUP_CHARACTERS));
}

/* ---- Gold instead ------------------------------------------------------------------------ */

// $FD:B201
void battle_turn_experience_to_gold(GameState *g)
{
    if (battle_members_turning_experience_to_gold(g) == 0)
        return;
    set_battle_gold_won(g, (uint16_t)(battle_gold_won(g) + battle_experience_won(g)));
    set_battle_experience_won(g, 0);
    set_battle_reward_flags(g, (uint8_t)((battle_reward_flags(g) & ~REWARD_EXPERIENCE) | REWARD_GOLD));
}
