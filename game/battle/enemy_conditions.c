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
 * Each condition says how it came out (ConditionResult); the block's testing
 * (battle/enemy_scripts.c) acts on that. A few keep the original's odd
 * outcomes: some hold without choosing anything, and some choose and then
 * fail.
 *
 * The conditions are a C table by kind, a row each: the test, the layout of
 * its operands, which the table decodes into the test's parameters, and how
 * far the original reads the script for it (the choosing looks for the
 * block's actions from there). */
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

static ConditionResult fails(void) { return (ConditionResult){ .verdict = CONDITION_FAILS }; }
static ConditionResult holds(void) { return (ConditionResult){ .verdict = CONDITION_HOLDS }; }
static ConditionResult holds_if(bool held) { return held ? holds() : fails(); }
static ConditionResult holds_unchosen(void) { return (ConditionResult){ .verdict = CONDITION_HOLDS_UNCHOSEN }; }

/* Nobody picked: the condition fails, having read no further than its
 * selector. */
static ConditionResult nobody_picked(void) { return (ConditionResult){ .verdict = CONDITION_FAILS, .stopped_at_selector = true }; }

/* It holds for the target it took, if it could take one; a block no longer
 * held comes out `otherwise`. */
static ConditionResult for_target(bool taken, ConditionVerdict otherwise)
{
    return (ConditionResult){
        .verdict = CONDITION_HOLDS, .takes_target = true, .target_taken = taken, .pairs = true, .otherwise = otherwise,
    };
}

static uint8_t self(const ScriptChoosing *c) { return (uint8_t)(BATTLE_FIRST_ENEMY + c->enemy); }

/* Pick the candidates by the selector: how many there are. */
static uint8_t select_by(GameState *g, const ScriptChoosing *c, Selector who)
{
    TIMED_AS(g, 0xC1AC14, battle_select_candidates(g, c->enemy, who));
    return battle_candidates_held(g);
}

static Candidate candidate(const GameState *g, unsigned place) { return battle_candidate(g, place); }

static void mark(GameState *g, unsigned place)
{
    set_battle_candidates(g, place, battle_candidates(g, place) | CANDIDATE_MARKED);
}

/* The marked candidates, `marked` of them, are the only ones, and one of them
 * is taken as the target: false where there is none to take. */
static bool take_marked(GameState *g, uint8_t marked)
{
    set_battle_candidate_count(g, marked);
    return battle_take_marked_candidate(g);
}

/* ---- About the candidates -------------------------------------------------------------------- */

/* The condition byte's top bit, as the high byte of a word read at it, is
 * stopped. */
enum { STOPPED_HIGH_BIT = 0x8000 };

// $C1:8EAB
/* Those picked at half their HP or less (and not stopped), one of them the
 * target. With nobody picked the condition holds without choosing. */
static ConditionResult at_half_hp(GameState *g, const ScriptChoosing *c, Selector who)
{
    uint8_t count = select_by(g, c, who);
    if (count == 0)
        return holds_unchosen();
    uint8_t marked = 0;
    for (uint8_t place = 0; place < count; place++) {
        uint8_t slot = candidate(g, place);
        if (combatant_stat_word_by_place(g, slot, STAT_CONDITION) & STOPPED_HIGH_BIT)
            continue;
        uint16_t hp = combatant_stat_word_by_place(g, slot, STAT_HP);
        ORIGINAL_EFFECT(battle_leave_word(g, 0x08, hp));
        if (combatant_stat_word_by_place(g, slot, STAT_MAX_HP) / 2 >= hp) {
            mark(g, place);
            marked++;
        }
    }
    battle_keep_word_at_0a(g, marked);
    ORIGINAL_EFFECT(set_battle_candidate_place(g, count));
    return for_target(take_marked(g, marked), CONDITION_HOLDS_UNCHOSEN);
}

// $C1:8F11
/* The party members in the fight, or the enemies, with all the bits the
 * condition gives of a byte of their stat block (counted from its condition
 * byte), one of them the target. */
enum { SIDE_PARTY = 0 };

static ConditionResult with_status(GameState *g, const ScriptChoosing *c, uint8_t side, uint8_t offset, uint8_t bits)
{
    battle_clear_candidates(g);
    TIMED_AS(g, 0xC1AC2C, battle_select_candidates_kept(g, c->enemy, side == SIDE_PARTY ? SELECT_PARTY : SELECT_ENEMIES));
    uint8_t count = battle_candidates_held(g), marked = 0, place = 0;
    do {
        if ((combatant_stat_byte(g, candidate(g, place), (uint16_t)(STAT_CONDITION + offset)) & bits) == bits) {
            marked++;
            mark(g, place);
        }
    } while (++place < count);
    ORIGINAL_EFFECT(battle_leave_word(g, 0x00, offset));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x02, bits));
    ORIGINAL_EFFECT(battle_leave_word(g, 0x04, place));
    ORIGINAL_EFFECT(battle_leave_word(g, 0x06, marked));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x08, side));
    return marked == 0 ? fails() : for_target(take_marked(g, marked), CONDITION_FAILS);
}

// $C1:8F87
/* Those picked that are the enemy the condition names, one of them the
 * target. */
static ConditionResult is_enemy(GameState *g, const ScriptChoosing *c, Selector who, uint8_t id)
{
    uint8_t count = select_by(g, c, who);
    if (count == 0)
        return nobody_picked();
    uint8_t marked = 0;
    for (uint8_t place = 0; place < count; place++)
        if (combatant_fighting_as(g, candidate(g, place)) == id) {
            marked++;
            mark(g, place);
        }
    ORIGINAL_EFFECT(battle_leave_word(g, 0x06, marked));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x08, id));
    return marked == 0 ? fails() : for_target(take_marked(g, marked), CONDITION_FAILS);
}

// $C1:90BE
/* Those picked with HP at most a number, one of them the target. The
 * original keeps the number in the cells at $08, where a candidate that is
 * none reads its HP (through the table of the stat blocks' places), so that
 * is kept plainly. */
static ConditionResult at_most_hp(GameState *g, const ScriptChoosing *c, Selector who, uint16_t most)
{
    uint8_t count = select_by(g, c, who);
    if (count == 0)
        return nobody_picked();
    battle_leave_word(g, 0x08, most);
    uint8_t marked = 0;
    for (uint8_t place = 0; place < count; place++)
        if (combatant_stat_word_by_wide_place(g, candidate(g, place), STAT_HP) <= most) {
            mark(g, place);
            marked++;
        }
    ORIGINAL_EFFECT(battle_leave_word(g, 0x08, (uint8_t)most));
    battle_keep_word_at_0a(g, count);
    ORIGINAL_EFFECT(battle_leave_word(g, 0x0C, marked));
    return marked == 0 ? fails() : for_target(take_marked(g, marked), CONDITION_FAILS);
}

// $C1:9130, $C1:91F9, $C1:95FA
/* Whether any of those picked has a byte of its stat block at least, at most,
 * or equal to a number; if one has, one of all those picked is taken as the
 * target, none marked (so with more than one, none is: the original's). */
typedef enum { AT_LEAST, AT_MOST } StatTest;

static bool passes(StatTest test, uint8_t stat, uint8_t value)
{
    return test == AT_LEAST ? stat >= value : stat <= value;
}

static ConditionResult any_stat(GameState *g, uint8_t count, uint16_t offset, uint8_t value, StatTest test)
{
    uint8_t place = 0;
    bool found = false;
    do {
        found = passes(test, combatant_stat_byte_by_wide_place(g, candidate(g, place), offset), value);
    } while (!found && ++place < count);
    ORIGINAL_EFFECT(battle_leave_word(g, 0x0E, place));
    return found ? for_target(take_marked(g, count), CONDITION_FAILS) : fails();
}

static ConditionResult stat_compared(GameState *g, const ScriptChoosing *c, Selector who, uint8_t offset, uint8_t value,
                                     StatTest test)
{
    if (test == AT_MOST)
        battle_keep_word_at_0a(g, 0);
    uint8_t count = select_by(g, c, who);
    if (count == 0)
        return nobody_picked();
    battle_keep_word_at_0a(g, offset);
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x08, value));
    return any_stat(g, count, offset, value, test);
}

static ConditionResult stat_at_least(GameState *g, const ScriptChoosing *c, Selector who, uint8_t offset, uint8_t value)
{
    return stat_compared(g, c, who, offset, value, AT_LEAST);
}

static ConditionResult stat_at_most(GameState *g, const ScriptChoosing *c, Selector who, uint8_t offset, uint8_t value)
{
    return stat_compared(g, c, who, offset, value, AT_MOST);
}

/* The test of a stat's value goes on from the first candidate to the one at
 * twice its slot in the list, and so on: the original keeps the doubled slot
 * it indexes the places with for the list as well. It counts in a cell its
 * stat's pointer can reach, so the count is kept there as it goes. */
static ConditionResult stat_equal(GameState *g, uint8_t count, uint16_t offset, uint8_t value)
{
    uint8_t place = 0;
    uint16_t at = 0;
    bool found;
    ORIGINAL_EFFECT(battle_leave_word(g, 0x0E, 0));
    for (;;) {
        Candidate slot = candidate(g, at);
        found = combatant_stat_byte_by_wide_place(g, slot, offset) == value;
        if (found || ++place >= count)
            break;
        ORIGINAL_EFFECT(battle_leave_byte(g, 0x0E, place));
        at = (uint16_t)(slot * 2);
    }
    ORIGINAL_EFFECT(battle_leave_word(g, 0x0E, place));
    return found ? for_target(take_marked(g, count), CONDITION_FAILS) : fails();
}

/* The stat is named by its offset's low byte; its high byte is whatever the
 * last word kept at $0A left (battle.h), as the original's pointer reads it. */
static ConditionResult stat_is(GameState *g, const ScriptChoosing *c, uint8_t value, Selector who, uint8_t offset_low)
{
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x08, value));
    uint8_t count = select_by(g, c, who);
    if (count == 0)
        return fails();
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x0A, offset_low));
    uint16_t offset = (uint16_t)(offset_low | battle_stat_offset_high(g) << 8);
    return stat_equal(g, count, offset, value);
}

// $C1:918E
/* Whether the one picked has any of the bits the condition gives of a byte
 * of its stat block. With several picked, the first by the enemies' drawn
 * order is the one. */
static ConditionResult stat_bits(GameState *g, const ScriptChoosing *c, Selector who, uint8_t offset, uint8_t bits)
{
    uint8_t count = select_by(g, c, who);
    if (count == 0 || (count != 1 && !battle_take_first_by_enemy_order(g)))
        return nobody_picked();
    uint8_t stat = combatant_stat_byte_by_wide_place(g, candidate(g, 0), offset);
    battle_keep_word_at_0a(g, offset);
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x08, stat));
    return holds_if((bits & stat) != 0);
}

// $C1:96D4
/* Whether the one picked is a party member of the fight (compared, as the
 * original does, with the party members' ids): the condition chooses where
 * it matches as the operands say, and fails whatever. */
static ConditionResult is_party_member(GameState *g, const ScriptChoosing *c, Selector who, uint8_t whatever_state,
                                       uint8_t not_member)
{
    uint8_t count = select_by(g, c, who);
    if (count == 0)
        return nobody_picked();
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x08, whatever_state));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x0A, not_member));
    uint8_t member = 0;
    while (member < BATTLE_PARTY_SIZE && candidate(g, 0) != battle_combatant_id(g, member))
        member++;
    bool choose;
    if (member == BATTLE_PARTY_SIZE)
        choose = not_member != 0;
    else
        choose = not_member != 1 && (whatever_state == 1 || combatant_in_fight(g, member));
    return (ConditionResult){ .verdict = choose ? CONDITION_CHOOSES_BUT_FAILS : CONDITION_FAILS };
}

// $C1:9728
/* Whether the enemy three slots after the one picked is out of the fight, or
 * in it: the original reads the fighting ids from the enemies' start with the
 * candidate's battle slot. */
static ConditionResult three_on_out(GameState *g, const ScriptChoosing *c, Selector who, uint8_t out_wanted)
{
    select_by(g, c, who);
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x0A, out_wanted));
    bool out = combatant_fighting_as(g, (unsigned)candidate(g, 0) + BATTLE_FIRST_ENEMY) == COMBATANT_NONE;
    return holds_if(out ? out_wanted != 0 : out_wanted == 0);
}

// $C1:97D5
/* Whether the one picked has a turn wait under a number, or not. */
static ConditionResult turn_wait(GameState *g, const ScriptChoosing *c, Selector who, uint8_t limit, uint8_t at_least)
{
    select_by(g, c, who);
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x08, limit));
    uint8_t wait = combatant_turn_wait(g, candidate(g, 0));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x09, 0));
    return holds_if(at_least == 0 ? wait < limit : wait >= limit);
}

/* ---- Where they stand --------------------------------------------------------------------- */

static SearchAnswer search(GameState *g, const ScriptChoosing *c, SearchKind kind, uint8_t other, uint8_t third)
{
    SearchAnswer a;
    TIMED_AS(g, 0xC12986, a = battle_search(g, kind, self(c), other, third));
    return a;
}

/* The answer asked for: that the search's test held, or `negated`, that it
 * didn't. */
static bool as_asked(uint8_t negated, bool held) { return negated != 0 ? !held : held; }

// $C1:925D, $C1:9765
/* Whether the first one picked is within the enemy's reach, or its wider
 * reach (or, as asked, not); where it holds it is the only candidate. */
static ConditionResult reach(GameState *g, const ScriptChoosing *c, Selector who, uint8_t negated, SearchKind kind)
{
    uint8_t count = select_by(g, c, who);
    if (count == 0)
        return fails();
    SearchAnswer a = search(g, c, kind, candidate(g, 0), battle_search_third(g));
    if (!as_asked(negated, a.held))
        return fails();
    set_battle_candidate_count(g, 1);
    return holds();
}

static ConditionResult within_reach(GameState *g, const ScriptChoosing *c, Selector who, uint8_t negated)
{
    return reach(g, c, who, negated, SEARCH_WITHIN_REACH);
}

static ConditionResult within_wide_reach(GameState *g, const ScriptChoosing *c, Selector who, uint8_t negated)
{
    return reach(g, c, who, negated, SEARCH_WITHIN_WIDE_REACH);
}

// $C1:92A3, $C1:9314
/* Those picked, the enemy left out, that stand level with it, or below it or
 * to its right as the condition's third operand says: one of them is taken
 * (not the pair's). Whether the block's actions are chosen then goes by the
 * last one's answer, as asked; otherwise, and with nobody picked, it holds
 * without choosing. */
static ConditionResult each_stands(GameState *g, const ScriptChoosing *c, Selector who, uint8_t negated, SearchKind kind)
{
    uint8_t count = select_by(g, c, who);
    if (count == 0)
        return holds_unchosen();
    set_battle_target_search(g, kind);
    set_battle_target_searcher(g, self(c));
    uint8_t marked = 0, place = 0;
    bool held = battle_search_failed(g) == 0;
    do {
        uint8_t other = candidate(g, place);
        if (other == self(c))
            continue;
        held = search(g, c, kind, other, battle_search_third(g)).held;
        if (held) {
            mark(g, place);
            marked++;
        }
    } while (++place < count);
    battle_keep_word_at_0a(g, place);
    ORIGINAL_EFFECT(battle_leave_word(g, 0x0C, marked));
    ORIGINAL_EFFECT(battle_leave_word(g, 0x0E, self(c)));
    ConditionResult r = for_target(take_marked(g, marked), CONDITION_HOLDS_UNCHOSEN);
    r.pairs = false;
    r.verdict = as_asked(negated, held) ? CONDITION_HOLDS : CONDITION_HOLDS_UNCHOSEN;
    return r;
}

static ConditionResult level_with(GameState *g, const ScriptChoosing *c, Selector who, uint8_t negated)
{
    return each_stands(g, c, who, negated, SEARCH_LEVEL);
}

/* The side is a search counted from the first of the two, in a byte. */
enum { FIRST_SIDE_SEARCH = SEARCH_OTHER_BELOW };

static SearchKind side_search(uint8_t side) { return (SearchKind)(uint8_t)(side + FIRST_SIDE_SEARCH); }

static ConditionResult stands_beside(GameState *g, const ScriptChoosing *c, Selector who, uint8_t negated, uint8_t side)
{
    return each_stands(g, c, who, negated, side_search(side));
}

// $C1:938D
/* Whether the enemy could walk to the first one picked (or, as asked, not),
 * keeping clear of the others as the third operand says. */
enum { KEEP_CLEAR = 0x01 };

static ConditionResult could_walk_to(GameState *g, const ScriptChoosing *c, Selector who, uint8_t negated, uint8_t clearance)
{
    uint8_t count = select_by(g, c, who);
    if (count == 0)
        return fails();
    uint8_t clear = (uint8_t)(clearance << 2 | KEEP_CLEAR);
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x0E, KEEP_CLEAR));
    return holds_if(as_asked(negated, search(g, c, SEARCH_WALK, candidate(g, 0), clear).held));
}

// $C1:93E6
/* Whether the enemy stands in the battlefield's lower or upper half, or at
 * its right or left edge, as the operands say. */
static ConditionResult stands_in(GameState *g, const ScriptChoosing *c, uint8_t upper_or_right, uint8_t edge)
{
    SearchKind kind = edge ? (upper_or_right ? SEARCH_AT_RIGHT_EDGE : SEARCH_AT_LEFT_EDGE)
                           : (upper_or_right ? SEARCH_IN_UPPER_HALF : SEARCH_IN_LOWER_HALF);
    return holds_if(search(g, c, kind, battle_search_other(g), battle_search_third(g)).held);
}

/* ---- About the battle ------------------------------------------------------------------------ */

// $C1:8FDA
/* Whether an enemy of the kind the condition names is in the fight (or, as
 * asked, not). The original looks until the enemies' count, from the first,
 * at least once. */
static ConditionResult enemy_present(GameState *g, const ScriptChoosing *c, uint8_t id, uint8_t absent)
{
    (void)c;
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x08, id));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x0A, absent));
    bool present = false;
    uint8_t enemy = 0;
    do
        present = combatant_fighting_as(g, (unsigned)BATTLE_FIRST_ENEMY + enemy) == id;
    while (!present && ++enemy != battle_enemy_count(g));
    return holds_if(present ? absent == 0 : absent != 0);
}

// $C1:9656
/* Whether an enemy of another kind than the condition names is in the fight:
 * the first one found (where asked, not the enemy itself) decides by the
 * third operand; with none, it holds unless asked about the others. */
static ConditionResult other_kind_present(GameState *g, const ScriptChoosing *c, uint8_t id, uint8_t not_itself,
                                          uint8_t holds_then)
{
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x08, id));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x0A, not_itself));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x0C, holds_then));
    uint8_t enemy = 0;
    do {
        uint8_t fighting = combatant_fighting_as(g, (uint8_t)(BATTLE_FIRST_ENEMY + enemy));
        if (fighting == COMBATANT_NONE || fighting == id)
            continue;
        if (not_itself != 0 && enemy == c->enemy)
            continue;
        return holds_if(holds_then != 0);
    } while (++enemy != battle_enemy_count(g));
    return holds_if(not_itself == 0);
}

// $C1:9013
/* Whether at most so many enemies are in the fight (not counting those whose
 * standing is anything but plain). */
static ConditionResult enemies_at_most(GameState *g, const ScriptChoosing *c, uint8_t most)
{
    (void)c;
    uint8_t counted = 0;
    for (uint8_t enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        if (combatant_in_fight(g, (uint8_t)(BATTLE_FIRST_ENEMY + enemy)) && battle_enemy_standing(g, enemy) == 0)
            counted++;
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x08, most));
    battle_keep_word_at_0a(g, counted);
    return holds_if(most >= counted);
}

// $C1:96A5
/* Whether at most so many party members are in the fight. */
static ConditionResult members_at_most(GameState *g, const ScriptChoosing *c, uint8_t most)
{
    (void)c;
    uint8_t limit = (uint8_t)(most + 1), fighting = 0;
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x08, limit));
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (combatant_in_fight(g, member))
            fighting++;
    return holds_if(fighting < limit);
}

// $C1:9045
/* Whether the battle has gone on at least so many frames. */
static ConditionResult frames_at_least(GameState *g, const ScriptChoosing *c, uint32_t least)
{
    (void)c;
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x08, (uint8_t)least));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x09, (uint8_t)(least >> 8)));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x0A, (uint8_t)(least >> 16)));
    return holds_if((battle_frames(g) & 0xFFFFFF) >= least);
}

// $C1:9082
/* Whether the enemy's own count (battle_enemy_script_counter) is at most a
 * number, or at least it. */
enum { COUNT_AT_MOST = 1 };

static ConditionResult count_compared(GameState *g, const ScriptChoosing *c, uint8_t number, uint8_t how)
{
    uint8_t count = battle_enemy_script_counter(g, c->enemy);
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x08, number));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x0A, how));
    return holds_if(how == COUNT_AT_MOST ? count <= number : count >= number);
}

// $C1:95DA
/* A chance, in percent. */
enum { PERCENT = 100 };

static ConditionResult chance(GameState *g, const ScriptChoosing *c, uint8_t percent)
{
    (void)c;
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x00, percent));
    return holds_if(battle_random(g, 0, PERCENT) < percent);
}

// $C1:97AB, $C1:97C0
/* Whether the enemy was beaten by the action, and whether its standing is
 * anything but plain. */
static ConditionResult was_beaten(GameState *g, const ScriptChoosing *c) { return holds_if(battle_enemy_beaten(g, c->enemy) != 0); }
static ConditionResult stands_apart(GameState *g, const ScriptChoosing *c)
{
    return holds_if(battle_enemy_standing(g, c->enemy) != 0);
}

// $C1:8EA7, $C1:975C
static ConditionResult always(GameState *g, const ScriptChoosing *c)
{
    (void)g;
    (void)c;
    return holds();
}

static ConditionResult chooses_but_fails(GameState *g, const ScriptChoosing *c)
{
    (void)g;
    (void)c;
    return (ConditionResult){ .verdict = CONDITION_CHOOSES_BUT_FAILS };
}

/* ---- What last acted on it ------------------------------------------------------------------ */

/* The enemy's own action record (battle_chosen_actions, by its slot) keeps
 * what last acted on it: in its first byte whether an attack ($4x) or a tech
 * ($5x), bit 4, and the battle slot of whoever did it; in its second the
 * attack or tech; in its third its elements. */
enum { BY_ATTACK = 0x40, BY_TECH = 0x50, HOW_ACTED = 0xF0, ACTED_BIT_4 = 0x10, ACTOR = 0x0F };

static uint8_t record_byte(const GameState *g, const ScriptChoosing *c, unsigned n)
{
    return (uint8_t)(battle_chosen_actions(g, self(c)) >> (8 * n));
}

/* An attack (0) or a tech (1), as the record says it; anything else is
 * neither. */
enum { NAMES_NEITHER = 0 };

static uint8_t how_named(uint8_t attack_or_tech)
{
    return attack_or_tech > 1 ? NAMES_NEITHER : attack_or_tech == 0 ? BY_ATTACK : BY_TECH;
}

// $C1:942A
/* Whether an attack (or a tech) last acted on it, or not. */
static ConditionResult acted_on_by(GameState *g, const ScriptChoosing *c, uint8_t attack_or_tech, uint8_t negated)
{
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x02, negated));
    uint8_t how = how_named(attack_or_tech);
    if (how == NAMES_NEITHER)
        return fails();
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x00, how));
    bool was = (record_byte(g, c, 0) & HOW_ACTED) == how;
    return holds_if(negated == 0 ? was : !was);
}

// $C1:9474
/* Whether that attack or tech last acted on it (or, asked, neither the kind
 * nor the number did). */
static ConditionResult acted_on_by_this(GameState *g, const ScriptChoosing *c, uint8_t attack_or_tech, uint8_t number,
                                        uint8_t negated)
{
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x04, negated));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x02, number));
    uint8_t how = how_named(attack_or_tech);
    if (how == NAMES_NEITHER)
        return fails();
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x00, how));
    bool kind = (record_byte(g, c, 0) & HOW_ACTED) == how, same = record_byte(g, c, 1) == number;
    return holds_if(negated == 0 ? kind && same : !kind && !same);
}

// $C1:94D2
/* Whether the record's bit 4 is as the condition says, or not. */
static ConditionResult acted_bit(GameState *g, const ScriptChoosing *c, uint8_t bit, uint8_t negated)
{
    uint8_t wanted = (uint8_t)((bit & 1) << 4);
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x02, negated));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x00, wanted));
    bool same = (record_byte(g, c, 0) & ACTED_BIT_4) == wanted;
    return holds_if(negated == 0 ? same : !same);
}

// $C1:9514
/* Whether whoever last acted on it (with the record's bit 4 clear) is the
 * party member the condition names. Its other forms keep the original's
 * slips: asked "not", it looks at the first party member whoever acted; and
 * asked about the whole party it fails, or asked "not" holds, whatever. */
static ConditionResult acted_on_by_member(GameState *g, const ScriptChoosing *c, uint8_t id, uint8_t whole_party,
                                          uint8_t negated)
{
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x04, negated));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x02, whole_party));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x00, id));
    uint8_t record = record_byte(g, c, 0);
    if (record & ACTED_BIT_4)
        return fails();
    if (whole_party != 0)
        return holds_if(negated != 0);
    if (negated == 0)
        return holds_if(combatant_id(g, record & ACTOR) == id);
    return holds_if(battle_combatant_id(g, 0) != id);
}

// $C1:959A
/* Whether what last acted on it had all the elements the condition names,
 * or not. */
static ConditionResult acted_with_elements(GameState *g, const ScriptChoosing *c, uint8_t elements, uint8_t negated)
{
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x04, negated));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x02, elements));
    bool all = (record_byte(g, c, 2) & elements) == elements;
    return holds_if(negated == 0 ? all : !all);
}

/* ---- The table ------------------------------------------------------------------------------- */

/* How a condition's three operands are laid out: which is its selector, and
 * which it reads as numbers (two bytes low first are a word, three a count
 * of frames). */
typedef enum {
    NO_OPERANDS,
    PICKED,              /* a selector */
    PICKED_AND_SECOND,   /* a selector, and a number in the second operand */
    PICKED_AND_THIRD,    /* a selector, and a number in the third */
    PICKED_AND_TWO,      /* a selector, and two numbers */
    PICKED_AND_WORD,     /* a selector, and a word */
    VALUE_PICKED_OFFSET, /* a number, a selector, and a stat's offset */
    FIRST,               /* a number in the first operand */
    FIRST_TWO,           /* numbers in the first and second */
    LAST_TWO,            /* numbers in the second and third */
    FIRST_AND_LAST,      /* numbers in the first and third */
    THREE,               /* three numbers */
    FRAMES,              /* a count of frames */
} OperandLayout;

typedef const ScriptChoosing Choosing;
typedef ConditionResult (*TestOf)(GameState *, Choosing *);
typedef ConditionResult (*TestOfPicked)(GameState *, Choosing *, Selector);
typedef ConditionResult (*TestOfPickedAnd)(GameState *, Choosing *, Selector, uint8_t);
typedef ConditionResult (*TestOfPickedAndTwo)(GameState *, Choosing *, Selector, uint8_t, uint8_t);
typedef ConditionResult (*TestOfPickedAndWord)(GameState *, Choosing *, Selector, uint16_t);
typedef ConditionResult (*TestOfValuePicked)(GameState *, Choosing *, uint8_t, Selector, uint8_t);
typedef ConditionResult (*TestOfNumber)(GameState *, Choosing *, uint8_t);
typedef ConditionResult (*TestOfTwo)(GameState *, Choosing *, uint8_t, uint8_t);
typedef ConditionResult (*TestOfThree)(GameState *, Choosing *, uint8_t, uint8_t, uint8_t);
typedef ConditionResult (*TestOfFrames)(GameState *, Choosing *, uint32_t);

typedef struct {
    OperandLayout layout;
    union {
        TestOf none;
        TestOfPicked picked;
        TestOfPickedAnd picked_and;
        TestOfPickedAndTwo picked_and_two;
        TestOfPickedAndWord picked_and_word;
        TestOfValuePicked value_picked;
        TestOfNumber number;
        TestOfTwo two;
        TestOfThree three;
        TestOfFrames frames;
    } test;
    /* How far the original reads the script for it, the operand whose place
     * it leaves (0 for the kind's own): past where a nobody-picked stops. */
    uint8_t reads_to;
    /* Its third operand names a side, a search past the searches (the
     * guards'). */
    bool names_side;
    uint32_t timed_as; /* the original's routine, whose time it is */
} ConditionRow;

#define ROW(layout_, field, f, reads, routine) { layout_, { .field = f }, reads, false, routine }

// $C1:B80D
static const ConditionRow conditions[CONDITION_KINDS] = {
    ROW(NO_OPERANDS, none, always, 0, 0xC18EA7),                            /* $00 */
    ROW(PICKED, picked, at_half_hp, 1, 0xC18EAB),                           /* $01 */
    ROW(THREE, three, with_status, 0, 0xC18F11),                            /* $02 */
    ROW(PICKED_AND_SECOND, picked_and, is_enemy, 2, 0xC18F87),              /* $03 */
    ROW(LAST_TWO, two, enemy_present, 2, 0xC18FDA),                         /* $04 */
    ROW(FIRST, number, enemies_at_most, 0, 0xC19013),                       /* $05 */
    ROW(FRAMES, frames, frames_at_least, 0, 0xC19045),                      /* $06 */
    ROW(FIRST_TWO, two, count_compared, 0, 0xC19082),                       /* $07 */
    ROW(PICKED_AND_WORD, picked_and_word, at_most_hp, 2, 0xC190BE),         /* $08 */
    ROW(PICKED_AND_TWO, picked_and_two, stat_at_least, 3, 0xC19130),        /* $09 */
    ROW(PICKED_AND_TWO, picked_and_two, stat_bits, 3, 0xC1918E),            /* $0A */
    ROW(PICKED_AND_TWO, picked_and_two, stat_at_most, 3, 0xC191F9),         /* $0B */
    ROW(PICKED_AND_SECOND, picked_and, within_reach, 1, 0xC1925D),          /* $0C */
    ROW(PICKED_AND_SECOND, picked_and, level_with, 1, 0xC192A3),            /* $0D */
    { PICKED_AND_TWO, { .picked_and_two = stands_beside }, 1, true, 0xC19314 }, /* $0E */
    ROW(PICKED_AND_TWO, picked_and_two, could_walk_to, 1, 0xC1938D),        /* $0F */
    ROW(LAST_TWO, two, stands_in, 0, 0xC193E6),                             /* $10 */
    ROW(FIRST_AND_LAST, two, acted_on_by, 0, 0xC1942A),                     /* $11 */
    ROW(THREE, three, acted_on_by_this, 0, 0xC19474),                       /* $12 */
    ROW(FIRST_AND_LAST, two, acted_bit, 0, 0xC194D2),                       /* $13 */
    ROW(THREE, three, acted_on_by_member, 0, 0xC19514),                     /* $14 */
    ROW(FIRST_AND_LAST, two, acted_with_elements, 0, 0xC1959A),             /* $15 */
    ROW(NO_OPERANDS, none, always, 0, 0xC18EA7),                            /* $16 */
    ROW(FIRST, number, chance, 0, 0xC195DA),                                /* $17 */
    ROW(VALUE_PICKED_OFFSET, value_picked, stat_is, 2, 0xC195FA),           /* $18 */
    ROW(NO_OPERANDS, none, always, 0, 0xC18EA7),                            /* $19 */
    ROW(THREE, three, other_kind_present, 1, 0xC19656),                     /* $1A */
    ROW(FIRST, number, members_at_most, 1, 0xC196A5),                       /* $1B */
    ROW(PICKED_AND_TWO, picked_and_two, is_party_member, 2, 0xC196D4),      /* $1C */
    ROW(PICKED_AND_THIRD, picked_and, three_on_out, 3, 0xC19728),           /* $1D */
    ROW(NO_OPERANDS, none, chooses_but_fails, 0, 0xC1975C),                 /* $1E */
    ROW(PICKED_AND_SECOND, picked_and, within_wide_reach, 1, 0xC19765),     /* $1F */
    ROW(NO_OPERANDS, none, was_beaten, 0, 0xC197AB),                        /* $20 */
    ROW(NO_OPERANDS, none, stands_apart, 0, 0xC197C0),                      /* $21 */
    ROW(PICKED_AND_TWO, picked_and_two, turn_wait, 1, 0xC197D5),            /* $22 */
    ROW(VALUE_PICKED_OFFSET, value_picked, stat_is, 2, 0xC195FA),           /* $23 */
    ROW(VALUE_PICKED_OFFSET, value_picked, stat_is, 2, 0xC195FA),           /* $24 */
    ROW(VALUE_PICKED_OFFSET, value_picked, stat_is, 2, 0xC195FA),           /* $25 */
    ROW(VALUE_PICKED_OFFSET, value_picked, stat_is, 2, 0xC195FA),           /* $26 */
    ROW(VALUE_PICKED_OFFSET, value_picked, stat_is, 2, 0xC195FA),           /* $27 */
    ROW(VALUE_PICKED_OFFSET, value_picked, stat_is, 2, 0xC195FA),           /* $28 */
};

/* The operand that names the selector in a layout (0 for none). */
static unsigned selector_operand(OperandLayout layout)
{
    switch (layout) {
    case PICKED:
    case PICKED_AND_SECOND:
    case PICKED_AND_THIRD:
    case PICKED_AND_TWO:
    case PICKED_AND_WORD: return 1;
    case VALUE_PICKED_OFFSET: return 2;
    default: return 0;
    }
}

/* Decode the condition's operands as its row lays them out, and test it. */
static ConditionResult run_condition(GameState *g, const ScriptChoosing *c, const ConditionRow *row, ScriptPlace at)
{
    uint8_t first = enemy_script_byte(g, (ScriptPlace)(at + 1)), second = enemy_script_byte(g, (ScriptPlace)(at + 2)),
            third = enemy_script_byte(g, (ScriptPlace)(at + 3));
    switch (row->layout) {
    case NO_OPERANDS: return row->test.none(g, c);
    case PICKED: return row->test.picked(g, c, first);
    case PICKED_AND_SECOND: return row->test.picked_and(g, c, first, second);
    case PICKED_AND_THIRD: return row->test.picked_and(g, c, first, third);
    case PICKED_AND_TWO: return row->test.picked_and_two(g, c, first, second, third);
    case PICKED_AND_WORD: return row->test.picked_and_word(g, c, first, (uint16_t)(second | third << 8));
    case VALUE_PICKED_OFFSET: return row->test.value_picked(g, c, first, second, third);
    case FIRST: return row->test.number(g, c, first);
    case FIRST_TWO: return row->test.two(g, c, first, second);
    case LAST_TWO: return row->test.two(g, c, second, third);
    case FIRST_AND_LAST: return row->test.two(g, c, first, third);
    case THREE: return row->test.three(g, c, first, second, third);
    case FRAMES: return row->test.frames(g, c, (uint32_t)(first | second << 8 | (uint32_t)third << 16));
    }
    return holds_unchosen();
}

/* The condition at `at` as its routine carries it out, the script read on as
 * far as it reads. A kind past the table (none in the scripts) does
 * nothing. */
static ConditionResult carry_out(GameState *g, ScriptChoosing *c, uint8_t kind, ScriptPlace at, bool timed)
{
    if (kind >= CONDITION_KINDS)
        return holds_unchosen();
    const ConditionRow *row = &conditions[kind];
    ConditionResult r;
    if (timed)
        TIMED_AS(g, row->timed_as, r = run_condition(g, c, row, at));
    else
        r = run_condition(g, c, row, at);
    c->place = (ScriptPlace)(at + (r.stopped_at_selector ? selector_operand(row->layout) : row->reads_to));
    return r;
}

ConditionResult enemy_test_condition(GameState *g, ScriptChoosing *c, uint8_t kind, ScriptPlace at)
{
    return carry_out(g, c, kind, at, true);
}

bool enemy_condition_in_tables(const GameState *g, uint8_t kind, ScriptPlace at)
{
    if (kind >= CONDITION_KINDS)
        return false;
    const ConditionRow *row = &conditions[kind];
    if (row->names_side && side_search(enemy_script_byte(g, (ScriptPlace)(at + 3))) >= SEARCH_KINDS)
        return false;
    unsigned operand = selector_operand(row->layout);
    if (operand == 0)
        return true;
    Selector selector = enemy_script_byte(g, (ScriptPlace)(at + operand));
    return selector < SELECTORS || (selector & SELECT_CONDITION_TARGET);
}

/* ---- The entries ------------------------------------------------------------------------ */

/* Each condition's routine is called with the script read at the condition
 * and the choosing in its cells, and acts on how it came out itself (the
 * block's testing, battle/enemy_scripts.c); it hands A back clear, which the
 * original's callers index with. */
static void condition_entry(Snes *s, uint8_t kind)
{
    GameState *g = state_of(s);
    ScriptChoosing c = enemy_choosing_in_cells(g);
    ConditionResult r = carry_out(g, &c, kind, c.place, false);
    enemy_act_on_condition(g, &c, r);
    enemy_hand_back_choosing(g, &c);
    s->cpu.a = 0;
    m_rts(s);
}

/* An entry and its guard, which knows the condition it carries out. */
#define CONDITION_ENTRY(name, kind) \
    void name(Snes *s) { condition_entry(s, kind); } \
    bool name##_ready(Snes *s) { return battle_script_entry_ready(s, TAKES_CONDITION, kind); }

CONDITION_ENTRY(battle_condition_always, 0x00)
CONDITION_ENTRY(battle_condition_at_half_hp, 0x01)
CONDITION_ENTRY(battle_condition_with_status, 0x02)
CONDITION_ENTRY(battle_condition_is_enemy, 0x03)
CONDITION_ENTRY(battle_condition_enemy_present, 0x04)
CONDITION_ENTRY(battle_condition_enemies_at_most, 0x05)
CONDITION_ENTRY(battle_condition_frames_at_least, 0x06)
CONDITION_ENTRY(battle_condition_count_compared, 0x07)
CONDITION_ENTRY(battle_condition_at_most_hp, 0x08)
CONDITION_ENTRY(battle_condition_stat_at_least, 0x09)
CONDITION_ENTRY(battle_condition_stat_bits, 0x0A)
CONDITION_ENTRY(battle_condition_stat_at_most, 0x0B)
CONDITION_ENTRY(battle_condition_within_reach, 0x0C)
CONDITION_ENTRY(battle_condition_level_with, 0x0D)
CONDITION_ENTRY(battle_condition_stands_beside, 0x0E)
CONDITION_ENTRY(battle_condition_could_walk_to, 0x0F)
CONDITION_ENTRY(battle_condition_stands_in, 0x10)
CONDITION_ENTRY(battle_condition_acted_on_by, 0x11)
CONDITION_ENTRY(battle_condition_acted_on_by_this, 0x12)
CONDITION_ENTRY(battle_condition_acted_bit, 0x13)
CONDITION_ENTRY(battle_condition_acted_on_by_member, 0x14)
CONDITION_ENTRY(battle_condition_acted_with_elements, 0x15)
CONDITION_ENTRY(battle_condition_chance, 0x17)
CONDITION_ENTRY(battle_condition_stat_is, 0x18)
CONDITION_ENTRY(battle_condition_other_kind_present, 0x1A)
CONDITION_ENTRY(battle_condition_members_at_most, 0x1B)
CONDITION_ENTRY(battle_condition_is_party_member, 0x1C)
CONDITION_ENTRY(battle_condition_three_on_out, 0x1D)
CONDITION_ENTRY(battle_condition_chooses_but_fails, 0x1E)
CONDITION_ENTRY(battle_condition_within_wide_reach, 0x1F)
CONDITION_ENTRY(battle_condition_was_beaten, 0x20)
CONDITION_ENTRY(battle_condition_stands_apart, 0x21)
CONDITION_ENTRY(battle_condition_turn_wait, 0x22)
