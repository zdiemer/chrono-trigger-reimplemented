/* Battle: whom the enemies' scripts aim at (battle/targets.h).
 *
 * The selectors are a C table, a row each: how the row picks, and what with.
 * They pick for the enemy whose script is running (its battle slot, `self`,
 * leaves it out or picks it) and write the candidates with their count; the choosing among them that
 * follows is at the end. */
#include "battle/targets.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/positions.h"
#include "battle/random.h"
#include "battle/rules_data.h"
#include "charge.h"
#include "original_effects.h"
#include "wram.h"
#include "vars.h"

/* ---- The candidates ---------------------------------------------------------------------- */

Candidate battle_candidate(const GameState *g, unsigned place)
{
    return place < BATTLE_CANDIDATES_COUNT ? battle_candidates(g, place) : battle_candidates_unchecked(g, place);
}
uint8_t battle_candidates_held(const GameState *g) { return battle_candidate_count(g); }

static void set_candidate(GameState *g, unsigned place, Candidate c) { set_battle_candidates(g, place, c); }
static void set_count(GameState *g, uint8_t n) { set_battle_candidate_count(g, n); }

static uint8_t slot_of(Candidate c) { return c & CANDIDATE_SLOT; }

/* Put a combatant after the candidates held. */
static void add_candidate(GameState *g, uint8_t slot)
{
    uint8_t n = battle_candidate_count(g);
    set_candidate(g, n, slot);
    set_count(g, (uint8_t)(n + 1));
}

/* The one candidate: it alone, the count 1. */
static void only_candidate(GameState *g, uint8_t slot)
{
    set_candidate(g, 0, slot);
    set_count(g, 1);
}

// $C1:AC46
void battle_clear_candidates(GameState *g)
{
    for (unsigned place = 0; place < BATTLE_CANDIDATES_COUNT; place++) {
        set_candidate(g, place, NO_CANDIDATE);
        set_battle_action_targets(g, place, NO_CANDIDATE);
    }
}

/* ---- What the selectors leave in their cells ---------------------------------------------- */

/* The direct-page cells the selectors work in (battle_low_scratch). They keep
 * a word at $0A too -- the enemy's own battle slot, or the stat block's byte a
 * selection by status looks at -- whose high byte is kept (battle.h). */
enum {
    LEFT_HP = 0x02,          /* the lowest or highest HP a selector found, a word ($02) */
    LEFT_BLOCK = 0x0C,       /* the stat block's place's low byte, and its high byte cleared ($0C) */
    LEFT_PLACE = 0x0E,       /* where a selector's loop over the combatants is, a word ($0E) */
};

/* Where a selector's loop over the combatants ended, a word: its low byte
 * is what an enemy tech's hits can be noted as (battle.h), so it is kept. */
static void keep_loop_end(GameState *g, uint16_t v)
{
    battle_keep_tech_hits_left(g, (uint8_t)v);
    ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_PLACE + 1, (uint8_t)(v >> 8)));
}

/* ---- Whom a selector can pick -------------------------------------------------------------- */

/* The party members in the fight, and then (for everyone) the enemies. */
static void pick_fighting(GameState *g, uint8_t first, uint8_t end, uint8_t left_out)
{
    for (uint8_t slot = first; slot < end; slot++)
        if (slot != left_out && combatant_in_fight(g, slot))
            add_candidate(g, slot);
}

static void pick_party(GameState *g)
{
    set_count(g, 0);
    pick_fighting(g, 0, BATTLE_PARTY_SIZE, NO_CANDIDATE);
}

/* The enemy's own action record ($B192 by its slot) keeps, in its first
 * byte's low four bits, the combatant that last acted on it. */
static uint8_t last_to_act_on(const GameState *g, uint8_t slot) { return (uint8_t)(battle_chosen_actions(g, slot) & CANDIDATE_SLOT); }

/* A party member at random, after the party's update for the turn
 * (battle/combatants.c): none while the party is defeated, or where none is
 * in the fight. The draw is by thirds of 0-98; a member out of the fight has
 * the party updated again and the draw made again. */
enum { THIRDS_LIMIT = 0x63, FIRST_THIRD = 0x21, SECOND_THIRD = 0x42 };

static uint8_t member_by_thirds(uint8_t drawn) { return drawn < FIRST_THIRD ? 0 : drawn < SECOND_THIRD ? 1 : 2; }

static bool member_out(const GameState *g, uint8_t member) { return (combatant_fighting_as(g, member) & 0x80) != 0; }

static void pick_random_member(GameState *g)
{
    set_count(g, 0);
    if (battle_party_defeated(g))
        return;
    uint8_t member;
    do {
        for (uint8_t m = 0; m < BATTLE_PARTY_SIZE; m++)
            TIMED_AS(g, 0xC1B279, combatant_update(g, m));
        if (member_out(g, 0) && member_out(g, 1) && member_out(g, 2))
            return;
        member = member_by_thirds(battle_random(g, 0, THIRDS_LIMIT));
    } while (member_out(g, member));
    only_candidate(g, member);
}

/* The one a search finds, as the action's one target too. */
static void pick_found(GameState *g, uint8_t self, SearchKind kind, bool must_be_fighting)
{
    set_count(g, 0);
    SearchAnswer a;
    TIMED_AS(g, 0xC12986, a = battle_search(g, kind, self, battle_search_other(g), battle_search_third(g)));
    if (a.found & 0x80)
        return;
    if (must_be_fighting && (combatant_fighting_as(g, a.found) & 0x80))
        return;
    only_candidate(g, a.found);
    set_battle_action_target_count(g, 1);
}

/* The nearest or farthest other enemy: none where the search finds the enemy
 * itself (it finds the last it had where it finds nobody). */
static void pick_found_enemy(GameState *g, uint8_t self, SearchKind kind)
{
    SearchAnswer a;
    TIMED_AS(g, 0xC12986, a = battle_search(g, kind, self, battle_search_other(g), battle_search_third(g)));
    if (a.found == self) {
        set_candidate(g, 0, NO_CANDIDATE);
        set_count(g, 0);
    } else {
        only_candidate(g, a.found);
    }
}

/* The party member with the least HP, or the most. The original starts the
 * least from $7FFF with the first member only as the one to fall back on, so
 * the first member is never compared, and a member out of the fight counts;
 * the most starts from the first member's. */
static void pick_member_by_hp(GameState *g, bool most)
{
    uint16_t best = most ? battle_stats_hp(g, 0) : 0x7FFF;
    uint8_t picked = 0;
    for (uint8_t member = 1; member < BATTLE_PARTY_SIZE; member++) {
        uint16_t hp = battle_stats_hp(g, member);
        if (most ? hp >= best : hp != 0 && hp < best) {
            best = hp;
            picked = member;
        }
    }
    only_candidate(g, picked);
    ORIGINAL_EFFECT(battle_leave_word(g, LEFT_HP, (uint8_t)best));
}

/* The enemy in the fight with the least HP (above none), the enemy itself
 * left out or not. Where there is none the candidate stays none, with the
 * count 1. */
static void pick_weakest_enemy(GameState *g, uint8_t left_out)
{
    uint16_t least = 0xFFFF;
    for (uint8_t slot = BATTLE_FIRST_ENEMY; slot < BATTLE_COMBATANTS; slot++) {
        uint16_t hp = battle_stats_hp(g, slot);
        if (slot != left_out && combatant_in_fight(g, slot) && hp != 0 && hp < least) {
            least = hp;
            set_candidate(g, 0, slot);
        }
    }
    set_count(g, 1);
    ORIGINAL_EFFECT(battle_leave_word(g, LEFT_HP, (uint8_t)least));
    battle_keep_tech_hits_left(g, BATTLE_COMBATANTS);
}

/* One of the enemies waiting in reserve, at random. The candidates are the
 * enemies' numbers here (battle slot less 3), as the original puts them. */
static void narrow_at_random(GameState *g, uint8_t count, bool fill_the_rest)
{
    uint8_t drawn = battle_random(g, 0, count);
    set_candidate(g, 0, battle_candidates(g, drawn));
    for (unsigned place = 1; place < BATTLE_CANDIDATES_COUNT; place++) {
        set_candidate(g, place, NO_CANDIDATE);
        if (!fill_the_rest)
            break;
    }
    set_count(g, 1);
}

static void pick_reserve_enemy(GameState *g)
{
    uint8_t count = 0;
    for (uint8_t enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        if (battle_enemy_standing(g, enemy) & ENEMY_IN_RESERVE)
            set_candidate(g, count++, enemy);
    set_count(g, count);
    if (count >= 2)
        narrow_at_random(g, count, true);
}

/* Another enemy in the fight, at random. The original means to clear the
 * candidates after the one drawn and clears only the next. */
static void pick_other_enemy_at_random(GameState *g, uint8_t self)
{
    set_count(g, 0);
    pick_fighting(g, BATTLE_FIRST_ENEMY, BATTLE_COMBATANTS, self);
    battle_keep_word_at_0a(g, self);
    uint8_t count = battle_candidate_count(g);
    if (count >= 2)
        narrow_at_random(g, count, false);
}

/* ---- Picking by status ---------------------------------------------------------------------- */

/* A combatant a selection by status picks: in the fight, not knocked out,
 * with one of the bits it looks for in a byte of its stat block. A combatant
 * with the aim-drawing or aim-turning status is marked so. */
enum { STATUS_DRAWS_AIM = 0x10, STATUS_TURNS_AIM = 0x20, STATUS_BYTE = STAT_STATUS_3 };

typedef struct {
    uint8_t stat;   /* the byte of the stat block */
    uint8_t bits;   /* the bits of it looked for */
} StatusTest;

static void consider(GameState *g, uint8_t slot, StatusTest test)
{
    ORIGINAL_EFFECT(set_battle_candidate_considered(g, slot));
    if (!combatant_in_fight(g, slot))
        return;
    ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_BLOCK, (uint8_t)battle_stat_block_place(g, slot)));
    if (!(battle_stats_block_byte(g, slot, test.stat) & test.bits))
        return;
    if (battle_stats_block_byte(g, slot, STAT_CONDITION) & CONDITION_KNOCKED_OUT)
        return;
    uint8_t place = battle_candidate_count(g);
    add_candidate(g, slot);
    uint8_t statuses = battle_stats_block_byte(g, slot, STATUS_BYTE);
    if (statuses & STATUS_TURNS_AIM) {
        set_candidate(g, place, battle_candidates(g, place) | CANDIDATE_TURNS_AIM);
        set_battle_candidates_turning(g, (uint8_t)(battle_candidates_turning(g) + 1));
    }
    if (statuses & STATUS_DRAWS_AIM) {
        set_candidate(g, place, battle_candidates(g, place) | CANDIDATE_MARKED);
        set_battle_candidates_drawing(g, (uint8_t)(battle_candidates_drawing(g) + 1));
    }
}

/* What considering a combatant leaves besides the stat block's byte it looks
 * at (a word at $0A, kept: battle.h): the bits, and its block's place's
 * high byte cleared. */
static void leave_considering(GameState *g, StatusTest test)
{
    battle_leave_byte(g, LEFT_BLOCK + 1, 0);
    set_battle_candidate_statuses(g, test.bits);
}

static void start_status_pick(GameState *g, StatusTest test)
{
    set_count(g, 0);
    set_battle_candidates_turning(g, 0);
    set_battle_candidates_drawing(g, 0);
    battle_keep_word_at_0a(g, test.stat);
    ORIGINAL_EFFECT(leave_considering(g, test));
}

static void narrow_by_aim(GameState *g, uint8_t self);

static void pick_members_with(GameState *g, uint8_t self, StatusTest test)
{
    start_status_pick(g, test);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        consider(g, member, test);
    narrow_by_aim(g, self);
}

static void pick_enemies_with(GameState *g, uint8_t self, StatusTest test, bool with_itself)
{
    start_status_pick(g, test);
    for (uint8_t slot = BATTLE_FIRST_ENEMY; slot < BATTLE_COMBATANTS; slot++)
        if (with_itself || slot != battle_actor(g))
            consider(g, slot, test);
    ORIGINAL_EFFECT(set_battle_candidate_considered(g, BATTLE_COMBATANTS));
    keep_loop_end(g, (uint16_t)(2 * BATTLE_COMBATANTS));
    narrow_by_aim(g, self);
}

/* $C1:A8C5: the enemies with a status in their stat block's third status
 * byte, the enemy itself included. The original means to keep its place in
 * the table of the stat blocks' places in a cell and stores its A there
 * instead: from the third enemy on it looks at the stat block the table's
 * word at that leftover (plus two) gives, read wherever it points, while
 * adding the enemy in turn. No script uses this selector. */
static uint8_t stray_status_byte(const GameState *g, uint16_t place, uint8_t stat)
{
    return wram_read8(g, 0x7E0000u | (uint16_t)(battle_stat_block_place_stray(g, place) + stat));
}

/* What the original's consideration leaves in A: the last byte it read, or
 * the mark it added. */
static uint8_t consider_stray(GameState *g, uint8_t slot, uint16_t place, StatusTest test)
{
    ORIGINAL_EFFECT(set_battle_candidate_considered(g, slot));
    uint8_t fighting = combatant_fighting_as(g, slot);
    if (fighting == COMBATANT_NONE)
        return fighting;
    ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_BLOCK, (uint8_t)battle_stat_block_place_stray(g, place)));
    uint8_t byte = stray_status_byte(g, place, test.stat);
    if (!(byte & test.bits))
        return byte;
    uint8_t condition = stray_status_byte(g, place, STAT_CONDITION);
    if (condition & CONDITION_KNOCKED_OUT)
        return condition;
    uint8_t at = battle_candidate_count(g);
    add_candidate(g, slot);
    uint8_t statuses = stray_status_byte(g, place, STATUS_BYTE);
    if (statuses & STATUS_TURNS_AIM) {
        set_candidate(g, at, battle_candidates(g, at) | CANDIDATE_TURNS_AIM);
        set_battle_candidates_turning(g, (uint8_t)(battle_candidates_turning(g) + 1));
    }
    if (!(statuses & STATUS_DRAWS_AIM))
        return statuses;
    set_candidate(g, at, battle_candidates(g, at) | CANDIDATE_MARKED);
    set_battle_candidates_drawing(g, (uint8_t)(battle_candidates_drawing(g) + 1));
    return battle_candidates(g, at);
}

static void pick_enemies_with_stray(GameState *g, uint8_t self, StatusTest test)
{
    start_status_pick(g, test);
    uint16_t place = 2 * BATTLE_FIRST_ENEMY, kept = place;
    for (uint8_t slot = BATTLE_FIRST_ENEMY; slot < BATTLE_COMBATANTS; slot++) {
        uint8_t leftover = consider_stray(g, slot, place, test);
        place = (uint16_t)(kept + 2);
        kept = leftover;
    }
    ORIGINAL_EFFECT(set_battle_candidate_considered(g, BATTLE_COMBATANTS));
    keep_loop_end(g, kept);
    narrow_by_aim(g, self);
}

/* ---- Choosing among them by the aim --------------------------------------------------------- */

/* $C1:ADA1: one of several candidates picked by status, by the enemies'
 * order drawn at the start (battle_enemy_order), which the original reads as
 * battle slots: the first candidate whose slot is an enemy's number there,
 * the enemies' order first; where some draw the aim, the first of those so.
 * A candidate that turns the aim gives way to the party member farthest from
 * the enemy. The one left is the candidate, unmarked.
 *
 * Where some draw the aim and the first candidate found doesn't, the original
 * never leaves its loop and the game hangs; the port takes that candidate (an
 * accepted divergence: docs/TODO.md). */
static void narrow_by_aim(GameState *g, uint8_t self)
{
    uint8_t count = battle_candidate_count(g);
    if (count == 0)
        return;
    if (count >= 2) {
        for (unsigned place = 0; place < BATTLE_ENEMIES; place++) {
            unsigned c = 0;
            while (c < count && slot_of(battle_candidates(g, c)) != battle_enemy_order(g, place))
                c++;
            if (c == count)
                continue;
            set_candidate(g, 0, battle_candidates(g, c));
            if (battle_candidates_turning(g) && (battle_candidates(g, 0) & CANDIDATE_TURNS_AIM)) {
                TIMED_AS(g, 0xC1A4E0, pick_found(g, self, SEARCH_FARTHEST_MEMBER, false));
                return;
            }
            break;
        }
    }
    only_candidate(g, slot_of(battle_candidates(g, 0)));
}

/* ---- The selectors ---------------------------------------------------------------------- */

typedef enum {
    PICK_NOBODY,
    PICK_PARTY,
    PICK_EVERYONE,
    PICK_ITSELF,
    PICK_LAST_TO_ACT_ON_IT,
    PICK_RANDOM_MEMBER,
    PICK_NEAREST_MEMBER,
    PICK_FARTHEST_MEMBER,
    PICK_WEAKEST_MEMBER,
    PICK_STRONGEST_MEMBER,
    PICK_MEMBERS_WITH,
    PICK_OTHER_ENEMIES,
    PICK_ENEMIES,
    PICK_NEAREST_OTHER_ENEMY,
    PICK_FARTHEST_ENEMY,
    PICK_WEAKEST_ENEMY,
    PICK_WEAKEST_OTHER_ENEMY,
    PICK_ENEMIES_WITH,
    PICK_OTHER_ENEMIES_WITH,
    PICK_ENEMIES_WITH_STRAY,
    PICK_SLOT,
    PICK_RESERVE_ENEMY,
    PICK_OTHER_ENEMY_AT_RANDOM,
} Pick;

typedef struct {
    Pick pick;
    StatusTest test;  /* PICK_*_WITH */
    uint8_t slot;     /* PICK_SLOT */
} SelectorRow;

/* The selections by status look at the condition, the four status bytes, and
 * two more of the stat block (StatOffset). */
enum { ANY_BIT = 0xFF };

#define WITH(p, s, b) { p, { s, b }, 0 }
#define SLOT(n) { PICK_SLOT, { 0, 0 }, n }

// $C1:B8BB
static const SelectorRow selectors[SELECTORS] = {
    { PICK_NOBODY, { 0, 0 }, 0 },                     // $00 $C1:A3F6
    { PICK_PARTY, { 0, 0 }, 0 },                      // $01 $C1:A3F7
    { PICK_EVERYONE, { 0, 0 }, 0 },                   // $02 $C1:A411
    { PICK_ITSELF, { 0, 0 }, 0 },                     // $03 $C1:A42E
    { PICK_LAST_TO_ACT_ON_IT, { 0, 0 }, 0 },          // $04 $C1:A43D
    { PICK_RANDOM_MEMBER, { 0, 0 }, 0 },              // $05 $C1:A452
    { PICK_NEAREST_MEMBER, { 0, 0 }, 0 },             // $06 $C1:A4AF
    { PICK_FARTHEST_MEMBER, { 0, 0 }, 0 },            // $07 $C1:A4E0
    { PICK_WEAKEST_MEMBER, { 0, 0 }, 0 },             // $08 $C1:A508
    WITH(PICK_MEMBERS_WITH, STAT_CONDITION, ANY_BIT), // $09 $C1:A541
    WITH(PICK_MEMBERS_WITH, STAT_STATUS_1, ANY_BIT),  // $0A $C1:A54B
    WITH(PICK_MEMBERS_WITH, STAT_STATUS_2, ANY_BIT),  // $0B $C1:A555
    WITH(PICK_MEMBERS_WITH, STAT_STATUS_3, ANY_BIT),  // $0C $C1:A55F
    WITH(PICK_MEMBERS_WITH, STAT_STATUS_4, ANY_BIT),  // $0D $C1:A569
    WITH(PICK_MEMBERS_WITH, STAT_STATUS_1, 0x02),     // $0E $C1:A573
    WITH(PICK_MEMBERS_WITH, STAT_STATUS_1, 0x80),     // $0F $C1:A5A3
    WITH(PICK_MEMBERS_WITH, STAT_STATUS_1, 0x04),     // $10 $C1:A5D3
    WITH(PICK_MEMBERS_WITH, STAT_STATUS_4, 0x04),     // $11 $C1:A603
    WITH(PICK_MEMBERS_WITH, STAT_STATUS_4, 0x40),     // $12 $C1:A633
    WITH(PICK_MEMBERS_WITH, STAT_STATUS_3, 0x10),     // $13 $C1:A663
    WITH(PICK_MEMBERS_WITH, STAT_OTHER, 0x08),        // $14 $C1:A693
    { PICK_OTHER_ENEMIES, { 0, 0 }, 0 },              // $15 $C1:A6C3
    { PICK_ENEMIES, { 0, 0 }, 0 },                    // $16 $C1:A6ED
    { PICK_NEAREST_OTHER_ENEMY, { 0, 0 }, 0 },        // $17 $C1:A709
    { PICK_FARTHEST_ENEMY, { 0, 0 }, 0 },             // $18 $C1:A737
    { PICK_WEAKEST_ENEMY, { 0, 0 }, 0 },              // $19 $C1:A765
    WITH(PICK_OTHER_ENEMIES_WITH, STAT_CONDITION, ANY_BIT), // $1A $C1:A7A9
    WITH(PICK_ENEMIES_WITH, STAT_CONDITION, ANY_BIT),       // $1B $C1:A7E5
    WITH(PICK_OTHER_ENEMIES_WITH, STAT_STATUS_1, ANY_BIT),  // $1C $C1:A819
    WITH(PICK_ENEMIES_WITH, STAT_STATUS_1, ANY_BIT),        // $1D $C1:A855
    WITH(PICK_OTHER_ENEMIES_WITH, STAT_STATUS_2, ANY_BIT),  // $1E $C1:A889
    WITH(PICK_ENEMIES_WITH_STRAY, STAT_STATUS_2, ANY_BIT),  // $1F $C1:A8C5
    WITH(PICK_OTHER_ENEMIES_WITH, STAT_STATUS_1, 0x02),     // $20 $C1:A8F9
    WITH(PICK_OTHER_ENEMIES_WITH, STAT_STATUS_1, 0x80),     // $21 $C1:A935
    WITH(PICK_OTHER_ENEMIES_WITH, STAT_STATUS_1, 0x04),     // $22 $C1:A971
    WITH(PICK_OTHER_ENEMIES_WITH, STAT_STATUS_4, 0x40),     // $23 $C1:A9AD
    WITH(PICK_OTHER_ENEMIES_WITH, STAT_CONDITION, 0x02),    // $24 $C1:A9E9
    WITH(PICK_OTHER_ENEMIES_WITH, STAT_FLAGS, 0x01),        // $25 $C1:AA25
    { PICK_WEAKEST_OTHER_ENEMY, { 0, 0 }, 0 },              // $26 $C1:AA61
    SLOT(3), SLOT(4), SLOT(5), SLOT(6), SLOT(7), SLOT(8), SLOT(9), SLOT(10), // $27-$2E $C1:AAAB-$AAF8
    { PICK_RESERVE_ENEMY, { 0, 0 }, 0 },                     // $2F $C1:AB03
    SLOT(0), SLOT(1), SLOT(2), SLOT(3), SLOT(4), SLOT(5), SLOT(6), // $30-$36 $C1:AB4E-$AB90
    { PICK_STRONGEST_MEMBER, { 0, 0 }, 0 },                 // $37 $C1:AB9B
    { PICK_OTHER_ENEMY_AT_RANDOM, { 0, 0 }, 0 },            // $38 $C1:ABC9
};

/* The selections by status through $C1:AD68 clear the place a condition's
 * test keeps in its cell, which the others leave. */
static bool clears_candidate_place(Selector selector) { return selector >= 0x09 && selector <= 0x0D; }

void battle_pick_candidates(GameState *g, uint8_t enemy, Selector selector)
{
    if (selector >= SELECTORS)
        return;
    const SelectorRow *row = &selectors[selector];
    uint8_t self = (uint8_t)(BATTLE_FIRST_ENEMY + enemy);
    if (clears_candidate_place(selector))
        ORIGINAL_EFFECT(set_battle_candidate_place(g, 0));
    switch (row->pick) {
    case PICK_NOBODY: break;
    case PICK_PARTY: pick_party(g); break;
    case PICK_EVERYONE:
        pick_party(g);
        pick_fighting(g, BATTLE_FIRST_ENEMY, BATTLE_COMBATANTS, NO_CANDIDATE);
        break;
    case PICK_ITSELF: only_candidate(g, self); break;
    case PICK_LAST_TO_ACT_ON_IT: only_candidate(g, last_to_act_on(g, self)); break;
    case PICK_RANDOM_MEMBER: pick_random_member(g); break;
    case PICK_NEAREST_MEMBER: pick_found(g, self, SEARCH_NEAREST_MEMBER, true); break;
    case PICK_FARTHEST_MEMBER: pick_found(g, self, SEARCH_FARTHEST_MEMBER, false); break;
    case PICK_WEAKEST_MEMBER: pick_member_by_hp(g, false); break;
    case PICK_STRONGEST_MEMBER: pick_member_by_hp(g, true); break;
    case PICK_MEMBERS_WITH: pick_members_with(g, self, row->test); break;
    case PICK_OTHER_ENEMIES:
        set_count(g, 0);
        pick_fighting(g, BATTLE_FIRST_ENEMY, BATTLE_COMBATANTS, self);
        battle_keep_word_at_0a(g, self);
        break;
    case PICK_ENEMIES:
        set_count(g, 0);
        pick_fighting(g, BATTLE_FIRST_ENEMY, BATTLE_COMBATANTS, NO_CANDIDATE);
        break;
    case PICK_NEAREST_OTHER_ENEMY: pick_found_enemy(g, self, SEARCH_NEAREST_OTHER_ENEMY); break;
    case PICK_FARTHEST_ENEMY: pick_found_enemy(g, self, SEARCH_FARTHEST_ENEMY); break;
    case PICK_WEAKEST_ENEMY: pick_weakest_enemy(g, NO_CANDIDATE); break;
    case PICK_WEAKEST_OTHER_ENEMY: pick_weakest_enemy(g, battle_actor(g)); break;
    case PICK_ENEMIES_WITH: pick_enemies_with(g, self, row->test, true); break;
    case PICK_OTHER_ENEMIES_WITH: pick_enemies_with(g, self, row->test, false); break;
    case PICK_ENEMIES_WITH_STRAY: pick_enemies_with_stray(g, self, row->test); break;
    case PICK_SLOT: only_candidate(g, row->slot); break;
    case PICK_RESERVE_ENEMY: pick_reserve_enemy(g); break;
    case PICK_OTHER_ENEMY_AT_RANDOM: pick_other_enemy_at_random(g, self); break;
    }
}

static void keep_first(GameState *g) { set_battle_first_candidate(g, battle_candidates(g, 0)); }

// $C1:AC2C
void battle_select_candidates_kept(GameState *g, uint8_t enemy, Selector selector)
{
    TIMED_AS(g, selector_routine(g, selector), battle_pick_candidates(g, enemy, selector));
    for (unsigned place = 0; place < BATTLE_CANDIDATES_COUNT; place++)
        set_battle_action_targets(g, place, battle_candidates(g, place));
    keep_first(g);
}

// $C1:AC14
void battle_select_candidates(GameState *g, uint8_t enemy, Selector selector)
{
    battle_clear_candidates(g);
    if (selector & SELECT_CONDITION_TARGET) {
        set_candidate(g, 0, battle_condition_target(g));
        keep_first(g);
    } else {
        battle_select_candidates_kept(g, enemy, selector);
    }
}

bool battle_take_first_by_enemy_order(GameState *g)
{
    uint8_t count = battle_candidate_count(g);
    for (unsigned place = 0; place < BATTLE_ENEMIES; place++)
        for (uint8_t c = 0; c < count; c++)
            if (battle_enemy_order(g, place) == battle_candidates(g, c)) {
                set_candidate(g, 0, battle_candidates(g, c));
                return true;
            }
    return false;
}

/* ---- A condition's target ------------------------------------------------------------------- */

/* $C1:AE21: the candidate a condition marked, as its target. With one marked
 * the original looks only at the first candidate, and takes battle slot 1
 * where that isn't the one marked; with more it goes by the order the timers
 * are looked for in (battle_timer_order), matching a candidate there but
 * testing the mark of the candidate at the order's place. */
bool battle_take_marked_candidate(GameState *g)
{
    uint8_t count = battle_candidate_count(g);
    if (count == 0)
        return false;
    Candidate taken;
    if (count == 1) {
        taken = (battle_candidates(g, 0) & CANDIDATE_MARKED) ? battle_candidates(g, 0) : 1;
    } else {
        bool found = false;
        for (unsigned c = 0; c < BATTLE_CANDIDATES_COUNT && !found; c++)
            for (unsigned place = 0; place < BATTLE_TIMER_ORDER_COUNT && !found; place++)
                if (slot_of(battle_candidates(g, c)) == battle_timer_order(g, place)
                    && (battle_candidates(g, place) & CANDIDATE_MARKED)) {
                    taken = battle_candidates(g, place);
                    found = true;
                }
        if (!found)
            return false;
    }
    only_candidate(g, taken & (uint8_t)~CANDIDATE_MARKED);
    return true;
}

/* ---- The action's targets as bits ---------------------------------------------------------- */

static uint16_t slot_bit(uint8_t slot) { return slot < 16 ? (uint16_t)(0x8000u >> slot) : 0; }

// $C1:AD09
void battle_mark_first_target(GameState *g)
{
    set_battle_action_target_bits(g, 0);
    Candidate first = battle_candidates(g, 0);
    if (battle_candidate_count(g) == 0 || first == NO_CANDIDATE)
        return;
    set_battle_action_target_bits(g, slot_bit(first));
    ORIGINAL_EFFECT(battle_leave_word(g, 0x00, 0));
}

// $C1:AD35
void battle_mark_other_targets(GameState *g)
{
    set_battle_action_other_targets(g, 0);
    uint8_t count = battle_candidate_count(g);
    if (count == 0)
        return;
    uint16_t bits = 0;
    for (uint8_t place = (uint8_t)(count - 1); place != 0; place--)
        bits |= slot_bit(battle_candidates(g, place));
    set_battle_action_other_targets(g, bits);
    if (count >= 2)
        ORIGINAL_EFFECT(battle_leave_word(g, 0x00, 0));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x02, 0));
}

/* ---- The entries ------------------------------------------------------------------------ */

/* Each selector's routine picks for the enemy the battle is working on
 * (battle_enemy_turn). */
static void selector_entry(Snes *s, Selector selector)
{
    GameState *g = state_of(s);
    battle_pick_candidates(g, battle_enemy_turn(g), selector);
    m_rts(s);
}

/* The party's selector leaves the count in X, where the everyone selector
 * goes on from it. */
void battle_select_party(Snes *s)
{
    GameState *g = state_of(s);
    battle_pick_candidates(g, battle_enemy_turn(g), SELECT_PARTY);
    s->cpu.x = battle_candidate_count(g);
    m_rts(s);
}

#define SELECTOR_ENTRY(name, selector) \
    void name(Snes *s) { selector_entry(s, selector); }
SELECTOR_ENTRY(battle_select_nobody, 0x00)
SELECTOR_ENTRY(battle_select_everyone, 0x02)
SELECTOR_ENTRY(battle_select_itself, 0x03)
SELECTOR_ENTRY(battle_select_last_to_act_on_it, 0x04)
SELECTOR_ENTRY(battle_select_random_member, 0x05)
SELECTOR_ENTRY(battle_select_nearest_member, 0x06)
SELECTOR_ENTRY(battle_select_farthest_member, 0x07)
SELECTOR_ENTRY(battle_select_weakest_member, 0x08)
SELECTOR_ENTRY(battle_select_members_by_condition, 0x09)
SELECTOR_ENTRY(battle_select_members_by_status_1, 0x0A)
SELECTOR_ENTRY(battle_select_members_by_status_2, 0x0B)
SELECTOR_ENTRY(battle_select_members_by_status_3, 0x0C)
SELECTOR_ENTRY(battle_select_members_by_status_4, 0x0D)
SELECTOR_ENTRY(battle_select_members_with_02, 0x0E)
SELECTOR_ENTRY(battle_select_members_with_80, 0x0F)
SELECTOR_ENTRY(battle_select_members_with_04, 0x10)
SELECTOR_ENTRY(battle_select_members_with_04_of_4, 0x11)
SELECTOR_ENTRY(battle_select_members_with_40_of_4, 0x12)
SELECTOR_ENTRY(battle_select_members_with_10_of_3, 0x13)
SELECTOR_ENTRY(battle_select_members_with_08_of_1, 0x14)
SELECTOR_ENTRY(battle_select_other_enemies, 0x15)
SELECTOR_ENTRY(battle_select_enemies, 0x16)
SELECTOR_ENTRY(battle_select_nearest_other_enemy, 0x17)
SELECTOR_ENTRY(battle_select_farthest_enemy, 0x18)
SELECTOR_ENTRY(battle_select_weakest_enemy, 0x19)
SELECTOR_ENTRY(battle_select_other_enemies_by_condition, 0x1A)
SELECTOR_ENTRY(battle_select_enemies_by_condition, 0x1B)
SELECTOR_ENTRY(battle_select_other_enemies_by_status_1, 0x1C)
SELECTOR_ENTRY(battle_select_enemies_by_status_1, 0x1D)
SELECTOR_ENTRY(battle_select_other_enemies_by_status_2, 0x1E)
SELECTOR_ENTRY(battle_select_enemies_by_status_2, 0x1F)
SELECTOR_ENTRY(battle_select_other_enemies_with_02, 0x20)
SELECTOR_ENTRY(battle_select_other_enemies_with_80, 0x21)
SELECTOR_ENTRY(battle_select_other_enemies_with_04, 0x22)
SELECTOR_ENTRY(battle_select_other_enemies_with_40_of_4, 0x23)
SELECTOR_ENTRY(battle_select_other_enemies_with_02_of_condition, 0x24)
SELECTOR_ENTRY(battle_select_other_enemies_with_01_of_19, 0x25)
SELECTOR_ENTRY(battle_select_weakest_other_enemy, 0x26)
SELECTOR_ENTRY(battle_select_slot_3, 0x27)
SELECTOR_ENTRY(battle_select_slot_4, 0x28)
SELECTOR_ENTRY(battle_select_slot_5, 0x29)
SELECTOR_ENTRY(battle_select_slot_6, 0x2A)
SELECTOR_ENTRY(battle_select_slot_7, 0x2B)
SELECTOR_ENTRY(battle_select_slot_8, 0x2C)
SELECTOR_ENTRY(battle_select_slot_9, 0x2D)
SELECTOR_ENTRY(battle_select_slot_10, 0x2E)
SELECTOR_ENTRY(battle_select_reserve_enemy, 0x2F)
SELECTOR_ENTRY(battle_select_slot_0, 0x30)
SELECTOR_ENTRY(battle_select_slot_1, 0x31)
SELECTOR_ENTRY(battle_select_slot_2, 0x32)
SELECTOR_ENTRY(battle_select_slot_3_again, 0x33)
SELECTOR_ENTRY(battle_select_slot_4_again, 0x34)
SELECTOR_ENTRY(battle_select_slot_5_again, 0x35)
SELECTOR_ENTRY(battle_select_slot_6_again, 0x36)
SELECTOR_ENTRY(battle_select_strongest_member, 0x37)
SELECTOR_ENTRY(battle_select_other_enemy_at_random, 0x38)

/* Selecting leaves the first candidate in A and the carry of its last compare,
 * set. The script's reading goes on at the selector. */
static void leave_selected(Snes *s)
{
    s->cpu.a = battle_candidates(state_of(s), 0);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

void battle_select_from_script(Snes *s)
{
    GameState *g = state_of(s);
    ScriptPlace at = (ScriptPlace)(battle_script_place(g) + 1);
    set_battle_script_place(g, at);
    battle_select_candidates(g, battle_enemy_turn(g), enemy_script_byte(g, at));
    leave_selected(s);
}

/* The selector comes in A, doubled in a byte to index the table: its top bit
 * is lost. */
uint8_t battle_selector_in_a(const Snes *s) { return (uint8_t)(s->cpu.a & 0x7F); }

void battle_select_by(Snes *s)
{
    GameState *g = state_of(s);
    battle_select_candidates_kept(g, battle_enemy_turn(g), battle_selector_in_a(s));
    leave_selected(s);
}

void battle_clear_candidates_entry(Snes *s)
{
    battle_clear_candidates(state_of(s));
    s->cpu.a = NO_CANDIDATE;
    s->cpu.x = BATTLE_CANDIDATES_COUNT;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* The bits' routines hand A back clear, and the carry of the last shift that
 * made a bit (set where the slot shifted it right out), or of what they
 * tested before, or as it came. */
enum { SLOT_SHIFTED_OUT = 16 };

static bool shift_carry(Candidate slot, bool carry) { return slot != 0 ? slot == SLOT_SHIFTED_OUT : carry; }

bool battle_first_mark_carry(const GameState *g, bool carry)
{
    if (battle_candidate_count(g) == 0)
        return carry;
    Candidate first = battle_candidates(g, 0);
    return first == NO_CANDIDATE || first == SLOT_SHIFTED_OUT;
}

bool battle_other_marks_carry(const GameState *g, bool carry)
{
    uint8_t count = battle_candidate_count(g);
    for (uint8_t place = (uint8_t)(count ? count - 1 : 0); place != 0; place--)
        carry = shift_carry(battle_candidates(g, place), carry);
    return carry;
}

/* The first's marking leaves A holding the none it found there. */
void battle_mark_first_target_entry(Snes *s)
{
    GameState *g = state_of(s);
    bool carry = battle_first_mark_carry(g, m_flag(s, CPU_FLAG_C));
    bool none_first = battle_candidate_count(g) != 0 && battle_candidates(g, 0) == NO_CANDIDATE;
    battle_mark_first_target(g);
    s->cpu.a = none_first ? NO_CANDIDATE : 0;
    m_set_flag(s, CPU_FLAG_C, carry);
    m_rts(s);
}

void battle_mark_other_targets_entry(Snes *s)
{
    GameState *g = state_of(s);
    bool carry = battle_other_marks_carry(g, m_flag(s, CPU_FLAG_C));
    battle_mark_other_targets(g);
    s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, carry);
    m_rts(s);
}

/* ---- The guards ------------------------------------------------------------------------- */

/* The candidates are counted as the battle counts them: at most one a
 * combatant. */
bool battle_candidates_counted(const GameState *g) { return battle_candidate_count(g) <= BATTLE_CANDIDATES_COUNT; }

bool battle_candidates_ready(Snes *s)
{
    vars_peek(true);
    bool counted = battle_candidates_counted(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && counted;
}
