/* Battle: the techs a won battle's tech points teach.
 *
 * A member still fighting that hasn't learned all its single techs puts the
 * tech points the enemies gave (battle_tech_points_won) towards its next:
 * the points it lacks for it are paid off, and where they reach it, it learns
 * it, and what is left goes on towards the one after. A locked tech waits
 * until the character's techs are unlocked. The points are then added to
 * the member's total, up to 9,999,999.
 *
 * Then the combination techs: each pair of the members and the three of them
 * have a record of techs (rewards_data.h), and they learn each one they
 * haven't that isn't locked, once each of them knows as many single techs as
 * it asks of them.
 *
 * What was learned is noted for the rewards' messages ($FD:AD17, the
 * original's): the single techs in the members' eight places of
 * battle_rewards_techs, the double techs in battle_rewards_double_techs,
 * the triple tech in battle_rewards_triple_tech, and each kind in the
 * member's byte of battle_rewards_noted -- for a combination, the last member
 * whose single techs were looked at. */
#include "battle/rewards.h"

#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/combatant.h"
#include "battle/effect_data.h"
#include "battle/rewards_data.h"
#include "battle/setup.h"
#include "original_effects.h"
#include "ram.h"
#include "vars.h"

/* The member's second byte of battle_rewards_noted, and what it notes. */
enum { NOTED_TECHS = BATTLE_PARTY_SIZE };
enum { NOTED_SINGLE_TECHS = 0x08, NOTED_DOUBLE_TECHS = 0x04, NOTED_TRIPLE_TECH = 0x02 };

static void note_learned(GameState *g, uint8_t member, uint8_t what)
{
    set_battle_rewards_noted(g, NOTED_TECHS + member, battle_rewards_noted(g, NOTED_TECHS + member) | what);
}

/* A learned bit, the first of eight at the top; an eighth or later has none. */
enum { BITS = 8 };
static uint8_t learned_bit(uint8_t n) { return n < BITS ? (uint8_t)(0x80 >> n) : 0; }

/* The cells where the original keeps what it works on. */
enum {
    MEMBER_BLOCK_AT = 0x00,  /* the member's stat block; a combination's learned byte */
    KNOWN_AT = 0x02,         /* the character's count of single techs known; the combination record */
    TECHS_AT = 0x04,         /* the character's techs' record; the combination's asks */
    CHARACTERS_AT = 0x08,    /* a combination record's characters */
    BIT_AT = 0x0A,           /* a learned bit; the character's costs */
    SHIFTING_AT = 0x0C,      /* the combination's characters, shifted through */
    KNOWN_TESTED_AT = 0x0E,  /* the count of single techs tested */
    LISTED_AT = 0x10,        /* the combination's characters listed; which is tested */
    CHARACTER_AT = 0x12,     /* the character looked at */
};

static uint16_t known_place(uint8_t character) { return (uint16_t)(RAM_PARTY_SINGLE_TECHS_KNOWN + character); }

/* ---- Single techs ----------------------------------------------------------------------- */

static uint16_t points_left(const GameState *g, uint8_t member)
{
    return (uint16_t)(battle_stats_block_byte(g, member, STAT_TECH_POINTS_LEFT)
                      | battle_stats_block_byte(g, member, STAT_TECH_POINTS_LEFT + 1) << 8);
}
static void set_points_left(GameState *g, uint8_t member, uint16_t v)
{
    set_battle_stats_block_byte(g, member, STAT_TECH_POINTS_LEFT, (uint8_t)v);
    set_battle_stats_block_byte(g, member, STAT_TECH_POINTS_LEFT + 1, (uint8_t)(v >> 8));
}

static bool all_learned(const GameState *g, uint8_t member)
{
    return (battle_stats_block_byte(g, member, STAT_NOTES) & STAT_NOTE_TECHS_ALL_LEARNED) != 0;
}

/* Where a locked tech waits for its character's bit, Crono's the lowest. */
static uint8_t unlock_bit(uint8_t character) { return character < BITS ? (uint8_t)(1u << character) : 0; }

/* What the original leaves in battle_tech_learned. */
enum { TECH_REACHED = 0x00, TECH_NOT_REACHED = 0xFF };

// $C1:F45F
/* The points go towards the member's next single tech, unless it is locked.
 * True where they reach it, the points left over then in its count. */
bool rewards_reach_next_tech(GameState *g, uint8_t member, uint8_t character, uint16_t points)
{
    CharacterTechs techs = character_techs(g, character);
    ORIGINAL_EFFECT(set_battle_learner_first_tech(g, techs.first));
    unsigned next = party_single_techs_known(g, character) + techs.first;
    uint8_t tech = (uint8_t)next;
    /* the original finds the tech's record by multiplying */
    battle_leave_multiply(g, tech, PARTY_TECH_BYTES);
    battle_leave_multiply_carry(g, next > 0xFF);
    if (party_tech_locked(g, tech) && !(party_techs_unlocked(g) & unlock_bit(character)))
        return false;
    set_battle_reward_flags(g, battle_reward_flags(g) | REWARD_TECH_POINTS);
    uint16_t left = points_left(g, member);
    if (left > points) {
        set_points_left(g, member, (uint16_t)(left - points));
        return false;
    }
    ORIGINAL_EFFECT(set_battle_tech_learned(g, TECH_REACHED));
    set_points_left(g, member, (uint16_t)(points - left));
    return true;
}

// $C1:F4DF
/* The member learns its next single tech: its learned bit, a place of its
 * eight in the messages' list, and all learned once it knows as many as it
 * has. `first` is its character's first. */
void rewards_learn_single_tech(GameState *g, uint8_t member, uint8_t character, uint8_t first)
{
    uint8_t known = party_single_techs_known(g, character);
    ORIGINAL_EFFECT(battle_leave_byte(g, BIT_AT, learned_bit(known)));
    set_party_single_techs_learned(g, character, party_single_techs_learned(g, character) | learned_bit(known));
    note_learned(g, member, NOTED_SINGLE_TECHS);
    uint16_t at = battle_rewards_tech_at(g);
    set_battle_rewards_techs(g, at, (uint8_t)(known + first));
    set_battle_rewards_tech_at(g, (uint16_t)((at & 0xFF00) | (uint8_t)(at + 1)));
    known++;
    set_party_single_techs_known(g, character, known);
    if (known >= character_techs(g, character).count)
        set_battle_stats_block_byte(g, member, STAT_NOTES, battle_stats_block_byte(g, member, STAT_NOTES) | STAT_NOTE_TECHS_ALL_LEARNED);
}

/* The most tech points a member holds, 9,999,999, which the original checks a
 * word and then a byte at a time: only where both are at their most or past
 * it is the total brought down. */
enum { MOST_POINTS_LOW = 0x967F, MOST_POINTS_HIGH = 0x98 };

static void add_tech_points(GameState *g, uint8_t member, uint16_t points)
{
    uint32_t low = (uint32_t)(battle_stats_block_byte(g, member, STAT_TECH_POINTS)
                              | battle_stats_block_byte(g, member, STAT_TECH_POINTS + 1) << 8) + points;
    set_battle_stats_block_byte(g, member, STAT_TECH_POINTS, (uint8_t)low);
    set_battle_stats_block_byte(g, member, STAT_TECH_POINTS + 1, (uint8_t)(low >> 8));
    if (low > 0xFFFF)
        set_battle_stats_block_byte(g, member, STAT_TECH_POINTS + 2, (uint8_t)(battle_stats_block_byte(g, member, STAT_TECH_POINTS + 2) + 1));
    if ((uint16_t)low < MOST_POINTS_LOW || battle_stats_block_byte(g, member, STAT_TECH_POINTS + 2) < MOST_POINTS_HIGH)
        return;
    set_battle_stats_block_byte(g, member, STAT_TECH_POINTS, (uint8_t)MOST_POINTS_LOW);
    set_battle_stats_block_byte(g, member, STAT_TECH_POINTS + 1, (uint8_t)(MOST_POINTS_LOW >> 8));
    set_battle_stats_block_byte(g, member, STAT_TECH_POINTS + 2, MOST_POINTS_HIGH);
}

/* The cell where the original keeps the battle's tech points while it spends
 * them. */
enum { POINTS_KEPT_AT = 8 };

// $C1:F39A
/* The battle's tech points towards the member's single techs, as many as they
 * reach. Where a tech is learned, the points it left over go on to the next,
 * whose cost is its count now; where they don't reach it, the next is paid
 * the battle's points again, as the original does. */
void rewards_count_tech_points(GameState *g, uint8_t member, uint8_t character)
{
    uint16_t won = battle_tech_points_won_low(g), points = won;
    ORIGINAL_EFFECT(set_battle_wide_math(g, POINTS_KEPT_AT, (uint8_t)won));
    ORIGINAL_EFFECT(set_battle_wide_math(g, POINTS_KEPT_AT + 1, (uint8_t)(won >> 8)));
    for (;;) {
        ORIGINAL_EFFECT(set_battle_tech_learned(g, TECH_NOT_REACHED));
        if (!rewards_reach_next_tech(g, member, character, points))
            break;
        ORIGINAL_EFFECT(battle_leave_word(g, KNOWN_AT, known_place(character)));
        ORIGINAL_EFFECT(battle_leave_word(g, TECHS_AT, character_techs_place(character)));
        if (all_learned(g, member))
            break;
        rewards_learn_single_tech(g, member, character, character_techs(g, character).first);
        ORIGINAL_EFFECT(battle_leave_word(g, BIT_AT, (uint16_t)(character * 16)));
        uint16_t cost = tech_points_for_next(g, character, party_single_techs_known(g, character));
        uint16_t over = points_left(g, member);
        if (cost < over) {
            battle_set_wide_math_first(g, cost);
            points = over;
            set_points_left(g, member, cost);
        } else {
            set_points_left(g, member, (uint16_t)(cost - over));
        }
    }
    add_tech_points(g, member, won);
}

/* ---- Combination techs ------------------------------------------------------------------ */

/* The learned bits of a combination record's techs: the double techs' bytes,
 * then the triple techs' after them. */
static uint8_t combination_learned(const GameState *g, uint8_t record)
{
    if (record < PARTY_DOUBLE_TECHS_LEARNED_COUNT)
        return party_double_techs_learned(g, record);
    return party_triple_techs_learned(g, (unsigned)(record - PARTY_DOUBLE_TECHS_LEARNED_COUNT));
}
static void set_combination_learned(GameState *g, uint8_t record, uint8_t v)
{
    if (record < PARTY_DOUBLE_TECHS_LEARNED_COUNT)
        set_party_double_techs_learned(g, record, v);
    else
        set_party_triple_techs_learned(g, (unsigned)(record - PARTY_DOUBLE_TECHS_LEARNED_COUNT), v);
}
static uint16_t combination_learned_place(uint8_t record) { return (uint16_t)(RAM_PARTY_DOUBLE_TECHS_LEARNED + record); }

/* No tech taught, in the messages' lists. */
enum { NO_TECH = 0xFF };

/* A combination of two characters, and of three. */
enum { DOUBLE = 2, TRIPLE = 3 };

/* The combination's characters in order, a byte past the last. */
enum { NO_MORE_CHARACTERS = 0xFF };

// $C1:F533
/* The combination learns each of its techs it hasn't that isn't locked, where
 * each of its characters knows as many single techs as the tech asks of it;
 * noted for `member`. */
void rewards_learn_combination_techs(GameState *g, Combination c, uint8_t member)
{
    uint8_t known[SETUP_CHARACTERS];
    uint8_t listed = 0, characters = c.characters;
    ORIGINAL_EFFECT(battle_leave_word(g, CHARACTER_AT, SETUP_CHARACTERS));
    for (uint8_t character = 0; character < SETUP_CHARACTERS; character++, characters = (uint8_t)(characters << 1))
        if (characters & 0x80) {
            known[listed] = party_single_techs_known(g, character);
            ORIGINAL_EFFECT(set_battle_combination_techs_known(g, listed, known[listed]));
            listed++;
        }
    ORIGINAL_EFFECT(set_battle_combination_techs_known(g, listed, NO_MORE_CHARACTERS));
    ORIGINAL_EFFECT(battle_leave_byte(g, SHIFTING_AT, characters));
    ORIGINAL_EFFECT(battle_leave_word(g, LISTED_AT, listed));

    uint8_t bit = 0x80, tech = c.first, left = c.count;
    uint16_t asks = c.asks;
    do {
        if (!(combination_learned(g, c.record) & bit)) {
            ORIGINAL_EFFECT(battle_leave_word(g, KNOWN_TESTED_AT, 0));
            ORIGINAL_EFFECT(set_battle_combination_short(g, 0));
            /* a count of techs known that is the list's end ends it too */
            uint8_t n = 0;
            bool short_of_techs = false;
            for (; n < listed && known[n] != NO_MORE_CHARACTERS; n++) {
                ORIGINAL_EFFECT(battle_leave_byte(g, KNOWN_TESTED_AT, known[n]));
                if (known[n] < combination_tech_asks(g, asks, n)) {
                    short_of_techs = true;
                    break;
                }
            }
            ORIGINAL_EFFECT(battle_leave_word(g, LISTED_AT, n));
            if (short_of_techs) {
                ORIGINAL_EFFECT(set_battle_combination_short(g, 0xFF));
            } else {
                /* the original finds the tech's record by multiplying, its carry
                 * set from the end of the list */
                battle_leave_multiply(g, tech, PARTY_TECH_BYTES);
                battle_leave_multiply_carry(g, true);
                if (!party_tech_locked(g, tech)) {
                    set_combination_learned(g, c.record, combination_learned(g, c.record) | bit);
                    if (n == DOUBLE) {
                        uint8_t at = battle_rewards_double_techs_listed(g);
                        set_battle_rewards_double_techs(g, at, tech);
                        set_battle_rewards_double_techs_listed(g, (uint8_t)(at + 1));
                        note_learned(g, member, NOTED_DOUBLE_TECHS);
                    } else if (n == TRIPLE) {
                        set_battle_rewards_triple_tech(g, tech);
                        note_learned(g, member, NOTED_TRIPLE_TECH);
                    }
                }
            }
        }
        tech++;
        asks = (uint16_t)(asks + COMBINATION_TECH_ASKS);
        bit >>= 1;
        left--;
    } while (left != 0);
    ORIGINAL_EFFECT(set_battle_combination_tech_bit(g, bit));
    ORIGINAL_EFFECT(set_battle_combination_tech(g, tech));
    ORIGINAL_EFFECT(battle_leave_word(g, TECHS_AT, asks));
    ORIGINAL_EFFECT(set_battle_combination_techs_left(g, 0));
}

/* The members' characters, a bit each: sorted, and each pair's and the
 * three's. */
typedef struct {
    uint8_t sets[BATTLE_COMBINATION_MEMBER_SETS_COUNT];
} MemberSets;

static void sort_two(uint8_t *a, uint8_t *b)
{
    if (*a >= *b) {
        uint8_t t = *a;
        *a = *b;
        *b = t;
    }
}

static MemberSets sort_members(GameState *g)
{
    uint8_t bits[BATTLE_PARTY_SIZE] = { 0 };
    uint8_t last = 0x80;
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t character = battle_combatant_fighting(g, member);
        last = 0x80;
        if (character != COMBATANT_NONE)
            bits[member] = last = learned_bit(character);
    }
    ORIGINAL_EFFECT(battle_leave_byte(g, BIT_AT, last));
    sort_two(&bits[0], &bits[1]);
    sort_two(&bits[0], &bits[2]);
    sort_two(&bits[1], &bits[2]);
    ORIGINAL_EFFECT(set_battle_combination_first_bit(g, bits[0]));
    ORIGINAL_EFFECT(set_battle_combination_second_bit(g, bits[1]));
    /* the cell is the reactions' animation kept, which they read as they
     * find it (battle/main_loop.c) */
    set_battle_combination_third_bit(g, bits[2]);
    MemberSets s = { { (uint8_t)(bits[0] | bits[1]), (uint8_t)(bits[0] | bits[2]), (uint8_t)(bits[1] | bits[2]),
                       (uint8_t)(bits[0] | bits[1] | bits[2]) } };
    for (unsigned n = 0; n < BATTLE_COMBINATION_MEMBER_SETS_COUNT; n++)
        ORIGINAL_EFFECT(set_battle_combination_member_sets(g, n, s.sets[n]));
    return s;
}

// $C1:F2DB
void rewards_sort_combination_members(GameState *g) { (void)sort_members(g); }

/* The record of the characters' combination techs, where they have one. */
static bool find_combination(GameState *g, uint8_t characters, Combination *c)
{
    uint16_t place = combination_records_place();
    uint8_t record = 0;
    for (;;) {
        ORIGINAL_EFFECT(battle_leave_word(g, KNOWN_AT, place));
        ORIGINAL_EFFECT(set_battle_combination_record_number(g, record));
        uint8_t these = combination_record_characters(g, place);
        if (these == NO_MORE_COMBINATIONS)
            return false;
        ORIGINAL_EFFECT(battle_leave_byte(g, CHARACTERS_AT, these));
        if (these == characters)
            break;
        place = (uint16_t)(place + COMBINATION_RECORD_BYTES);
        record++;
    }
    CombinationRecord r = combination_record(g, place);
    if (r.first == NO_MORE_COMBINATIONS)
        return false;
    *c = (Combination){ record, r.characters, r.first, r.count, r.asks };
    return true;
}

// $C1:F205
void rewards_learn_techs(GameState *g)
{
    for (unsigned n = 0; n < BATTLE_REWARDS_DOUBLE_TECHS_COUNT; n++)
        set_battle_rewards_double_techs(g, n, NO_TECH);
    set_battle_rewards_triple_tech(g, NO_TECH);
    set_battle_rewards_double_techs_listed(g, 0);

    uint8_t noted = (uint8_t)battle_rewards_member(g);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_rewards_combination(g, member);
        if (battle_combatant_fighting(g, member) == COMBATANT_NONE)
            continue;
        set_battle_rewards_tech_at(g, (uint16_t)(member * BITS));
        set_battle_rewards_member(g, member);
        noted = member;
        ORIGINAL_EFFECT(battle_leave_word(g, MEMBER_BLOCK_AT, (uint16_t)(member * BATTLE_STAT_BLOCK_BYTES)));
        if (all_learned(g, member))
            continue;
        uint8_t character = battle_stats_block_byte(g, member, STAT_ID);
        ORIGINAL_EFFECT(set_battle_tech_learner(g, character));
        rewards_count_tech_points(g, member, character);
    }

    MemberSets s = sort_members(g);
    for (uint8_t set = 0; set < BATTLE_COMBINATION_MEMBER_SETS_COUNT; set++) {
        set_battle_rewards_combination(g, set);
        Combination c;
        if (!find_combination(g, s.sets[set], &c))
            continue;
        ORIGINAL_EFFECT(battle_leave_word(g, MEMBER_BLOCK_AT, combination_learned_place(c.record)));
        ORIGINAL_EFFECT(set_battle_combination_members(g, c.characters));
        ORIGINAL_EFFECT(set_battle_combination_tech(g, c.first));
        ORIGINAL_EFFECT(set_battle_combination_techs_left(g, c.count));
        ORIGINAL_EFFECT(battle_leave_word(g, TECHS_AT, c.asks));
        rewards_learn_combination_techs(g, c, noted);
    }
    /* the cell is the effect's hits left, which the next effect that hits
     * several times reads as it finds it (battle/effect_work.c) */
    set_battle_rewards_combination(g, BATTLE_COMBINATION_MEMBER_SETS_COUNT);
}
