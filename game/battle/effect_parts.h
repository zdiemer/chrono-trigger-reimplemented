/* Battle: what the pieces of an effect's working out share
 * (battle/effect_work.c, effect_amounts.c, effect_statuses.c,
 * effect_modifiers.c). Not for the rest of the battle: battle/effect_work.h is
 * that. */
#ifndef CT_GAME_BATTLE_EFFECT_PARTS_H
#define CT_GAME_BATTLE_EFFECT_PARTS_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/effect_work.h"
#include "original_effects.h"
#include "platform/platform.h"
#include "state.h"
#include "vars.h"

/* ---- The cells the original's effect code works in ----------------------------------------- */

/* The effect code's routines hand each other their numbers in the direct
 * page's cells $16-$21, two bytes each (the first, the second, ... the
 * sixth), and keep working numbers in a few more. The port hands them over as
 * C values; what the original leaves in the cells is scaffolding. */
enum { EFFECT_ARG_1 = 0x16, EFFECT_ARG_2 = 0x18, EFFECT_ARG_3 = 0x1A, EFFECT_ARG_4 = 0x1C, EFFECT_ARG_5 = 0x1E, EFFECT_ARG_6 = 0x20 };

/* $30-$31: a working word the formulas keep beside the long arithmetic's
 * cells (battle_wide_math's ninth and tenth bytes). */
enum { WORKING_WORD = 8 };

static inline void leave_working_word(GameState *g, uint16_t v)
{
    set_battle_wide_math(g, WORKING_WORD, (uint8_t)v);
    set_battle_wide_math(g, WORKING_WORD + 1, (uint8_t)(v >> 8));
}

/* $0E: the effect code leaves there what it last worked with -- a draw, a
 * number's place, a target's statuses -- which an enemy's tech notes as its
 * hits where its targeting doesn't set them (battle.h): game state, kept
 * plainly, never read back by the work. */
static inline void effect_leave_last_worked(GameState *g, uint16_t v) { battle_keep_word(g, BATTLE_TECH_HITS_LEFT, v); }
static inline void effect_leave_last_worked_byte(GameState *g, uint8_t v) { battle_keep_byte(g, BATTLE_TECH_HITS_LEFT, v); }

/* ---- The long arithmetic, as the effect code does it ------------------------------------------ */

/* A word times a word ($C1:C90B), the low word of the product; its high word
 * is the long arithmetic's leftover (battle_long_high_word). */
static inline uint16_t effect_times(GameState *g, uint16_t a, uint16_t b) { return (uint16_t)battle_multiply_words(g, a, b); }

/* The leftover high word and `low` over `divisor` ($C1:C92A), so that a
 * product just made is divided whole. */
static inline BattleLongQuotient effect_over(GameState *g, uint16_t low, uint16_t divisor)
{
    return battle_divide_leftover(g, low, divisor);
}

/* ---- Recording what an effect does (battle/effect_amounts.c) ------------------------------ */

/* How a damage table entry is marked (battle_damage.flags). */
enum { DAMAGE_HEALS = 0x80, DAMAGE_TO_MP = 0x40, DAMAGE_MISSED = 0x20, DAMAGE_PLAIN = 0x00 };

/* How a number over a combatant shows (battle_number_records.kind). */
enum { NUMBER_HP_RESTORED = 1, NUMBER_MP_RESTORED = 2, NUMBER_DAMAGE = 3, NUMBER_MP_DAMAGE = 4, NUMBER_MISS = 5 };

/* $C1:EBF8: the effect's amount (battle_effect_amount; negated where it
 * heals) recorded in the damage table for this hit and `slot`, marked so. */
void effect_record_damage(GameState *g, uint8_t slot, uint8_t marks);

/* $C1:E89F: the place of the target's number record, in the set the effect is
 * at (battle_effect_tally); and a record's value and kind set by its place. */
uint8_t effect_target_number(GameState *g);
void set_effect_number(GameState *g, uint8_t place, uint16_t value, uint8_t kind);

/* $C1:E8E2: the effect missed the target: its number shows a miss and its
 * damage table entry is marked so. `dodged` says it was the target's dodge
 * (not the attacker's miss), which picks which of the effect's miss bits
 * mark the action (`misses`). */
void effect_missed(GameState *g, uint8_t misses, bool dodged);

/* The target the work is at, as the original reads it from the action's
 * list: the one at the count less one, in a byte. */
uint8_t effect_listed_target(const GameState *g);

/* An enemy felled by a hit. */
void effect_mark_beaten(GameState *g, uint8_t slot);

/* About a tenth of a number, as the original works it out with shifts
 * ($C1:C1B2). */
uint16_t effect_about_a_tenth(GameState *g, uint16_t v);

/* ---- Pieces of the kinds (battle/effect_amounts.c) ---------------------------------------- */

/* $C1:DA37: HP and MP restored to the target by `power` times the effect's
 * multiple (or all of it, for $0F), and a random part. */
typedef struct {
    uint8_t power, multiple, restores;
} Healing;
enum { RESTORES_HP = 0x80, RESTORES_MP = 0x40, RESTORES_ALL = 0x0F };
void effect_heal(GameState *g, Healing h);

/* $C1:E77B: some of the target's HP (or MP) drained to the actor: a share of
 * its maximum, `share` twentieths, and a random part. */
typedef enum { DRAIN_HP = 3, DRAIN_MP = 7 } Drain;
void effect_drain(GameState *g, Drain what, uint8_t share);

/* The hit's tests of landing: whether the target's kind turns the effect
 * away ($C1:DE87), and whether the attacker's aim beats the target's evasion
 * ($C1:DE9E, $C1:DEF0, $C1:DC64). */
bool effect_turned_away(GameState *g, uint8_t kinds);
bool effect_lands(GameState *g, uint8_t aim_stat, uint8_t evasion_stat);

/* $C1:E0F5, $C1:E65F, $C1:DCAD: a hit's damage by its formula, the target's
 * defence off it, and applied (the modifiers with it, which may run the
 * frame's input work on `pf`). */
typedef struct {
    uint16_t damage;
    uint8_t defence;
} HitAmounts;
HitAmounts effect_hit_amounts(GameState *g, const EffectRecord *e);
void effect_apply_hit(GameState *g, Platform *pf, HitAmounts hit);

/* $C1:E63B: a hit wakes the target and brings it to its senses, and its turn
 * wait starts again. */
void effect_wake_target(GameState *g);

/* $C1:BD6F: a combatant's turn wait starts again from its full wait
 * (battle_turn_wait): halved while it is hasted, doubled (to 255 at most)
 * while slowed, at least 1, and a party member's gauge measures it. Returns
 * the wait it starts from. */
uint8_t battle_restart_turn_wait(GameState *g, uint8_t slot);

/* ---- Statuses (battle/effect_statuses.c) -------------------------------------------------- */

/* $C1:DB5E: the target cured: revived where `revives`, its statuses gone. */
void effect_cure(GameState *g, bool revives);

/* $C1:DBAA: the status bits `bits` of the target's status byte `which` (0-5,
 * $FF none) inflicted, where it doesn't shrug them off. */
typedef enum { INFLICTED, SHRUGGED_OFF, NOTHING_INFLICTED } Inflicted;
Inflicted effect_inflict_status(GameState *g, uint16_t which, uint8_t bits);

/* $C1:E976: a hit's chance (a percent) of statuses of the first status byte. */
void effect_status_chance(GameState *g, uint8_t chance, uint8_t bits);

/* ---- Stealing (battle/effect_steal.c) ---------------------------------------------------- */

/* $C1:E9D5: the actor steals from the target, with a percent's chance: it
 * takes an item, misses, or finds nothing left to take. How the last steal of
 * an action came out is kept for the member's turn to tell (battle_steal_outcome):
 * nothing tried yet, an item taken (with bit 1 where the member keeps it), a
 * miss, or nothing to take. */
typedef enum { STEAL_TOOK, STEAL_MISSED, STEAL_NOTHING, STEAL_NOT_TRIED } StealOutcome;
enum {
    STEAL_OUTCOME_NOT_TRIED = 0x00, STEAL_OUTCOME_TOOK = 0x01, STEAL_OUTCOME_KEPT_BY_MEMBER = 0x02,
    STEAL_OUTCOME_MISSED = 0xFE, STEAL_OUTCOME_NOTHING = 0xFF,
};
StealOutcome effect_steal(GameState *g, uint8_t chance);

static inline StealOutcome effect_last_steal(const GameState *g)
{
    uint8_t kept = battle_steal_outcome(g);
    return kept == STEAL_OUTCOME_NOT_TRIED ? STEAL_NOT_TRIED
           : kept == STEAL_OUTCOME_MISSED  ? STEAL_MISSED
           : kept == STEAL_OUTCOME_NOTHING ? STEAL_NOTHING
                                           : STEAL_TOOK;
}

/* ---- Modifiers (battle/effect_modifiers.c) ------------------------------------------------ */

/* $C1:EA6F: the action's modifier applied to the hit's damage, then the
 * attacker's weapon's where the effect says so. */
void effect_apply_modifiers(GameState *g, Platform *pf);

/* $C1:EB1C with the steal's modifier, a charm's chance ($C1:EFC3): whether
 * the steal takes the enemy's charm, where it has one. */
bool effect_charm_comes(GameState *g);

#endif
