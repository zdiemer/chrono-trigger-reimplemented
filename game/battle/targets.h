/* Battle: whom the enemies' scripts aim at.
 *
 * An enemy's script says whom a condition is about, or whom an action is
 * done to, by a selector: one of 57 ways of picking combatants -- the party
 * members in the fight, the other enemies, itself, the one that acted on it
 * last, the party member nearest it, those with a given status, one at
 * random. Selecting fills the candidates (battle_candidates): the combatants
 * picked, a battle slot each, in order. A condition marks those it holds for
 * and takes one of them as its target; an action takes the candidates for
 * its targets, the first before the others (battle_action_targets).
 *
 * Two statuses bear on the enemies' aim when a selection by status picks a
 * combatant: one draws it, marking the combatant so that it is taken before
 * the others; one turns it, and an enemy that would take a combatant with it
 * takes the party member farthest from it instead.
 *
 * Choosing among the candidates keeps the original's quirks, which the game
 * shows: see each operation. */
#ifndef CT_GAME_BATTLE_TARGETS_H
#define CT_GAME_BATTLE_TARGETS_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* A candidate: a battle slot, with the marks. */
typedef uint8_t Candidate;
enum {
    CANDIDATE_SLOT = 0x0F,
    CANDIDATE_MARKED = 0x80,    /* a condition picked it out, or it draws the aim */
    CANDIDATE_TURNS_AIM = 0x40, /* it turns the aim */
    NO_CANDIDATE = 0xFF,
};

/* A selector names a way of picking, $00-$38; one with bit 7 set names the
 * combatant the first of a block's two conditions settled on
 * (battle_condition_target). */
typedef uint8_t Selector;
enum {
    SELECT_NOBODY = 0x00,
    SELECT_PARTY = 0x01,
    SELECT_ENEMIES = 0x16,
    SELECT_CONDITION_TARGET = 0x80,
    SELECTORS = 0x39,
};

/* $C1:AC46: no candidates, and no targets. */
void battle_clear_candidates(GameState *g);

/* $C1:AC14, less its reading of the script: pick the candidates by a
 * selector for the enemy whose script is running (battle_enemy_turn), and
 * keep them as the action's targets. The first is kept apart too
 * (battle_first_candidate). A selector past the 57 (none in the scripts)
 * picks nobody. */
void battle_select_candidates(GameState *g, Selector selector);

/* $C1:AC2C: the same by a selector the caller gives, the candidates already
 * cleared. */
void battle_select_candidates_kept(GameState *g, Selector selector);

/* The candidates alone ($C1:B8BB's table), for a selector below
 * SELECTORS. */
void battle_pick_candidates(GameState *g, Selector selector);

/* Take the first candidate by the enemies' order drawn at the start
 * (battle_enemy_order), compared, as the original does, with the candidates
 * as they are: false where none matches. */
bool battle_take_first_by_enemy_order(GameState *g);

/* $C1:AE21: take one of the candidates a condition marked as its target.
 * False where there is none to take. */
bool battle_take_marked_candidate(GameState *g);

/* $C1:AD09, $C1:AD35: the action's first target, and its others, as a bit
 * each (bit 15 battle slot 0, down) for the effect code. */
void battle_mark_first_target(GameState *g);
void battle_mark_other_targets(GameState *g);

Candidate battle_candidate(const GameState *g, unsigned place);
uint8_t battle_candidates_held(const GameState *g);

/* The candidates are counted as the battle counts them (the guards'). */
bool battle_candidates_counted(const GameState *g);

/* The carry the original's marking of the first target ($C1:AD09) and of the
 * others ($C1:AD35) leaves, from the candidates and the carry it found: the
 * original's callees after it read it (battle/enemy_turn.c). */
bool battle_first_mark_carry(const GameState *g, bool carry);
bool battle_other_marks_carry(const GameState *g, bool carry);

#endif
