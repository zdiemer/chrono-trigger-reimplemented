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
 * turn, battle/enemy_actions.c) and moves on to the next; it chooses again
 * after.
 *
 * Reacting. After a party member's action, each enemy it hit looks through
 * its script's reaction section the same way, for the actor; a block whose
 * conditions hold is carried out at once (battle/main_loop.c).
 *
 * The scripts' two sets of commands, conditions and actions, are C tables
 * (battle/enemy_conditions.c, battle/enemy_actions.c), a row a command, with
 * the operands each takes decoded by its row. What the enemy aims at is a
 * selector's choice (battle/targets.h), and where the combatants stand is the
 * target search's (battle/positions.h).
 *
 * The state the choosing keeps is the original's, in its cells: where the
 * script is being read (battle_script_place), how its testing came out
 * (battle_script_outcome), each enemy's chosen block and next action. The
 * choosing reads the scripts' bytes as they are; the conditions and actions
 * the tables don't have (none in any script) are the original's. */
#ifndef CT_GAME_BATTLE_ENEMY_SCRIPTS_H
#define CT_GAME_BATTLE_ENEMY_SCRIPTS_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/rules_data.h"
#include "state.h"

/* How testing a block came out (battle_script_outcome): its conditions held
 * (and its actions were chosen), they didn't, or they held but the actions
 * found nobody to act on. */
typedef enum { OUTCOME_HELD = 0, OUTCOME_FAILED = 1, OUTCOME_NOBODY_TO_ACT_ON = 2 } ScriptOutcome;

/* The byte that ends a block's conditions and its actions, and a section. */
enum { SCRIPT_BLOCK_END = 0xFE, SCRIPT_SECTION_END = 0xFF, SCRIPT_CONDITION_BYTES = 4 };

/* A script's action bytes run below this; anything else ends the actions. */
enum { SCRIPT_ACTIONS = 0x80 };

/* ---- Choosing (battle/enemy_scripts.c) ----------------------------------------------------- */

/* $C1:8C09: every enemy that is in the fight, or waiting in reserve, and has
 * nothing chosen, looks through its script. */
void enemy_scripts_choose(GameState *g);

/* $C1:AFD2: the enemy the battle is working on (battle_enemy_turn) looks
 * through the turn section of the script for `id`. */
void enemy_script_choose(GameState *g, uint8_t id);

/* $C1:B488: where the reaction section of the script for `id` starts, for
 * the enemy the battle is working on (its status may put another script in
 * place of its own); left where the script is read. */
void enemy_script_find_reactions(GameState *g, uint8_t id);

/* A block of a reaction section tested for the actor: its conditions, and
 * where they hold its actions chosen (battle_script_outcome says how it came
 * out). */
void enemy_test_block(GameState *g, ScriptPlace block);

/* On from the block tested to the next, the place being read left at its
 * start; false at the section's end. */
bool enemy_script_next_block(GameState *g);

/* ---- What the commands share (battle/enemy_scripts.c) ------------------------------------ */

/* The place a script is read at, for what reads it next. */
void read_script_at(GameState *g, ScriptPlace at);

/* $C1:8C3E: a condition held. The enemy's last condition held is noted; unless
 * this was the first of two, its block's actions are chosen. */
void enemy_condition_held(GameState *g);

/* $C1:8C63: choose from the actions at battle_script_actions_place: the next
 * of the block the enemy is going through, or the first of a block it
 * starts. */
void enemy_choose_actions(GameState *g);

/* The enemy the battle is working on, and its battle slot. */
uint8_t enemy_at_work(const GameState *g);
uint8_t enemy_at_work_slot(const GameState *g);

/* The block being tested failed: the outcome is counted on. */
void enemy_block_fails(GameState *g);

/* ---- The commands (battle/enemy_conditions.c, battle/enemy_actions.c) --------------------- */

/* Test the condition at the place being read (its kind byte). */
void enemy_test_condition(GameState *g, uint8_t kind);

/* Choose an action, its byte read: whom it will act on, how the enemy shows
 * it. `place` is where it is. */
void enemy_choose_action(GameState *g, uint8_t action);

enum { CONDITION_KINDS = 0x29, ACTION_KINDS = 0x17 };

/* The original's routine (in bank C1) that carries out a condition, an
 * action's choosing and its turn, whose time each is timed as; 0 for none.
 * The guards also know an entry by it. */
uint16_t enemy_condition_routine(uint8_t kind);
uint16_t enemy_action_choice_routine(uint8_t action);
uint16_t enemy_action_turn_routine(uint8_t action);

#endif
