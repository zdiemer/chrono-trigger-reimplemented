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

/* ---- The long arithmetic, as the effect code does it ------------------------------------------ */

/* A word times a word ($C1:C90B), the carry the original's instructions before
 * the call leave (it stays in the second number's cell), the low word of the
 * product returned. The product's high word is the long arithmetic's
 * leftover (battle_long_high_word). */
static inline uint16_t effect_times(GameState *g, uint16_t a, uint16_t b, bool carry)
{
    return (uint16_t)battle_multiply_words_carrying(g, a, b, carry);
}

/* The long arithmetic's leftover high word and `low` over `divisor`
 * ($C1:C92A), the carry the instructions before leave (it comes in at the
 * top of the high word left where it divides): so a product just made is
 * divided whole. */
static inline BattleLongQuotient effect_over(GameState *g, uint16_t low, uint16_t divisor, bool carry)
{
    return battle_divide_carrying(g, low, divisor, carry);
}

/* ---- The overflow the original's adds leave ------------------------------------------------ */

/* The original's code leaves the overflow flag of its last add, which the
 * showing of the action after the work takes along (EffectWork.machine_v):
 * the signed overflow of an 8-bit or a 16-bit add. The pieces that end a
 * kind's work say what they leave. */
static inline bool overflow8(uint8_t a, uint8_t b)
{
    uint8_t sum = (uint8_t)(a + b);
    return ((a ^ sum) & (b ^ sum) & 0x80) != 0;
}
static inline bool overflow16(uint16_t a, uint16_t b)
{
    uint16_t sum = (uint16_t)(a + b);
    return ((a ^ sum) & (b ^ sum) & 0x8000) != 0;
}
static inline void leave_overflow(bool *machine_v, bool v)
{
    if (machine_v)
        *machine_v = v;
}

/* ---- Recording what an effect does (battle/effect_amounts.c) ------------------------------ */

/* How a damage table entry is marked (battle_damage.flags). */
enum { DAMAGE_HEALS = 0x80, DAMAGE_TO_MP = 0x40, DAMAGE_MISSED = 0x20, DAMAGE_PLAIN = 0x00 };

/* How a number over a combatant shows (battle_number_records.kind). */
enum { NUMBER_HP_RESTORED = 1, NUMBER_MP_RESTORED = 2, NUMBER_DAMAGE = 3, NUMBER_MP_DAMAGE = 4, NUMBER_MISS = 5 };

/* $C1:EBF8: the effect's amount (battle_effect_amount; negated where it
 * heals) recorded in the damage table for this hit and `slot`, marked so;
 * `carry` is the one the original's call enters with. */
bool effect_record_damage(GameState *g, uint8_t slot, uint8_t marks, bool carry);

/* $C1:E89F: the place of the target's number record, in the set the effect
 * is at (battle_effect_tally), and the carry the original leaves after it. */
uint8_t effect_target_number(GameState *g);
uint8_t effect_target_number_carrying(GameState *g, bool *carry, bool *machine_v);
void set_effect_number(GameState *g, uint8_t record, uint16_t value, uint8_t kind);

/* $C1:E8E2: the effect missed the target: its number shows a miss and its
 * damage table entry is marked so. `dodged` says it was the target's dodge
 * (not the attacker's miss), which picks which of the effect's miss bits
 * mark the action (`misses`). */
bool effect_missed(GameState *g, uint8_t misses, bool dodged);

/* The target the work is at, as the original reads it from the action's
 * list: the one at the count less one, in a byte. */
uint8_t effect_listed_target(const GameState *g);

/* An enemy beaten outright by a hit or a modifier. */
void effect_mark_beaten(GameState *g, uint8_t slot);

/* About a tenth of a number, as the original works it out with shifts
 * ($C1:C1B2), and what that leaves in its Y register. */
uint16_t effect_about_a_tenth(GameState *g, uint16_t v, uint16_t *machine_y);

/* ---- Pieces of the kinds (battle/effect_amounts.c) ---------------------------------------- */

/* $C1:DA37: HP and MP restored to the target by `power` times the effect's
 * multiple (or all of it, for $0F), and a random part. */
typedef struct {
    uint8_t power, multiple, restores;
} Healing;
enum { RESTORES_HP = 0x80, RESTORES_MP = 0x40, RESTORES_ALL = 0x0F };
void effect_heal(EffectWork *w, Healing h);

/* $C1:E77B: some of the target's HP (or MP) drained to the actor: a share of
 * its maximum, `share` twentieths, and a random part. */
typedef enum { DRAIN_HP = 3, DRAIN_MP = 7 } Drain;
void effect_drain(EffectWork *w, Drain what, uint8_t share);

/* The hit's tests of landing: whether the target's kind turns the effect
 * away ($C1:DE87), and whether the attacker's aim beats the target's evasion
 * ($C1:DE9E, $C1:DEF0, $C1:DC64). */
bool effect_turned_away(EffectWork *w, uint8_t kinds);
bool effect_lands(EffectWork *w, uint8_t aim_stat, uint8_t evasion_stat);

/* $C1:E0F5, $C1:E65F, $C1:DCAD: a hit's damage by one of the formulas, the
 * target's defence off it, and applied (the modifiers with it). */
typedef struct {
    uint16_t damage;
    uint8_t defence;
} HitAmounts;
HitAmounts effect_hit_amounts(EffectWork *w, uint8_t formula, uint8_t defence_stat);
void effect_apply_hit(EffectWork *w, HitAmounts hit);

/* $C1:E63B: a hit wakes the target from the statuses a hit ends, and starts
 * its turn wait again. */
void effect_wake_target(GameState *g);

/* $C1:BD6F: a combatant's turn wait as it starts again, by its statuses. */
void battle_adjust_turn_wait(GameState *g, uint8_t slot);

/* ---- Statuses (battle/effect_statuses.c) -------------------------------------------------- */

/* $C1:DB5E: the target cured: revived where `revives`, its statuses gone. */
void effect_cure(GameState *g, bool revives);

/* $C1:DBAA: the status bits `bits` of the target's status byte `which` (0-5,
 * $FF none) inflicted, where it doesn't shrug them off. What it gives back is
 * what its callers read after it: $FF for none, 1 where the first status
 * byte's bits were shrugged off (the original marks that in $40), else 0. */
enum { INFLICTED_NONE = 0xFF, INFLICTED_SHRUGGED_OFF = 1, INFLICTED = 0 };
uint8_t effect_inflict_status(GameState *g, uint16_t which, uint8_t bits, bool *machine_v);

/* The carry inflicting leaves its caller. */
bool effect_inflicting_carry(uint16_t which);

/* $C1:E976: a hit's chance (a percent) of a status of the first status byte:
 * what it leaves for a steal after it (the chance where it didn't come,
 * otherwise what inflicting gave back). */
uint8_t effect_status_chance(GameState *g, uint8_t chance, uint8_t bits, bool *machine_v);

/* ---- Modifiers and stealing (battle/effect_modifiers.c) --------------------------------- */

/* $C1:EA6F: the action's modifier applied to the hit's damage, then the
 * attacker's weapon's where the effect says so. */
bool effect_apply_modifiers(EffectWork *w, bool carry);

/* $C1:E9D5: a try at stealing from the target, a `chance` in percent: what
 * came of it (0 an item taken, $FE missed, $FF nothing left to take). */
enum { STEAL_TOOK = 0x00, STEAL_MISSED = 0xFE, STEAL_NOTHING = 0xFF };
uint8_t effect_steal(EffectWork *w, uint8_t chance);

#endif
