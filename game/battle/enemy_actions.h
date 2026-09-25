/* Battle: the actions of the enemies' scripts (battle/enemy_actions.c), and
 * the enemy's turn that carries them out (battle/enemy_turn.c).
 *
 * An action is a byte below $80 and its operands, a length each has
 * ($FD:BA4A). Each is chosen, when the enemy's script picks its block --
 * whom it will act on, the pose it shows -- and carried out on the enemy's
 * turn:
 *
 *   - its attacks and techs, worked out and shown by the effect code, which is
 *     the original's still (battle/enemy_turn.c);
 *   - changing its own stats, or a combatant's turn wait;
 *   - leaving the fight, or dying, or changing into an enemy waiting in
 *     reserve, or bringing back enemies that were beaten;
 *   - its own count and flags, and a message.
 *
 * What an action leaves for its turn to do after it -- work its effect out
 * on its first target and show it, only show it, or nothing
 * (battle_action_follow_up) -- and the message it shows as it ends
 * (battle_action_message) are in the battle's cells, as the turn reads them. */
#ifndef CT_GAME_BATTLE_ENEMY_ACTIONS_H
#define CT_GAME_BATTLE_ENEMY_ACTIONS_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/rules_data.h"
#include "state.h"

/* The actions, by byte. */
typedef enum {
    ACTION_POSE = 0x00,           /* show a pose toward a combatant, and nothing more */
    ACTION_ATTACK = 0x01,
    ACTION_TECH = 0x02,
    ACTION_ONE_OF_FOUR = 0x04,    /* one of the four actions after it, drawn */
    ACTION_CHOOSE_AGAIN = 0x05,
    ACTION_CHANGE_INTO = 0x07,    /* change into an enemy waiting in reserve */
    ACTION_LEAVE = 0x09,
    ACTION_DIE = 0x0A,
    ACTION_SET_STAT = 0x0B,
    ACTION_ADD_TO_STAT = 0x0C,
    ACTION_FLAGS = 0x0D,
    ACTION_MESSAGE = 0x0F,
    ACTION_BRING_BACK = 0x10,     /* bring back the enemies that were beaten */
    ACTION_SET_STATS = 0x11,
    ACTION_SET_STATS_THEN_TECH = 0x12,
    ACTION_ADD_TO_TURN_WAIT = 0x13,
    ACTION_ADD_TO_STATS = 0x14,
    ACTION_ADD_TO_STATS_THEN_TECH = 0x15,
    ACTION_CALL_IN = 0x16,
} EnemyAction;

/* What an action leaves its turn to do after it (battle_action_follow_up). */
typedef enum { FOLLOW_WITH_EFFECT = 0, FOLLOW_WITH_SHOWING = 1, FOLLOW_WITH_NOTHING = 2 } ActionFollowUp;

/* ---- Choosing (battle/enemy_actions.c) ------------------------------------------------------ */

/* The pose an action is announced with sets the low bits of
 * battle_action_pose_kind ($C1:ACCE). */
void enemy_note_pose_kind(GameState *g);

/* ---- Carrying out: the parts between the original's calls ---------------------------------- */

/* $C1:ACF2: the action record the effect code reads: the actor, the action
 * and its number. */
void enemy_record_action(GameState *g);

/* $C1:AC89: the acting enemy's bits for the effect code. */
void enemy_note_actor_bits(GameState *g, uint8_t enemy);

/* The attack: its target is the enemy's (battle_enemy_target), its number and
 * pose from the script at `at`. */
void enemy_attack_begins(GameState *g, ScriptPlace at);
void enemy_attack_ends(GameState *g);

/* A tech: its number, and its first target the enemy's foe; the second and
 * third by the selectors at `at` + 3 and + 4, where given (true where either
 * is). Then, once its
 * targeting ($C1:C1DD) has run, whether it could be used: false where it
 * couldn't (the block's outcome is then nobody to act on). */
bool enemy_tech_begins(GameState *g, ScriptPlace at, bool none_checked);
bool enemy_tech_aimed(GameState *g);
void enemy_tech_ends(GameState *g);

/* The stat changes, the counter and flags, the bringing back: the actions
 * with no call of the original's but their message's. */
void enemy_carry_out(GameState *g, EnemyAction action, ScriptPlace at);

/* $C1:9B8D: change into the enemy waiting in reserve the action names; false
 * where there is none. Its coming in is the original's code where the action
 * asks for it (`brought_in`). */
bool enemy_change_into(GameState *g, ScriptPlace at, bool *brought_in);
void enemy_change_takes_hp(GameState *g);
void enemy_change_ends(GameState *g);

/* $C1:9E78: the enemies that were beaten come back, each brought in by the
 * original's code: the next to bring in, from `enemy`, or BATTLE_ENEMIES. */
uint8_t enemy_next_to_bring_back(const GameState *g, uint8_t enemy);
void enemy_bring_back_starts(GameState *g, ScriptPlace at);
void enemy_bring_back(GameState *g, uint8_t enemy, uint8_t count);
void enemy_brought_back(GameState *g, uint8_t enemy, ScriptPlace at);
void enemy_bring_back_ends(GameState *g, ScriptPlace at, uint8_t count);

/* $C1:9B48: choose again, as the action says (no script has it). */
void enemy_choose_again(GameState *g, ScriptPlace at);

/* $C1:A396: what the calling in gives the original's code that does it. */
void enemy_call_in_starts(GameState *g, ScriptPlace at);

/* The stat changes a set-stats-then-tech action makes first; true where the
 * last of an adding one carried past a byte or took away no more than there
 * was (the carry its change leaves, which the original's targeting after it
 * reads where the tech names no further target). */
bool enemy_set_stats_before_tech(GameState *g, ScriptPlace at, bool adding);

#endif
