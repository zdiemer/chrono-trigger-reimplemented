/* Battle: an effect's statuses -- curing them, inflicting them, and the
 * timers of those that last (battle/effect_parts.h).
 *
 * A combatant's stat block holds its condition and four status bytes from
 * $1D, and the bits of the first three it shrugs off from $22. A status that
 * lasts starts its timer (battle/timers.h) at the length the combatant has for
 * it (battle_timer_lengths). */
#include "battle/effect_parts.h"

#include "battle/combatant.h"
#include "battle/random.h"
#include "battle/timers.h"

/* ---- Curing ------------------------------------------------------------------------------ */

/* What a cure clears: the condition's low bits (and with a revival, the
 * knock-out), the first status byte, the second's but $C2. A party member's
 * status timers for them stop, and its revive mark goes. */
enum { CONDITION_KEPT = 0xC0, STATUS_2_KEPT = 0xC2, REVIVE_MARKS = 3 };

// $C1:DB5E
void effect_cure(GameState *g, bool revives)
{
    uint8_t condition = effect_target_stat(g, STAT_CONDITION);
    if (revives)
        condition &= (uint8_t)~CONDITION_KNOCKED_OUT;
    set_effect_target_stat(g, STAT_CONDITION, condition);
    set_effect_target_stat(g, STAT_CONDITION, condition & CONDITION_KEPT);
    set_effect_target_stat(g, STAT_STATUS_1, 0);
    set_effect_target_stat(g, STAT_STATUS_2, effect_target_stat(g, STAT_STATUS_2) & STATUS_2_KEPT);
    uint8_t slot = effect_target(g);
    if (slot >= BATTLE_PARTY_SIZE)
        return;
    battle_stop_timer(g, BATTLE_TIMER_STOP, slot);
    battle_stop_timer(g, BATTLE_TIMER_11, slot);
    battle_stop_timer(g, BATTLE_TIMER_POISON, slot);
    battle_stop_timer(g, BATTLE_TIMER_SLOW, slot);
    battle_stop_timer(g, BATTLE_TIMER_HP_LOSS, slot);
    if (battle_member_revive_marks(g, slot) < REVIVE_MARKS)
        set_battle_member_revive_marks(g, slot, 0);
}

/* ---- Timers ------------------------------------------------------------------------------ */

// $C1:E006
/* The timer a status's first bit given starts, by the status byte and the
 * bit, the highest first: the first byte's stop, poison (its length made
 * longer by the target's stamina), slow and the HP lost; the third's haste
 * and evading much better; the fourth's shield and the MP regained. The
 * second byte's have none (battle/status_timers.c says what each does when
 * it runs out). */
static void start_timer_for(GameState *g, uint8_t which, uint8_t bits)
{
    uint8_t slot = effect_target(g);
    switch (which) {
    case 1:
        if (bits & STATUS_STOP) {
            battle_start_timer(g, BATTLE_TIMER_STOP, slot, battle_timer_length(g, BATTLE_TIMER_STOP, slot));
        } else if (bits & STATUS_POISON) {
            uint8_t length = battle_timer_length(g, BATTLE_TIMER_POISON, slot), longer = effect_target_stat(g, STAT_STAMINA);
            battle_start_timer(g, BATTLE_TIMER_POISON, slot, (uint8_t)(length + longer));
        } else if (bits & STATUS_SLOW) {
            battle_start_timer(g, BATTLE_TIMER_SLOW, slot, battle_timer_length(g, BATTLE_TIMER_SLOW, slot));
        } else if (bits & STATUS_LOSES_HP) {
            battle_start_timer(g, BATTLE_TIMER_HP_LOSS, slot, battle_timer_length(g, BATTLE_TIMER_HP_LOSS, slot));
        }
        break;
    case 3:
        if (bits & STATUS3_HASTE)
            battle_start_timer(g, BATTLE_TIMER_HASTE, slot, battle_timer_length(g, BATTLE_TIMER_HASTE, slot));
        else if (bits & STATUS3_EVADES_MUCH_BETTER)
            battle_start_timer(g, BATTLE_TIMER_EVASION, slot, battle_timer_length(g, BATTLE_TIMER_EVASION, slot));
        break;
    case 4:
        if (bits & STATUS4_SHIELD)
            battle_start_timer(g, BATTLE_TIMER_SHIELD, slot, battle_timer_length(g, BATTLE_TIMER_SHIELD, slot));
        else if (bits & STATUS4_REGAINS_MP)
            battle_start_timer(g, BATTLE_TIMER_MP_REGAIN, slot, battle_timer_length(g, BATTLE_TIMER_MP_REGAIN, slot));
        break;
    default:
        break;
    }
}

/* ---- Inflicting --------------------------------------------------------------------------- */

/* The status bytes by number: 0 the condition, 1-4 the four status bytes, 5
 * the byte after; the first three's bits it shrugs off are its own. A status
 * of the first byte shrugged off is marked for the screen script. */
enum { SHRUGS_OFF_BYTES = 2, OTHER_STATUS_BYTES = 4, LAST_STATUS_BYTE = 5, NO_STATUS_BYTE = 0xFF, SHRUGGED_OFF_MARK = 0x80 };

static uint16_t status_byte(uint16_t which) { return (uint16_t)(STAT_CONDITION + which); }

static void give_bits(GameState *g, uint16_t which, uint8_t bits)
{
    set_effect_target_stat(g, status_byte(which), effect_target_stat(g, status_byte(which)) | bits);
}

/* Slow and haste undo each other: a combatant slowed loses the haste it
 * doesn't keep; otherwise the slow goes. */
static void settle_slow_and_haste(GameState *g)
{
    if ((effect_target_stat(g, STAT_STATUS_1) & STATUS_SLOW) && !(effect_target_stat(g, STAT_KEPT_STATUS_3) & STATUS3_HASTE))
        set_effect_target_stat(g, STAT_STATUS_3, effect_target_stat(g, STAT_STATUS_3) & (uint8_t)~STATUS3_HASTE);
    else
        set_effect_target_stat(g, STAT_STATUS_1, effect_target_stat(g, STAT_STATUS_1) & (uint8_t)~STATUS_SLOW);
}

// $C1:DBAA
Inflicted effect_inflict_status(GameState *g, uint16_t which, uint8_t bits)
{
    uint8_t kind = (uint8_t)which;
    if (kind == NO_STATUS_BYTE)
        return NOTHING_INFLICTED;
    Inflicted outcome = INFLICTED;
    if (kind <= SHRUGS_OFF_BYTES) {
        if (effect_target_stat(g, (uint16_t)(STAT_SHRUGS_OFF + which)) & bits) {
            if (kind == 1) {
                set_battle_shrugged_off_mark(g, SHRUGGED_OFF_MARK);
                return SHRUGGED_OFF;
            }
            outcome = SHRUGGED_OFF;
        } else {
            give_bits(g, which, bits);
            start_timer_for(g, kind, bits);
            settle_slow_and_haste(g);
        }
    } else if (kind <= OTHER_STATUS_BYTES) {
        if (kind == 3 && (bits & STATUS3_HASTE))
            set_effect_target_stat(g, STAT_STATUS_1, effect_target_stat(g, STAT_STATUS_1) & (uint8_t)~STATUS_SLOW);
        give_bits(g, which, bits);
        start_timer_for(g, kind, bits);
    } else if (kind == LAST_STATUS_BYTE) {
        give_bits(g, which, bits);
    }
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, 0));
    return outcome;
}

// $C1:E976
/* A hit's chance (a percent) of a status of the first status byte; where it
 * lands, the attacker's weapon's modifier is noted for the target. */
enum { PERCENT = 100, NO_WEAPON_MODIFIER = 0xFF };

void effect_status_chance(GameState *g, uint8_t chance, uint8_t bits)
{
    uint8_t drawn = battle_random(g, 0, PERCENT);
    effect_leave_last_worked_byte(g, drawn);
    if (chance < drawn)
        return;
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, 1));
    effect_inflict_status(g, 1, bits);
    uint8_t weapon_modifier = effect_actor_stat(g, STAT_WEAPON_MODIFIER);
    if (weapon_modifier == NO_WEAPON_MODIFIER)
        return;
    battle_keep_byte(g, BATTLE_STAT_OFFSET_LOW, weapon_modifier);
    uint8_t slot = effect_target(g);
    if (slot < BATTLE_EFFECT_GIVEN_STATUS_COUNT)
        set_battle_effect_given_status(g, slot, weapon_modifier);
    else
        set_battle_effect_given_status_unchecked(g, slot, weapon_modifier);
}
