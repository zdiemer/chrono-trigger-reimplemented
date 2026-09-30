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
 * member's battle_rewards_techs_noted -- for a combination, the last member
 * whose single techs were looked at. */
#include "battle/rewards.h"

#include "battle/arith.h"
#include "battle/effect_data.h"
#include "battle/rewards_data.h"
#include "battle/rewards_work.h"
#include "battle/setup.h"
#include "charge.h"
#include "original_effects.h"

/* What a member's techs noted says it learned. */
enum { NOTED_SINGLE_TECHS = 0x08, NOTED_DOUBLE_TECHS = 0x04, NOTED_TRIPLE_TECH = 0x02 };

static void note_learned(GameState *g, uint8_t member, uint8_t what)
{
    set_battle_rewards_techs_noted(g, member, battle_rewards_techs_noted(g, member) | what);
}

/* A member's single techs have eight places in the messages' list. */
enum { PLACES_A_MEMBER = 8 };

/* A learned bit: a character's single techs from its first, a combination's
 * techs from its first, at the top. */
static uint8_t learned_bit(uint8_t n) { return character_bit(n); }

/* ---- Single techs ----------------------------------------------------------------------- */

static uint16_t points_left(const GameState *g, uint8_t member) { return record_word(g, grower_member(member), STAT_TECH_POINTS_LEFT); }
static void set_points_left(GameState *g, uint8_t member, uint16_t v) { set_record_word(g, grower_member(member), STAT_TECH_POINTS_LEFT, v); }

static bool all_learned(const GameState *g, uint8_t member)
{
    return (battle_stats_block_byte(g, member, STAT_NOTES) & STAT_NOTE_TECHS_ALL_LEARNED) != 0;
}

/* Where a locked tech waits for its character's bit, Crono's the lowest. */
static uint8_t unlock_bit(uint8_t character) { return character < 8 ? (uint8_t)(1u << character) : 0; }

/* What the original leaves in battle_tech_learned. */
enum { TECH_REACHED = 0x00, TECH_NOT_REACHED = 0xFF };

/* The original finds the tech's record by multiplying, the carry of its sum
 * of the character's first tech and its count coming in with it. */
enum { MULTIPLY_WORDS = 0xC1C90B };

static void leave_tech_record_found(GameState *g, uint8_t known, uint8_t first)
{
    TIMED_AS(g, MULTIPLY_WORDS, battle_leave_multiply(g, (uint8_t)(known + first), PARTY_TECH_BYTES));
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, known + first > 0xFF));
    ORIGINAL_EFFECT(set_battle_learner_first_tech(g, first));
}

// $C1:F45F
/* The points go towards the member's next single tech, unless it is locked.
 * True where they reach it, the points left over then in its count. */
bool rewards_reach_next_tech(GameState *g, uint8_t member, uint8_t character, uint16_t points)
{
    CharacterTechs techs = character_techs(g, character);
    uint8_t known = party_single_techs_known(g, character), tech = (uint8_t)(known + techs.first);
    leave_tech_record_found(g, known, techs.first);
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
/* The member learns its next single tech: its learned bit, its place in the
 * messages' list at `at` (the next place after it left there), and all learned
 * once it knows as many as it has. `first` is its character's first. */
void rewards_learn_single_tech(GameState *g, uint8_t member, uint8_t character, uint8_t first, uint8_t *at)
{
    uint8_t known = party_single_techs_known(g, character);
    set_party_single_techs_learned(g, character, party_single_techs_learned(g, character) | learned_bit(known));
    note_learned(g, member, NOTED_SINGLE_TECHS);
    set_battle_rewards_techs(g, *at, (uint8_t)(known + first));
    (*at)++;
    set_party_single_techs_known(g, character, (uint8_t)(known + 1));
    if ((uint8_t)(known + 1) >= character_techs(g, character).count)
        set_battle_stats_block_byte(g, member, STAT_NOTES, battle_stats_block_byte(g, member, STAT_NOTES) | STAT_NOTE_TECHS_ALL_LEARNED);
    ORIGINAL_EFFECT(battle_leave_byte(g, WORK_BIT_AT, learned_bit(known)));
    ORIGINAL_EFFECT(set_battle_rewards_tech_at(g, *at));
}

/* The most tech points a member holds, 9,999,999, as the original brings a
 * total down to it: only where the total's low word and its high byte are
 * each at the most's or past it, so a total just over the most with a low word
 * under the most's (up to $99967E) stays. */
enum { MOST_TECH_POINTS = 0x98967F };

static uint32_t capped_tech_points(uint32_t total)
{
    bool over = (uint16_t)total >= (uint16_t)MOST_TECH_POINTS && total >> 16 >= MOST_TECH_POINTS >> 16;
    return over ? MOST_TECH_POINTS : total;
}

static void add_tech_points(GameState *g, uint8_t member, uint16_t points)
{
    Grower who = grower_member(member);
    uint32_t total = (record_long(g, who, STAT_TECH_POINTS) + points) & 0xFFFFFF;
    set_record_long(g, who, STAT_TECH_POINTS, capped_tech_points(total));
}

/* The cell where the original keeps the battle's tech points while it spends
 * them. */
enum { POINTS_KEPT_AT = 8 };

// $C1:F39A
/* The battle's tech points towards the member's single techs, as many as they
 * reach, noted in the messages' list from `at`. Where a tech is learned, the
 * points it left over go on to the next, whose cost is its count now; where
 * they don't reach it, the next is paid the battle's points again, as the
 * original does (TODO.md). */
void rewards_count_tech_points(GameState *g, uint8_t member, uint8_t character, uint8_t *at)
{
    uint16_t won = battle_tech_points_won_low(g), points = won;
    ORIGINAL_EFFECT(set_battle_wide_math(g, POINTS_KEPT_AT, (uint8_t)won));
    ORIGINAL_EFFECT(set_battle_wide_math(g, POINTS_KEPT_AT + 1, (uint8_t)(won >> 8)));
    bool reached = false, learned = false;
    for (;;) {
        TIMED_AS(g, 0xC1F45F, reached = rewards_reach_next_tech(g, member, character, points));
        if (!reached || all_learned(g, member))
            break;
        TIMED_AS(g, 0xC1F4DF, rewards_learn_single_tech(g, member, character, character_techs(g, character).first, at));
        learned = true;
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
    /* what the original leaves: where the count and the techs' record are
     * once one was reached, and the character's costs once one was learned */
    if (!reached)
        ORIGINAL_EFFECT(set_battle_tech_learned(g, TECH_NOT_REACHED));
    if (reached || learned) {
        ORIGINAL_EFFECT(battle_leave_word(g, WORK_KNOWN_AT, known_place(character)));
        ORIGINAL_EFFECT(battle_leave_word(g, WORK_TECHS_AT, character_techs_place(character)));
    }
    if (learned)
        ORIGINAL_EFFECT(battle_leave_word(g, WORK_BIT_AT, (uint16_t)(character * 16)));
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

/* No tech taught, in the messages' lists. */
enum { NO_TECH = 0xFF };

/* A combination of two characters, and of three. */
enum { DOUBLE = 2, TRIPLE = 3 };

/* The combination's characters' counts of techs known, in order, a byte past
 * the last; a count that is that byte ends the list too. */
enum { END_OF_LIST = 0xFF };

typedef struct {
    uint8_t count[SETUP_CHARACTERS + 1];
    uint8_t listed;
} KnownList;

static KnownList list_techs_known(GameState *g, uint8_t characters)
{
    KnownList list = { { 0 }, 0 };
    for (uint8_t character = 0; character < SETUP_CHARACTERS; character++)
        if (characters & character_bit(character))
            list.count[list.listed++] = party_single_techs_known(g, character);
    list.count[list.listed] = END_OF_LIST;
    for (unsigned n = 0; n <= list.listed; n++)
        ORIGINAL_EFFECT(set_battle_combination_techs_known(g, n, list.count[n]));
    ORIGINAL_EFFECT(battle_leave_byte(g, WORK_SHIFTING_AT, (uint8_t)(characters << SETUP_CHARACTERS)));
    ORIGINAL_EFFECT(battle_leave_word(g, WORK_CHARACTER_AT, SETUP_CHARACTERS));
    ORIGINAL_EFFECT(battle_leave_word(g, WORK_LISTED_AT, list.listed));
    return list;
}

/* How the characters measure up to a tech: how many met what it asks before
 * one fell short or the list ended, the last count tested. */
typedef struct {
    bool short_of_techs;
    uint8_t met, tested;
} AsksMet;

static AsksMet asks_met(const GameState *g, const KnownList *list, uint16_t asks, uint8_t tech)
{
    AsksMet m = { false, 0, 0 };
    for (; m.met < list->listed && list->count[m.met] != END_OF_LIST; m.met++) {
        m.tested = list->count[m.met];
        if (m.tested < combination_tech_asks(g, asks, tech, m.met)) {
            m.short_of_techs = true;
            break;
        }
    }
    return m;
}

static void leave_asks_met(GameState *g, AsksMet m)
{
    ORIGINAL_EFFECT(battle_leave_word(g, WORK_TESTED_AT, m.tested));
    ORIGINAL_EFFECT(battle_leave_word(g, WORK_LISTED_AT, m.met));
    ORIGINAL_EFFECT(set_battle_combination_short(g, m.short_of_techs ? 0xFF : 0x00));
}

// $C1:F533
/* The combination learns each of its techs it hasn't that isn't locked, where
 * each of its characters knows as many single techs as the tech asks of it;
 * noted for `member`. */
void rewards_learn_combination_techs(GameState *g, Combination c, uint8_t member)
{
    KnownList list = list_techs_known(g, c.characters);
    bool tested = false;
    AsksMet last = { false, 0, 0 };
    uint8_t n = 0;
    do {
        uint8_t tech = (uint8_t)(c.first + n), bit = (uint8_t)(n < 8 ? 0x80 >> n : 0);
        if (combination_learned(g, c.record) & bit)
            continue;
        last = asks_met(g, &list, c.asks, n);
        tested = true;
        if (last.short_of_techs)
            continue;
        /* the original finds the tech's record by multiplying, its carry set
         * from the end of the list */
        TIMED_AS(g, MULTIPLY_WORDS, battle_leave_multiply(g, tech, PARTY_TECH_BYTES));
        ORIGINAL_EFFECT(battle_leave_multiply_carry(g, true));
        if (party_tech_locked(g, tech))
            continue;
        set_combination_learned(g, c.record, combination_learned(g, c.record) | bit);
        if (last.met == DOUBLE) {
            uint8_t at = battle_rewards_double_techs_listed(g);
            set_battle_rewards_double_techs(g, at, tech);
            set_battle_rewards_double_techs_listed(g, (uint8_t)(at + 1));
            note_learned(g, member, NOTED_DOUBLE_TECHS);
        } else if (last.met == TRIPLE) {
            set_battle_rewards_triple_tech(g, tech);
            note_learned(g, member, NOTED_TRIPLE_TECH);
        }
    } while (++n != c.count);
    if (tested)
        leave_asks_met(g, last);
    ORIGINAL_EFFECT(set_battle_combination_tech_bit(g, (uint8_t)(n < 8 ? 0x80 >> n : 0)));
    ORIGINAL_EFFECT(set_battle_combination_tech(g, (uint8_t)(c.first + n)));
    ORIGINAL_EFFECT(battle_leave_word(g, WORK_TECHS_AT, combination_tech_asks_place(c.asks, n)));
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

static uint8_t member_bit(const GameState *g, uint8_t member)
{
    uint8_t character = battle_combatant_fighting(g, member);
    return character == COMBATANT_NONE ? 0 : character_bit(character);
}

/* The original works each member's bit out in a cell, starting from the top
 * bit, and keeps the sorted bits and the sets. */
static void leave_members_sorted(GameState *g, const uint8_t bits[BATTLE_PARTY_SIZE], MemberSets s)
{
    uint8_t last = BATTLE_PARTY_SIZE - 1;
    ORIGINAL_EFFECT(battle_leave_byte(g, WORK_BIT_AT, battle_combatant_fighting(g, last) == COMBATANT_NONE ? 0x80 : member_bit(g, last)));
    ORIGINAL_EFFECT(set_battle_combination_first_bit(g, bits[0]));
    ORIGINAL_EFFECT(set_battle_combination_second_bit(g, bits[1]));
    /* the cell is the reactions' animation kept, which they read as they
     * find it (battle/main_loop.c) */
    set_battle_combination_third_bit(g, bits[2]);
    for (unsigned n = 0; n < BATTLE_COMBINATION_MEMBER_SETS_COUNT; n++)
        ORIGINAL_EFFECT(set_battle_combination_member_sets(g, n, s.sets[n]));
}

static MemberSets sort_members(GameState *g)
{
    uint8_t bits[BATTLE_PARTY_SIZE];
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        bits[member] = member_bit(g, member);
    sort_two(&bits[0], &bits[1]);
    sort_two(&bits[0], &bits[2]);
    sort_two(&bits[1], &bits[2]);
    MemberSets s = { { (uint8_t)(bits[0] | bits[1]), (uint8_t)(bits[0] | bits[2]), (uint8_t)(bits[1] | bits[2]),
                       (uint8_t)(bits[0] | bits[1] | bits[2]) } };
    leave_members_sorted(g, bits, s);
    return s;
}

// $C1:F2DB
void rewards_sort_combination_members(GameState *g) { (void)sort_members(g); }

/* The record of the characters' combination techs, where they have one. */
static bool find_combination(GameState *g, uint8_t characters, Combination *c)
{
    uint8_t record = 0, these;
    while ((these = combination_record_characters(g, record)) != NO_MORE_COMBINATIONS && these != characters)
        record++;
    /* the original walks the records in a cell, counting them in another, and
     * keeps the last characters it compared */
    ORIGINAL_EFFECT(battle_leave_word(g, WORK_KNOWN_AT, combination_record_place(record)));
    ORIGINAL_EFFECT(set_battle_combination_record_number(g, record));
    if (record > 0 || these != NO_MORE_COMBINATIONS)
        ORIGINAL_EFFECT(battle_leave_byte(g, WORK_CHARACTERS_AT, these != NO_MORE_COMBINATIONS ? these : combination_record_characters(g, (uint8_t)(record - 1))));
    if (these == NO_MORE_COMBINATIONS)
        return false;
    CombinationRecord r = combination_record(g, record);
    if (r.first == COMBINATION_HAS_NO_TECHS)
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

    /* noted for a combination: the last member fighting (a combination needs
     * two, so there is one) */
    uint8_t noted = 0;
    bool fighting = false, learning = false;
    uint8_t at = 0, learner = 0;
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        if (battle_combatant_fighting(g, member) == COMBATANT_NONE)
            continue;
        fighting = true;
        noted = member;
        at = (uint8_t)(member * PLACES_A_MEMBER);
        if (all_learned(g, member))
            continue;
        learning = true;
        learner = battle_stats_block_byte(g, member, STAT_ID);
        TIMED_AS(g, 0xC1F39A, rewards_count_tech_points(g, member, learner, &at));
    }

    MemberSets s;
    TIMED_AS(g, 0xC1F2DB, s = sort_members(g));
    bool combined = false;
    Combination c = { 0, 0, 0, 0, 0 };
    for (uint8_t set = 0; set < BATTLE_COMBINATION_MEMBER_SETS_COUNT; set++)
        if (find_combination(g, s.sets[set], &c)) {
            TIMED_AS(g, 0xC1F533, rewards_learn_combination_techs(g, c, noted));
            combined = true;
        }

    /* what the original leaves: the member and its place in the list, the
     * character learning, and the member's block or the combination's
     * learned byte and its characters */
    if (fighting) {
        set_battle_rewards_member(g, noted);
        ORIGINAL_EFFECT(set_battle_rewards_tech_at(g, at));
        ORIGINAL_EFFECT(set_battle_rewards_tech_at_high(g, 0));
    }
    if (learning)
        ORIGINAL_EFFECT(set_battle_tech_learner(g, learner));
    if (combined) {
        ORIGINAL_EFFECT(battle_leave_word(g, WORK_BLOCK_AT, combination_learned_place(c.record)));
        ORIGINAL_EFFECT(set_battle_combination_members(g, c.characters));
    } else if (fighting) {
        ORIGINAL_EFFECT(battle_leave_word(g, WORK_BLOCK_AT, (uint16_t)(noted * BATTLE_STAT_BLOCK_BYTES)));
    }
    /* the cell is the effect's hits left, which the next effect that hits
     * several times reads as it finds it (battle/effect_work.c) */
    set_battle_rewards_combination(g, BATTLE_COMBINATION_MEMBER_SETS_COUNT);
}
