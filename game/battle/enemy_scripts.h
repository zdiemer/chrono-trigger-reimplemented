/* Battle: the enemies' minds -- their scripts.
 *
 * Every enemy acts by a script in the ROM, found by what the enemy is (its id,
 * battle_combatant_id). A script is two sections: what the enemy does on its
 * turns, and how it reacts when the party acts. A section is blocks, each one
 * or two conditions and then actions:
 *
 *     if the party member with the lowest HP is at half or less   (a condition)
 *         cast Fire on it; then attack it                          (two actions)
 *     otherwise, always
 *         attack a party member at random
 *
 * Choosing. After each action the battle has every enemy in the fight that
 * has nothing chosen look through its script's turn section (the choosing,
 * enemy_scripts_choose): its blocks from the first, until one whose
 * conditions hold. A condition that holds for someone takes them as its
 * target. The block's actions are then chosen: the enemy is going through
 * them, one a turn, and the one for its next turn is looked at now -- whom it
 * will act on, and the pose it takes to show it (battle/enemy_actions.c). A
 * block chosen again goes on with its next action; its last done, it starts
 * over; another block starts with its first. A block that starts with the
 * "one of these four" action draws again each time.
 *
 * The turn. When its turn comes the enemy carries out the action chosen (its
 * turn, battle/enemy_turn.c) and moves on to the next; it chooses again
 * after.
 *
 * Reacting. After a party member's action, each enemy it hit looks through
 * its script's reaction section the same way, for the actor; a block whose
 * conditions hold is carried out at once (battle/main_loop.c).
 *
 * The scripts' two sets of commands, conditions and actions, are C tables
 * (battle/enemy_conditions.c, battle/enemy_actions.c), a row a command, each
 * row with its operands' layout, which decodes them into the command's named
 * values. A condition says how it came out (ConditionResult) and the block's
 * testing here acts on it: failing the block, choosing its actions. What the
 * enemy aims at is a selector's choice (battle/targets.h), and where the
 * combatants stand is the target search's (battle/positions.h).
 *
 * A choosing's state is a ScriptChoosing, made by whoever starts one and
 * passed down. What each enemy has chosen -- its block, its next action --
 * is the battle's (battle_enemy_*), kept as the game goes. The choosing reads
 * the scripts' bytes as they are; the conditions and actions the tables don't
 * have (none in any script) are the original's. */
#ifndef CT_GAME_BATTLE_ENEMY_SCRIPTS_H
#define CT_GAME_BATTLE_ENEMY_SCRIPTS_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/rules_data.h"
#include "state.h"

/* How testing a block came out: its conditions held (and its actions were
 * chosen), they didn't, or they held but the actions found nobody to act on.
 * The original counts a failure on from where it stands, so a block counted
 * failing twice reads as nobody to act on, which the choosing then goes by. */
typedef enum { OUTCOME_HELD = 0, OUTCOME_FAILED = 1, OUTCOME_NOBODY_TO_ACT_ON = 2 } ScriptOutcome;

/* The byte that ends a block's conditions and its actions, and a section. */
enum { SCRIPT_BLOCK_END = 0xFE, SCRIPT_SECTION_END = 0xFF, SCRIPT_CONDITION_BYTES = 4 };

/* A script's action bytes run below this; anything else ends the actions. */
enum { SCRIPT_ACTIONS = 0x80 };

/* How far a block's two conditions have got: a block of one; the first of two
 * being tested; the first held, keeping its target for the second
 * (battle_condition_target). */
typedef enum { PAIR_NONE = 0x00, PAIR_FIRST = 0x01, PAIR_FIRST_HELD = 0x80 } ConditionPair;

/* One enemy's choosing, as it goes. */
typedef struct {
    uint8_t enemy;            /* the enemy choosing (BATTLE_ENEMIES once all have) */
    uint8_t condition;        /* the kind of the condition being tested */
    ConditionPair pair;
    ScriptOutcome outcome;    /* as the block's testing stands */
    ScriptPlace place;        /* how far the script has been read (below) */
    ScriptPlace block_start;  /* the block being tested */
    ScriptPlace actions_from; /* the actions being chosen from */
} ScriptChoosing;

/* How far the script has been read: each condition and action reads its
 * operands in order and the original keeps the place of the last it read.
 * The block's actions are looked for from there, after the first end marker
 * past it, and so is the next block; an operand byte that is the marker
 * (a number, an offset) moves where they are found, as in the original. */

/* A choosing for `enemy`, with no pair under way and nothing failed. */
ScriptChoosing enemy_choosing(const GameState *g, uint8_t enemy);

/* The original keeps a choosing in its cells (battle_enemy_turn,
 * battle_script_place, battle_block_start, battle_script_actions_place,
 * battle_condition_pair, battle_script_outcome, battle_condition_kind). Its
 * still-generated code, shadow mode and the guards read them, so the choosing
 * leaves them as the original would, once, as scaffolding. */
void enemy_leave_choosing(GameState *g, const ScriptChoosing *c);

/* For the machine's side (the entries, and the turns that are still machine
 * entries): a choosing as the original's callers leave it in the cells, and
 * handed back to them, the cells written as state the machine goes on from. */
ScriptChoosing enemy_choosing_in_cells(const GameState *g);
void enemy_hand_back_choosing(GameState *g, const ScriptChoosing *c);

/* ---- Choosing (battle/enemy_scripts.c) ----------------------------------------------------- */

/* $C1:8C09: every enemy that is in the fight, or waiting in reserve, and has
 * nothing chosen, looks through its script. */
void enemy_scripts_choose(GameState *g);

/* $C1:B488: where the reaction section of the script for `id` starts, for
 * `enemy` (its status may put another script in place of its own). With
 * `enemy` BATTLE_ENEMIES (the choosing of them all leaves it so) the statuses
 * are those of the block after the last enemy's. */
ScriptPlace enemy_reactions_start(GameState *g, uint8_t enemy, uint8_t id);

/* A block of a reaction section tested for the actor (the one at
 * c->block_start): its conditions, and where they hold its actions chosen;
 * c->outcome says how it came out. */
void enemy_test_block(GameState *g, ScriptChoosing *c);

/* On from the block tested to the next, c->place left at its start; false at
 * the section's end. */
bool enemy_script_next_block(GameState *g, ScriptChoosing *c);

/* $C1:8C63: choose from the actions at c->actions_from: the next of the block
 * the enemy is going through, or the first of a block it starts. */
void enemy_choose_actions(GameState *g, ScriptChoosing *c);

/* ---- The commands (battle/enemy_conditions.c, battle/enemy_actions.c) --------------------- */

enum { CONDITION_KINDS = 0x29, ACTION_KINDS = 0x17 };

/* How a condition came out, for the block's testing to act on: the block
 * fails; its actions are chosen; it holds with nothing chosen; or its actions
 * are chosen and it fails all the same. */
typedef enum { CONDITION_FAILS, CONDITION_HOLDS, CONDITION_HOLDS_UNCHOSEN, CONDITION_CHOOSES_BUT_FAILS } ConditionVerdict;

typedef struct {
    ConditionVerdict verdict;
    /* A condition about some of those picked takes one it holds for as the
     * block's target. Where it couldn't take one the block is counted failing
     * once more, and a block no longer held (this failure, or a first of two
     * that failed) comes out `otherwise` instead; one still held notes the
     * target for its pair (`pairs`). */
    bool takes_target, target_taken, pairs;
    ConditionVerdict otherwise;
    /* Nobody was picked: the condition read no further than its selector. */
    bool stopped_at_selector;
} ConditionResult;

/* Test the condition of kind `kind` at `at` for the choosing: how it came out,
 * and c->place moved on past what it read. */
ConditionResult enemy_test_condition(GameState *g, ScriptChoosing *c, uint8_t kind, ScriptPlace at);

/* Act on how a condition came out: the block counted failing, a pair's
 * target noted, the block's actions chosen. */
void enemy_act_on_condition(GameState *g, ScriptChoosing *c, ConditionResult r);

/* Choose the action at `at`, its byte read: whom it will act on, how the
 * enemy shows it. The choosing's place and outcome move as it says. The
 * draw among four isn't chosen here but by enemy_draw_one_of_four. */
void enemy_choose_action(GameState *g, ScriptChoosing *c, uint8_t action, ScriptPlace at);

/* $C1:98C5: one of the four actions after the draw at `at`, a quarter chance
 * each: where it is, and which of the four it is. */
typedef struct {
    ScriptPlace place;
    uint8_t quarter;
} DrawnAction;
DrawnAction enemy_draw_one_of_four(GameState *g, ScriptPlace at);

/* The choosing goes on from a draw: the action drawn is chosen as
 * enemy_choose_actions chooses, drawing again where it is a draw itself. */
void enemy_choose_after_draw(GameState *g, ScriptChoosing *c, DrawnAction drawn);

/* What the choosing hands its caller in A: the action's byte where the
 * actions had run out, the enemy's number where it names none, or the mark
 * that it chose. */
uint8_t enemy_choosing_result(const GameState *g, uint8_t enemy);

/* Whether a condition of `kind`, or the action, at `at` is one the tables
 * have, naming only selectors and searches there are (the guards'): a place
 * the game never leaves a script at can land on bytes that aren't. */
bool enemy_condition_in_tables(const GameState *g, uint8_t kind, ScriptPlace at);
bool enemy_action_in_tables(const GameState *g, uint8_t action, ScriptPlace at);

#endif
