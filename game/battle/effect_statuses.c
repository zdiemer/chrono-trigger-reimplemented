/* Battle: an effect's statuses -- curing them, inflicting them, and the
 * timers of those that last (battle/effect_parts.h).
 *
 * A combatant's stat block holds its condition and four status bytes from
 * $1D, and the bits of each it shrugs off five bytes on, from $22. A status
 * that lasts starts its timer (battle/timers.h) at the length the combatant
 * has for it (battle_timer_lengths). */
#include "battle/effect_parts.h"

#include "battle/combatant.h"
#include "battle/random.h"
#include "battle/timers.h"

/* The stat block's status bytes by number: 0 the condition, 1-4 the four
 * status bytes, 5 the byte after; each's bits shrugged off five bytes on. */
enum { STATUS_BYTES_FROM = 0x1D, SHRUGGED_OFF_FROM = 0x22, STAT_KEPT_STATUS_3 = 0x25, STAT_WEAPON_STATUS = 0x46 };

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
    battle_stop_timer(g, BATTLE_TIMER_STATUS_11, slot);
    battle_stop_timer(g, BATTLE_TIMER_STATUS_1, slot);
    battle_stop_timer(g, BATTLE_TIMER_STATUS_2, slot);
    battle_stop_timer(g, BATTLE_TIMER_STATUS_8, slot);
    if (battle_member_revive_marks(g, slot) < REVIVE_MARKS)
        set_battle_member_revive_marks(g, slot, 0);
}

/* ---- Timers ------------------------------------------------------------------------------ */

/* A status that lasts starts its timer at the target's length for it. */
static void start_status_timer(GameState *g, BattleTimer timer, uint8_t slot, uint8_t length)
{
    set_battle_timer_counts_byte(g, timer, slot, length);
    battle_restart_timer(g, timer, slot);
}

static uint8_t length_of(const GameState *g, BattleTimer timer, uint8_t slot)
{
    return battle_timer_lengths(g, (unsigned)timer * BATTLE_COMBATANTS + slot);
}

// $C1:E006
/* The timer a status's first bit given starts, by the status byte and the
 * bit, the highest first: the first byte's $80 (stop), $40 (its length
 * lengthened by the target's stat $37), $20 and $10; the third's $80 and $40;
 * the fourth's $40 and $20. The second byte's have none (the original tests
 * for them and does nothing). */
static void start_timer_for(GameState *g, uint8_t which, uint8_t bits, bool *machine_v)
{
    uint8_t slot = effect_target(g);
    switch (which) {
    case 1:
        if (bits & 0x80) {
            start_status_timer(g, BATTLE_TIMER_STOP, slot, length_of(g, BATTLE_TIMER_STOP, slot));
        } else if (bits & 0x40) {
            uint8_t length = length_of(g, BATTLE_TIMER_STATUS_1, slot), longer = effect_target_stat(g, 0x37);
            leave_overflow(machine_v, overflow8(length, longer));
            start_status_timer(g, BATTLE_TIMER_STATUS_1, slot, (uint8_t)(length + longer));
        }
        else if (bits & 0x20)
            start_status_timer(g, BATTLE_TIMER_STATUS_2, slot, length_of(g, BATTLE_TIMER_STATUS_2, slot));
        else if (bits & 0x10)
            start_status_timer(g, BATTLE_TIMER_STATUS_8, slot, length_of(g, BATTLE_TIMER_STATUS_8, slot));
        break;
    case 3:
        if (bits & 0x80)
            start_status_timer(g, BATTLE_TIMER_STATUS_3, slot, length_of(g, BATTLE_TIMER_STATUS_3, slot));
        else if (bits & 0x40)
            start_status_timer(g, BATTLE_TIMER_STATUS_4, slot, length_of(g, BATTLE_TIMER_STATUS_4, slot));
        break;
    case 4:
        if (bits & 0x40)
            start_status_timer(g, BATTLE_TIMER_STATUS_5, slot, length_of(g, BATTLE_TIMER_STATUS_5, slot));
        else if (bits & 0x20)
            start_status_timer(g, BATTLE_TIMER_STATUS_9, slot, length_of(g, BATTLE_TIMER_STATUS_9, slot));
        break;
    default:
        break;
    }
}

/* ---- Inflicting --------------------------------------------------------------------------- */

/* The status bits that undo each other: the first byte's $20 (slowed) and the
 * third's $80 (hasted), unless the haste is kept (worn). */
enum { SLOWED = 0x20, HASTED = 0x80 };

/* The original marks a status of the first byte shrugged off in $40. */
enum { SHRUGGED_OFF_MARK = 0x80, NO_STATUS_BYTE = 0xFF };

static void give_bits(GameState *g, uint16_t which, uint8_t bits)
{
    uint16_t at = (uint16_t)(STATUS_BYTES_FROM + which);
    set_effect_target_stat(g, at, effect_target_stat(g, at) | bits);
}

// $C1:DBAA
uint8_t effect_inflict_status(GameState *g, uint16_t which, uint8_t bits, bool *machine_v)
{
    uint8_t kind = (uint8_t)which;
    if (kind == NO_STATUS_BYTE)
        return INFLICTED_NONE;
    /* the byte is found by adding its number to the stat block's offset */
    if (kind <= 5)
        leave_overflow(machine_v, overflow16(which, (uint16_t)(effect_target(g) * BATTLE_STAT_BLOCK_BYTES)));
    if (kind <= 2) {
        if (effect_target_stat(g, (uint16_t)(SHRUGGED_OFF_FROM + which)) & bits) {
            if (kind == 1) {
                set_battle_shrugged_off_mark(g, SHRUGGED_OFF_MARK);
                return INFLICTED_SHRUGGED_OFF;
            }
        } else {
            give_bits(g, which, bits);
            start_timer_for(g, kind, bits, machine_v);
            if ((effect_target_stat(g, STAT_STATUS_1) & SLOWED) && !(effect_target_stat(g, STAT_KEPT_STATUS_3) & HASTED))
                set_effect_target_stat(g, STAT_STATUS_3, effect_target_stat(g, STAT_STATUS_3) & (uint8_t)~HASTED);
            else
                set_effect_target_stat(g, STAT_STATUS_1, effect_target_stat(g, STAT_STATUS_1) & (uint8_t)~SLOWED);
        }
    } else if (kind == 3 || kind == 4) {
        if (kind == 3 && (bits & HASTED))
            set_effect_target_stat(g, STAT_STATUS_1, effect_target_stat(g, STAT_STATUS_1) & (uint8_t)~SLOWED);
        give_bits(g, which, bits);
        start_timer_for(g, kind, bits, machine_v);
    } else if (kind == 5) {
        give_bits(g, which, bits);
    }
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, 0));
    return INFLICTED;
}

/* The carry inflicting leaves its caller: clear for the condition's byte and
 * the fifth, set for the others (and for none). */
bool effect_inflicting_carry(uint16_t which)
{
    uint8_t kind = (uint8_t)which;
    return !(kind == 0 || kind == 5);
}

// $C1:E976
/* A hit's chance (a percent) of a status of the first status byte; where it
 * lands, the status the attacker's weapon adds is noted for the target. */
enum { NO_WEAPON_STATUS = 0xFF };

uint8_t effect_status_chance(GameState *g, uint8_t chance, uint8_t bits, bool *machine_v)
{
    uint8_t drawn = battle_random(g, 0, 100);
    /* the draw's own subtraction and add leave no overflow (a demo battle's
     * draws do neither) */
    if (!battle_is_demo(g))
        leave_overflow(machine_v, false);
    battle_keep_byte(g, BATTLE_TECH_HITS_LEFT, drawn);
    if (chance < drawn)
        return chance;
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, 1));
    uint8_t left = effect_inflict_status(g, 1, bits, machine_v);
    uint8_t weapon_status = effect_actor_stat(g, STAT_WEAPON_STATUS);
    if (weapon_status == NO_WEAPON_STATUS)
        return left;
    battle_keep_byte(g, BATTLE_STAT_OFFSET_LOW, weapon_status);
    uint8_t slot = effect_target(g);
    if (slot < BATTLE_EFFECT_GIVEN_STATUS_COUNT)
        set_battle_effect_given_status(g, slot, weapon_status);
    else
        set_battle_effect_given_status_unchecked(g, slot, weapon_status);
    return left;
}
