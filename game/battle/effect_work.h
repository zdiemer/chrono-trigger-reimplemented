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
 * The party's techs work their effects out with the same kinds, a part of the
 * tech for each member who performs it (battle/party_work.c), and so do the
 * items the party uses; the steal is the party's alone (battle/effect_steal.c).
 * One modifier runs the frame's input work again mid-work (the clock's,
 * battle/effect_modifiers.c), which is why the work takes the platform. */
#ifndef CT_GAME_BATTLE_EFFECT_WORK_H
#define CT_GAME_BATTLE_EFFECT_WORK_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/effect_data.h"
#include "platform/platform.h"
#include "state.h"

/* $C1:D7C4, $C1:D8D1: the effect of the acting enemy's tech or attack
 * (battle_action_detail its number) worked out on the action's targets. */
void effect_work_out_enemy_action(GameState *g, Platform *pf, ActionTable table);

/* A kind's work on the action's targets, as the party's code runs it: the
 * effect's record is the one in battle_effect_record. */
void effect_run_kind(GameState *g, Platform *pf, EffectKind kind);

/* $C1:EB68: the hits' numbers added up, the `sets` after the first into it. */
void effect_add_up_hits(GameState *g, uint8_t sets);

/* $C1:BF79, $C1:BF46: a party member's tech (or attack) worked out
 * (battle/party_work.c), and how it came out; and the item a member uses. */
typedef enum {
    TECH_WORKED_OUT,         /* every part worked out */
    TECH_CANT_BE_DONE,       /* a member couldn't take part, and it didn't go ahead */
    TECH_STOPPED_AT_MISS,    /* a part missed, and the tech stops at a miss */
    TECH_NOTHING_TO_AIM_AT,  /* an attack found no enemy left in the fight */
    TECH_MEMBER_ABSENT,      /* a part's member fights in no slot */
} TechOutcome;
TechOutcome effect_work_out_member_tech(GameState *g, Platform *pf);

/* What a part of a party tech costs its member in MP, cut by the member's
 * accessory (by its battle slot). */
uint8_t effect_part_mp_cost(const GameState *g, const PartyTech *t, unsigned part, uint8_t slot);

/* A cost cut by the accessory that halves it or quarters it, rounding up. */
uint8_t effect_mp_cost_cut(uint8_t cost, uint8_t accessory);

/* $C1:CB93: the tech's members taken from its record (battle_tech_members). */
void effect_take_tech_members(GameState *g, uint8_t tech);

void effect_use_item(GameState *g, Platform *pf);

/* For the entries' guards: whether the port works out an effect of a kind
 * with these flags and modifier for this actor, and whether the action's
 * targets are combatants (battle/effect_work.c). */
bool effect_kind_ported(const GameState *g, uint8_t actor, uint8_t kind, uint8_t flags, uint8_t more_flags,
                        uint8_t modifier);
bool effect_targets_named(const GameState *g);

/* $FD:AAD2, $FD:AB01: the acting enemy's tech or attack noted for its
 * targets: what marks it gives those it acts on, and its elements. */
void effect_note_enemy_action(GameState *g, ActionTable table);

/* Whether the port works a modifier out (battle/effect_modifiers.c): a routine
 * past the original's table is the original's. Whether it is a charm's
 * chance, the one a steal's must be. */
bool effect_modifier_ported(const GameState *g, uint8_t modifier);
bool effect_modifier_is_charm_chance(const GameState *g, uint8_t modifier);

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
