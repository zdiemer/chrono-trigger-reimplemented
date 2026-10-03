/* Battle: the amounts an effect works out -- healing, draining, a hit's
 * landing and its damage -- and how they are recorded (battle/effect_parts.h).
 *
 * The original works each out with its long arithmetic ($C1:C90B, $C1:C92A),
 * whose cells keep what each call leaves; a division divides the high word
 * the last call left with the number it is given, which is how a product just
 * made is divided whole. Where a leftover moves a result the game shows, the
 * port keeps it and says so (the evasion's level bonus, the defence's
 * quarters, formula 8). */
#include "battle/effect_parts.h"

#include "battle/combatant.h"
#include "battle/random.h"
#include "battle/timers.h"

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

/* ---- Where the numbers go ----------------------------------------------------------------- */

uint8_t effect_listed_target(const GameState *g)
{
    uint8_t place = (uint8_t)(battle_action_target_count(g) - 1);
    return place < BATTLE_ACTION_TARGETS_COUNT ? battle_action_targets(g, place) : battle_action_targets_unchecked(g, place);
}

/* The damage table holds a set of eleven entries of four bytes for each hit
 * of the action; the number records a set for each number the showing puts
 * up. The original finds an entry by the set's place (a multiply) and the
 * slot's, in a byte. */
enum { SET_BYTES = 0x2C, ENTRY_BYTES = 4 };

// $C1:EBF8
void effect_record_damage(GameState *g, uint8_t slot, uint8_t marks)
{
    set_battle_damage_flags_given(g, marks);
    set_battle_damage_flags_kept(g, marks);
    if (marks & DAMAGE_HEALS)
        set_battle_effect_amount(g, (uint16_t)-battle_effect_amount(g));
    uint8_t set_place = (uint8_t)effect_times(g, battle_damage_hit(g), SET_BYTES);
    unsigned entry = (uint8_t)(slot * ENTRY_BYTES + set_place) / ENTRY_BYTES;
    if (entry < BATTLE_DAMAGE_COUNT) {
        set_battle_damage_amount(g, entry, battle_effect_amount(g));
        set_battle_damage_flags(g, entry, marks);
    } else {
        set_battle_damage_amount_unchecked(g, entry, battle_effect_amount(g));
        set_battle_damage_flags_unchecked(g, entry, marks);
    }
}

/* A number record's place, a byte's offset among the records: the
 * combatant's in the set the effect is at (battle_effect_tally). The
 * original multiplies the set by its size one way round for the target and
 * the other for the actor (the tally first), and keeps the place in a cell
 * ($0E for the target, $10 for the actor). */
static uint8_t number_place(GameState *g, uint8_t slot, bool tally_first)
{
    uint8_t set_place = tally_first ? (uint8_t)effect_times(g, battle_effect_tally(g), SET_BYTES)
                                    : (uint8_t)effect_times(g, SET_BYTES, battle_effect_tally(g));
    uint8_t slot_place = (uint8_t)(slot * ENTRY_BYTES);
    return (uint8_t)(set_place + slot_place);
}

/* Where the original keeps the actor's number's place (scaffolding). */
enum { ACTOR_NUMBER_PLACE_CELL = 0x10 };

/* A record made after a number's place is found is made with the carry of
 * the place's add, which the multiply leaves in its cell (scaffolding). */
static void leave_number_place_carry(GameState *g, uint8_t place, uint8_t slot)
{
    battle_leave_multiply_carry(g, place < (uint8_t)(slot * ENTRY_BYTES));
}

// $C1:E89F
uint8_t effect_target_number(GameState *g)
{
    uint8_t place = number_place(g, effect_target(g), false);
    effect_leave_last_worked(g, place);
    return place;
}

// $C1:E8C0
static uint8_t actor_number_place(GameState *g)
{
    uint8_t place = number_place(g, actor(g), true);
    ORIGINAL_EFFECT(battle_leave_word(g, ACTOR_NUMBER_PLACE_CELL, place));
    return place;
}

/* A record by its byte's place; a place past the records reads on. */
static void set_number_value(GameState *g, uint8_t place, uint16_t value)
{
    unsigned r = place / ENTRY_BYTES;
    if (r < BATTLE_NUMBER_RECORDS_COUNT)
        set_battle_number_records_value(g, r, value);
    else
        set_battle_number_records_value_unchecked(g, r, value);
}
static void set_number_kind(GameState *g, uint8_t place, uint8_t kind)
{
    unsigned r = place / ENTRY_BYTES;
    if (r < BATTLE_NUMBER_RECORDS_COUNT)
        set_battle_number_records_kind(g, r, kind);
    else
        set_battle_number_records_kind_unchecked(g, r, kind);
}
/* The kind written as a word: the record's last byte cleared with it. */
static void set_number_kind_word(GameState *g, uint8_t place, uint8_t kind)
{
    set_number_kind(g, place, kind);
    unsigned r = place / ENTRY_BYTES;
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

void effect_missed(GameState *g, uint8_t misses, bool dodged)
{
    uint8_t mark = (uint8_t)(misses & (dodged ? DODGED : MISSED_BY_ATTACKER));
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, mark));
    uint8_t place = effect_target_number(g);
    set_number_kind(g, place, NUMBER_MISS);
    set_number_value(g, place, 0);
    effect_record_damage(g, effect_target(g), DAMAGE_MISSED);
    ORIGINAL_EFFECT(leave_number_place_carry(g, place, effect_target(g)));
    if (mark)
        set_battle_action_extra(g, battle_action_extra(g) | ACTION_MISSED);
}

/* ---- What statuses do ------------------------------------------------------------------------ */

/* A status's timer's doing (battle/status_timers.c), recorded for the
 * combatant as a hit's would be, with its number where the showing puts it. */
static void status_amount(GameState *g, uint8_t slot, uint16_t amount, uint8_t number, uint8_t marks)
{
    set_battle_effect_amount(g, amount);
    set_battle_effect_target(g, slot);
    uint8_t place = effect_target_number(g);
    set_effect_number(g, place, amount, number);
    effect_record_damage(g, slot, marks);
    ORIGINAL_EFFECT(leave_number_place_carry(g, place, slot));
}

void effect_show_status_damage(GameState *g, uint8_t slot, uint16_t damage)
{
    status_amount(g, slot, damage, NUMBER_DAMAGE, DAMAGE_PLAIN);
}

void effect_show_magic_regained(GameState *g, uint8_t slot, uint16_t magic)
{
    status_amount(g, slot, magic, NUMBER_MP_RESTORED, DAMAGE_HEALS | DAMAGE_TO_MP);
}

void effect_show_hp_regained(GameState *g, uint8_t slot, uint16_t hp)
{
    status_amount(g, slot, hp, NUMBER_HP_RESTORED, DAMAGE_HEALS);
}

void effect_record_status_damage(GameState *g, uint8_t slot, uint16_t damage)
{
    set_battle_effect_amount(g, damage);
    set_battle_effect_target(g, slot);
    effect_record_damage(g, slot, DAMAGE_PLAIN);
}

/* ---- Healing and draining ------------------------------------------------------------------ */

/* Healing the party is at most 999; any damage at most 9999. */
enum { MOST_HEALED = 999, MOST_DAMAGE = 9999 };

/* The amount healed or drained is kept in the direct page's first word, which
 * a modifier reads back ($C1:EE27): kept plainly. */
static void keep_amount_worked(GameState *g, uint16_t v) { battle_keep_word(g, BATTLE_CLOCK_READS, v); }

/* One restoration: `base` times the multiple, where that isn't all of the
 * stat; with a random part up to `power` where the product isn't a whole
 * number of `whole` (fifty for HP, ten for MP). */
static uint16_t restoration(GameState *g, uint8_t power, uint16_t base, uint8_t multiple, uint16_t whole, uint16_t all_of_it)
{
    if (multiple == RESTORES_ALL)
        return all_of_it;
    uint16_t product = effect_times(g, base, multiple);
    keep_amount_worked(g, product);
    if ((uint8_t)effect_over(g, product, whole).remainder == 0)
        return product;
    uint8_t drawn = battle_random(g, 0, power);
    return (uint16_t)(product + drawn);
}

/* How the showing's number over the target shows the part: the HP's, the
 * MP's (the original leaves it in $04). */
static void restored(GameState *g, uint8_t slot, uint16_t amount, uint8_t marks, uint8_t number)
{
    keep_amount_worked(g, amount);
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x04, number));
    set_battle_effect_amount(g, amount);
    effect_record_damage(g, slot, marks);
    set_effect_number(g, effect_target_number(g), amount, number);
}

// $C1:DA37
void effect_heal(GameState *g, Healing h)
{
    if (h.restores & RESTORES_HP) {
        uint16_t amount = restoration(g, h.power, h.power, h.multiple, 0x32, effect_target_stat_word(g, STAT_MAX_HP));
        keep_amount_worked(g, amount);
        uint8_t slot = effect_listed_target(g);
        if (battle_effect_target(g) < BATTLE_FIRST_ENEMY && amount >= MOST_HEALED)
            amount = MOST_HEALED;
        restored(g, slot, amount, DAMAGE_HEALS, NUMBER_HP_RESTORED);
        set_battle_effect_tally(g, 1);
    }
    set_battle_damage_hit(g, (uint8_t)(battle_damage_hit(g) + 1));
    if (h.restores & RESTORES_MP) {
        uint16_t amount = restoration(g, h.power, 0x0A, h.multiple, 0x0A, effect_target_stat_word(g, STAT_MAX_MP));
        restored(g, effect_listed_target(g), amount, DAMAGE_HEALS | DAMAGE_TO_MP, NUMBER_MP_RESTORED);
    }
    set_battle_effect_tally(g, 0);
    set_battle_damage_hit(g, (uint8_t)(battle_damage_hit(g) - 1));
}

// $C1:E77B
/* The drain takes a share of the target's maximum HP or MP, `share`
 * twentieths, and a random part up to a tenth of that and three more; at
 * least 1, and no more than the target has. The actor's number shows what it
 * gains, the target's what it loses; and the drain wakes the target. */
enum { MOST_OFFSET = 2 };

void effect_drain(GameState *g, Drain what, uint8_t share)
{
    uint16_t most = effect_target_stat_word(g, (uint16_t)(what + MOST_OFFSET));
    uint16_t base = effect_over(g, effect_times(g, most, share), 0x14).quotient;
    ORIGINAL_EFFECT(leave_working_word(g, base));
    uint8_t limit = (uint8_t)(effect_over(g, base, 0x0A).quotient + 3);
    uint8_t drawn = battle_random(g, 0, limit);
    uint16_t amount = (uint16_t)(drawn + base);
    if (amount == 0)
        amount = 1;
    keep_amount_worked(g, amount);
    uint8_t gain_marks = what == DRAIN_MP ? DAMAGE_HEALS | DAMAGE_TO_MP : DAMAGE_HEALS;
    set_battle_damage_flags_given(g, gain_marks);
    uint16_t has = effect_target_stat_word(g, what == DRAIN_MP ? STAT_MP : STAT_HP);
    if (has < amount) {
        amount = has;
        keep_amount_worked(g, amount);
    }
    uint8_t gains = actor_number_place(g);
    set_number_value(g, gains, amount);
    set_battle_effect_amount(g, amount);
    effect_record_damage(g, actor(g), gain_marks);
    uint8_t loses = effect_target_number(g);
    set_number_value(g, loses, amount);
    set_battle_effect_amount(g, amount);
    effect_record_damage(g, effect_target(g), what == DRAIN_HP ? DAMAGE_PLAIN : DAMAGE_TO_MP);
    /* the last record is made with the carry of the compare before it, set */
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, true));
    set_number_kind_word(g, loses, what == DRAIN_HP ? NUMBER_DAMAGE : NUMBER_MP_DAMAGE);
    set_number_kind_word(g, gains, what == DRAIN_HP ? NUMBER_HP_RESTORED : NUMBER_MP_RESTORED);
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
    if ((combatant_stat_byte(g, slot, STAT_STATUS_3) | combatant_stat_byte(g, slot, STAT_KEPT_STATUS_3)) & STATUS3_HASTE) {
        wait >>= 1;
        set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, wait);
    }
    if (combatant_stat_byte(g, slot, STAT_STATUS_1) & STATUS_SLOW) {
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
    if (!(statuses & STATUSES_A_HIT_ENDS))
        return;
    set_effect_target_stat(g, STAT_STATUS_1, statuses & (uint8_t)~STATUSES_A_HIT_ENDS);
    uint8_t slot = effect_target(g);
    battle_restart_timer(g, BATTLE_TIMER_TURN, slot);
    set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, battle_turn_wait(g, slot));
    battle_adjust_turn_wait(g, slot);
}

/* ---- A hit's tests of landing --------------------------------------------------------------- */

// $C1:D160, $C1:DE87
/* An enemy of a kind the effect names (by its stat block's kind bits) turns
 * it away. */
bool effect_turned_away(GameState *g, uint8_t kinds)
{
    uint8_t kind = effect_target_stat(g, STAT_KIND);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, kind));
    bool away = effect_target(g) >= BATTLE_FIRST_ENEMY && (kinds & kind);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, away ? 1 : 0));
    return away;
}

/* A number scaled by `times` twentieths, and one more (the aim's and the
 * evasion's status changes). */
static uint16_t twentieths_and_one(GameState *g, uint8_t v, uint8_t times)
{
    return (uint16_t)(effect_over(g, effect_times(g, v, times), 0x14).quotient + 1);
}

// $C1:DE9E
/* The attacker's aim: a quarter as good while it is blind or confused, three
 * quarters while poisoned. */
static uint16_t aim_of(GameState *g, uint8_t aim)
{
    uint8_t statuses = effect_actor_stat(g, STAT_STATUS_1);
    if (statuses & (STATUS_BLIND | STATUS_CONFUSED))
        return twentieths_and_one(g, aim, 5);
    if (statuses & STATUS_POISON)
        return twentieths_and_one(g, aim, 0x0F);
    return aim;
}

/* The level bonus the evasion takes: the levels the attacker has over the
 * target divided by twenty, twice. The original comes into both divisions
 * with its carry set, which leaves the top bit of the high word set where one
 * divides: so the second divides a number of more than two billion, and its
 * quotient's low byte is what the bonus is -- no longer a bonus the attacker's
 * levels make, but a number the player can't predict, up to 255, that takes
 * the evasion to 255 where it goes past a byte. A difference of 0 divides
 * nothing, and the bonus is 0. */
enum { LEVELS_A_STEP = 0x14 };

static uint8_t level_bonus(GameState *g, uint8_t levels_over)
{
    uint16_t steps = battle_divide_leftover_marked(g, levels_over, LEVELS_A_STEP).quotient;
    return (uint8_t)battle_divide_leftover_marked(g, steps, LEVELS_A_STEP).quotient;
}

// $C1:DEF0
/* The target's evasion, raised or lowered by its statuses, then raised by the
 * level bonus to 255 at most. */
static uint16_t evasion_of(GameState *g, uint16_t evasion)
{
    uint8_t guarded = (uint8_t)(effect_target_stat(g, STAT_STATUS_3) | effect_target_stat(g, STAT_KEPT_STATUS_3));
    if (guarded & STATUS3_EVADES_MUCH_BETTER) {
        uint16_t half = (uint16_t)(effect_times(g, evasion, 5) >> 1);
        evasion = half >= 0xFF ? 0xFF : half;
    } else if (guarded & STATUS3_EVADES_BETTER) {
        evasion = effect_times(g, evasion, 2);
    } else {
        uint8_t statuses = effect_target_stat(g, STAT_STATUS_1);
        effect_leave_last_worked_byte(g, statuses);
        if (statuses & (STATUS_STOP | STATUS_ASLEEP)) {
            evasion = twentieths_and_one(g, (uint8_t)evasion, 4);
        } else if (statuses & (STATUS_BLIND | STATUS_CONFUSED)) {
            evasion = twentieths_and_one(g, (uint8_t)evasion, 0x0A);
        } else {
            if (statuses & STATUS_SLOW)
                evasion = twentieths_and_one(g, (uint8_t)evasion, 0x0F);
            if (statuses & STATUS_POISON)
                evasion = twentieths_and_one(g, (uint8_t)evasion, 0x0A);
        }
    }
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, evasion));
    uint8_t own = effect_actor_stat(g, STAT_LEVEL), theirs = effect_target_stat(g, STAT_LEVEL);
    if (own < theirs)
        return evasion;
    uint8_t bonus = level_bonus(g, (uint8_t)(own - theirs));
    if (bonus + (uint8_t)evasion > 0xFF) {
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

bool effect_lands(GameState *g, uint8_t aim_stat, uint8_t evasion_stat)
{
    uint8_t aim = effect_actor_stat(g, aim_stat), evade = effect_target_stat(g, evasion_stat);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, aim));
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, evade));
    uint16_t aimed = aim_of(g, aim);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, aimed));
    uint16_t evasion = evasion_of(g, evade);
    uint8_t a = (uint8_t)aimed, e = (uint8_t)evasion;
    if (a >= e) {
        a = SURE_AIM;
        e = SURE_EVASION;
        ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, a));
        ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_2, e));
    }
    uint16_t chance = effect_over(g, effect_times(g, a, PERCENT), e).quotient;
    uint8_t drawn = battle_random(g, 0, PERCENT);
    bool lands = (uint8_t)chance >= drawn;
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, (uint16_t)((chance & 0xFF00) | (lands ? 1 : 0))));
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_2, drawn));
    set_battle_effect_hit_test(g, 0);
    return lands;
}

/* ---- A hit's damage ------------------------------------------------------------------------ */

/* The random part a formula adds is drawn up to one past (or two past) a
 * share of a stat: about three tenths (77/256), a fifth (205/1024), or a tenth
 * of a word worked out as the original's shifts do (each part rounded down on
 * its own). The original leaves its last shift in $08, a number the enemies'
 * conditions can read, and the share in $30. */
static uint8_t about_three_tenths(GameState *g, uint8_t stat)
{
    battle_keep_word(g, BATTLE_NUMBER_TESTED, stat);
    uint8_t share = (uint8_t)((stat * 77u) >> 8);
    ORIGINAL_EFFECT(leave_working_word(g, share));
    return share;
}

static uint8_t about_a_fifth(GameState *g, uint8_t stat)
{
    battle_keep_word(g, BATTLE_NUMBER_TESTED, (uint16_t)(stat >> 2));
    uint8_t share = (uint8_t)((stat * 205u) >> 10);
    ORIGINAL_EFFECT(leave_working_word(g, share));
    return share;
}

uint16_t effect_about_a_tenth(GameState *g, uint16_t v)
{
    battle_keep_word(g, BATTLE_NUMBER_TESTED, (uint16_t)(v >> 10));
    uint16_t share = (uint16_t)((v >> 3) - (v >> 6) - (v >> 7) - (v >> 10));
    ORIGINAL_EFFECT(leave_working_word(g, share));
    return share;
}

/* The random part of the damage, up to one or two past a share (a byte). */
static void draw_variance(GameState *g, uint8_t share, uint8_t past)
{
    set_battle_damage_variance(g, battle_random(g, 0, (uint8_t)(share + past)));
}

/* The power times a sum of the attacker's stats, over a divisor. */
static uint16_t powered(GameState *g, uint8_t power, uint16_t sum, uint8_t divisor)
{
    return effect_over(g, effect_times(g, power, sum), divisor).quotient;
}

/* A formula's parts, each the attacker's stat times its weight, added up. */
static uint16_t weighted(GameState *g, uint8_t stat, uint8_t weight) { return effect_times(g, effect_actor_stat(g, stat), weight); }

/* The formulas that are hits wake the target first. */
static uint16_t formula(GameState *g, uint8_t kind, uint8_t power)
{
    if (kind != 3)
        effect_wake_target(g);
    switch (kind) {
    case 1: {
        /* the attacker's power and weapon */
        uint16_t power_part = weighted(g, STAT_POWER, 0x0C);
        battle_keep_stat_offset_word(g, power_part);
        uint16_t sum = (uint16_t)(weighted(g, STAT_WEAPON_ATTACK, 5) + power_part);
        ORIGINAL_EFFECT(leave_working_word(g, sum));
        uint16_t damage = powered(g, power, sum, 0x12);
        ORIGINAL_EFFECT(leave_working_word(g, damage));
        uint8_t third = (uint8_t)effect_over(g, effect_actor_stat(g, STAT_POWER), 3).quotient;
        draw_variance(g, third, 1);
        battle_set_wide_math_quotient(g, damage);
        return damage;
    }
    case 2: {
        /* hit and weapon */
        uint16_t hit_part = weighted(g, STAT_HIT, 8);
        battle_keep_stat_offset_word(g, hit_part);
        uint16_t sum = (uint16_t)(weighted(g, STAT_WEAPON_ATTACK, 8) + hit_part);
        ORIGINAL_EFFECT(leave_working_word(g, sum));
        uint16_t damage = powered(g, power, sum, 0x12);
        draw_variance(g, about_three_tenths(g, effect_actor_stat(g, STAT_HIT)), 1);
        return damage;
    }
    case 3: {
        /* level and magic */
        uint16_t level_part = weighted(g, STAT_LEVEL, 0x0C);
        battle_keep_stat_offset_word(g, level_part);
        uint16_t sum = (uint16_t)(weighted(g, STAT_MAGIC, 0x0C) + level_part);
        battle_keep_stat_offset_word(g, sum);
        uint16_t damage = powered(g, power, sum, 0x0F);
        draw_variance(g, about_three_tenths(g, effect_actor_stat(g, STAT_MAGIC)), 1);
        return damage;
    }
    case 4: {
        /* attack */
        uint16_t attack_part = weighted(g, STAT_ATTACK, 2);
        battle_keep_stat_offset_word(g, attack_part);
        uint16_t damage = powered(g, power, attack_part, 0x12);
        draw_variance(g, about_a_fifth(g, effect_actor_stat(g, STAT_ATTACK)), 2);
        return damage;
    }
    case 5: {
        /* power, and the level squared */
        uint16_t power_part = weighted(g, STAT_POWER, 0x10);
        battle_keep_stat_offset_word(g, power_part);
        uint8_t level = effect_actor_stat(g, STAT_LEVEL);
        uint16_t fifth = effect_over(g, effect_times(g, level, level), 5).quotient;
        uint16_t sum = (uint16_t)(fifth + power_part);
        ORIGINAL_EFFECT(leave_working_word(g, sum));
        uint16_t damage = powered(g, power, sum, 0x14);
        ORIGINAL_EFFECT(leave_working_word(g, damage));
        uint8_t third = (uint8_t)effect_over(g, level, 3).quotient;
        draw_variance(g, third, 1);
        battle_set_wide_math_quotient(g, damage);
        return damage;
    }
    case 6: {
        /* the HP the attacker has lost */
        uint16_t powered_stamina = effect_times(g, effect_actor_stat(g, STAT_STAMINA), power);
        uint16_t missing = (uint16_t)(effect_actor_stat_word(g, STAT_MAX_HP) - effect_actor_stat_word(g, STAT_HP));
        battle_keep_stat_offset_word(g, missing);
        uint16_t damage = effect_over(g, effect_times(g, missing, powered_stamina), 0x8C).quotient;
        draw_variance(g, (uint8_t)effect_about_a_tenth(g, missing), 1);
        return damage;
    }
    case 7: {
        /* power and another block's weapon */
        uint8_t strength = effect_actor_stat(g, STAT_POWER);
        uint16_t other = (uint16_t)(combatant_stat_byte_at_block(g, battle_effect_other_block(g), STAT_WEAPON_ATTACK)
                                    | combatant_stat_byte_at_block(g, battle_effect_other_block(g), STAT_WEAPON_ATTACK + 1) << 8);
        uint16_t sum = (uint16_t)((strength << 1) + other);
        ORIGINAL_EFFECT(leave_working_word(g, sum));
        uint16_t damage = powered(g, power, sum, 0x12);
        draw_variance(g, about_three_tenths(g, strength), 1);
        return damage;
    }
    default: {
        /* the weapon's: the attack of the block the element of the attacker's
         * attack names (the element's word read, its high byte kept) */
        uint8_t elements = effect_actor_stat(g, STAT_ELEMENTS);
        unsigned which = elements & 0x80 ? 0 : elements & 0x40 ? 1 : elements & 0x20 ? 2 : 3;
        uint16_t place = (uint16_t)(effect_actor_stat_word(g, (uint16_t)(STAT_ELEMENT_AFFINITY + which)) + battle_effect_other_block(g));
        uint16_t attack = (uint16_t)((place & 0xFF00) | combatant_stat_byte_at_block(g, place, STAT_WEAPON_ATTACK));
        ORIGINAL_EFFECT(leave_working_word(g, attack));
        uint16_t damage = powered(g, power, attack, 0x12);
        draw_variance(g, about_three_tenths(g, effect_actor_stat(g, STAT_POWER)), 1);
        return damage;
    }
    }
}

// $C1:E0F5
/* A hit's damage by its formula, then raised or lowered by the attacker's
 * statuses. Formula 8 works nothing out: its damage is whatever the long
 * arithmetic's quotient cell holds from the numbers worked out before -- the
 * hit's chance of landing, as the game plays. (Formulas 9 and 10 in the
 * original are never reached: 9 and up take the weapon's.) */
enum { FORMULA_LEFT_AS_IT_IS = 8 };

static uint16_t damage_worked_out(GameState *g, uint8_t kind, uint8_t power)
{
    uint16_t damage = kind == FORMULA_LEFT_AS_IT_IS ? battle_wide_math_quotient(g) : formula(g, kind, power);
    uint8_t strength = (uint8_t)(effect_actor_stat(g, STAT_STATUS_4) | effect_actor_stat(g, STAT_KEPT_STATUS_4));
    if (strength & STATUS4_HITS_HALF_AGAIN) {
        damage = (uint16_t)((damage >> 1) + damage);
        battle_set_wide_math_quotient(g, damage);
    } else {
        if (strength & STATUS4_HITS_HALF_AGAIN_ROUNDED)
            damage = effect_times(g, effect_over(g, damage, 2).quotient, 3);
        else if (strength & STATUS4_HITS_A_QUARTER_HARDER)
            damage = effect_times(g, effect_over(g, damage, 4).quotient, 5);
        uint8_t own = effect_actor_stat(g, STAT_STATUS_1);
        if (own & STATUS_POISON)
            damage = effect_over(g, effect_times(g, damage, 8), 0x0A).quotient;
        if (own & STATUS_SLOW)
            damage = effect_over(g, effect_times(g, damage, 8), 0x0A).quotient;
        if (own & STATUS_CONFUSED)
            damage = effect_over(g, damage, 4).quotient;
    }
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, damage));
    battle_keep_stat_offset_word(g, 0);
    return damage;
}

// $C1:E65F
/* The target's defence: the stat the effect names, $3E as it is and $3C two
 * and a half times; any other number is the defence itself. Its statuses
 * lower it, and the effect's own flags scale it by quarters; at most 255. */
enum { DEFENCE_STAT = 0x3E, DEFENCE_STAT_TIMES = 0x3C };

static uint8_t defence_of(GameState *g, uint8_t named, uint8_t quarters)
{
    uint16_t defence = named;
    if (named == DEFENCE_STAT) {
        defence = effect_target_stat(g, named);
    } else if (named == DEFENCE_STAT_TIMES) {
        defence = effect_over(g, effect_times(g, effect_target_stat(g, named), 5), 2).quotient;
    }
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, defence));
    uint8_t statuses = effect_target_stat(g, STAT_STATUS_1);
    effect_leave_last_worked_byte(g, statuses);
    if (statuses & (STATUS_STOP | STATUS_ASLEEP)) {
        defence = effect_over(g, effect_times(g, (uint8_t)defence, 0x10), 0x14).quotient;
    } else {
        if (statuses & STATUS_POISON)
            defence = effect_over(g, effect_times(g, (uint8_t)defence, 0x12), 0x14).quotient;
        if (statuses & (STATUS_BLIND | STATUS_CONFUSED))
            defence = effect_over(g, effect_times(g, (uint8_t)defence, 0x12), 0x14).quotient;
        if (statuses & STATUS_SLOW)
            defence = effect_over(g, effect_times(g, (uint8_t)defence, 0x12), 0x14).quotient;
    }
    if (quarters) {
        /* The original sets the defence's low byte alone in the first number's
         * cell: its high byte is what the arithmetic before left there (the
         * number it last worked on, as the game plays 0), and a defence over
         * 255 or with that byte set is 255. */
        uint16_t leftover_defence = (uint16_t)((battle_wide_math_first(g) & 0xFF00) | (uint8_t)defence);
        defence = (uint16_t)(effect_times(g, leftover_defence, quarters) >> 2);
    }
    if ((defence & 0x8000) || defence >= 0xFF)
        defence = 0xFF;
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, defence));
    return (uint8_t)defence;
}

HitAmounts effect_hit_amounts(GameState *g, const EffectRecord *e)
{
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, e->damage.formula));
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, e->damage.defence_stat));
    uint16_t damage = damage_worked_out(g, e->damage.formula, e->power);
    uint8_t defence = defence_of(g, e->damage.defence_stat, battle_effect_flags(g, 1) & EFFECT_DEFENCE_QUARTERS);
    return (HitAmounts){ damage, defence };
}

// $C1:DCAD
/* The hit's damage applied: its defence taken off (as a share of 256), the
 * variance added, at least 1; the modifiers; a barrier or a shield (not where
 * the effect passes it) takes it to seven tenths; at most 9999. A hit the
 * target absorbs heals it; otherwise it is recorded, and a hit that takes all
 * the target's HP fells it: an enemy is marked beaten, its turn timer stops,
 * and the enemies aiming at a party member forget what they chose. The
 * action's damage is added up as it goes. */
enum { LEAVES_ONE_HP = 0x01, ABSORBED = 0x40, ENEMY_ANIMATION_BEATEN = 0 };

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

/* The damage a barrier or a shield leaves. */
static void guarded_against(GameState *g, uint8_t guards)
{
    if (battle_action_state(g) & LEAVES_ONE_HP)
        return;
    uint8_t flags = battle_effect_flags(g, 0);
    bool barrier = !(flags & EFFECT_PASSES_BARRIER) && (guards & STATUS4_BARRIER);
    bool shield = !barrier && !(flags & EFFECT_PASSES_SHIELD) && (guards & STATUS4_SHIELD);
    if (barrier || shield)
        set_battle_effect_amount(g, effect_times(g, effect_over(g, battle_effect_amount(g), 0x0A).quotient, 7));
}

/* The target felled: an enemy marked beaten, its turn timer stopped; a party
 * member forgotten by the enemies aiming at it, which the original tells by
 * the target where the hit did damage, or else by the number's place. */
static void felled(GameState *g, uint8_t place)
{
    uint8_t aimed_at = place;
    if (battle_effect_amount(g) != 0) {
        uint8_t beaten = effect_listed_target(g);
        effect_leave_last_worked(g, beaten);
        aimed_at = beaten;
        effect_record_damage(g, beaten, DAMAGE_PLAIN);
        if (beaten >= BATTLE_FIRST_ENEMY)
            effect_mark_beaten(g, beaten);
    }
    uint8_t gone = effect_listed_target(g);
    stop_turn_timer(g, gone);
    if (gone < BATTLE_PARTY_SIZE)
        for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
            if (battle_enemy_target(g, enemy) == aimed_at)
                set_battle_enemy_last_condition(g, enemy, NO_CONDITION);
    set_battle_candidate_count(g, 1);
}

void effect_apply_hit(GameState *g, Platform *pf, HitAmounts hit)
{
    set_effect_target_stat(g, STAT_STATUS_1, effect_target_stat(g, STAT_STATUS_1) & (uint8_t)~STATUSES_A_HIT_ENDS);
    uint16_t kept = (uint16_t)(0x100 - hit.defence);
    uint16_t share = effect_over(g, effect_times(g, kept, hit.damage), 0x100).quotient;
    uint8_t variance = battle_damage_variance(g);
    uint16_t amount = (uint16_t)(share + variance);
    set_battle_effect_amount(g, amount ? amount : 1);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, 0));
    effect_apply_modifiers(g, pf);
    uint8_t guards = (uint8_t)(effect_target_stat(g, STAT_STATUS_4) | effect_target_stat(g, STAT_KEPT_STATUS_4));
    effect_leave_last_worked_byte(g, guards);
    guarded_against(g, guards);
    uint8_t place = effect_target_number(g);
    bool most = battle_effect_amount(g) >= MOST_DAMAGE;
    if (most)
        set_battle_effect_amount(g, MOST_DAMAGE);
    if (battle_action_state(g) & ABSORBED) {
        effect_record_damage(g, effect_listed_target(g), DAMAGE_HEALS);
        /* made with the carry of the compare with 9999 */
        ORIGINAL_EFFECT(battle_leave_multiply_carry(g, most));
        set_number_kind(g, place, NUMBER_HP_RESTORED);
        set_battle_action_state(g, battle_action_state(g) & (uint8_t)~ABSORBED);
        set_battle_effect_amount(g, (uint16_t)-battle_effect_amount(g));
    } else {
        uint16_t hp = effect_target_stat_word(g, STAT_HP);
        if (hp == 0)
            return;
        if (hp <= battle_effect_amount(g))
            felled(g, place);
        else
            effect_record_damage(g, effect_listed_target(g), DAMAGE_PLAIN);
        place = effect_target_number(g);
        set_number_kind(g, place, NUMBER_DAMAGE);
    }
    set_number_value(g, place, battle_effect_amount(g));
    set_battle_effect_total(g, (uint16_t)(battle_effect_total(g) + battle_effect_amount(g)));
}
