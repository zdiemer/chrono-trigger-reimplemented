/* Battle: the enemies' scripts -- choosing what an enemy does
 * (battle/enemy_scripts.h).
 *
 * The choosing goes through a section block by block. A block's conditions
 * are tested from the conditions' table (battle/enemy_conditions.c), each
 * saying how it came out; the block's testing here acts on that -- counting
 * the block failed, noting a pair's target, choosing its actions -- and the
 * actions are chosen from the actions' table (battle/enemy_actions.c). */
#include "battle/enemy_scripts.h"

#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/enemy_actions.h"
#include "battle/entries.h"
#include "battle/rules_data.h"
#include "battle/targets.h"
#include "charge.h"
#include "original_effects.h"
#include "vars.h"

/* ---- The choosing's state ------------------------------------------------------------------ */

static void write_choosing(GameState *g, const ScriptChoosing *c)
{
    set_battle_enemy_turn(g, c->enemy);
    set_battle_condition_kind(g, c->condition);
    set_battle_condition_pair(g, c->pair);
    set_battle_script_outcome(g, c->outcome);
    set_battle_script_place(g, c->place);
    set_battle_block_start(g, c->block_start);
    set_battle_script_actions_place(g, c->actions_from);
}

ScriptChoosing enemy_choosing_in_cells(const GameState *g)
{
    return (ScriptChoosing){
        .enemy = battle_enemy_turn(g),
        .condition = battle_condition_kind(g),
        .pair = (ConditionPair)battle_condition_pair(g),
        .outcome = (ScriptOutcome)battle_script_outcome(g),
        .place = battle_script_place(g),
        .block_start = battle_block_start(g),
        .actions_from = battle_script_actions_place(g),
    };
}

/* What a choosing doesn't come to set, the original leaves in its cells as it
 * was: so the scaffolding starts from the cells, which the logic never reads
 * back (each field is set before it is used). */
ScriptChoosing enemy_choosing(const GameState *g, uint8_t enemy)
{
    ScriptChoosing c = { .enemy = enemy, .pair = PAIR_NONE, .outcome = OUTCOME_HELD };
    ORIGINAL_EFFECT(c = enemy_choosing_in_cells(g), c.enemy = enemy);
    return c;
}

/* The enemy at work is read by the original's search for reactions after the
 * choosing of them all has left it past the last ($C1:B488's caller), so it
 * is kept plainly; the rest is scaffolding. */
void enemy_leave_choosing(GameState *g, const ScriptChoosing *c)
{
    ORIGINAL_EFFECT(write_choosing(g, c));
    set_battle_enemy_turn(g, c->enemy);
}
void enemy_hand_back_choosing(GameState *g, const ScriptChoosing *c) { write_choosing(g, c); }

/* ---- Reading a script ---------------------------------------------------------------------- */

/* The place of the byte that ends the conditions or the actions a place is
 * in: the first end marker after it. */
static ScriptPlace end_after(const GameState *g, ScriptPlace at)
{
    do
        at++;
    while (enemy_script_byte(g, at) != SCRIPT_BLOCK_END);
    return at;
}

static bool is_end_of_actions(const GameState *g, ScriptPlace at) { return enemy_script_byte(g, at) >= SCRIPT_ACTIONS; }

static uint8_t slot_of_enemy(uint8_t enemy) { return (uint8_t)(BATTLE_FIRST_ENEMY + enemy); }

/* ---- Choosing the actions ------------------------------------------------------------------ */

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

/* The action to choose: a reaction's first; the first of a block the enemy
 * starts; or the next of the block it is going through, starting over once
 * its last is done (a block that draws among four draws again). */
static ScriptPlace action_to_choose(GameState *g, uint8_t enemy, ScriptPlace first)
{
    if (battle_action_hit_enemy(g))
        return first;
    if (battle_enemy_script_block(g, enemy) != battle_enemy_actions_block(g, enemy))
        return start_block_actions(g, enemy, first);
    ScriptPlace next = enemy_script_byte(g, first) == ACTION_ONE_OF_FOUR ? first : battle_enemy_next_action(g, enemy);
    ScriptPlace at = take_next_action(g, enemy, next);
    if (is_end_of_actions(g, at))
        at = start_block_actions(g, enemy, first);
    return at;
}

/* After a draw among four, the one drawn is the block's actions' place from
 * then on, and the enemy is at it. */
static ScriptPlace after_draw(GameState *g, ScriptChoosing *c, DrawnAction drawn)
{
    c->actions_from = drawn.place;
    ScriptPlace at = take_next_action(g, c->enemy, drawn.place);
    if (is_end_of_actions(g, at))
        at = start_block_actions(g, c->enemy, drawn.place);
    return at;
}

/* Choose the action at `at`, drawing again where it is a draw among four. */
static void choose_from(GameState *g, ScriptChoosing *c, ScriptPlace at)
{
    uint8_t enemy = c->enemy;
    for (;;) {
        c->place = at;
        uint8_t action = enemy_script_byte(g, at);
        set_battle_action_kind(g, action);
        if (action >= SCRIPT_ACTIONS)
            return;
        if (action != ACTION_ONE_OF_FOUR) {
            enemy_choose_action(g, c, action, at);
            break;
        }
        DrawnAction drawn;
        TIMED_AS(g, 0xC198C5, drawn = enemy_draw_one_of_four(g, at));
        at = after_draw(g, c, drawn);
    }
    if (enemy < BATTLE_ENEMIES)
        set_battle_enemy_action_chosen(g, enemy, true);
}

// $C1:8C63
void enemy_choose_actions(GameState *g, ScriptChoosing *c) { choose_from(g, c, action_to_choose(g, c->enemy, c->actions_from)); }

// $C1:8C90
void enemy_choose_after_draw(GameState *g, ScriptChoosing *c, DrawnAction drawn) { choose_from(g, c, after_draw(g, c, drawn)); }

/* ---- Testing a block ------------------------------------------------------------------------ */

// $C1:8C3E
/* A condition held: it is the enemy's last condition held, and unless it was
 * the first of two its block's actions are chosen, from past the conditions'
 * end marker after how far the script has been read. */
static void condition_held(GameState *g, ScriptChoosing *c)
{
    set_battle_enemy_last_condition(g, c->enemy, c->condition);
    if (c->pair == PAIR_FIRST_HELD)
        return;
    c->actions_from = (ScriptPlace)(end_after(g, c->place) + 1);
    enemy_choose_actions(g, c);
}

// $C1:AEFD
/* A pair's bookkeeping, once a condition has taken its target: the first of
 * two keeps the target for the second, and the second closes the pair. */
static void note_pair(GameState *g, ScriptChoosing *c)
{
    if (c->pair & PAIR_FIRST) {
        set_battle_condition_target(g, battle_candidates(g, 0) & CANDIDATE_SLOT);
        c->pair = PAIR_FIRST_HELD;
    } else if (c->pair & PAIR_FIRST_HELD) {
        c->pair = PAIR_NONE;
    }
}

void enemy_act_on_condition(GameState *g, ScriptChoosing *c, ConditionResult r)
{
    ConditionVerdict verdict = r.verdict;
    if (r.takes_target) {
        if (!r.target_taken)
            c->outcome = (ScriptOutcome)(uint8_t)(c->outcome + 1); /* counted in a byte */
        if (c->outcome != OUTCOME_HELD)
            verdict = r.otherwise;
        else if (r.pairs)
            note_pair(g, c);
    }
    switch (verdict) {
    case CONDITION_FAILS: c->outcome = OUTCOME_FAILED; break;
    case CONDITION_HOLDS: condition_held(g, c); break;
    case CONDITION_CHOOSES_BUT_FAILS:
        condition_held(g, c);
        c->outcome = OUTCOME_FAILED;
        break;
    case CONDITION_HOLDS_UNCHOSEN: break;
    }
}

static void test_condition_at(GameState *g, ScriptChoosing *c, ScriptPlace at)
{
    uint8_t kind = enemy_script_byte(g, at);
    c->condition = kind;
    c->place = at;
    enemy_act_on_condition(g, c, enemy_test_condition(g, c, kind, at));
}

/* The second condition of the block being tested, where it has one (a
 * failed first is counted in the outcome, which the second can't undo). */
static void test_second_condition(GameState *g, ScriptChoosing *c)
{
    if (c->pair == PAIR_NONE)
        return;
    test_condition_at(g, c, (ScriptPlace)(c->block_start + SCRIPT_CONDITION_BYTES));
}

static bool has_two_conditions(const GameState *g, ScriptPlace block)
{
    return enemy_script_byte(g, (ScriptPlace)(block + SCRIPT_CONDITION_BYTES)) != SCRIPT_BLOCK_END;
}

void enemy_test_block(GameState *g, ScriptChoosing *c)
{
    c->pair = has_two_conditions(g, c->block_start) ? PAIR_FIRST : PAIR_NONE;
    test_condition_at(g, c, c->block_start);
    test_second_condition(g, c);
}

/* A block that held but found nobody to act on is already past its
 * conditions; any other is looked past from them. */
bool enemy_script_next_block(GameState *g, ScriptChoosing *c)
{
    if (c->outcome != OUTCOME_NOBODY_TO_ACT_ON) {
        c->pair = PAIR_NONE;
        c->place = end_after(g, c->place);
    }
    c->place = (ScriptPlace)(end_after(g, c->place) + 1);
    return enemy_script_byte(g, c->place) != SCRIPT_SECTION_END;
}

/* ---- Looking through a script ------------------------------------------------------------- */

/* Two statuses put a script of their own in place of the enemy's: one that
 * makes it attack the nearest party member (bit 7 of its fourth status byte,
 * or of the statuses it keeps there), and one that makes it attack anyone
 * (its confusion, bit 2 of the first status byte, unless it shrugs that off).
 * With no enemy at work (the one past the last) the statuses read are those
 * of the block after the last, by the bytes' offsets. */
enum { ATTACKS_NEAREST = 0x80, CONFUSED = 0x04 };

static uint8_t attack_statuses(const GameState *g, uint8_t slot)
{
    return (uint8_t)(combatant_stat_byte(g, slot, STAT_STATUS_4) | combatant_stat_byte(g, slot, STAT_KEPT_STATUS_4));
}

static uint8_t confusion_statuses(const GameState *g, uint8_t slot)
{
    return (uint8_t)(combatant_stat_byte(g, slot, STAT_STATUS_1) & (uint8_t)~combatant_stat_byte(g, slot, STAT_STATUS_IGNORED));
}

// $C1:B4AA
/* The original works out the stat block's place with a multiply first, whose
 * leftover high word the random draw goes on with (battle_long_high_word). */
static ScriptPlace script_place(GameState *g, uint8_t enemy, ScriptPlace at)
{
    uint8_t slot = slot_of_enemy(enemy);
    battle_multiply_words(g, slot, BATTLE_STAT_BLOCK_BYTES);
    if (attack_statuses(g, slot) & ATTACKS_NEAREST)
        return SCRIPT_ATTACK_NEAREST;
    if (confusion_statuses(g, slot) & CONFUSED)
        return SCRIPT_ATTACK_ANYONE;
    return at;
}

// $C1:B488
ScriptPlace enemy_reactions_start(GameState *g, uint8_t enemy, uint8_t id)
{
    ScriptPlace at = script_place(g, enemy, enemy_script_start(g, id));
    while (enemy_script_byte(g, at) != SCRIPT_SECTION_END)
        at++;
    return (ScriptPlace)(at + 1);
}

// $C1:AFD2
/* The enemy looks through the turn section of the script for `id`, a block
 * at a time, until one holds or the section ends. How each block came out is
 * the enemy's, and the blocks it went past are counted (battle_enemy_script_
 * block, which starts over after). The original clears the pair of cells at
 * $0A first, whose second the stat test reads (battle.h). */
static void choose_by_script(GameState *g, ScriptChoosing *c, uint8_t id)
{
    uint8_t enemy = c->enemy;
    c->block_start = c->place = enemy_script_start(g, id);
    battle_keep_stat_offset_word(g, 0);
    for (;;) {
        c->outcome = OUTCOME_HELD;
        if (has_two_conditions(g, c->block_start))
            c->pair = PAIR_FIRST;
        test_condition_at(g, c, script_place(g, enemy, c->block_start));
        test_second_condition(g, c);
        set_battle_enemy_script_outcome(g, enemy, c->outcome);
        if (c->outcome == OUTCOME_NOBODY_TO_ACT_ON)
            set_battle_enemy_action_chosen(g, enemy, false);
        if (c->outcome == OUTCOME_HELD)
            break;
        set_battle_enemy_last_condition(g, enemy, NO_CONDITION);
        set_battle_action_hit_enemy(g, false);
        if (!enemy_script_next_block(g, c))
            break;
        c->block_start = c->place;
        set_battle_enemy_script_block(g, enemy, (uint8_t)(battle_enemy_script_block(g, enemy) + 1));
    }
    set_battle_enemy_script_block(g, enemy, 0);
}

/* Of the enemies, those that choose: in the fight or waiting in reserve (an
 * enemy in reserve looks through the script of the id it is fighting as,
 * whatever it is), with nothing chosen. */
static bool chooses(const GameState *g, uint8_t enemy)
{
    bool waiting = (battle_enemy_standing(g, enemy) & ENEMY_IN_RESERVE) != 0;
    return (waiting || combatant_in_fight(g, slot_of_enemy(enemy))) && !battle_enemy_action_chosen(g, enemy);
}

// $C1:8C09
void enemy_scripts_choose(GameState *g)
{
    ScriptChoosing c = enemy_choosing(g, 0);
    for (uint8_t enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        c.enemy = enemy;
        c.pair = PAIR_NONE;
        set_battle_enemy_script_outcome(g, enemy, OUTCOME_HELD);
        set_battle_enemy_script_block(g, enemy, 0);
        if (chooses(g, enemy))
            TIMED_AS(g, 0xC1AFD2, choose_by_script(g, &c, battle_combatant_fighting(g, slot_of_enemy(enemy))));
    }
    c.enemy = BATTLE_ENEMIES;
    enemy_leave_choosing(g, &c);
}

/* ---- The entries ------------------------------------------------------------------------ */

/* Each takes the choosing from the cells its callers keep it in and hands it
 * back there. */
void battle_enemy_scripts_choose(Snes *s)
{
    enemy_scripts_choose(state_of(s));
    m_rts(s);
}

/* The enemy's id comes in A. */
void battle_enemy_script_choose(Snes *s)
{
    GameState *g = state_of(s);
    ScriptChoosing c = enemy_choosing_in_cells(g);
    choose_by_script(g, &c, (uint8_t)s->cpu.a);
    enemy_hand_back_choosing(g, &c);
    m_rts(s);
}

enum { CHOSEN_MARK = 1 };

uint8_t enemy_choosing_result(const GameState *g, uint8_t enemy)
{
    uint8_t action = battle_action_kind(g);
    if (action >= SCRIPT_ACTIONS)
        return action;
    return enemy >= BATTLE_ENEMIES ? enemy : CHOSEN_MARK;
}

void battle_enemy_condition_held(Snes *s)
{
    GameState *g = state_of(s);
    ScriptChoosing c = enemy_choosing_in_cells(g);
    bool first_of_two = c.pair == PAIR_FIRST_HELD;
    condition_held(g, &c);
    enemy_hand_back_choosing(g, &c);
    s->cpu.a = first_of_two ? PAIR_FIRST_HELD : enemy_choosing_result(g, c.enemy);
    m_rts(s);
}

void battle_enemy_choose_actions(Snes *s)
{
    GameState *g = state_of(s);
    ScriptChoosing c = enemy_choosing_in_cells(g);
    enemy_choose_actions(g, &c);
    enemy_hand_back_choosing(g, &c);
    s->cpu.a = enemy_choosing_result(g, c.enemy);
    m_rts(s);
}

/* The enemy's id comes in A; the section's end marker is left there, and the
 * place the reactions start is where the caller goes on reading. */
void battle_enemy_find_reactions(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_script_place(g, enemy_reactions_start(g, battle_enemy_turn(g), (uint8_t)s->cpu.a));
    s->cpu.a = SCRIPT_SECTION_END;
    m_rts(s);
}

/* The place comes in X; A is left with the status byte it tested last. */
static uint8_t status_tested_last(const GameState *g, uint8_t slot)
{
    uint8_t fourth = attack_statuses(g, slot);
    return (fourth & ATTACKS_NEAREST) ? fourth : confusion_statuses(g, slot);
}

void battle_enemy_find_script_place(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t enemy = battle_enemy_turn(g);
    set_battle_script_place(g, script_place(g, enemy, s->cpu.x));
    s->cpu.a = status_tested_last(g, slot_of_enemy(enemy));
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
 * guards look at what the entry would take up from the place: a condition and
 * the selector it names, an action and its selectors, and after a draw among
 * four, those four. */
static bool selector_in_table(uint8_t selector) { return selector < SELECTORS || (selector & SELECT_CONDITION_TARGET); }

/* An action by its byte: the end of the actions, or one of the tables'. */
static bool action_byte_in_tables(const GameState *g, ScriptPlace at)
{
    uint8_t action = enemy_script_byte(g, at);
    return action >= SCRIPT_ACTIONS || enemy_action_in_tables(g, action, at);
}

enum { DRAWN_AMONG = 4 };

/* In the scripts, as the game's places always are. */
static bool in_scripts(ScriptPlace at) { return at >= ENEMY_SCRIPTS_START && at < ENEMY_SCRIPTS_END; }

static bool actions_in_table(const GameState *g, ScriptPlace at)
{
    if (!in_scripts(at) || !action_byte_in_tables(g, at))
        return false;
    if (enemy_script_byte(g, at) != ACTION_ONE_OF_FOUR)
        return true;
    /* the draw steps past itself, then over the first three of the four by
     * their lengths: all four are actions */
    at = (ScriptPlace)(at + 1);
    for (unsigned n = 0; n < DRAWN_AMONG; n++) {
        uint8_t action = enemy_script_byte(g, at);
        if (!in_scripts(at) || !enemy_action_in_tables(g, action, at))
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
        if (!enemy_condition_in_tables(g, n == 0 ? kind : enemy_script_byte(g, condition), condition))
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
    if (battle_enemy_turn(g) >= BATTLE_ENEMIES || battle_enemy_acting(g) >= BATTLE_ENEMIES || battle_actor(g) >= BATTLE_COMBATANTS)
        return false;
    return in_scripts(battle_script_place(g)) && no_member_comes_back(g);
}

/* An action entry reads its operands at the place; the draw takes up the
 * four after it. */
static bool action_entry_in_tables(const GameState *g, uint8_t action, ScriptPlace at)
{
    if (action == ACTION_ONE_OF_FOUR)
        return actions_in_table(g, at);
    return enemy_action_in_tables(g, action, at);
}

/* The next actions the enemy at work kept, where its block is the one they
 * are for; and the actions the choosing is at: those it keeps its place at,
 * and those. */
static bool next_actions_kept_in_table(const GameState *g)
{
    uint8_t enemy = battle_enemy_turn(g);
    return next_actions_in_table(g, enemy, battle_enemy_script_block(g, enemy));
}

static bool choosing_actions_in_table(const GameState *g)
{
    return actions_in_table(g, battle_script_actions_place(g)) && next_actions_kept_in_table(g);
}

static bool action_entry_fits_the_tables(const GameState *g, uint8_t action, ScriptPlace at)
{
    /* choosing again goes through the choosing's actions too */
    return action_entry_in_tables(g, action, at) && (action != ACTION_CHOOSE_AGAIN || choosing_actions_in_table(g));
}

bool enemy_action_entry_fits(const GameState *g, uint8_t action)
{
    return working_on_an_enemy(g) && action_entry_fits_the_tables(g, action, battle_script_place(g));
}

static bool takes_what_the_tables_hold(const Snes *s, const GameState *g, ScriptUse use, uint8_t command)
{
    ScriptPlace at = battle_script_place(g);
    switch (use) {
    case TAKES_CONDITION: return block_in_tables(g, command, at) && next_actions_kept_in_table(g);
    case TAKES_HELD: return actions_in_table(g, (ScriptPlace)(end_after(g, at) + 1)) && next_actions_kept_in_table(g);
    case TAKES_ACTION: return action_entry_fits_the_tables(g, command, at);
    case TAKES_SELECTOR: return selector_in_table(enemy_script_byte(g, (ScriptPlace)(at + 1)));
    case TAKES_SELECTOR_IN_A: return battle_selector_in_a(s) < SELECTORS;
    case TAKES_ACTIONS: return choosing_actions_in_table(g);
    case TAKES_NOTHING: return true;
    }
    return true;
}

bool battle_script_entry_ready(Snes *s, ScriptUse use, uint8_t command)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ready = working_on_an_enemy(g) && takes_what_the_tables_hold(s, g, use, command);
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && ready;
}

bool battle_enemy_script_ready(Snes *s) { return battle_script_entry_ready(s, TAKES_NOTHING, 0); }
bool battle_enemy_held_ready(Snes *s) { return battle_script_entry_ready(s, TAKES_HELD, 0); }
bool battle_script_selector_ready(Snes *s) { return battle_script_entry_ready(s, TAKES_SELECTOR, 0); }
bool battle_selector_in_a_ready(Snes *s) { return battle_script_entry_ready(s, TAKES_SELECTOR_IN_A, 0); }
bool battle_enemy_actions_ready(Snes *s) { return battle_script_entry_ready(s, TAKES_ACTIONS, 0); }

/* The turn starts from the acting enemy alone: a reaction carries out the
 * action its conditions chose, a turn the next the enemy kept. */
bool battle_enemy_turn_fits(const GameState *g)
{
    uint8_t enemy = battle_enemy_acting(g);
    return enemy < BATTLE_ENEMIES && no_member_comes_back(g)
           && actions_in_table(g, battle_action_hit_enemy(g) ? battle_script_actions_place(g)
                                                            : battle_enemy_next_action(g, enemy));
}

bool battle_enemy_turn_ready(Snes *s)
{
    vars_peek(true);
    bool ready = battle_enemy_turn_fits(state_of(s));
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
    uint8_t enemy = battle_enemy_turn(g);
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
