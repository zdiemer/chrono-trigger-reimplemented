/* Battle: the enemies' scripts -- choosing what an enemy does
 * (battle/enemy_scripts.h). */
#include "battle/enemy_scripts.h"

#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/positions.h"
#include "battle/rules_data.h"
#include "battle/targets.h"
#include "charge.h"
#include "original_effects.h"
#include "vars.h"

/* ---- Reading a script ---------------------------------------------------------------------- */

void read_script_at(GameState *g, ScriptPlace at) { set_battle_script_place(g, at); }

/* The place of the byte that ends the conditions or the actions a place is
 * in: the first end marker after it. */
static ScriptPlace end_after(const GameState *g, ScriptPlace at)
{
    do
        at++;
    while (enemy_script_byte(g, at) != SCRIPT_BLOCK_END);
    return at;
}

uint8_t enemy_at_work(const GameState *g) { return battle_enemy_turn(g); }
uint8_t enemy_at_work_slot(const GameState *g) { return (uint8_t)(BATTLE_FIRST_ENEMY + enemy_at_work(g)); }

void enemy_block_fails(GameState *g) { set_battle_script_outcome(g, (uint8_t)(battle_script_outcome(g) + 1)); }

/* How far a block's two conditions have got (battle_condition_pair). */
enum { PAIR_NONE = 0, PAIR_FIRST = 1, PAIR_FIRST_HELD = 0x80 };

/* The enemy's last condition held, where none is. */
enum { NO_CONDITION = 0xFF };

/* ---- Choosing the actions ------------------------------------------------------------------ */

/* The action that is a draw among the four after it. */
enum { ACTION_ONE_OF_FOUR = 0x04 };

/* The enemy starts going through a block's actions from its first. */
static ScriptPlace start_block_actions(GameState *g, uint8_t enemy, ScriptPlace first)
{
    set_battle_enemy_actions_block(g, enemy, battle_enemy_script_block(g, enemy));
    set_battle_enemy_next_action(g, enemy, first);
    set_battle_enemy_block_actions(g, enemy, first);
    return first;
}

/* The action the enemy's next turn carries out, as the choosing leaves it:
 * where it is going through its block's actions, the chosen action kept
 * beside it. */
static ScriptPlace take_next_action(GameState *g, uint8_t enemy, ScriptPlace at)
{
    set_battle_enemy_next_action(g, enemy, at);
    set_battle_enemy_block_actions(g, enemy, at);
    return at;
}

// $C1:8C63
void enemy_choose_actions(GameState *g)
{
    uint8_t enemy = enemy_at_work(g);
    ScriptPlace first = battle_script_actions_place(g), at;
    if (battle_action_hit_enemy(g)) {
        at = first;
    } else if (battle_enemy_script_block(g, enemy) != battle_enemy_actions_block(g, enemy)) {
        at = start_block_actions(g, enemy, first);
    } else {
        if (enemy_script_byte(g, first) == ACTION_ONE_OF_FOUR)
            set_battle_enemy_next_action(g, enemy, first);
        at = take_next_action(g, enemy, battle_enemy_next_action(g, enemy));
        if (enemy_script_byte(g, at) >= SCRIPT_ACTIONS)
            at = start_block_actions(g, enemy, first);
    }
    for (;;) {
        read_script_at(g, at);
        uint8_t action = enemy_script_byte(g, at);
        set_battle_action_kind(g, action);
        if (action >= SCRIPT_ACTIONS)
            return;
        enemy_choose_action(g, action);
        if (action != ACTION_ONE_OF_FOUR)
            break;
        /* the draw left the action it drew as the block's actions' place:
         * that is the one chosen, and the enemy is at it */
        at = take_next_action(g, enemy, battle_script_actions_place(g));
        if (enemy_script_byte(g, at) >= SCRIPT_ACTIONS)
            at = start_block_actions(g, enemy, battle_script_actions_place(g));
    }
    uint8_t now = enemy_at_work(g);
    if (now < BATTLE_ENEMIES)
        set_battle_enemy_action_chosen(g, now, true);
}

// $C1:8C3E
void enemy_condition_held(GameState *g)
{
    set_battle_enemy_last_condition(g, enemy_at_work(g), battle_condition_kind(g));
    if (battle_condition_pair(g) == PAIR_FIRST_HELD)
        return;
    set_battle_script_actions_place(g, (ScriptPlace)(end_after(g, battle_script_place(g)) + 1));
    enemy_choose_actions(g);
}

/* ---- Looking through a script ------------------------------------------------------------- */

/* Two statuses put a script of their own in place of the enemy's: one that
 * makes it attack the nearest party member (bit 7 of its fourth status byte,
 * or of the statuses it keeps there), and one that makes it attack anyone
 * (its confusion, bit 2 of the first status byte, unless it shrugs that off:
 * battle_stats_status_ignored). The original works out the stat block's place
 * with a multiply, which it leaves in its cells. Looking for the reactions of
 * an enemy an action beat, the battle may have no enemy at work (the one past
 * the last, as the choosing leaves it), and reads the statuses of the block
 * after the last: the bytes by their offsets, then. */
enum { ATTACKS_NEAREST = 0x80, CONFUSED = 0x04, STAT_STATUS_1 = 0x1E, STAT_STATUS_4 = 0x21, STAT_STATUS_IGNORED = 0x23,
       STAT_KEPT_STATUS_4 = 0x26 };

static uint8_t attack_statuses(const GameState *g, uint8_t slot)
{
    return (uint8_t)(combatant_stat_byte(g, slot, STAT_STATUS_4) | combatant_stat_byte(g, slot, STAT_KEPT_STATUS_4));
}

static uint8_t confusion_statuses(const GameState *g, uint8_t slot)
{
    return (uint8_t)(combatant_stat_byte(g, slot, STAT_STATUS_1) & (uint8_t)~combatant_stat_byte(g, slot, STAT_STATUS_IGNORED));
}

// $C1:B4AA
static void find_script_place(GameState *g, ScriptPlace at)
{
    uint8_t slot = enemy_at_work_slot(g);
    ORIGINAL_EFFECT(battle_multiply_words(g, slot, BATTLE_STAT_BLOCK_BYTES));
    if (attack_statuses(g, slot) & ATTACKS_NEAREST)
        at = SCRIPT_ATTACK_NEAREST;
    else if (confusion_statuses(g, slot) & CONFUSED)
        at = SCRIPT_ATTACK_ANYONE;
    read_script_at(g, at);
}

// $C1:B488
void enemy_script_find_reactions(GameState *g, uint8_t id)
{
    find_script_place(g, enemy_script_start(g, id));
    ScriptPlace at = battle_script_place(g);
    while (enemy_script_byte(g, at) != SCRIPT_SECTION_END)
        at++;
    read_script_at(g, (ScriptPlace)(at + 1));
}

/* Test the condition at a place, as the one being tested. */
static void test_condition_at(GameState *g, ScriptPlace at)
{
    uint8_t kind = enemy_script_byte(g, at);
    set_battle_condition_kind(g, kind);
    enemy_test_condition(g, kind);
}

/* The second condition of the block being tested, where it has one (a
 * failed first is counted on in the outcome, which the second can't
 * undo). */
static void test_second_condition(GameState *g)
{
    if (battle_condition_pair(g) == PAIR_NONE)
        return;
    ScriptPlace second = (ScriptPlace)(battle_block_start(g) + SCRIPT_CONDITION_BYTES);
    read_script_at(g, second);
    test_condition_at(g, second);
}

void enemy_test_block(GameState *g, ScriptPlace block)
{
    read_script_at(g, block);
    set_battle_condition_pair(g, enemy_script_byte(g, (ScriptPlace)(block + SCRIPT_CONDITION_BYTES)) != SCRIPT_BLOCK_END ? PAIR_FIRST : PAIR_NONE);
    test_condition_at(g, block);
    test_second_condition(g);
}

bool enemy_script_next_block(GameState *g)
{
    if (battle_script_outcome(g) != OUTCOME_NOBODY_TO_ACT_ON) {
        set_battle_condition_pair(g, PAIR_NONE);
        read_script_at(g, end_after(g, battle_script_place(g)));
    }
    ScriptPlace next = (ScriptPlace)(end_after(g, battle_script_place(g)) + 1);
    read_script_at(g, next);
    return enemy_script_byte(g, next) != SCRIPT_SECTION_END;
}

// $C1:AFD2
void enemy_script_choose(GameState *g, uint8_t id)
{
    uint8_t enemy = enemy_at_work(g);
    ScriptPlace block = enemy_script_start(g, id);
    read_script_at(g, block);
    set_battle_block_start(g, block);
    ORIGINAL_EFFECT(set_battle_low_scratch(g, 0x0A, 0));
    ORIGINAL_EFFECT(set_battle_low_scratch(g, 0x0B, 0));
    for (;;) {
        set_battle_script_outcome(g, OUTCOME_HELD);
        if (enemy_script_byte(g, (ScriptPlace)(block + SCRIPT_CONDITION_BYTES)) != SCRIPT_BLOCK_END)
            set_battle_condition_pair(g, PAIR_FIRST);
        find_script_place(g, block);
        test_condition_at(g, battle_script_place(g));
        test_second_condition(g);
        uint8_t outcome = battle_script_outcome(g);
        set_battle_enemy_script_outcome(g, enemy, outcome);
        if (outcome == OUTCOME_NOBODY_TO_ACT_ON)
            set_battle_enemy_action_chosen(g, enemy, false);
        if (outcome == OUTCOME_HELD)
            break;
        set_battle_enemy_last_condition(g, enemy, NO_CONDITION);
        set_battle_action_hit_enemy(g, false);
        if (!enemy_script_next_block(g))
            break;
        block = battle_script_place(g);
        set_battle_block_start(g, block);
        set_battle_enemy_script_block(g, enemy, (uint8_t)(battle_enemy_script_block(g, enemy) + 1));
    }
    set_battle_enemy_script_block(g, enemy, 0);
}

/* An enemy waiting in reserve looks through the script of the id it is
 * fighting as, whatever it is. */
enum { IN_RESERVE = 0x80 };

// $C1:8C09
void enemy_scripts_choose(GameState *g)
{
    for (uint8_t enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        set_battle_enemy_turn(g, enemy);
        set_battle_condition_pair(g, PAIR_NONE);
        set_battle_enemy_script_outcome(g, enemy, OUTCOME_HELD);
        set_battle_enemy_script_block(g, enemy, 0);
        uint8_t slot = (uint8_t)(BATTLE_FIRST_ENEMY + enemy);
        bool waiting = (battle_enemy_standing(g, enemy) & IN_RESERVE) != 0;
        if (!waiting && !combatant_in_fight(g, slot))
            continue;
        if (battle_enemy_action_chosen(g, enemy))
            continue;
        TIMED_AS(g, 0xC1AFD2, enemy_script_choose(g, battle_combatant_fighting(g, slot)));
    }
    set_battle_enemy_turn(g, BATTLE_ENEMIES);
}

/* ---- The entries ------------------------------------------------------------------------ */

void battle_enemy_scripts_choose(Snes *s)
{
    enemy_scripts_choose(state_of(s));
    m_rts(s);
}

/* The enemy's id comes in A. */
void battle_enemy_script_choose(Snes *s)
{
    enemy_script_choose(state_of(s), (uint8_t)s->cpu.a);
    m_rts(s);
}

/* What the choosing leaves in A: the action's byte where the actions had run
 * out, the enemy's number where it names none, or the mark that it chose. */
enum { CHOSEN_MARK = 1 };

static uint8_t chosen_a(const GameState *g)
{
    uint8_t action = battle_action_kind(g);
    if (action >= SCRIPT_ACTIONS)
        return action;
    return enemy_at_work(g) >= BATTLE_ENEMIES ? enemy_at_work(g) : CHOSEN_MARK;
}

void battle_enemy_condition_held(Snes *s)
{
    GameState *g = state_of(s);
    bool first_of_two = battle_condition_pair(g) == PAIR_FIRST_HELD;
    enemy_condition_held(g);
    s->cpu.a = first_of_two ? PAIR_FIRST_HELD : chosen_a(g);
    m_rts(s);
}

void battle_enemy_choose_actions(Snes *s)
{
    GameState *g = state_of(s);
    enemy_choose_actions(g);
    s->cpu.a = chosen_a(g);
    m_rts(s);
}

/* The enemy's id comes in A; the section's end marker is left there. */
void battle_enemy_find_reactions(Snes *s)
{
    enemy_script_find_reactions(state_of(s), (uint8_t)s->cpu.a);
    s->cpu.a = SCRIPT_SECTION_END;
    m_rts(s);
}

/* The place comes in X; A is left with the status byte it tested last. */
static uint8_t status_tested_last(const GameState *g)
{
    uint8_t slot = enemy_at_work_slot(g);
    uint8_t fourth = attack_statuses(g, slot);
    return (fourth & ATTACKS_NEAREST) ? fourth : confusion_statuses(g, slot);
}

void battle_enemy_find_script_place(Snes *s)
{
    GameState *g = state_of(s);
    find_script_place(g, s->cpu.x);
    s->cpu.a = status_tested_last(g);
    m_rts(s);
}

/* ---- The guards ------------------------------------------------------------------------- */

/* A party member picked at random is updated in C, which is every call the
 * game makes but where a knocked-out member would come back (the battle's
 * update has always seen to it first). */
static bool no_member_comes_back(const GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (combatant_update_comes_back(g, member))
            return false;
    return true;
}

/* The commands an entry takes up are the tables': a script holds no others,
 * but a place the game never leaves a script at (a fuzzed one) can land on
 * bytes that aren't, which the original dispatches past its tables -- a
 * condition or an action into the tables after, a selector into code. So the
 * guards look at what the entry would take up from the place: a condition's
 * kind and the selector it names, an action and its selectors, and after a
 * draw among four, those four. */
static bool selector_in_table(uint8_t selector) { return selector < SELECTORS || (selector & SELECT_CONDITION_TARGET); }

/* Which operand of a condition names a selector (0 for none). */
static unsigned condition_selector_operand(uint8_t kind)
{
    switch (kind) {
    case 0x01: case 0x03: case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F:
    case 0x1C: case 0x1D: case 0x1F: case 0x22:
        return 1;
    case 0x18: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28:
        return 2;
    default:
        return 0;
    }
}

/* The condition that asks whether those picked stand below the enemy or to
 * its right names the search by its third operand, from the first of the
 * two. */
enum { CONDITION_STANDS_BESIDE = 0x0E, SEARCH_STANDS_BESIDE_FIRST = 6 };

static bool condition_selector_in_table(const GameState *g, uint8_t kind, ScriptPlace at)
{
    if (kind == CONDITION_STANDS_BESIDE
        && (uint8_t)(enemy_script_byte(g, (ScriptPlace)(at + 3)) + SEARCH_STANDS_BESIDE_FIRST) >= SEARCH_KINDS)
        return false;
    unsigned operand = condition_selector_operand(kind);
    return operand == 0 || selector_in_table(enemy_script_byte(g, (ScriptPlace)(at + operand)));
}

static bool condition_in_tables(const GameState *g, ScriptPlace at)
{
    uint8_t kind = enemy_script_byte(g, at);
    return kind < CONDITION_KINDS && condition_selector_in_table(g, kind, at);
}

/* The operands of an action that name selectors: the pose's and the aim's
 * second, a tech's third and fourth too, the stats-then-tech actions' third
 * and fourth, a turn wait's first. */
enum { ACTION_TECH = 0x02, ACTION_CHOOSE_AGAIN = 0x05, ACTION_SET_STATS_THEN_TECH = 0x12, ACTION_ADD_TO_TURN_WAIT = 0x13,
       ACTION_ADD_TO_STATS_THEN_TECH = 0x15 };

static bool action_selectors_in_table(const GameState *g, uint8_t action, ScriptPlace at)
{
    unsigned first = 0, last = 0;
    if (action <= ACTION_TECH)
        first = last = 2;
    if (action == ACTION_TECH || action == ACTION_SET_STATS_THEN_TECH || action == ACTION_ADD_TO_STATS_THEN_TECH)
        last = 4;
    if (action == ACTION_SET_STATS_THEN_TECH || action == ACTION_ADD_TO_STATS_THEN_TECH)
        first = 3;
    if (action == ACTION_ADD_TO_TURN_WAIT)
        first = last = 1;
    for (unsigned operand = first; operand != 0 && operand <= last; operand++)
        if (!selector_in_table(enemy_script_byte(g, (ScriptPlace)(at + operand))))
            return false;
    return true;
}

static bool action_in_tables(const GameState *g, ScriptPlace at)
{
    uint8_t action = enemy_script_byte(g, at);
    if (action >= SCRIPT_ACTIONS)
        return true;
    return action < ACTION_KINDS && action_selectors_in_table(g, action, at);
}

enum { DRAWN_AMONG = 4 };

/* In the scripts, as the game's places always are. */
static bool in_scripts(ScriptPlace at) { return at >= ENEMY_SCRIPTS_START && at < ENEMY_SCRIPTS_END; }

static bool actions_in_table(const GameState *g, ScriptPlace at)
{
    uint8_t action = enemy_script_byte(g, at);
    if (!in_scripts(at) || !action_in_tables(g, at))
        return false;
    if (action != ACTION_ONE_OF_FOUR)
        return true;
    /* the draw steps past itself, then over the first three of the four by
     * their lengths: all four are actions */
    at = (ScriptPlace)(at + 1);
    for (unsigned n = 0; n < DRAWN_AMONG; n++) {
        action = enemy_script_byte(g, at);
        if (!in_scripts(at) || action >= ACTION_KINDS || !action_in_tables(g, at))
            return false;
        uint8_t length = enemy_action_length(g, action);
        if (n + 1 < DRAWN_AMONG && (length == 0 || length >= SCRIPT_ACTIONS))
            return false;
        at = (ScriptPlace)(at + length);
    }
    return true;
}

/* A block from a condition the entry carries out (`kind`): its conditions
 * (one or two), then its actions, which the choosing looks for past the first
 * end marker after wherever the conditions left the place (in an operand,
 * where a byte there is the marker). */
enum { CONDITIONS_A_BLOCK_HOLDS = 2 };

static bool block_in_tables(const GameState *g, uint8_t kind, ScriptPlace at)
{
    for (unsigned n = 0; n < CONDITIONS_A_BLOCK_HOLDS; n++) {
        ScriptPlace condition = (ScriptPlace)(at + n * SCRIPT_CONDITION_BYTES);
        if (n > 0 && enemy_script_byte(g, condition) == SCRIPT_BLOCK_END)
            break;
        if (n == 0 ? !condition_selector_in_table(g, kind, condition) : !condition_in_tables(g, condition))
            return false;
        for (unsigned left_at = 0; left_at < SCRIPT_CONDITION_BYTES; left_at++)
            if (!actions_in_table(g, (ScriptPlace)(end_after(g, (ScriptPlace)(condition + left_at)) + 1)))
                return false;
    }
    return true;
}

/* An enemy going on through its block's actions takes up the next it kept:
 * where the block it kept them for is one the choosing can reach from
 * `block` (a script has fewer than BLOCKS_A_SCRIPT_HOLDS). */
enum { BLOCKS_A_SCRIPT_HOLDS = 16 };

static bool next_actions_in_table(const GameState *g, uint8_t enemy, uint8_t block)
{
    uint8_t kept = battle_enemy_actions_block(g, enemy);
    bool reachable = kept >= block && kept - block < BLOCKS_A_SCRIPT_HOLDS;
    return !reachable || actions_in_table(g, battle_enemy_next_action(g, enemy));
}

/* The choosing works on the enemy in battle_enemy_turn (the turn in
 * battle_enemy_acting, the actor in battle_actor), each naming an enemy or a
 * combatant as every battle has them, and reads the script at a place in the
 * scripts. */
static bool working_on_an_enemy(const GameState *g)
{
    uint8_t enemy = enemy_at_work(g);
    if (enemy >= BATTLE_ENEMIES || battle_enemy_acting(g) >= BATTLE_ENEMIES || battle_actor(g) >= BATTLE_COMBATANTS)
        return false;
    return in_scripts(battle_script_place(g)) && no_member_comes_back(g);
}

/* The guards by what the entry takes up at the place: nothing of it (the
 * selectors), a condition (one the entry carries out), the actions after the
 * condition that held, an action (one the entry chooses or carries out), the
 * selector after the place, the selector in A, or the actions the choosing is
 * at. An entry is known by where it starts, which the machine is at. */
enum { TECH_BODY = 0x9A3D, NONE_TAKEN = 0xFF };

static uint8_t condition_of_entry(const Snes *s)
{
    for (uint8_t kind = 0; kind < CONDITION_KINDS; kind++)
        if (enemy_condition_routine(kind) == s->cpu.pc)
            return kind;
    return NONE_TAKEN;
}

static uint8_t action_of_entry(const Snes *s)
{
    uint16_t entry = s->cpu.pc;
    if (entry == TECH_BODY)
        return ACTION_TECH;
    for (uint8_t action = 0; action < ACTION_KINDS; action++)
        if (enemy_action_choice_routine(action) == entry || enemy_action_turn_routine(action) == entry)
            return action;
    return NONE_TAKEN;
}

/* An action entry reads its operands at the place; the draw takes up the
 * four after it. */
static bool action_entry_in_tables(const GameState *g, uint8_t action, ScriptPlace at)
{
    if (action == ACTION_ONE_OF_FOUR)
        return actions_in_table(g, at);
    return action < ACTION_KINDS && action_selectors_in_table(g, action, at);
}

typedef enum {
    TAKES_NOTHING,
    TAKES_CONDITION,
    TAKES_HELD,
    TAKES_ACTION,
    TAKES_SELECTOR,
    TAKES_SELECTOR_IN_A,
    TAKES_ACTIONS
} ScriptUse;

/* The next actions the enemy at work kept, where its block is the one they
 * are for; and the actions the choosing is at: those it keeps its place at,
 * and those. */
static bool next_actions_kept_in_table(const GameState *g)
{
    uint8_t enemy = enemy_at_work(g);
    return next_actions_in_table(g, enemy, battle_enemy_script_block(g, enemy));
}

static bool choosing_actions_in_table(const GameState *g)
{
    return actions_in_table(g, battle_script_actions_place(g)) && next_actions_kept_in_table(g);
}

static bool takes_what_the_tables_hold(const Snes *s, const GameState *g, ScriptUse use)
{
    ScriptPlace at = battle_script_place(g);
    switch (use) {
    case TAKES_CONDITION: return block_in_tables(g, condition_of_entry(s), at) && next_actions_kept_in_table(g);
    case TAKES_HELD: return actions_in_table(g, (ScriptPlace)(end_after(g, at) + 1)) && next_actions_kept_in_table(g);
    case TAKES_ACTION: {
        /* choosing again goes through the choosing's actions too */
        uint8_t action = action_of_entry(s);
        return action_entry_in_tables(g, action, at)
               && (action != ACTION_CHOOSE_AGAIN || choosing_actions_in_table(g));
    }
    case TAKES_SELECTOR: return selector_in_table(enemy_script_byte(g, (ScriptPlace)(at + 1)));
    case TAKES_SELECTOR_IN_A: return battle_selector_in_a(s) < SELECTORS;
    case TAKES_ACTIONS: return choosing_actions_in_table(g);
    default: return true;
    }
}

static bool script_ready(Snes *s, ScriptUse use)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ready = working_on_an_enemy(g) && takes_what_the_tables_hold(s, g, use);
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && ready;
}

bool battle_enemy_script_ready(Snes *s) { return script_ready(s, TAKES_NOTHING); }
bool battle_enemy_condition_ready(Snes *s) { return script_ready(s, TAKES_CONDITION); }
bool battle_enemy_held_ready(Snes *s) { return script_ready(s, TAKES_HELD); }
bool battle_enemy_action_ready(Snes *s) { return script_ready(s, TAKES_ACTION); }
bool battle_script_selector_ready(Snes *s) { return script_ready(s, TAKES_SELECTOR); }
bool battle_selector_in_a_ready(Snes *s) { return script_ready(s, TAKES_SELECTOR_IN_A); }
bool battle_enemy_actions_ready(Snes *s) { return script_ready(s, TAKES_ACTIONS); }
/* The turn starts from the acting enemy alone: a reaction carries out the
 * action its conditions chose, a turn the next the enemy kept. */
bool battle_enemy_turn_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t enemy = battle_enemy_acting(g);
    bool ready = enemy < BATTLE_ENEMIES && no_member_comes_back(g)
                 && actions_in_table(g, battle_action_hit_enemy(g) ? battle_script_actions_place(g)
                                                                  : battle_enemy_next_action(g, enemy));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && ready;
}

/* The choosing's starts find the script themselves: they need only the
 * enemy it is for (or the one past the last, as the choosing of them all
 * leaves it, which the search for a beaten enemy's reactions can run with),
 * the actions it would go on with, and no member that would come back. */
bool battle_enemy_choosing_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t enemy = enemy_at_work(g);
    bool ready = enemy <= BATTLE_ENEMIES
                 && (enemy == BATTLE_ENEMIES || next_actions_in_table(g, enemy, battle_enemy_script_block(g, enemy)))
                 && no_member_comes_back(g);
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && ready;
}

/* The choosing of one enemy starts with no pair of conditions under way, as
 * the choosing of them all leaves it: one under way would take the end marker
 * of a block of one condition for a second. */
bool battle_enemy_first_block_ready(Snes *s)
{
    vars_peek(true);
    bool none_under_way = battle_condition_pair(state_of(s)) == PAIR_NONE;
    vars_peek(false);
    return battle_enemy_choosing_ready(s) && none_under_way;
}

/* Of them all, those that choose. */
static bool chooses(const GameState *g, uint8_t enemy)
{
    uint8_t slot = (uint8_t)(BATTLE_FIRST_ENEMY + enemy);
    return ((battle_enemy_standing(g, enemy) & IN_RESERVE) || combatant_in_fight(g, slot)) && !battle_enemy_action_chosen(g, enemy);
}

bool battle_enemies_choose_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ready = no_member_comes_back(g);
    for (uint8_t enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        ready = ready && (!chooses(g, enemy) || next_actions_in_table(g, enemy, 0));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && ready;
}
