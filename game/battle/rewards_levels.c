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
 * gained (battle_rewards_levels_gained, battle_rewards_levels_noted), the
 * techs learned. A party that wears the accessory that turns the experience
 * into gold gets gold instead, and no levels.
 *
 * A member's record and a reserve character's lay out their level, experience
 * and stats alike, so a level-up grows either (Grower). */
#include "battle/rewards.h"

#include "battle/arith.h"
#include "battle/rewards_data.h"
#include "battle/rewards_work.h"
#include "battle/setup.h"
#include "charge.h"
#include "original_effects.h"

/* ---- Experience -------------------------------------------------------------------------- */

/* The most experience a record holds, 9,999,999, as the original brings a
 * total down to it: where the total's high byte is past the most's, the high
 * byte comes down to the most's, and the low word only where it is past the
 * most's own -- so a total over the most with a low word under it keeps that
 * low word. */
enum { MOST_EXPERIENCE = 0x98967F };

static uint32_t capped_experience(uint32_t total)
{
    if (total >> 16 < MOST_EXPERIENCE >> 16)
        return total;
    uint16_t low = (uint16_t)total;
    return (MOST_EXPERIENCE & 0xFF0000) | (low > (uint16_t)MOST_EXPERIENCE ? (uint16_t)MOST_EXPERIENCE : low);
}

// $C1:F908
void rewards_gain_experience(GameState *g, Grower who, uint16_t amount)
{
    uint32_t total = (record_long(g, who, STAT_EXPERIENCE) + amount) & 0xFFFFFF;
    set_record_long(g, who, STAT_EXPERIENCE, capped_experience(total));
}

/* ---- A level ----------------------------------------------------------------------------- */

/* What the level-up leaves for the original's code: whether a level was
 * reached (battle_growth_no_level). */
enum { LEVEL_REACHED = 0x00, NO_LEVEL = 0xFF };

/* A level gained is counted for the messages. The original counts it as a
 * word, so a count at its most carries into the next byte: the reserve's
 * count is kept from battle to battle, so it gets there. */
static void count_level_gained(GameState *g, uint8_t member)
{
    uint8_t count = (uint8_t)(battle_rewards_levels_gained_unchecked(g, member) + 1);
    set_battle_rewards_levels_gained_unchecked(g, member, count);
    if (count == 0)
        set_battle_rewards_levels_gained_unchecked(g, member + 1u, (uint8_t)(battle_rewards_levels_gained_unchecked(g, member + 1u) + 1));
}

enum { NOTED_LEVEL_GAINED = 0x80 };

static bool knocked_out(const GameState *g, Grower who) { return (record_byte(g, who, STAT_CONDITION) & CONDITION_KNOCKED_OUT) != 0; }

// $C1:F7B9
/* The experience in the pot pays for the record's next level where it reaches
 * it, or goes towards it. A record knocked out takes none; one at the top
 * level takes the rest, its count towards the next left as it is. True where
 * a level was reached, noted for `member`. */
bool rewards_reach_level(GameState *g, Grower who, uint8_t member, uint16_t *pot)
{
    if (!knocked_out(g, who)) {
        set_battle_reward_flags(g, battle_reward_flags(g) | REWARD_EXPERIENCE);
        if (grower_level(g, who) >= TOP_LEVEL) {
            *pot = 0;
            ORIGINAL_EFFECT(set_battle_growth_no_level(g, NO_LEVEL));
            return false;
        }
    }
    bool reached = false;
    if (!knocked_out(g, who)) {
        uint16_t left = record_word(g, who, STAT_EXPERIENCE_LEFT);
        reached = *pot >= left;
        if (reached) {
            *pot = (uint16_t)(*pot - left);
            uint8_t level = (uint8_t)(grower_level(g, who) + 1);
            set_record_byte(g, who, STAT_LEVEL, level);
            set_record_word(g, who, STAT_EXPERIENCE_LEFT, experience_for_next_level(g, level));
            set_battle_rewards_levels_noted(g, member, battle_rewards_levels_noted(g, member) | NOTED_LEVEL_GAINED);
            count_level_gained(g, member);
        } else {
            set_record_word(g, who, STAT_EXPERIENCE_LEFT, (uint16_t)(left - *pot));
            *pot = 0;
        }
    }
    if (grower_level(g, who) >= TOP_LEVEL)
        set_record_word(g, who, STAT_EXPERIENCE_LEFT, 0);
    ORIGINAL_EFFECT(set_battle_growth_no_level(g, reached ? LEVEL_REACHED : NO_LEVEL));
    return reached;
}

/* The most HP and MP a level brings them to. */
enum { MOST_MAX_HP = 999, MOST_MAX_MP = 99 };

/* The first of the character's steps that goes up to the level. */
static uint8_t growth_step_for(const GameState *g, uint8_t character, uint8_t level,
                               GrowthStep (*step)(const GameState *, uint8_t, uint8_t))
{
    uint8_t n = 0;
    while (step(g, character, n).up_to_level < level)
        n++;
    return n;
}

/* The original finds the step by its place in the character's bytes, and
 * leaves the place and its count of the steps' bytes behind. */
static void leave_step_found(GameState *g, uint8_t character, uint8_t step)
{
    ORIGINAL_EFFECT(battle_leave_word(g, WORK_BLOCK_AT, growth_step_place(character, step)));
    ORIGINAL_EFFECT(battle_leave_word(g, WORK_LISTED_AT, (uint8_t)(step * GROWTH_STEP_BYTES)));
}

// $C1:F871
/* A level grows the record's maximum HP and MP by its character's steps. */
void rewards_grow_hp_and_mp(GameState *g, Grower who)
{
    uint8_t character = grower_character(g, who), level = grower_level(g, who);
    uint8_t hp_step = growth_step_for(g, character, level, hp_growth_step);
    uint16_t hp = (uint16_t)(record_word(g, who, STAT_MAX_HP) + hp_growth_step(g, character, hp_step).gain);
    set_record_word(g, who, STAT_MAX_HP, hp >= MOST_MAX_HP ? MOST_MAX_HP : hp);
    uint8_t mp_step = growth_step_for(g, character, level, mp_growth_step);
    uint16_t mp = (uint16_t)(record_word(g, who, STAT_MAX_MP) + mp_growth_step(g, character, mp_step).gain);
    set_record_word(g, who, STAT_MAX_MP, mp >= MOST_MAX_MP ? MOST_MAX_MP : mp);
    leave_step_found(g, character, mp_step);
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

/* The battle's shared multiply and divide, which the original calls: its
 * divide goes bit by bit, far longer than the port's, so their work is timed
 * as theirs. */
enum { MULTIPLY_WORDS = 0xC1C90B, DIVIDE_LONG = 0xC1C92A };

/* A stat is its value at the first level, and its rate for each level past
 * it; its hundredths dropped, and a byte's worth. The arithmetic is the
 * battle's shared multiply and divide, whose cells the next caller finds; the
 * original loads the first-level value into the first number's cell as it
 * adds. */
static uint8_t grown_stat(GameState *g, Grower who, uint8_t character, uint8_t rate_at)
{
    uint8_t levels = (uint8_t)(grower_level(g, who) - 1);
    uint32_t product;
    BattleLongQuotient growth;
    TIMED_AS(g, MULTIPLY_WORDS, product = battle_multiply_words(g, stat_growth_rate(g, character, rate_at), levels));
    TIMED_AS(g, DIVIDE_LONG, growth = battle_divide_leftover(g, (uint16_t)product, GROWTH_RATE_SCALE));
    uint8_t first = record_byte(g, who, STAT_FIRST_LEVEL_STATS + rate_at);
    battle_set_wide_math_first(g, first);
    return (uint8_t)(first + (uint8_t)growth.quotient);
}

static void grow_stats(GameState *g, Grower who)
{
    uint8_t character = grower_character(g, who);
    /* the original finds the rates by multiplying, and keeps their place */
    TIMED_AS(g, MULTIPLY_WORDS, battle_leave_multiply(g, character, STAT_GROWTH_RATES));
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
/* The next level, where the pot reaches it, and what it grows. */
void rewards_level_up(GameState *g, Grower who, uint8_t member, uint16_t *pot)
{
    bool reached;
    TIMED_AS(g, 0xC1F7B9, reached = rewards_reach_level(g, who, member, pot));
    if (!reached)
        return;
    TIMED_AS(g, 0xC1F871, rewards_grow_hp_and_mp(g, who));
    grow_stats(g, who);
}

/* ---- Sharing it out ---------------------------------------------------------------------- */

/* The original keeps where the record growing is, with the other two
 * members' blocks after it. */
static void leave_record_growing(GameState *g, Grower who)
{
    ORIGINAL_EFFECT(set_battle_growth_record_places(g, 0, grower_place(who)));
    if (who.in_reserve)
        return;
    for (unsigned n = 1; n < BATTLE_GROWTH_RECORD_PLACES_COUNT; n++)
        ORIGINAL_EFFECT(set_battle_growth_record_places(g, n, grower_place(grower_member((uint8_t)((who.who + n) % BATTLE_PARTY_SIZE)))));
}

// $C1:F93E
void battle_share_experience(GameState *g)
{
    uint16_t won = battle_experience_won(g);
    bool grown = false;
    Grower last = grower_member(0);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_rewards_levels_gained(g, member, 0);
        if (battle_combatant_fighting(g, member) & OUT_OF_FIGHT)
            continue;
        Grower who = grower_member(member);
        TIMED_AS(g, 0xC1F908, rewards_gain_experience(g, who, won));
        /* one knocked out takes none, which the original offers it for ever
         * (the game never leaves one in the fight) */
        uint16_t pot = won;
        do
            TIMED_AS(g, 0xC1F623, rewards_level_up(g, who, member, &pot));
        while (pot != 0 && !knocked_out(g, who));
        grown = true;
        last = who;
    }
    if (grown)
        leave_record_growing(g, last);
    TIMED_AS(g, 0xC1F205, rewards_learn_techs(g));
    TIMED_AS(g, 0xC1F9EB, rewards_share_with_reserve(g));
}

/* A character in reserve takes fifteen twentieths of the experience. */
enum { RESERVE_SHARE = 15, RESERVE_SHARE_OF = 20 };

// $C1:F9EB
/* Each character who has joined the party but isn't in the fight gains a
 * share of the experience, and a level at most. */
void rewards_share_with_reserve(GameState *g)
{
    set_battle_rewards_member(g, REWARDS_RESERVE);
    /* the original clears the byte after its own cell with it: the first
     * combatant's status script seen, which the next fight's start clears
     * again (TODO.md) */
    set_battle_status_script_seen(g, 0, 0);
    bool shared = false;
    uint8_t last = 0;
    for (uint8_t character = 0; character < SETUP_CHARACTERS; character++) {
        if (!character_in_reserve(g, character))
            continue;
        Grower who = grower_in_reserve(character);
        /* the original finds the record by multiplying */
        TIMED_AS(g, MULTIPLY_WORDS, battle_leave_multiply(g, RAM_CHARACTER_RECORDS_STRIDE, character));
        uint32_t scaled;
        uint16_t share;
        TIMED_AS(g, MULTIPLY_WORDS, scaled = battle_multiply_words(g, battle_experience_won(g), RESERVE_SHARE));
        TIMED_AS(g, DIVIDE_LONG, share = (uint16_t)battle_divide_leftover(g, (uint16_t)scaled, RESERVE_SHARE_OF).quotient);
        TIMED_AS(g, 0xC1F908, rewards_gain_experience(g, who, share));
        TIMED_AS(g, 0xC1F623, rewards_level_up(g, who, REWARDS_RESERVE, &share));
        shared = true;
        last = character;
    }
    if (shared)
        leave_record_growing(g, grower_in_reserve(last));
    /* the original shifts the joined bits through a cell of its own as it
     * counts the characters in another */
    ORIGINAL_EFFECT(set_battle_reserve_bits(g, (uint8_t)(party_characters_joined(g) << SETUP_CHARACTERS)));
    ORIGINAL_EFFECT(battle_leave_word(g, WORK_KNOWN_AT, SETUP_CHARACTERS));
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
