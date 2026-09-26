/* Battle: working out an action's effect on its targets.
 *
 * When an enemy's attack or tech lands, its effect is worked out on each of
 * its targets in turn (battle/effect_work.c): the effect's record says its
 * kind -- restore HP or MP, cure, inflict a status, hit, drain, hit several
 * times -- and the numbers the kind takes. A hit first tests whether it lands
 * (the target's defences against the attacker's aim), then works its damage
 * out by one of the formulas from the attacker's stats, takes the target's
 * defence off, and applies the action's modifier -- a critical hit, an
 * element, a chance of a status, of death -- before it is recorded
 * (battle/effect_amounts.c, battle/effect_modifiers.c). Statuses are the
 * target's stat block's bits, with their timers (battle/effect_statuses.c).
 *
 * What it works out is recorded, not yet done: the damage table (battle_damage)
 * for each hit and target, which the action's showing applies, and the numbers
 * shown over the combatants (battle_number_records). Only statuses and a few
 * of the modifiers' outcomes are the target's at once.
 *
 * The party's techs work their effects out with the same kinds, from code that
 * is the original's still; each kind is an entry of its own for it. What the
 * port leaves to the original, its entries turning it away: stealing (the
 * party's alone, which gives the item to the party's inventory or its battle
 * records), and the modifier that reads the controller mid-work ($C1:EE27,
 * whose read waits on the console). */
#ifndef CT_GAME_BATTLE_EFFECT_WORK_H
#define CT_GAME_BATTLE_EFFECT_WORK_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/effect_data.h"
#include "state.h"

/* $C1:D7C4, $C1:D8D1: the effect of the acting enemy's tech or attack
 * (battle_action_detail its number) worked out on the action's targets. */
void effect_work_out_enemy_action(GameState *g, ActionTable table);

/* A kind's work on the action's targets, as the party's code runs it: the
 * effect's record is the one in battle_effect_record. */
void effect_run_kind(GameState *g, EffectKind kind);

/* $FD:AAD2, $FD:AB01: the acting enemy's tech or attack noted for its
 * targets: what marks it gives those it acts on, and its elements. */
void effect_note_enemy_action(GameState *g, ActionTable table);

/* Whether the port works a modifier out (battle/effect_modifiers.c): a routine
 * past the original's table, or the clock's, is the original's. */
bool effect_modifier_ported(const GameState *g, uint8_t modifier);

/* What a status's timer does to a combatant (battle/status_timers.c), put
 * where the action's showing shows it: poison's damage and the MP regained
 * shown as numbers over it, and a point of HP lost recorded without one. */
void effect_show_status_damage(GameState *g, uint8_t slot, uint16_t damage);
void effect_show_magic_regained(GameState *g, uint8_t slot, uint16_t magic);
void effect_record_status_damage(GameState *g, uint8_t slot, uint16_t damage);

/* ---- The pieces the kinds share -------------------------------------------------------------- */

/* The acting combatant's and the target's stat blocks, by offset, as the
 * effect code reaches them (battle_effect_actor_block, battle_effect_target_block). */
uint8_t effect_actor_stat(const GameState *g, uint16_t offset);
uint8_t effect_target_stat(const GameState *g, uint16_t offset);
uint16_t effect_actor_stat_word(const GameState *g, uint16_t offset);
uint16_t effect_target_stat_word(const GameState *g, uint16_t offset);
void set_effect_target_stat(GameState *g, uint16_t offset, uint8_t v);

/* The combatant the effect is being worked out on (battle_effect_target). */
uint8_t effect_target(const GameState *g);

#endif
