/* Battle: the amounts an effect works out -- healing, draining, a hit's
 * landing and its damage -- and how they are recorded (battle/effect_parts.h).
 *
 * The original works each out with its long arithmetic ($C1:C90B, $C1:C92A),
 * whose cells keep what each call leaves, and some of its steps divide what
 * the last one left there rather than what they were meant to: those steps
 * are kept (see each). The carry each call enters with is the one the
 * original's instructions before it leave; it shows in the high word a
 * division leaves, which the random draws divide by (battle/random.c), so the
 * port follows it where it can differ. */
#include "battle/effect_parts.h"

#include "battle/combatant.h"
#include "battle/random.h"
#include "battle/timers.h"
#include "charge.h"

/* ---- The combatants' stats, as the effect code reaches them ------------------------------- */

uint8_t effect_target(const GameState *g) { return battle_effect_target(g); }

static uint8_t actor(const GameState *g) { return battle_actor(g); }

uint8_t effect_actor_stat(const GameState *g, uint16_t offset) { return combatant_stat_byte(g, actor(g), offset); }
uint8_t effect_target_stat(const GameState *g, uint16_t offset) { return combatant_stat_byte(g, effect_target(g), offset); }
uint16_t effect_actor_stat_word(const GameState *g, uint16_t offset)
{
    return (uint16_t)(effect_actor_stat(g, offset) | effect_actor_stat(g, (uint16_t)(offset + 1)) << 8);
}
uint16_t effect_target_stat_word(const GameState *g, uint16_t offset)
{
    return (uint16_t)(effect_target_stat(g, offset) | effect_target_stat(g, (uint16_t)(offset + 1)) << 8);
}
void set_effect_target_stat(GameState *g, uint16_t offset, uint8_t v) { set_combatant_stat_byte(g, effect_target(g), offset, v); }

/* The stat bytes the amounts read. */
enum {
    STAT_MAX_MP = 0x09, STAT_MP = 0x07, STAT_LEVEL = 0x12, STAT_POWER = 0x36, STAT_HIT = 0x37, STAT_MAGIC = 0x39,
    STAT_STAMINA = 0x3A, STAT_SPEED = 0x3D, STAT_WEAPON_ATTACK = 0x43, STAT_ELEMENT_BITS = 0x01,
    STAT_FIRST_ELEMENT_POWER = 0x3F, STAT_KIND = 0x19, STAT_STATUS_3_WORN = 0x25,
};

/* The status bits the amounts look at, by what they do here. The fourth
 * status byte (with the kept ones): damage up by a half, by a half after
 * halving, by a quarter; a barrier and a shield. The first: weakened twice
 * over, a quarter of the damage, blind (the aim), its aim slowed, asleep. The
 * third (with the worn ones): hasted, and two that help evading. */
enum {
    DAMAGE_UP_A_HALF = 0x80, DAMAGE_UP_A_HALF_ROUNDED = 0x08, DAMAGE_UP_A_QUARTER = 0x02, BARRIER = 0x04, SHIELD = 0x40,
    WEAKENED = 0x40, WEAKENED_TOO = 0x20, QUARTERED = 0x04, BLIND = 0x05, SLOWED_AIM = 0x40, ASLEEP = 0x06, SLOWED = 0x20,
    STOPPED_OR_ASLEEP = 0x82, BLIND_OR_WEAK = 0x05, DAZED = 0x20, CONFUSED_HIT = 0x40,
    HASTED = 0x80, EVADES_BETTER = 0x40, EVADES_A_LITTLE = 0x01,
};

/* ---- Where the numbers go ----------------------------------------------------------------- */

/* The current target, as the effect code reads it again from the targets'
 * list: the one at the count less one, a byte. */
uint8_t effect_listed_target(const GameState *g)
{
    uint8_t place = (uint8_t)(battle_action_target_count(g) - 1);
    return place < BATTLE_ACTION_TARGETS_COUNT ? battle_action_targets(g, place) : battle_action_targets_unchecked(g, place);
}

// $C1:EBF8
bool effect_record_damage(GameState *g, uint8_t slot, uint8_t marks, bool carry)
{
    set_battle_damage_flags_given(g, marks);
    set_battle_damage_flags_kept(g, marks);
    if (marks & DAMAGE_HEALS)
        set_battle_effect_amount(g, (uint16_t)-battle_effect_amount(g));
    uint8_t hit_place = (uint8_t)effect_times(g, battle_damage_hit(g), 0x2C, carry);
    unsigned entry = (uint8_t)(slot * 4 + hit_place) / 4;
    if (entry < BATTLE_DAMAGE_COUNT) {
        set_battle_damage_amount(g, entry, battle_effect_amount(g));
        set_battle_damage_flags(g, entry, marks);
    } else {
        set_battle_damage_amount_unchecked(g, entry, battle_effect_amount(g));
        set_battle_damage_flags_unchecked(g, entry, marks);
    }
    return overflow8((uint8_t)(slot << 2), hit_place);
}

/* A number record's place, a byte's offset among the records: the
 * combatant's, in the set the effect is at (battle_effect_tally). The
 * original keeps it in a cell (the target's at $0E, the actor's at $10); its
 * multiply enters with the carry of doubling the slot a second time. */
typedef struct {
    uint8_t place;
    bool carry, overflow;
} NumberPlace;

static NumberPlace number_place(GameState *g, uint8_t slot, bool tally_first)
{
    uint8_t doubled = (uint8_t)(slot << 1);
    bool carry = (doubled & 0x80) != 0;
    uint16_t set = tally_first ? effect_times(g, battle_effect_tally(g), 0x2C, carry)
                               : effect_times(g, 0x2C, battle_effect_tally(g), carry);
    unsigned place = (uint8_t)set + (uint8_t)(doubled << 1);
    return (NumberPlace){ (uint8_t)place, place > 0xFF, overflow8((uint8_t)set, (uint8_t)(doubled << 1)) };
}

static NumberPlace target_number_place(GameState *g)
{
    NumberPlace p = number_place(g, effect_target(g), false);
    battle_keep_word(g, BATTLE_TECH_HITS_LEFT, p.place);
    return p;
}

// $C1:E89F
uint8_t effect_target_number(GameState *g) { return target_number_place(g).place; }

uint8_t effect_target_number_carrying(GameState *g, bool *carry, bool *machine_v)
{
    NumberPlace p = target_number_place(g);
    *carry = p.carry;
    leave_overflow(machine_v, p.overflow);
    return p.place;
}

// $C1:E8C0
static NumberPlace actor_number_place(GameState *g)
{
    NumberPlace p = number_place(g, actor(g), true);
    ORIGINAL_EFFECT(battle_leave_word(g, 0x10, p.place));
    return p;
}

/* A record by its byte's place; a place past the records reads on. */
static void set_number_value(GameState *g, uint8_t place, uint16_t value)
{
    unsigned r = place / 4;
    if (r < BATTLE_NUMBER_RECORDS_COUNT)
        set_battle_number_records_value(g, r, value);
    else
        set_battle_number_records_value_unchecked(g, r, value);
}
static void set_number_kind(GameState *g, uint8_t place, uint8_t kind)
{
    unsigned r = place / 4;
    if (r < BATTLE_NUMBER_RECORDS_COUNT)
        set_battle_number_records_kind(g, r, kind);
    else
        set_battle_number_records_kind_unchecked(g, r, kind);
}
/* The kind written as a word: the record's last byte cleared with it. */
static void set_number_kind_word(GameState *g, uint8_t place, uint8_t kind)
{
    set_number_kind(g, place, kind);
    unsigned r = place / 4;
    if (r < BATTLE_NUMBER_RECORDS_COUNT)
        set_battle_number_records_last_byte(g, r, 0);
    else
        set_battle_number_records_last_byte_unchecked(g, r, 0);
}

void set_effect_number(GameState *g, uint8_t place, uint16_t value, uint8_t kind)
{
    set_number_value(g, place, value);
    set_number_kind(g, place, kind);
}

// $C1:E8E2
/* The miss marks the effect's record gives (its twelfth byte): bit 7 for the
 * attacker's miss, bit 6 for the target's dodge; either marks the action as
 * having missed (battle_action_extra). */
enum { MISSED_BY_ATTACKER = 0x80, DODGED = 0x40, ACTION_MISSED = 0x40 };

bool effect_missed(GameState *g, uint8_t misses, bool dodged)
{
    uint8_t mark = (uint8_t)(misses & (dodged ? DODGED : MISSED_BY_ATTACKER));
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, mark));
    NumberPlace p = target_number_place(g);
    set_number_kind(g, p.place, NUMBER_MISS);
    set_number_value(g, p.place, 0);
    bool v = effect_record_damage(g, effect_target(g), DAMAGE_MISSED, p.carry);
    if (mark)
        set_battle_action_extra(g, battle_action_extra(g) | ACTION_MISSED);
    return v;
}

/* ---- Healing and draining ------------------------------------------------------------------ */

/* Healing the party is at most 999; any damage at most 9999. */
enum { MOST_HEALED = 999, MOST_DAMAGE = 9999 };

/* The amount healed or drained is kept in the direct page's first word, which
 * a modifier reads back ($C1:EE27): kept plainly. */
static void keep_amount_worked(GameState *g, uint16_t v) { battle_keep_word(g, 0x00, v); }

/* One restoration: `base` times the multiple, where that isn't all of the
 * stat; with a random part up to `power` where the product isn't a whole
 * number of `whole` (fifty for HP, ten for MP). */
typedef struct {
    uint16_t amount;
    bool carry;
} Worked;

static Worked restoration(GameState *g, uint8_t power, uint16_t base, uint8_t multiple, uint16_t whole, uint16_t all_of_it)
{
    if (multiple == RESTORES_ALL)
        return (Worked){ all_of_it, true };
    uint16_t product = effect_times(g, base, multiple, multiple > RESTORES_ALL);
    keep_amount_worked(g, product);
    BattleLongQuotient q = effect_over(g, product, whole, false);
    if ((uint8_t)q.remainder == 0)
        return (Worked){ product, true };
    uint32_t sum = (uint32_t)product + battle_random(g, 0, power);
    return (Worked){ (uint16_t)sum, sum > 0xFFFF };
}

// $C1:DA37
void effect_heal(EffectWork *w, Healing h)
{
    GameState *g = w->g;
    if (h.restores & RESTORES_HP) {
        Worked r = restoration(g, h.power, h.power, h.multiple, 0x32, effect_target_stat_word(g, STAT_MAX_HP));
        uint16_t amount = r.amount;
        keep_amount_worked(g, amount);
        uint8_t slot = effect_listed_target(g);
        ORIGINAL_EFFECT(battle_leave_byte(g, 0x04, NUMBER_HP_RESTORED));
        bool carry = battle_effect_target(g) >= BATTLE_FIRST_ENEMY;
        if (!carry) {
            carry = amount >= MOST_HEALED;
            if (carry) {
                amount = MOST_HEALED;
                keep_amount_worked(g, amount);
            }
        }
        set_battle_effect_amount(g, amount);
        effect_record_damage(g, slot, DAMAGE_HEALS, carry);
        NumberPlace p = target_number_place(g);
        w->machine_v = p.overflow;
        set_effect_number(g, p.place, amount, NUMBER_HP_RESTORED);
        set_battle_effect_tally(g, 1);
    }
    set_battle_damage_hit(g, (uint8_t)(battle_damage_hit(g) + 1));
    if (h.restores & RESTORES_MP) {
        Worked r = restoration(g, h.power, 0x0A, h.multiple, 0x0A, effect_target_stat_word(g, STAT_MAX_MP));
        keep_amount_worked(g, r.amount);
        uint8_t slot = effect_listed_target(g);
        ORIGINAL_EFFECT(battle_leave_byte(g, 0x04, NUMBER_MP_RESTORED));
        set_battle_effect_amount(g, r.amount);
        effect_record_damage(g, slot, DAMAGE_HEALS | DAMAGE_TO_MP, r.carry);
        NumberPlace p = target_number_place(g);
        w->machine_v = p.overflow;
        set_effect_number(g, p.place, r.amount, NUMBER_MP_RESTORED);
    }
    set_battle_effect_tally(g, 0);
    set_battle_damage_hit(g, (uint8_t)(battle_damage_hit(g) - 1));
}

// $C1:E77B
/* The drain takes a share of the target's maximum HP or MP, `share`
 * twentieths, and a random part up to a tenth of that and three more; at
 * least 1, and no more than the target has. The actor's number shows what it
 * gains, the target's what it loses; and the drain wakes the target. */
enum { DRAIN_KIND_HIGH = EFFECT_ARG_3 + 1 };

void effect_drain(EffectWork *w, Drain what, uint8_t share)
{
    GameState *g = w->g;
    /* the original reads the stat's offset as a word from the cell it hands
     * the kind over in, whose high byte nothing in the effect code sets: it is
     * whatever the code before left there (0 as the game plays) */
    uint16_t offset = (uint16_t)(what | battle_low_scratch(g, DRAIN_KIND_HIGH) << 8);
    uint16_t block = (uint16_t)(battle_effect_target_block(g) + offset);
    uint16_t most = (uint16_t)(combatant_stat_byte_at_block(g, block, 2) | combatant_stat_byte_at_block(g, block, 3) << 8);
    uint16_t product = effect_times(g, most, share, false);
    uint16_t base = effect_over(g, product, 0x14, false).quotient;
    ORIGINAL_EFFECT(leave_working_word(g, base));
    uint16_t tenth = effect_over(g, base, 0x0A, false).quotient;
    uint16_t amount = (uint16_t)(battle_random(g, 0, (uint8_t)(tenth + 3)) + base);
    if (amount == 0)
        amount = 1;
    keep_amount_worked(g, amount);
    uint8_t gain_marks = battle_damage_flags_given(g);
    if (what == DRAIN_MP || what == DRAIN_HP) {
        gain_marks = what == DRAIN_MP ? DAMAGE_HEALS | DAMAGE_TO_MP : DAMAGE_HEALS;
        set_battle_damage_flags_given(g, gain_marks);
        uint16_t has = what == DRAIN_MP ? effect_target_stat_word(g, STAT_MP) : effect_target_stat_word(g, STAT_HP);
        if (has < amount) {
            amount = has;
            keep_amount_worked(g, amount);
        }
    }
    NumberPlace gains = actor_number_place(g);
    set_number_value(g, gains.place, amount);
    set_battle_effect_amount(g, amount);
    effect_record_damage(g, actor(g), gain_marks, gains.carry);
    uint8_t loses = effect_target_number(g);
    set_number_value(g, loses, amount);
    set_battle_effect_amount(g, amount);
    w->machine_v = effect_record_damage(g, effect_target(g), what == DRAIN_HP ? DAMAGE_PLAIN : DAMAGE_TO_MP, true);
    set_number_kind_word(g, loses, what == DRAIN_HP ? NUMBER_DAMAGE : NUMBER_MP_DAMAGE);
    set_number_kind_word(g, gains.place, what == DRAIN_HP ? NUMBER_HP_RESTORED : NUMBER_MP_RESTORED);
    effect_wake_target(g);
}

/* ---- Waking the target --------------------------------------------------------------------- */

// $C1:BD6F
/* A combatant's turn wait as it starts again: halved while hasted, doubled
 * (to 255 at most) while slowed, at least 1; a party member's gauge measures
 * it. */
void battle_adjust_turn_wait(GameState *g, uint8_t slot)
{
    uint8_t wait = battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot);
    if ((combatant_stat_byte(g, slot, STAT_STATUS_3) | combatant_stat_byte(g, slot, STAT_STATUS_3_WORN)) & HASTED) {
        wait >>= 1;
        set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, wait);
    }
    if (combatant_stat_byte(g, slot, STAT_STATUS_1) & SLOWED) {
        wait = wait & 0x80 ? 0xFF : (uint8_t)(wait << 1);
        set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, wait);
    }
    if (wait == 0) {
        wait = 1;
        set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, wait);
    }
    if (slot < BATTLE_PARTY_SIZE) {
        set_battle_gauge_wait(g, slot, wait);
        set_battle_gauge_wait_full(g, slot, wait);
    }
}

// $C1:E63B
void effect_wake_target(GameState *g)
{
    uint8_t statuses = effect_target_stat(g, STAT_STATUS_1);
    if (!(statuses & ASLEEP))
        return;
    set_effect_target_stat(g, STAT_STATUS_1, statuses & (uint8_t)~ASLEEP);
    uint8_t slot = effect_target(g);
    battle_restart_timer(g, BATTLE_TIMER_TURN, slot);
    set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, battle_turn_wait(g, slot));
    battle_adjust_turn_wait(g, slot);
}

/* The carry waking leaves: of its test of a party member's slot where it
 * woke the target, or as it found it. */
static bool wake_target(GameState *g, bool carry)
{
    bool woke = (effect_target_stat(g, STAT_STATUS_1) & ASLEEP) != 0;
    effect_wake_target(g);
    return woke ? effect_target(g) >= BATTLE_PARTY_SIZE : carry;
}

/* ---- A hit's tests of landing --------------------------------------------------------------- */

// $C1:D160, $C1:DE87
/* An enemy of a kind the effect names (by its stat block's kind bits) turns
 * it away. */
bool effect_turned_away(EffectWork *w, uint8_t kinds)
{
    GameState *g = w->g;
    uint8_t kind = effect_target_stat(g, STAT_KIND);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, kind));
    bool away = effect_target(g) >= BATTLE_FIRST_ENEMY && (kinds & kind);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, away ? 1 : 0));
    return away;
}

/* A number scaled by `times` twentieths, and one more (the aim's and the
 * evasion's status changes). */
static uint16_t twentieths_and_one(GameState *g, uint8_t v, uint8_t times, bool carry)
{
    uint16_t product = effect_times(g, v, times, carry);
    return (uint16_t)(effect_over(g, product, 0x14, false).quotient + 1);
}

// $C1:DE9E
/* The attacker's aim, lowered while it is blind or its aim is slowed. */
static uint16_t aim_of(GameState *g, uint8_t aim)
{
    uint8_t statuses = effect_actor_stat(g, STAT_STATUS_1);
    if (statuses & BLIND)
        return twentieths_and_one(g, aim, 5, false);
    if (statuses & SLOWED_AIM)
        return twentieths_and_one(g, aim, 0x0F, false);
    return aim;
}

// $C1:DEF0
/* The target's evasion, raised or lowered by its statuses; then raised by
 * the levels the attacker has over it, as the original divides them: the
 * difference is divided by twenty with the carry of its subtraction coming in
 * at the top, and that quotient again by the twenty the division left as its
 * divisor, the sum with the evasion only saturating it where it goes past a
 * byte (the sum itself isn't kept). */
static uint16_t evasion_of(GameState *g, uint16_t evasion)
{
    uint8_t guarded = (uint8_t)(effect_target_stat(g, STAT_STATUS_3) | effect_target_stat(g, STAT_STATUS_3_WORN));
    if (guarded & EVADES_BETTER) {
        uint16_t half = (uint16_t)(effect_times(g, evasion, 5, false) >> 1);
        evasion = half >= 0xFF ? 0xFF : half;
    } else if (guarded & EVADES_A_LITTLE) {
        evasion = effect_times(g, evasion, 2, false);
    } else {
        uint8_t statuses = effect_target_stat(g, STAT_STATUS_1);
        battle_keep_byte(g, BATTLE_TECH_HITS_LEFT, statuses);
        if (statuses & STOPPED_OR_ASLEEP) {
            evasion = twentieths_and_one(g, (uint8_t)evasion, 4, false);
        } else if (statuses & BLIND_OR_WEAK) {
            evasion = twentieths_and_one(g, (uint8_t)evasion, 0x0A, false);
        } else {
            if (statuses & DAZED)
                evasion = twentieths_and_one(g, (uint8_t)evasion, 0x0F, false);
            if (statuses & CONFUSED_HIT)
                evasion = twentieths_and_one(g, (uint8_t)evasion, 0x0A, false);
        }
    }
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, evasion));
    uint8_t own = effect_actor_stat(g, STAT_LEVEL), theirs = effect_target_stat(g, STAT_LEVEL);
    if (own < theirs)
        return evasion;
    uint16_t first = effect_over(g, (uint8_t)(own - theirs), 0x14, true).quotient;
    uint16_t again = effect_over(g, first, battle_wide_math_second(g), true).quotient;
    if ((uint8_t)again + (uint8_t)evasion > 0xFF) {
        evasion = (uint16_t)((evasion & 0xFF00) | 0xFF);
        ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_2, 0xFF));
    }
    return evasion;
}

// $C1:D174, $C1:DC64
/* A hit lands where a percent drawn is at most its chance: the attacker's
 * aim over the target's evasion, in hundredths (99 of 100 where the aim is
 * at least the evasion). */
enum { SURE_AIM = 0x63, SURE_EVASION = 0x64, PERCENT = 100 };

bool effect_lands(EffectWork *w, uint8_t aim_stat, uint8_t evasion_stat)
{
    GameState *g = w->g;
    uint8_t aim = effect_actor_stat(g, aim_stat), evade = effect_target_stat(g, evasion_stat);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, aim));
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, evade));
    uint16_t aimed = aim_of(g, aim);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, aimed));
    uint16_t evasion = evasion_of(g, evade);
    uint8_t a = (uint8_t)aimed, e = (uint8_t)evasion;
    bool sure = a >= e;
    if (sure) {
        a = SURE_AIM;
        e = SURE_EVASION;
        ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, a));
        ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_2, e));
    }
    uint16_t product = effect_times(g, a, PERCENT, sure);
    uint16_t chance = effect_over(g, product, e, false).quotient;
    uint8_t drawn = battle_random(g, 0, PERCENT);
    w->machine_y &= 0x00FF;
    bool lands = (uint8_t)chance >= drawn;
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, (uint16_t)((chance & 0xFF00) | (lands ? 1 : 0))));
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_2, drawn));
    set_battle_effect_hit_test(g, 0);
    return lands;
}

/* ---- A hit's damage ------------------------------------------------------------------------ */

/* The random part a formula adds is drawn up to one past (or two past) a
 * share of a stat, which the original works out by shifts and subtractions,
 * leaving its last shift in $08 and the share in $30: about three tenths, a
 * fifth, a tenth. It keeps the difference before the last subtraction in its
 * Y register (`machine_y`), which the controller's reading saves on the stack
 * where a modifier reads it later. */
static uint16_t about_three_tenths(GameState *g, uint8_t stat, uint16_t *machine_y)
{
    uint16_t v = (uint16_t)(stat << 8);
    *machine_y = (uint16_t)((v >> 1) - (v >> 3) - (v >> 4) - (v >> 7));
    uint16_t share = (uint16_t)((uint16_t)((v >> 1) - (v >> 3) - (v >> 4) - (v >> 7) - (v >> 8)) >> 8);
    battle_keep_word(g, BATTLE_NUMBER_TESTED, (uint16_t)(v >> 8));
    ORIGINAL_EFFECT(leave_working_word(g, share));
    return share;
}

static uint16_t about_a_fifth(GameState *g, uint8_t stat, uint16_t *machine_y)
{
    uint16_t v = (uint16_t)(stat << 8);
    *machine_y = (uint16_t)((v >> 2) - (v >> 5) - (v >> 6) - (v >> 9));
    uint16_t share = (uint16_t)((uint16_t)((v >> 2) - (v >> 5) - (v >> 6) - (v >> 9) - (v >> 10)) >> 8);
    battle_keep_word(g, BATTLE_NUMBER_TESTED, (uint16_t)(v >> 10));
    ORIGINAL_EFFECT(leave_working_word(g, share));
    return share;
}

uint16_t effect_about_a_tenth(GameState *g, uint16_t v, uint16_t *machine_y)
{
    *machine_y = (uint16_t)((v >> 3) - (v >> 6) - (v >> 7));
    uint16_t share = (uint16_t)((v >> 3) - (v >> 6) - (v >> 7) - (v >> 10));
    battle_keep_word(g, BATTLE_NUMBER_TESTED, (uint16_t)(v >> 10));
    ORIGINAL_EFFECT(leave_working_word(g, share));
    return share;
}

/* The random part of the damage, up to `limit` (a byte): the carry the draw
 * leaves. */
static bool draw_variance(GameState *g, uint8_t limit)
{
    uint8_t drawn = battle_random(g, 0, limit);
    set_battle_damage_variance(g, drawn);
    return battle_draw_carry(g, 0, limit, drawn);
}

static uint8_t power(const GameState *g) { return battle_effect_record(g, 9); }

/* The power times a sum of the attacker's stats, over a divisor: the power
 * first multiplied by one, the way round each formula does it. */
static uint16_t powered(GameState *g, uint16_t sum, uint8_t divisor, bool carry)
{
    uint16_t once = effect_times(g, power(g), 1, carry);
    uint16_t product = effect_times(g, once, sum, false);
    return effect_over(g, product, divisor, false).quotient;
}

static uint16_t powered_once(GameState *g, uint16_t sum, uint8_t divisor, bool carry)
{
    uint16_t once = effect_times(g, 1, power(g), carry);
    uint16_t product = effect_times(g, once, sum, false);
    return effect_over(g, product, divisor, false).quotient;
}

/* The damage a formula works out, and the carry the working leaves. */
typedef struct {
    uint16_t damage;
    bool carry;
} Formula;

/* A sum and the carry of its addition. */
static uint16_t added(uint16_t a, uint16_t b, bool *carry)
{
    uint32_t sum = (uint32_t)a + b;
    *carry = sum > 0xFFFF;
    return (uint16_t)sum;
}

static Formula formula(GameState *g, uint8_t kind, bool carry, uint16_t *machine_y)
{
    switch (kind) {
    case 1: {
        /* the attacker's power and weapon */
        carry = wake_target(g, carry);
        uint16_t power_part = effect_times(g, effect_actor_stat(g, STAT_POWER), 0x0C, carry);
        battle_keep_stat_offset_word(g, power_part);
        uint16_t sum = added(effect_times(g, effect_actor_stat(g, STAT_WEAPON_ATTACK), 5, false), power_part, &carry);
        ORIGINAL_EFFECT(leave_working_word(g, sum));
        uint16_t damage = powered(g, sum, 0x12, carry);
        ORIGINAL_EFFECT(leave_working_word(g, damage));
        uint16_t third = effect_over(g, effect_actor_stat(g, STAT_POWER), 3, false).quotient;
        bool drawn = draw_variance(g, (uint8_t)(third + 1));
        battle_set_wide_math_quotient(g, damage);
        return (Formula){ damage, drawn };
    }
    case 2: {
        /* stamina and weapon */
        carry = wake_target(g, carry);
        uint16_t stamina_part = effect_times(g, effect_actor_stat(g, STAT_STAMINA), 8, carry);
        battle_keep_stat_offset_word(g, stamina_part);
        uint16_t sum = added(effect_times(g, effect_actor_stat(g, STAT_WEAPON_ATTACK), 8, false), stamina_part, &carry);
        ORIGINAL_EFFECT(leave_working_word(g, sum));
        uint16_t damage = powered(g, sum, 0x12, carry);
        uint16_t share = about_three_tenths(g, effect_actor_stat(g, STAT_STAMINA), machine_y);
        return (Formula){ damage, draw_variance(g, (uint8_t)(share + 1)) };
    }
    case 3: {
        /* level and magic */
        uint16_t level_part = effect_times(g, effect_actor_stat(g, STAT_LEVEL), 0x0C, carry);
        battle_keep_stat_offset_word(g, level_part);
        uint16_t sum = added(effect_times(g, effect_actor_stat(g, STAT_MAGIC), 0x0C, false), level_part, &carry);
        battle_keep_stat_offset_word(g, sum);
        uint16_t damage = powered_once(g, sum, 0x0F, carry);
        uint16_t share = about_three_tenths(g, effect_actor_stat(g, STAT_MAGIC), machine_y);
        return (Formula){ damage, draw_variance(g, (uint8_t)(share + 1)) };
    }
    case 4: {
        /* speed */
        carry = wake_target(g, carry);
        uint16_t speed_part = effect_times(g, effect_actor_stat(g, STAT_SPEED), 2, carry);
        battle_keep_stat_offset_word(g, speed_part);
        uint16_t damage = powered(g, speed_part, 0x12, false);
        uint16_t share = about_a_fifth(g, effect_actor_stat(g, STAT_SPEED), machine_y);
        return (Formula){ damage, draw_variance(g, (uint8_t)(share + 2)) };
    }
    case 5: {
        /* power, and the level squared */
        carry = wake_target(g, carry);
        uint16_t power_part = effect_times(g, effect_actor_stat(g, STAT_POWER), 0x10, carry);
        battle_keep_stat_offset_word(g, power_part);
        uint8_t level = effect_actor_stat(g, STAT_LEVEL);
        uint16_t fifth = effect_over(g, effect_times(g, level, level, false), 5, false).quotient;
        uint16_t sum = added(fifth, power_part, &carry);
        ORIGINAL_EFFECT(leave_working_word(g, sum));
        uint16_t damage = powered(g, sum, 0x14, carry);
        ORIGINAL_EFFECT(leave_working_word(g, damage));
        uint16_t third = effect_over(g, level, 3, false).quotient;
        bool drawn = draw_variance(g, (uint8_t)(third + 1));
        battle_set_wide_math_quotient(g, damage);
        return (Formula){ damage, drawn };
    }
    case 6: {
        /* the HP the attacker has lost */
        carry = wake_target(g, carry);
        uint16_t hit = effect_times(g, effect_actor_stat(g, STAT_HIT), 1, carry);
        uint16_t powered_hit = effect_times(g, hit, power(g), false);
        uint16_t most = effect_actor_stat_word(g, STAT_MAX_HP), has = effect_actor_stat_word(g, STAT_HP);
        uint16_t missing = (uint16_t)(most - has);
        battle_keep_stat_offset_word(g, missing);
        uint16_t damage = effect_over(g, effect_times(g, missing, powered_hit, most >= has), 0x8C, false).quotient;
        uint16_t share = effect_about_a_tenth(g, missing, machine_y);
        return (Formula){ damage, draw_variance(g, (uint8_t)(share + 1)) };
    }
    case 7: {
        /* power and another block's weapon */
        carry = wake_target(g, carry);
        uint8_t strength = effect_actor_stat(g, STAT_POWER);
        uint16_t other = (uint16_t)(combatant_stat_byte_at_block(g, battle_effect_other_block(g), STAT_WEAPON_ATTACK)
                                    | combatant_stat_byte_at_block(g, battle_effect_other_block(g), STAT_WEAPON_ATTACK + 1) << 8);
        uint16_t sum = added((uint16_t)(strength << 1), other, &carry);
        ORIGINAL_EFFECT(leave_working_word(g, sum));
        uint16_t damage = powered(g, sum, 0x12, carry);
        uint16_t share = about_three_tenths(g, strength, machine_y);
        return (Formula){ damage, draw_variance(g, (uint8_t)(share + 1)) };
    }
    default: {
        /* the weapon's: the attack of the block the element the attacker's
         * weapon has names (the stat's word read, its high byte kept) */
        carry = wake_target(g, carry);
        uint8_t elements = effect_actor_stat(g, STAT_ELEMENT_BITS);
        unsigned which = elements & 0x80 ? 0 : elements & 0x40 ? 1 : elements & 0x20 ? 2 : 3;
        uint16_t place = (uint16_t)(effect_actor_stat_word(g, (uint16_t)(STAT_FIRST_ELEMENT_POWER + which)) + battle_effect_other_block(g));
        uint16_t attack = (uint16_t)((place & 0xFF00) | combatant_stat_byte_at_block(g, place, STAT_WEAPON_ATTACK));
        ORIGINAL_EFFECT(leave_working_word(g, attack));
        uint16_t damage = powered(g, attack, 0x12, false);
        uint16_t share = about_three_tenths(g, effect_actor_stat(g, STAT_POWER), machine_y);
        return (Formula){ damage, draw_variance(g, (uint8_t)(share + 1)) };
    }
    }
}

// $C1:E0F5
/* A hit's damage by its formula (the effect record's sixth byte), then
 * raised or lowered by the attacker's statuses. Formula 8 works nothing out:
 * the damage is whatever the long arithmetic's quotient cell holds (and the
 * formulas 9 and 10 the original has are never reached: 9 and up take the
 * weapon's). */
enum { FORMULA_LEFT_AS_IT_IS = 8 };

static uint16_t damage_worked_out(GameState *g, uint8_t kind, uint16_t *machine_y)
{
    uint16_t damage;
    bool carry;
    if (kind == FORMULA_LEFT_AS_IT_IS) {
        damage = battle_wide_math_quotient(g);
        carry = false;
    } else {
        Formula f = formula(g, kind, kind != 0, machine_y);
        /* its draw works with 8-bit index registers, which clears their high
         * bytes (effect_work.h, machine_y) */
        *machine_y &= 0x00FF;
        damage = f.damage;
        carry = f.carry;
    }
    uint8_t statuses = (uint8_t)(effect_actor_stat(g, STAT_STATUS_4) | effect_actor_stat(g, STAT_KEPT_STATUS_4));
    if (statuses & DAMAGE_UP_A_HALF) {
        damage = (uint16_t)((damage >> 1) + damage);
        battle_set_wide_math_quotient(g, damage);
    } else {
        if (statuses & DAMAGE_UP_A_HALF_ROUNDED) {
            damage = effect_times(g, effect_over(g, damage, 2, carry).quotient, 3, carry);
            carry = false;
        } else if (statuses & DAMAGE_UP_A_QUARTER) {
            damage = effect_times(g, effect_over(g, damage, 4, carry).quotient, 5, carry);
            carry = false;
        }
        uint8_t own = effect_actor_stat(g, STAT_STATUS_1);
        if (own & WEAKENED) {
            damage = effect_over(g, effect_times(g, damage, 8, carry), 0x0A, false).quotient;
            carry = false;
        }
        if (own & WEAKENED_TOO) {
            damage = effect_over(g, effect_times(g, damage, 8, carry), 0x0A, false).quotient;
            carry = false;
        }
        if (own & QUARTERED)
            damage = effect_over(g, damage, 4, carry).quotient;
    }
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, damage));
    battle_keep_stat_offset_word(g, 0);
    return damage;
}

// $C1:E65F
/* The target's defence: the stat the effect names (its seventh byte), $3E
 * as it is and $3C two and a half times; any other number is the defence
 * itself. Its statuses lower it, and the effect's own flags scale it by
 * quarters; at most 255. */
enum { DEFENCE_STAT = 0x3E, DEFENCE_STAT_TIMES = 0x3C, QUARTER_STEPS = 0x03 };

static uint16_t defence_of(GameState *g, uint8_t named)
{
    uint16_t defence = named;
    bool carry;
    if (named == DEFENCE_STAT) {
        defence = effect_target_stat(g, named);
        carry = false;
    } else if (named == DEFENCE_STAT_TIMES) {
        defence = effect_over(g, effect_times(g, effect_target_stat(g, named), 5, false), 2, false).quotient;
        carry = false;
    } else {
        carry = named >= DEFENCE_STAT_TIMES;
    }
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, defence));
    uint8_t statuses = effect_target_stat(g, STAT_STATUS_1);
    battle_keep_byte(g, BATTLE_TECH_HITS_LEFT, statuses);
    if (statuses & STOPPED_OR_ASLEEP) {
        defence = effect_over(g, effect_times(g, (uint8_t)defence, 0x10, carry), 0x14, false).quotient;
        carry = false;
    } else {
        if (statuses & CONFUSED_HIT) {
            defence = effect_over(g, effect_times(g, (uint8_t)defence, 0x12, carry), 0x14, false).quotient;
            carry = false;
        }
        if (statuses & BLIND_OR_WEAK) {
            defence = effect_over(g, effect_times(g, (uint8_t)defence, 0x12, carry), 0x14, false).quotient;
            carry = false;
        }
        if (statuses & DAZED) {
            defence = effect_over(g, effect_times(g, (uint8_t)defence, 0x12, carry), 0x14, false).quotient;
            carry = false;
        }
    }
    uint8_t quarters = battle_effect_flags(g, 1) & QUARTER_STEPS;
    if (quarters) {
        /* the original sets the numbers' low bytes alone: their high bytes
         * are what the arithmetic left */
        uint16_t first = (uint16_t)(battle_wide_math(g, 1) << 8 | (uint8_t)defence);
        uint16_t second = (uint16_t)(battle_wide_math(g, 3) << 8 | quarters);
        defence = (uint16_t)(effect_times(g, first, second, carry) >> 2);
    }
    if ((defence & 0x8000) || defence >= 0xFF)
        defence = 0xFF;
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, defence));
    return defence;
}

HitAmounts effect_hit_amounts(EffectWork *w, uint8_t formula_kind, uint8_t defence_stat)
{
    GameState *g = w->g;
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, formula_kind));
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, defence_stat));
    uint16_t damage = damage_worked_out(g, formula_kind, &w->machine_y);
    uint16_t defence = defence_of(g, defence_stat);
    return (HitAmounts){ damage, (uint8_t)defence };
}

// $C1:DCAD
/* The hit's damage applied: its defence taken off (as a share of 256), the
 * variance added, at least 1; the modifiers; a barrier or a shield (not where
 * the effect passes it) takes it to seven tenths; at most 9999. A hit the
 * target absorbs heals it; otherwise it is recorded, and a hit that takes all
 * the target's HP beats it: an enemy is marked beaten, its turn timer stops,
 * and the enemies aiming at a party member forget what they chose. */
enum { PASSES_BARRIER = 0x40, PASSES_SHIELD = 0x80, LEAVES_ONE_HP = 0x01, ABSORBED = 0x40, ENEMY_ANIMATION_BEATEN = 0 };

void effect_mark_beaten(GameState *g, uint8_t slot)
{
    set_battle_enemy_beaten_any(g, 1);
    unsigned enemy = (uint8_t)(slot - BATTLE_FIRST_ENEMY);
    if (enemy < BATTLE_ENEMIES) {
        set_battle_enemy_beaten(g, enemy, 1);
        set_battle_enemy_animation(g, enemy, ENEMY_ANIMATION_BEATEN);
    } else {
        set_battle_enemy_beaten_unchecked(g, enemy, 1);
        set_battle_enemy_animation_unchecked(g, enemy, ENEMY_ANIMATION_BEATEN);
    }
}

static void stop_turn_timer(GameState *g, uint8_t slot)
{
    if (slot < BATTLE_COMBATANTS)
        battle_stop_timer(g, BATTLE_TIMER_TURN, slot);
    else
        set_battle_timer_flags_byte_unchecked(g, BATTLE_TIMER_TURN, slot, 0);
}

void effect_apply_hit(EffectWork *w, HitAmounts hit)
{
    GameState *g = w->g;
    set_effect_target_stat(g, STAT_STATUS_1, effect_target_stat(g, STAT_STATUS_1) & (uint8_t)~ASLEEP);
    uint16_t kept = (uint16_t)(0x100 - hit.defence);
    uint16_t product = effect_times(g, kept, hit.damage, true); /* the subtraction never borrows */
    bool carry;
    uint16_t amount = added(effect_over(g, product, 0x100, false).quotient, battle_damage_variance(g), &carry);
    if (amount == 0)
        amount = 1;
    set_battle_effect_amount(g, amount);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, 0));
    w->first_left = 0;
    effect_step_in(w, STEP_MODIFIERS);
    carry = effect_apply_modifiers(w, carry);
    effect_step_out(w);
    if (w->stopped)
        return;
    uint8_t guards = (uint8_t)(effect_target_stat(g, STAT_STATUS_4) | effect_target_stat(g, STAT_KEPT_STATUS_4));
    battle_keep_byte(g, BATTLE_TECH_HITS_LEFT, guards);
    if (!(battle_action_state(g) & LEAVES_ONE_HP)) {
        bool barrier = !(battle_effect_flags(g, 0) & PASSES_BARRIER) && (guards & BARRIER);
        bool shield = !barrier && !(battle_effect_flags(g, 0) & PASSES_SHIELD) && (guards & SHIELD);
        if (barrier || shield) {
            uint16_t tenths = effect_over(g, battle_effect_amount(g), 0x0A, carry).quotient;
            set_battle_effect_amount(g, effect_times(g, tenths, 7, carry));
        }
    }
    bool unused_carry;
    uint8_t place = effect_target_number_carrying(g, &unused_carry, &w->machine_v);
    carry = battle_effect_amount(g) >= MOST_DAMAGE;
    if (carry)
        set_battle_effect_amount(g, MOST_DAMAGE);
    if (battle_action_state(g) & ABSORBED) {
        uint8_t slot = effect_listed_target(g);
        effect_record_damage(g, slot, DAMAGE_HEALS, carry);
        /* the recording leaves the entry's place in Y (see effect_work.h) */
        w->machine_y = (uint8_t)(slot * 4 + (uint8_t)(battle_damage_hit(g) * 0x2C));
        set_number_kind(g, place, NUMBER_HP_RESTORED);
        set_battle_action_state(g, battle_action_state(g) & (uint8_t)~ABSORBED);
        set_battle_effect_amount(g, (uint16_t)-battle_effect_amount(g));
    } else {
        uint16_t hp = effect_target_stat_word(g, STAT_HP);
        if (hp == 0)
            return;
        if (hp <= battle_effect_amount(g)) {
            if (battle_effect_amount(g) != 0) {
                uint8_t beaten = effect_listed_target(g);
                battle_keep_word(g, BATTLE_TECH_HITS_LEFT, beaten);
                effect_record_damage(g, beaten, DAMAGE_PLAIN, true);
                if (beaten >= BATTLE_FIRST_ENEMY)
                    effect_mark_beaten(g, beaten);
            }
            uint8_t gone = effect_listed_target(g);
            stop_turn_timer(g, gone);
            if (gone < BATTLE_PARTY_SIZE) {
                /* the enemies aiming at it forget what they chose; the
                 * original compares with what $0E holds, which is the member
                 * only where the hit did damage */
                uint8_t aimed_at = battle_low_scratch(g, BATTLE_TECH_HITS_LEFT);
                for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
                    if (battle_enemy_target(g, enemy) == aimed_at)
                        set_battle_enemy_last_condition(g, enemy, NO_CONDITION);
            }
            set_battle_candidate_count(g, 1);
        } else {
            effect_record_damage(g, effect_listed_target(g), DAMAGE_PLAIN, true);
        }
        place = effect_target_number(g);
        set_number_kind(g, place, NUMBER_DAMAGE);
        w->machine_y = effect_listed_target(g);
    }
    set_number_value(g, place, battle_effect_amount(g));
    w->machine_v = overflow16(battle_effect_amount(g), battle_effect_total(g));
    set_battle_effect_total(g, (uint16_t)(battle_effect_total(g) + battle_effect_amount(g)));
}
