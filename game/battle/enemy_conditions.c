/* Battle: the conditions of the enemies' scripts.
 *
 * A block of an enemy's script runs when its conditions hold. A condition is
 * four bytes, its kind and three operands, and most are about whom the enemy
 * is looking at: they pick candidates by a selector (battle/targets.h), mark
 * those the condition holds for -- at half their HP or less, with a status,
 * a party member it could reach -- and take one of those marked as the
 * block's target. Others are about the battle: whether an enemy of some kind
 * is there, how many enemies are left, how long the battle has gone on, what
 * last hit the enemy, a chance.
 *
 * A condition that holds has its block's actions chosen at once
 * (enemy_condition_held); one that doesn't leaves the block failed
 * (battle_script_outcome). A few keep the original's odd outcomes: some hold
 * without choosing anything, and some choose and then fail.
 *
 * The conditions are a C table by kind, a row each: the test, and for the
 * tests of where the combatants stand, which search they ask. Each reads its
 * operands at the condition's place, and moves the place the script is read
 * at (battle_script_place) as the original does, which is where the block's
 * actions are looked for from. */
#include "battle/enemy_scripts.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/positions.h"
#include "battle/random.h"
#include "battle/rules_data.h"
#include "battle/targets.h"
#include "charge.h"
#include "original_effects.h"
#include "vars.h"

/* ---- What they share ------------------------------------------------------------------------ */

/* An operand of the condition at `at`, the first after its kind. */
static uint8_t operand(const GameState *g, ScriptPlace at, unsigned n) { return enemy_script_byte(g, (ScriptPlace)(at + n)); }

static void fails(GameState *g) { set_battle_script_outcome(g, OUTCOME_FAILED); }

static void holds_if(GameState *g, bool held)
{
    if (held)
        enemy_condition_held(g);
    else
        fails(g);
}

/* Choose, and yet fail: two conditions choose the block's actions and still
 * leave the block failed. */
static void chooses_and_fails(GameState *g)
{
    enemy_condition_held(g);
    fails(g);
}

/* Pick the candidates by the selector in the condition's first operand. The
 * script is read at the selector from then on. */
static uint8_t select_by(GameState *g, ScriptPlace at)
{
    read_script_at(g, (ScriptPlace)(at + 1));
    TIMED_AS(g, 0xC1AC14, battle_select_candidates(g, operand(g, at, 1)));
    return battle_candidates_held(g);
}

static uint8_t enemy(const GameState *g) { return enemy_at_work(g); }
static uint8_t self(const GameState *g) { return enemy_at_work_slot(g); }

static Candidate candidate(const GameState *g, unsigned place) { return battle_candidate(g, place); }

static void mark(GameState *g, unsigned place)
{
    set_battle_candidates(g, place, battle_candidates(g, place) | CANDIDATE_MARKED);
}

/* The pair's bookkeeping, once a condition has taken its target: the first
 * of two keeps the target for the second, and the second closes the pair
 * ($C1:AEFD). */
enum { PAIR_FIRST = 0x01, PAIR_FIRST_HELD = 0x80 };

static void note_pair(GameState *g)
{
    uint8_t pair = battle_condition_pair(g);
    if (pair & PAIR_FIRST) {
        set_battle_condition_target(g, battle_candidates(g, 0) & CANDIDATE_SLOT);
        set_battle_condition_pair(g, PAIR_FIRST_HELD);
    } else if (pair & PAIR_FIRST_HELD) {
        set_battle_condition_pair(g, 0);
    }
}

/* The marked candidates, `marked` of them, are the only ones, and one of them
 * is taken as the target; the block's actions are chosen if the block still
 * holds (a first condition that failed leaves the second nothing to choose).
 * False where it doesn't. */
static bool take_marked(GameState *g, uint8_t marked)
{
    set_battle_candidate_count(g, marked);
    if (!battle_take_marked_candidate(g))
        enemy_block_fails(g);
    if (battle_script_outcome(g) != OUTCOME_HELD)
        return false;
    note_pair(g);
    enemy_condition_held(g);
    return true;
}

/* Most conditions leave a block they can't take a target for failed once,
 * however it stood. */
static void take_marked_or_fail(GameState *g, uint8_t marked)
{
    if (!take_marked(g, marked))
        fails(g);
}

/* The direct-page cells the conditions work in (battle_low_scratch). */
static void leave_byte(GameState *g, unsigned at, uint8_t v) { set_battle_low_scratch(g, at, v); }
static void leave_word(GameState *g, unsigned at, uint16_t v)
{
    set_battle_low_scratch(g, at, (uint8_t)v);
    set_battle_low_scratch(g, at + 1, (uint8_t)(v >> 8));
}

/* ---- About the candidates -------------------------------------------------------------------- */

/* A stat block's bytes the conditions read. */
enum { STAT_HP = 0x03, STAT_MAX_HP = 0x05, STAT_CONDITION = 0x1D, STOPPED_HIGH_BIT = 0x8000 };

// $C1:8EAB
/* Those picked at half their HP or less (and not stopped), one of them the
 * target. With nobody picked the condition holds without choosing. */
static void at_half_hp(GameState *g, ScriptPlace at)
{
    uint8_t count = select_by(g, at);
    if (count == 0)
        return;
    uint8_t marked = 0;
    for (uint8_t place = 0; place < count; place++) {
        uint8_t slot = candidate(g, place);
        if (combatant_stat_word_by_place(g, slot, STAT_CONDITION) & STOPPED_HIGH_BIT)
            continue;
        uint16_t hp = combatant_stat_word_by_place(g, slot, STAT_HP);
        ORIGINAL_EFFECT(leave_word(g, 0x08, hp));
        if (combatant_stat_word_by_place(g, slot, STAT_MAX_HP) / 2 >= hp) {
            mark(g, place);
            marked++;
        }
    }
    ORIGINAL_EFFECT(leave_word(g, 0x0A, marked));
    ORIGINAL_EFFECT(set_battle_candidate_place(g, count));
    take_marked(g, marked);
}

// $C1:8F11
/* The party members in the fight, or the enemies, with all the bits the
 * condition gives of a byte of their stat block (counted from its condition
 * byte), one of them the target. */
enum { SIDE_PARTY = 0 };

static void with_status(GameState *g, ScriptPlace at)
{
    uint8_t side = operand(g, at, 1), offset = operand(g, at, 2), bits = operand(g, at, 3);
    battle_clear_candidates(g);
    TIMED_AS(g, 0xC1AC2C, battle_select_candidates_kept(g, side == SIDE_PARTY ? SELECT_PARTY : SELECT_ENEMIES));
    uint8_t count = battle_candidates_held(g), marked = 0, place = 0;
    do {
        if ((combatant_stat_byte(g, candidate(g, place), (uint16_t)(STAT_CONDITION + offset)) & bits) == bits) {
            marked++;
            mark(g, place);
        }
    } while (++place < count);
    ORIGINAL_EFFECT(leave_word(g, 0x00, offset));
    ORIGINAL_EFFECT(leave_byte(g, 0x02, bits));
    ORIGINAL_EFFECT(leave_word(g, 0x04, place));
    ORIGINAL_EFFECT(leave_word(g, 0x06, marked));
    ORIGINAL_EFFECT(leave_byte(g, 0x08, side));
    if (marked == 0)
        fails(g);
    else
        take_marked_or_fail(g, marked);
}

// $C1:8F87
/* Those picked that are the enemy the condition names, one of them the
 * target. */
static void is_enemy(GameState *g, ScriptPlace at)
{
    uint8_t count = select_by(g, at);
    if (count == 0) {
        fails(g);
        return;
    }
    read_script_at(g, (ScriptPlace)(at + 2));
    uint8_t id = operand(g, at, 2), marked = 0;
    for (uint8_t place = 0; place < count; place++)
        if (combatant_fighting_as(g, candidate(g, place)) == id) {
            marked++;
            mark(g, place);
        }
    ORIGINAL_EFFECT(leave_word(g, 0x06, marked));
    ORIGINAL_EFFECT(leave_byte(g, 0x08, id));
    if (marked == 0)
        fails(g);
    else
        take_marked_or_fail(g, marked);
}

// $C1:90BE
/* Those picked with HP at most a number, one of them the target. */
static void at_most_hp(GameState *g, ScriptPlace at)
{
    uint8_t count = select_by(g, at);
    if (count == 0) {
        fails(g);
        return;
    }
    read_script_at(g, (ScriptPlace)(at + 2));
    uint16_t most = (uint16_t)(operand(g, at, 2) | operand(g, at, 3) << 8);
    /* the original keeps the number where a candidate that is none can read
     * it back as its HP */
    ORIGINAL_EFFECT(leave_word(g, 0x08, most));
    uint8_t marked = 0;
    for (uint8_t place = 0; place < count; place++)
        if (combatant_stat_word_by_wide_place(g, candidate(g, place), STAT_HP) <= most) {
            mark(g, place);
            marked++;
        }
    ORIGINAL_EFFECT(leave_word(g, 0x08, (uint8_t)most));
    ORIGINAL_EFFECT(leave_word(g, 0x0A, count));
    ORIGINAL_EFFECT(leave_word(g, 0x0C, marked));
    if (marked == 0)
        fails(g);
    else
        take_marked_or_fail(g, marked);
}

// $C1:9130, $C1:91F9, $C1:95FA
/* Whether any of those picked has a byte of its stat block at least, at most,
 * or equal to a number; if one has, one of all those picked is taken as the
 * target, none marked (so with more than one, none is: the original's). The
 * last reads the byte at the offset with whatever the cell after its offset
 * holds as the offset's high byte, as the original's pointer does. */
typedef enum { AT_LEAST, AT_MOST } StatTest;

static bool passes(StatTest test, uint8_t stat, uint8_t value)
{
    return test == AT_LEAST ? stat >= value : stat <= value;
}

static void any_stat(GameState *g, uint8_t count, uint16_t offset, uint8_t value, StatTest test)
{
    uint8_t place = 0;
    bool found = false;
    do {
        found = passes(test, combatant_stat_byte_by_wide_place(g, candidate(g, place), offset), value);
    } while (!found && ++place < count);
    ORIGINAL_EFFECT(leave_word(g, 0x0E, place));
    if (found)
        take_marked_or_fail(g, count);
    else
        fails(g);
}

static void stat_compared(GameState *g, ScriptPlace at, StatTest test)
{
    if (test == AT_MOST)
        ORIGINAL_EFFECT(leave_word(g, 0x0A, 0));
    uint8_t count = select_by(g, at);
    if (count == 0) {
        fails(g);
        return;
    }
    read_script_at(g, (ScriptPlace)(at + 3));
    uint8_t offset = operand(g, at, 2), value = operand(g, at, 3);
    ORIGINAL_EFFECT(leave_word(g, 0x0A, offset));
    ORIGINAL_EFFECT(leave_byte(g, 0x08, value));
    any_stat(g, count, offset, value, test);
}

static void stat_at_least(GameState *g, ScriptPlace at) { stat_compared(g, at, AT_LEAST); }
static void stat_at_most(GameState *g, ScriptPlace at) { stat_compared(g, at, AT_MOST); }

/* The test of a stat's value goes on from the first candidate to the one at
 * twice its slot in the list, and so on: the original keeps the doubled slot
 * it indexes the places with for the list as well. It counts in a cell its
 * stat's pointer can reach (with the offset's high byte whatever the cell
 * after it holds), so the count is kept there as it goes. */
static void stat_equal(GameState *g, uint8_t count, uint16_t offset, uint8_t value)
{
    uint8_t place = 0;
    uint16_t at = 0;
    bool found;
    ORIGINAL_EFFECT(leave_word(g, 0x0E, 0));
    for (;;) {
        Candidate slot = candidate(g, at);
        found = combatant_stat_byte_by_wide_place(g, slot, offset) == value;
        if (found || ++place >= count)
            break;
        ORIGINAL_EFFECT(leave_byte(g, 0x0E, place));
        at = (uint16_t)(slot * 2);
    }
    ORIGINAL_EFFECT(leave_word(g, 0x0E, place));
    if (found)
        take_marked_or_fail(g, count);
    else
        fails(g);
}

static void stat_is(GameState *g, ScriptPlace at)
{
    read_script_at(g, (ScriptPlace)(at + 1));
    uint8_t value = operand(g, at, 1);
    ORIGINAL_EFFECT(leave_byte(g, 0x08, value));
    uint8_t count = select_by(g, (ScriptPlace)(at + 1));
    if (count == 0) {
        fails(g);
        return;
    }
    uint8_t offset_low = operand(g, at, 3);
    ORIGINAL_EFFECT(leave_byte(g, 0x0A, offset_low));
    uint16_t offset = (uint16_t)(offset_low | battle_low_scratch(g, 0x0B) << 8);
    stat_equal(g, count, offset, value);
}

// $C1:918E
/* Whether the one picked has any of the bits the condition gives of a byte
 * of its stat block. With several picked, the first by the enemies' drawn
 * order is the one. */
static void stat_bits(GameState *g, ScriptPlace at)
{
    uint8_t count = select_by(g, at);
    if (count == 0 || (count != 1 && !battle_take_first_by_enemy_order(g))) {
        fails(g);
        return;
    }
    read_script_at(g, (ScriptPlace)(at + 2));
    uint8_t offset = operand(g, at, 2);
    uint8_t stat = combatant_stat_byte_by_wide_place(g, candidate(g, 0), offset);
    ORIGINAL_EFFECT(leave_word(g, 0x0A, offset));
    ORIGINAL_EFFECT(leave_byte(g, 0x08, stat));
    read_script_at(g, (ScriptPlace)(at + 3));
    holds_if(g, (operand(g, at, 3) & stat) != 0);
}

// $C1:96D4
/* Whether the one picked is a party member of the fight (compared, as the
 * original does, with the party members' ids): the condition chooses where
 * it matches as the operands say, and fails whatever. */
static void is_party_member(GameState *g, ScriptPlace at)
{
    uint8_t count = select_by(g, at);
    if (count == 0) {
        fails(g);
        return;
    }
    read_script_at(g, (ScriptPlace)(at + 2));
    uint8_t whatever_state = operand(g, at, 2), not_member = operand(g, at, 3);
    ORIGINAL_EFFECT(leave_byte(g, 0x08, whatever_state));
    ORIGINAL_EFFECT(leave_byte(g, 0x0A, not_member));
    uint8_t member = 0;
    while (member < BATTLE_PARTY_SIZE && candidate(g, 0) != battle_combatant_id(g, member))
        member++;
    bool choose;
    if (member == BATTLE_PARTY_SIZE)
        choose = not_member != 0;
    else
        choose = not_member != 1 && (whatever_state == 1 || combatant_in_fight(g, member));
    if (choose)
        enemy_condition_held(g);
    fails(g);
}

// $C1:9728
/* Whether the enemy three slots after the one picked is out of the fight, or
 * in it: the original reads the fighting ids from the enemies' start with the
 * candidate's battle slot. */
static void three_on_out(GameState *g, ScriptPlace at)
{
    select_by(g, at);
    read_script_at(g, (ScriptPlace)(at + 3));
    uint8_t out_wanted = operand(g, at, 3);
    ORIGINAL_EFFECT(leave_byte(g, 0x0A, out_wanted));
    bool out = combatant_fighting_as(g, (unsigned)candidate(g, 0) + BATTLE_FIRST_ENEMY) == COMBATANT_NONE;
    holds_if(g, out ? out_wanted != 0 : out_wanted == 0);
}

// $C1:97D5
/* Whether the one picked has a turn wait under a number, or not. */
static void turn_wait(GameState *g, ScriptPlace at)
{
    select_by(g, at);
    uint8_t limit = operand(g, at, 2), at_least = operand(g, at, 3);
    ORIGINAL_EFFECT(leave_byte(g, 0x08, limit));
    uint8_t wait = combatant_turn_wait(g, candidate(g, 0));
    holds_if(g, at_least == 0 ? wait < limit : wait >= limit);
    ORIGINAL_EFFECT(leave_byte(g, 0x09, 0));
}

/* ---- Where they stand --------------------------------------------------------------------- */

static SearchAnswer search(GameState *g, SearchKind kind, uint8_t other, uint8_t third)
{
    SearchAnswer a;
    TIMED_AS(g, 0xC12986, a = battle_search(g, kind, self(g), other, third));
    return a;
}

/* The answer the condition's second operand asks for: that the search's test
 * held, or with it set, that it didn't. */
static bool as_asked(GameState *g, ScriptPlace at, bool held) { return operand(g, at, 2) != 0 ? !held : held; }

// $C1:925D, $C1:9765
/* Whether the first one picked is within the enemy's reach, or its wider
 * reach (or, as asked, not); where it holds it is the only candidate. */
static void reach(GameState *g, ScriptPlace at, SearchKind kind)
{
    uint8_t count = select_by(g, at);
    if (count == 0) {
        fails(g);
        return;
    }
    SearchAnswer a = search(g, kind, candidate(g, 0), battle_search_third(g));
    if (!as_asked(g, at, a.held)) {
        fails(g);
        return;
    }
    set_battle_candidate_count(g, 1);
    enemy_condition_held(g);
}

// $C1:92A3, $C1:9314
/* Those picked, the enemy left out, that stand level with it, or below it or
 * to its right as the condition's third operand says: one of them is taken.
 * Whether the block's actions are chosen then goes by the last one's
 * answer, as the second operand asks; otherwise, and with nobody picked, it
 * holds without choosing. */
static void each_stands(GameState *g, ScriptPlace at, SearchKind kind)
{
    uint8_t count = select_by(g, at);
    if (count == 0)
        return;
    set_battle_target_search(g, kind);
    set_battle_target_searcher(g, self(g));
    uint8_t marked = 0, place = 0;
    bool held = battle_search_failed(g) == 0;
    do {
        uint8_t other = candidate(g, place);
        if (other == self(g))
            continue;
        held = search(g, kind, other, battle_search_third(g)).held;
        if (held) {
            mark(g, place);
            marked++;
        }
    } while (++place < count);
    ORIGINAL_EFFECT(leave_word(g, 0x0A, place));
    ORIGINAL_EFFECT(leave_word(g, 0x0C, marked));
    ORIGINAL_EFFECT(leave_word(g, 0x0E, self(g)));
    set_battle_candidate_count(g, marked);
    if (!battle_take_marked_candidate(g))
        enemy_block_fails(g);
    if (battle_script_outcome(g) == OUTCOME_HELD && as_asked(g, at, held))
        enemy_condition_held(g);
}

static void level_with(GameState *g, ScriptPlace at) { each_stands(g, at, SEARCH_LEVEL); }

enum { FIRST_SIDE_SEARCH = SEARCH_OTHER_BELOW };

static void stands_beside(GameState *g, ScriptPlace at)
{
    each_stands(g, at, (SearchKind)(uint8_t)(operand(g, at, 3) + FIRST_SIDE_SEARCH));
}

// $C1:938D
/* Whether the enemy could walk to the first one picked (or, as asked, not). */
enum { KEEP_CLEAR = 0x01 };

static void could_walk_to(GameState *g, ScriptPlace at)
{
    uint8_t count = select_by(g, at);
    if (count == 0) {
        fails(g);
        return;
    }
    uint8_t clear = (uint8_t)(operand(g, at, 3) << 2 | KEEP_CLEAR);
    ORIGINAL_EFFECT(leave_byte(g, 0x0E, KEEP_CLEAR));
    SearchAnswer a = search(g, SEARCH_WALK, candidate(g, 0), clear);
    holds_if(g, as_asked(g, at, a.held));
}

// $C1:93E6
/* Whether the enemy stands in the battlefield's lower or upper half, or at
 * its right or left edge, as the operands say. */
static void stands_in(GameState *g, ScriptPlace at)
{
    bool edge = operand(g, at, 3) != 0, second = operand(g, at, 2) != 0;
    SearchKind kind = edge ? (second ? SEARCH_AT_RIGHT_EDGE : SEARCH_AT_LEFT_EDGE)
                           : (second ? SEARCH_IN_UPPER_HALF : SEARCH_IN_LOWER_HALF);
    holds_if(g, search(g, kind, battle_search_other(g), battle_search_third(g)).held);
}

/* ---- About the battle ------------------------------------------------------------------------ */

// $C1:8FDA
/* Whether an enemy of the kind the condition names is in the fight (or, as
 * asked, not). The original looks until the enemies' count, from the first,
 * at least once. */
static void enemy_present(GameState *g, ScriptPlace at)
{
    read_script_at(g, (ScriptPlace)(at + 2));
    uint8_t id = operand(g, at, 2), absent = operand(g, at, 3);
    ORIGINAL_EFFECT(leave_byte(g, 0x08, id));
    ORIGINAL_EFFECT(leave_byte(g, 0x0A, absent));
    bool present = false;
    uint8_t enemy = 0;
    do
        present = combatant_fighting_as(g, (unsigned)BATTLE_FIRST_ENEMY + enemy) == id;
    while (!present && ++enemy != battle_enemy_count(g));
    holds_if(g, present ? absent == 0 : absent != 0);
}

// $C1:9656
/* Whether an enemy of another kind than the condition names is in the fight:
 * the first one found (where asked, not the enemy itself) decides by the
 * condition's third operand; with none, it holds unless asked about the
 * others. */
static void other_kind_present(GameState *g, ScriptPlace at)
{
    read_script_at(g, (ScriptPlace)(at + 1));
    uint8_t id = operand(g, at, 1), not_itself = operand(g, at, 2), holds_then = operand(g, at, 3);
    ORIGINAL_EFFECT(leave_byte(g, 0x08, id));
    ORIGINAL_EFFECT(leave_byte(g, 0x0A, not_itself));
    ORIGINAL_EFFECT(leave_byte(g, 0x0C, holds_then));
    uint8_t enemy = 0;
    do {
        uint8_t fighting = combatant_fighting_as(g, (uint8_t)(BATTLE_FIRST_ENEMY + enemy));
        if (fighting == COMBATANT_NONE || fighting == id)
            continue;
        if (not_itself != 0 && enemy == enemy_at_work(g))
            continue;
        holds_if(g, holds_then != 0);
        return;
    } while (++enemy != battle_enemy_count(g));
    holds_if(g, not_itself == 0);
}

// $C1:9013
/* Whether at most so many enemies are in the fight (not counting those whose
 * standing is anything but plain). */
static void enemies_at_most(GameState *g, ScriptPlace at)
{
    uint8_t most = operand(g, at, 1), counted = 0;
    for (uint8_t enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        if (combatant_in_fight(g, (uint8_t)(BATTLE_FIRST_ENEMY + enemy)) && battle_enemy_standing(g, enemy) == 0)
            counted++;
    ORIGINAL_EFFECT(leave_byte(g, 0x08, most));
    ORIGINAL_EFFECT(leave_word(g, 0x0A, counted));
    holds_if(g, most >= counted);
}

// $C1:96A5
/* Whether at most so many party members are in the fight. */
static void members_at_most(GameState *g, ScriptPlace at)
{
    read_script_at(g, (ScriptPlace)(at + 1));
    uint8_t limit = (uint8_t)(operand(g, at, 1) + 1), fighting = 0;
    ORIGINAL_EFFECT(leave_byte(g, 0x08, limit));
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (combatant_in_fight(g, member))
            fighting++;
    holds_if(g, fighting < limit);
}

// $C1:9045
/* Whether the battle has gone on at least so many frames. */
static void frames_at_least(GameState *g, ScriptPlace at)
{
    uint32_t least = (uint32_t)(operand(g, at, 1) | operand(g, at, 2) << 8 | (uint32_t)operand(g, at, 3) << 16);
    ORIGINAL_EFFECT(leave_byte(g, 0x08, operand(g, at, 1)));
    ORIGINAL_EFFECT(leave_byte(g, 0x09, operand(g, at, 2)));
    ORIGINAL_EFFECT(leave_byte(g, 0x0A, operand(g, at, 3)));
    holds_if(g, (battle_frames(g) & 0xFFFFFF) >= least);
}

// $C1:9082
/* Whether the enemy's own count (battle_enemy_script_counter) is at most a
 * number, or at least it. */
enum { COUNT_AT_MOST = 1 };

static void count_compared(GameState *g, ScriptPlace at)
{
    uint8_t number = operand(g, at, 1), how = operand(g, at, 2), count = battle_enemy_script_counter(g, enemy(g));
    ORIGINAL_EFFECT(leave_byte(g, 0x08, number));
    ORIGINAL_EFFECT(leave_byte(g, 0x0A, how));
    holds_if(g, how == COUNT_AT_MOST ? count <= number : count >= number);
}

// $C1:95DA
/* A chance, in percent. */
enum { PERCENT = 100 };

static void chance(GameState *g, ScriptPlace at)
{
    uint8_t percent = operand(g, at, 1);
    ORIGINAL_EFFECT(leave_byte(g, 0x00, percent));
    holds_if(g, battle_random(g, 0, PERCENT) < percent);
}

// $C1:97AB, $C1:97C0
/* Whether the enemy was beaten by the action, and whether its standing is
 * anything but plain. */
static void was_beaten(GameState *g, ScriptPlace at)
{
    (void)at;
    holds_if(g, battle_enemy_beaten(g, enemy(g)) != 0);
}

static void stands_apart(GameState *g, ScriptPlace at)
{
    (void)at;
    holds_if(g, battle_enemy_standing(g, enemy(g)) != 0);
}

static void always(GameState *g, ScriptPlace at)
{
    (void)at;
    enemy_condition_held(g);
}

static void chooses_but_fails(GameState *g, ScriptPlace at)
{
    (void)at;
    chooses_and_fails(g);
}

/* ---- What last acted on it ------------------------------------------------------------------ */

/* The enemy's own action record (battle_chosen_actions, by its slot) keeps
 * what last acted on it: in its first byte whether an attack ($4x) or a tech
 * ($5x), bit 4, and the battle slot of whoever did it; in its second the
 * attack or tech; in its third its elements. */
enum { BY_ATTACK = 0x40, BY_TECH = 0x50, HOW_ACTED = 0xF0, ACTED_BIT_4 = 0x10, ACTOR = 0x0F };

static uint8_t record_byte(const GameState *g, unsigned n) { return (uint8_t)(battle_chosen_actions(g, self(g)) >> (8 * n)); }

/* The attack or tech the first operand names, as the record says it; false
 * for anything else. */
static bool how_named(uint8_t operand_1, uint8_t *how)
{
    if (operand_1 > 1)
        return false;
    *how = operand_1 == 0 ? BY_ATTACK : BY_TECH;
    return true;
}

// $C1:942A
/* Whether an attack (or a tech) last acted on it, or not. */
static void acted_on_by(GameState *g, ScriptPlace at)
{
    uint8_t negated = operand(g, at, 3), how;
    ORIGINAL_EFFECT(leave_byte(g, 0x02, negated));
    if (!how_named(operand(g, at, 1), &how)) {
        fails(g);
        return;
    }
    ORIGINAL_EFFECT(leave_byte(g, 0x00, how));
    bool was = (record_byte(g, 0) & HOW_ACTED) == how;
    holds_if(g, negated == 0 ? was : !was);
}

// $C1:9474
/* Whether that attack or tech last acted on it (or, asked, neither the kind
 * nor the number did). */
static void acted_on_by_this(GameState *g, ScriptPlace at)
{
    uint8_t number = operand(g, at, 2), negated = operand(g, at, 3), how;
    ORIGINAL_EFFECT(leave_byte(g, 0x04, negated));
    ORIGINAL_EFFECT(leave_byte(g, 0x02, number));
    if (!how_named(operand(g, at, 1), &how)) {
        fails(g);
        return;
    }
    ORIGINAL_EFFECT(leave_byte(g, 0x00, how));
    bool kind = (record_byte(g, 0) & HOW_ACTED) == how, same = record_byte(g, 1) == number;
    holds_if(g, negated == 0 ? kind && same : !kind && !same);
}

// $C1:94D2
/* Whether the record's bit 4 is as the condition says, or not. */
static void acted_bit(GameState *g, ScriptPlace at)
{
    uint8_t negated = operand(g, at, 3), wanted = (uint8_t)((operand(g, at, 1) & 1) << 4);
    ORIGINAL_EFFECT(leave_byte(g, 0x02, negated));
    ORIGINAL_EFFECT(leave_byte(g, 0x00, wanted));
    bool same = (record_byte(g, 0) & ACTED_BIT_4) == wanted;
    holds_if(g, negated == 0 ? same : !same);
}

// $C1:9514
/* Whether whoever last acted on it (with the record's bit 4 clear) is the
 * party member the condition names. Its other forms keep the original's
 * slips: asked "not", it looks at the first party member whoever acted; and
 * asked about the whole party it fails, or asked "not" holds, whatever. */
static void acted_on_by_member(GameState *g, ScriptPlace at)
{
    uint8_t id = operand(g, at, 1), whole_party = operand(g, at, 2), negated = operand(g, at, 3);
    ORIGINAL_EFFECT(leave_byte(g, 0x04, negated));
    ORIGINAL_EFFECT(leave_byte(g, 0x02, whole_party));
    ORIGINAL_EFFECT(leave_byte(g, 0x00, id));
    uint8_t record = record_byte(g, 0);
    if (record & ACTED_BIT_4) {
        fails(g);
        return;
    }
    if (whole_party != 0) {
        holds_if(g, negated != 0);
        return;
    }
    if (negated == 0)
        holds_if(g, combatant_id(g, record & ACTOR) == id);
    else
        holds_if(g, battle_combatant_id(g, 0) != id);
}

// $C1:959A
/* Whether what last acted on it had all the elements the condition names,
 * or not. */
static void acted_with_elements(GameState *g, ScriptPlace at)
{
    uint8_t elements = operand(g, at, 1), negated = operand(g, at, 3);
    ORIGINAL_EFFECT(leave_byte(g, 0x04, negated));
    ORIGINAL_EFFECT(leave_byte(g, 0x02, elements));
    bool all = (record_byte(g, 2) & elements) == elements;
    holds_if(g, negated == 0 ? all : !all);
}

/* ---- The table ------------------------------------------------------------------------------- */

typedef void (*ConditionTest)(GameState *g, ScriptPlace at);

typedef struct {
    ConditionTest test;
    SearchKind search;  /* reach */
    uint16_t routine;   /* the original's, in bank C1: its time is the condition's */
} ConditionRow;

static void within_reach(GameState *g, ScriptPlace at, SearchKind kind) { reach(g, at, kind); }

#define TEST(f, routine) { f, SEARCH_KINDS, routine }
#define REACH(k, routine) { NULL, k, routine }

// $C1:B80D
static const ConditionRow conditions[CONDITION_KINDS] = {
    TEST(always, 0x8EA7),                    /* $00 */
    TEST(at_half_hp, 0x8EAB),                /* $01 */
    TEST(with_status, 0x8F11),               /* $02 */
    TEST(is_enemy, 0x8F87),                  /* $03 */
    TEST(enemy_present, 0x8FDA),             /* $04 */
    TEST(enemies_at_most, 0x9013),           /* $05 */
    TEST(frames_at_least, 0x9045),           /* $06 */
    TEST(count_compared, 0x9082),            /* $07 */
    TEST(at_most_hp, 0x90BE),                /* $08 */
    TEST(stat_at_least, 0x9130),             /* $09 */
    TEST(stat_bits, 0x918E),                 /* $0A */
    TEST(stat_at_most, 0x91F9),              /* $0B */
    REACH(SEARCH_WITHIN_REACH, 0x925D),      /* $0C */
    TEST(level_with, 0x92A3),                /* $0D */
    TEST(stands_beside, 0x9314),             /* $0E */
    TEST(could_walk_to, 0x938D),             /* $0F */
    TEST(stands_in, 0x93E6),                 /* $10 */
    TEST(acted_on_by, 0x942A),               /* $11 */
    TEST(acted_on_by_this, 0x9474),          /* $12 */
    TEST(acted_bit, 0x94D2),                 /* $13 */
    TEST(acted_on_by_member, 0x9514),        /* $14 */
    TEST(acted_with_elements, 0x959A),       /* $15 */
    TEST(always, 0x8EA7),                    /* $16 */
    TEST(chance, 0x95DA),                    /* $17 */
    TEST(stat_is, 0x95FA),                   /* $18 */
    TEST(always, 0x8EA7),                    /* $19 */
    TEST(other_kind_present, 0x9656),        /* $1A */
    TEST(members_at_most, 0x96A5),           /* $1B */
    TEST(is_party_member, 0x96D4),           /* $1C */
    TEST(three_on_out, 0x9728),              /* $1D */
    TEST(chooses_but_fails, 0x975C),         /* $1E */
    REACH(SEARCH_WITHIN_WIDE_REACH, 0x9765), /* $1F */
    TEST(was_beaten, 0x97AB),                /* $20 */
    TEST(stands_apart, 0x97C0),              /* $21 */
    TEST(turn_wait, 0x97D5),                 /* $22 */
    TEST(stat_is, 0x95FA), TEST(stat_is, 0x95FA), TEST(stat_is, 0x95FA), /* $23-$25 */
    TEST(stat_is, 0x95FA), TEST(stat_is, 0x95FA), TEST(stat_is, 0x95FA), /* $26-$28 */
};

uint16_t enemy_condition_routine(uint8_t kind) { return kind < CONDITION_KINDS ? conditions[kind].routine : 0; }

void enemy_test_condition(GameState *g, uint8_t kind)
{
    if (kind >= CONDITION_KINDS)
        return;
    const ConditionRow *row = &conditions[kind];
    ScriptPlace at = battle_script_place(g);
    if (row->test)
        TIMED_AS(g, 0xC10000u | row->routine, row->test(g, at));
    else
        TIMED_AS(g, 0xC10000u | row->routine, within_reach(g, at, row->search));
}

/* ---- The entries ------------------------------------------------------------------------ */

/* Each condition's routine is called with the script read at the condition,
 * and hands A back clear, which the original's callers index with. */
static void condition_entry(Snes *s, ConditionTest test)
{
    GameState *g = state_of(s);
    test(g, battle_script_place(g));
    s->cpu.a = 0;
    m_rts(s);
}

static void reach_entry(Snes *s, SearchKind kind)
{
    GameState *g = state_of(s);
    reach(g, battle_script_place(g), kind);
    s->cpu.a = 0;
    m_rts(s);
}

#define CONDITION_ENTRY(name, test) \
    void name(Snes *s) { condition_entry(s, test); }
CONDITION_ENTRY(battle_condition_always, always)
CONDITION_ENTRY(battle_condition_at_half_hp, at_half_hp)
CONDITION_ENTRY(battle_condition_with_status, with_status)
CONDITION_ENTRY(battle_condition_is_enemy, is_enemy)
CONDITION_ENTRY(battle_condition_enemy_present, enemy_present)
CONDITION_ENTRY(battle_condition_enemies_at_most, enemies_at_most)
CONDITION_ENTRY(battle_condition_frames_at_least, frames_at_least)
CONDITION_ENTRY(battle_condition_count_compared, count_compared)
CONDITION_ENTRY(battle_condition_at_most_hp, at_most_hp)
CONDITION_ENTRY(battle_condition_stat_at_least, stat_at_least)
CONDITION_ENTRY(battle_condition_stat_bits, stat_bits)
CONDITION_ENTRY(battle_condition_stat_at_most, stat_at_most)
CONDITION_ENTRY(battle_condition_level_with, level_with)
CONDITION_ENTRY(battle_condition_stands_beside, stands_beside)
CONDITION_ENTRY(battle_condition_could_walk_to, could_walk_to)
CONDITION_ENTRY(battle_condition_stands_in, stands_in)
CONDITION_ENTRY(battle_condition_acted_on_by, acted_on_by)
CONDITION_ENTRY(battle_condition_acted_on_by_this, acted_on_by_this)
CONDITION_ENTRY(battle_condition_acted_bit, acted_bit)
CONDITION_ENTRY(battle_condition_acted_on_by_member, acted_on_by_member)
CONDITION_ENTRY(battle_condition_acted_with_elements, acted_with_elements)
CONDITION_ENTRY(battle_condition_chance, chance)
CONDITION_ENTRY(battle_condition_stat_is, stat_is)
CONDITION_ENTRY(battle_condition_other_kind_present, other_kind_present)
CONDITION_ENTRY(battle_condition_members_at_most, members_at_most)
CONDITION_ENTRY(battle_condition_is_party_member, is_party_member)
CONDITION_ENTRY(battle_condition_three_on_out, three_on_out)
CONDITION_ENTRY(battle_condition_chooses_but_fails, chooses_but_fails)
CONDITION_ENTRY(battle_condition_was_beaten, was_beaten)
CONDITION_ENTRY(battle_condition_stands_apart, stands_apart)
CONDITION_ENTRY(battle_condition_turn_wait, turn_wait)

void battle_condition_within_reach(Snes *s) { reach_entry(s, SEARCH_WITHIN_REACH); }
void battle_condition_within_wide_reach(Snes *s) { reach_entry(s, SEARCH_WITHIN_WIDE_REACH); }
