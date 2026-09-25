/* Battle: working an action's effect out on its targets (battle/effect_work.h).
 *
 * An enemy's attack or tech names an effect; its record's kind says what the
 * effect does to each target, and the rest of the record the numbers the kind
 * takes. The work goes through the action's targets from the last listed to
 * the first, and after each notes for the target what acted on it (its
 * chosen action's record, which the enemies' scripts test). A tally of the
 * numbers shown over the combatants adds the hits' up where the action says so.
 *
 * The kinds' pieces are battle/effect_amounts.c (healing, draining, a hit's
 * landing and damage), battle/effect_statuses.c and battle/effect_modifiers.c.
 * The machine's side -- the entries, and the host that makes the original's
 * calls from inside the work -- is at the end. */
#include "battle/effect_parts.h"

#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/random.h"
#include "charge.h"
#include "port/native.h"

/* ---- The effect's record ---------------------------------------------------------------------- */

/* The bytes of the effect's record (battle_effect_record) the kinds read: the
 * two numbers every kind takes, a hit's aim and evasion stats, its formula
 * and defence stat, the kinds of enemy that turn it away, and its miss marks.
 * Its flags are battle_effect_flags. */
enum {
    RECORD_KIND = 0, RECORD_FIRST = 1, RECORD_SECOND = 2, RECORD_AIM_STAT = 3, RECORD_EVASION_STAT = 4,
    RECORD_FORMULA = 5, RECORD_DEFENCE_STAT = 6, RECORD_FLAGS = 7, RECORD_MORE_FLAGS = 8,
    RECORD_KINDS_TURNED_AWAY = 10, RECORD_MISSES = 11,
};

static uint8_t record(const GameState *g, unsigned n) { return battle_effect_record(g, n); }

/* The effect's flags (its record's eighth byte): a hit that can't be turned
 * away, one that can't miss, a hit's chance of a status, and its chance to
 * steal. */
enum { CANT_BE_TURNED_AWAY = 0x10, CANT_MISS = 0x20, HIT_GIVES_STATUS = 0x04, HIT_STEALS = 0x02 };

static uint8_t effect_flags(const GameState *g) { return battle_effect_flags(g, 0); }

/* $80 once the effect missed the target it was at. */
enum { MISSED = 0x80, LANDED = 0x00 };

static void set_missed(GameState *g, bool missed) { set_battle_effect_missed(g, missed ? MISSED : LANDED); }

/* ---- Going through the targets ------------------------------------------------------------- */

/* The work keeps its combatants as stat blocks' offsets, which it works out
 * by multiplying (so the arithmetic's cells keep what that leaves). */
enum { STAT_BLOCK_BYTES = 0x80 };

// $C1:E9A3
static void take_actor(GameState *g, bool carry)
{
    set_battle_effect_actor_block(g, effect_times(g, STAT_BLOCK_BYTES, battle_actor(g), carry));
}

// $C1:E9B8
/* The target the work is at: the last of the action's targets left. */
static void take_target(GameState *g, bool carry)
{
    uint8_t target = effect_listed_target(g);
    set_battle_effect_target(g, target);
    set_battle_effect_target_block(g, effect_times(g, STAT_BLOCK_BYTES, target, carry));
}

/* The next target, counting the targets left down in a byte: false once none
 * is. The loops' first target is taken with the carry the actor's block
 * leaves (clear), the others with the one noting the last leaves (set). */
static bool another_target(GameState *g)
{
    uint8_t left = (uint8_t)(battle_action_target_count(g) - 1);
    set_battle_action_target_count(g, left);
    return left != 0;
}

// $C1:D508
/* What acted on the target is noted as its chosen action's record: the
 * action's marks, detail, elements and step (battle_action_actor_bits on). The
 * original finds the record by the target's slot times four in a byte. */
enum { Y_AFTER_NOTING = 4 };

static void note_acted_on(EffectWork *w)
{
    GameState *g = w->g;
    w->machine_y = Y_AFTER_NOTING;
    unsigned slot = (uint8_t)(effect_listed_target(g) << 2) / 4;
    uint32_t acted = (uint32_t)battle_action_actor_bits(g) | (uint32_t)battle_action_tech_hits(g) << 8
                     | (uint32_t)battle_action_elements(g) << 16 | (uint32_t)battle_action_step(g) << 24;
    if (slot < BATTLE_CHOSEN_ACTIONS_COUNT)
        set_battle_chosen_actions(g, slot, acted);
    else
        set_battle_chosen_actions_unchecked(g, slot, acted);
}

/* ---- The numbers the kinds take ------------------------------------------------------------ */

/* The kinds hand their numbers on in the direct page's cells, which the port
 * leaves as the original's loaders do: scaffolding. */
static void leave_numbers(GameState *g, uint8_t first, uint8_t second)
{
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, first));
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, second));
}

// $C1:D14F
static void leave_record_numbers(GameState *g) { leave_numbers(g, record(g, RECORD_FIRST), record(g, RECORD_SECOND)); }

// $C1:D1B5
static uint8_t misses(GameState *g)
{
    uint8_t marks = record(g, RECORD_MISSES);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, marks));
    return marks;
}

/* The effect missed the target: dodged, or the attacker's miss. */
static void miss(EffectWork *w, bool dodged)
{
    GameState *g = w->g;
    uint8_t marks = misses(g);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_4, dodged ? 1 : 0));
    w->machine_v = effect_missed(g, marks, dodged);
    set_missed(g, true);
}

// $C1:D132
/* Healing by the actor's magic: the multiple in the first number's low five
 * bits, what it restores in the second. */
enum { STAT_MAGIC = 0x39, MULTIPLE_BITS = 0x1F };

static Healing healing_by_magic(GameState *g)
{
    Healing h = { effect_actor_stat(g, STAT_MAGIC), (uint8_t)(record(g, RECORD_FIRST) & MULTIPLE_BITS), record(g, RECORD_SECOND) };
    leave_numbers(g, h.power, h.multiple);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_3, h.restores));
    return h;
}

// $C1:D1D3
/* Healing by the item used (battle_action_detail). */
enum { ITEM_POWER_BITS = 0x3F, ITEM_AMOUNT_BITS = 0x0F };

static Healing healing_by_item(GameState *g)
{
    uint8_t item = battle_action_detail(g);
    Healing h = { (uint8_t)(item_healing_byte(g, item, ITEM_POWER) & ITEM_POWER_BITS),
                  (uint8_t)(item_healing_byte(g, item, ITEM_AMOUNT) & ITEM_AMOUNT_BITS), item_healing_byte(g, item, ITEM_HEALS) };
    leave_numbers(g, h.power, h.multiple);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_3, h.restores));
    return h;
}

/* What a cure does: bit 7 revives, bit 6 heals as well. */
enum { CURE_REVIVES = 0x80, CURE_HEALS = 0x40 };

/* ---- The kinds ----------------------------------------------------------------------------- */

// $C1:D221
/* Restore each target's HP or MP by the actor's magic. */
static bool restore(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    take_actor(g, carry);
    bool again = false;
    do {
        take_target(g, again);
        effect_heal(w, healing_by_magic(g));
        note_acted_on(w);
        again = true;
    } while (another_target(g));
    set_missed(g, false);
    return true;
}

// $C1:D23E
/* Cure each target, reviving it where the effect says so, and heal it too. */
static bool cure(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    take_actor(g, carry);
    bool again = false;
    do {
        take_target(g, again);
        leave_record_numbers(g);
        uint8_t how = record(g, RECORD_FIRST);
        effect_cure(g, (how & CURE_REVIVES) != 0);
        if (how & CURE_HEALS)
            effect_heal(w, healing_by_magic(g));
        note_acted_on(w);
        again = true;
    } while (another_target(g));
    set_missed(g, false);
    return true;
}

// $C1:D267
/* Give each target a status (the first number the status byte, the second
 * its bits), where the effect can't miss or a percent drawn is at most its
 * chance: the formula byte, and the actor's magic over the defence byte. */
enum { PERCENT = 100 };

static bool status_lands(EffectWork *w)
{
    GameState *g = w->g;
    uint8_t base = record(g, RECORD_FORMULA), divisor = record(g, RECORD_DEFENCE_STAT);
    leave_numbers(g, base, divisor);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_3, base));
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_4, divisor));
    uint8_t part = (uint8_t)effect_over(g, effect_actor_stat(g, STAT_MAGIC), divisor, false).quotient;
    uint8_t chance = (uint8_t)(part + base);
    w->machine_v = overflow8(part, base);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, chance));
    uint8_t drawn = battle_random(g, 0, PERCENT);
    if (!battle_is_demo(g))
        w->machine_v = false; /* the draw's own add leaves no overflow */
    if (drawn <= chance)
        return true;
    /* the original tells a dodge by the fourth cell, which holds the divisor */
    w->machine_v = effect_missed(g, misses(g), divisor == 1);
    set_missed(g, true);
    return false;
}

static bool inflict(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    take_actor(g, carry);
    bool again = false;
    do {
        take_target(g, again);
        if ((effect_flags(g) & CANT_MISS) || status_lands(w)) {
            leave_record_numbers(g);
            uint8_t left = effect_inflict_status(g, record(g, RECORD_FIRST), record(g, RECORD_SECOND), &w->machine_v);
            ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, left));
            set_missed(g, false);
        }
        note_acted_on(w);
        again = true;
    } while (another_target(g));
    return true;
}

// $C1:D2DE
/* A hit on one target: turned away by its kind, or missing it, unless the
 * effect says it can't be; its damage worked out and applied; then its chance
 * of a status and of stealing, where it has them. */
static void hit_target(EffectWork *w)
{
    GameState *g = w->g;
    if (!(effect_flags(g) & CANT_BE_TURNED_AWAY) && effect_turned_away(w, record(g, RECORD_KINDS_TURNED_AWAY))) {
        miss(w, true);
        return;
    }
    if (!(effect_flags(g) & CANT_MISS) && !effect_lands(w, record(g, RECORD_AIM_STAT), record(g, RECORD_EVASION_STAT))) {
        miss(w, false);
        return;
    }
    leave_numbers(g, record(g, RECORD_FORMULA), record(g, RECORD_DEFENCE_STAT));
    HitAmounts amounts = effect_hit_amounts(w, record(g, RECORD_FORMULA), record(g, RECORD_DEFENCE_STAT));
    effect_step_in(w, STEP_APPLY_DAMAGE);
    effect_apply_hit(w, amounts);
    effect_step_out(w);
    if (w->stopped)
        return;
    if (effect_flags(g) & HIT_GIVES_STATUS) {
        leave_record_numbers(g);
        w->first_left = effect_status_chance(g, record(g, RECORD_FIRST), record(g, RECORD_SECOND), &w->machine_v);
        w->machine_y &= 0x00FF;
        /* a status given starts its timer, which indexes by the target's
         * stat block in Y */
        if (w->first_left == INFLICTED)
            w->machine_y = (uint16_t)(effect_target(g) * STAT_BLOCK_BYTES);
    }
    if (effect_flags(g) & HIT_STEALS) {
        effect_step_in(w, STEP_STEAL_IN_HIT);
        effect_steal(w, w->first_left);
        effect_step_out(w);
        if (w->stopped)
            return;
    }
    set_missed(g, false);
}

/* The action hits each of its targets in turn, unless it is an attack that
 * stops at its first (battle_action_state bit 1). The count of targets left
 * is kept from going below 0. */
enum { STOPS_AT_FIRST = 0x02 };

static bool hit(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    take_actor(g, carry);
    bool again = false;
    for (;;) {
        take_target(g, again);
        hit_target(w);
        if (w->stopped)
            return false;
        note_acted_on(w);
        if (battle_action_state(g) & STOPS_AT_FIRST)
            break;
        uint8_t left = (uint8_t)(battle_action_target_count(g) - 1);
        set_battle_action_target_count(g, left);
        if (left == 0)
            break;
        if (left & 0x80) {
            set_battle_action_target_count(g, 0);
            break;
        }
        again = true;
    }
    return true;
}

// $C1:D371
/* Drain each target: its HP where the first number has bit 7, its MP where
 * bit 6, a share of the second number twentieths of its maximum; the HP's
 * numbers shown in the first set, the MP's in the next. */
enum { DRAINS_HP = 0x80, DRAINS_MP = 0x40, MP_DRAIN_CELL = 0x14 };

static bool drain(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    leave_record_numbers(g);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_6, record(g, RECORD_FIRST)));
    take_actor(g, carry);
    bool again = false;
    do {
        take_target(g, again);
        leave_record_numbers(g);
        uint8_t what = record(g, RECORD_FIRST);
        ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_5, what));
        set_battle_effect_tally(g, 0);
        if (what & DRAINS_HP) {
            ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, DRAIN_HP));
            effect_drain(w, DRAIN_HP, record(g, RECORD_SECOND));
            set_battle_effect_tally(g, (uint8_t)(battle_effect_tally(g) + 1));
        }
        if (what & DRAINS_MP) {
            ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, DRAIN_MP));
            ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, MP_DRAIN_CELL));
            effect_drain(w, DRAIN_MP, record(g, RECORD_SECOND));
        }
        note_acted_on(w);
        again = true;
    } while (another_target(g));
    set_missed(g, false);
    return true;
}

// $C1:D3BF
/* Steal from the first target, a chance of the first number. */
static bool steal(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    take_actor(g, carry);
    take_target(g, false);
    leave_record_numbers(g);
    effect_step_in(w, STEP_STEAL_EFFECT);
    uint8_t outcome = effect_steal(w, record(g, RECORD_FIRST));
    effect_step_out(w);
    if (w->stopped)
        return false;
    if (outcome == STEAL_MISSED)
        miss(w, false);
    else if (outcome == STEAL_NOTHING)
        set_missed(g, true);
    note_acted_on(w);
    set_missed(g, false);
    return true;
}

// $C1:D3F4
/* Restore the targets, and show the actor's HP as restored in the first set
 * of numbers. */
static bool restore_showing_hp(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    TIMED_AS(g, 0xC1D221, restore(w, carry));
    set_battle_effect_amount(g, effect_actor_stat_word(g, STAT_HP));
    effect_record_damage(g, battle_actor(g), DAMAGE_PLAIN, true);
    set_battle_effect_tally(g, 0);
    bool carry_left;
    uint8_t place = effect_target_number_carrying(g, &carry_left, &w->machine_v);
    set_effect_number(g, place, battle_effect_amount(g), NUMBER_HP_RESTORED);
    set_missed(g, false);
    return carry_left;
}

// $C1:D431
/* Hit the targets again and again, the first number of times: once one hit
 * lands the rest can't miss. Each hit's numbers are a set of their own; the
 * damage of them all is recorded for the last target. */
static bool hit_repeatedly(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    (void)carry;
    leave_record_numbers(g);
    set_battle_hits_left(g, record(g, RECORD_FIRST));
    set_battle_effect_tally(g, 0);
    set_battle_damage_hit(g, 0);
    set_missed(g, true);
    set_battle_effect_target_count(g, battle_action_target_count(g));
    do {
        if (battle_effect_missed(g) == LANDED)
            set_battle_effect_flags(g, 0, CANT_MISS);
        set_battle_action_target_count(g, battle_effect_target_count(g));
        set_battle_effect_modifier_kept(g, battle_effect_modifier(g));
        effect_step_in(w, STEP_HIT_AGAIN);
        TIMED_AS(g, 0xC1D2DE, hit(w, true));
        effect_step_out(w);
        if (w->stopped)
            return false;
        set_battle_effect_modifier(g, battle_effect_modifier_kept(g));
        set_battle_effect_tally(g, (uint8_t)(battle_effect_tally(g) + 1));
        set_battle_hits_left(g, (uint8_t)(battle_hits_left(g) - 1));
    } while (battle_hits_left(g) != 0);
    set_battle_effect_amount(g, battle_effect_total(g));
    w->machine_v = effect_record_damage(g, battle_effect_target(g), DAMAGE_PLAIN, true);
    note_acted_on(w);
    return true;
}

// $C1:D490
/* An item restores each target's HP or MP. */
static bool item_restore(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    take_actor(g, carry);
    bool again = false;
    do {
        take_target(g, again);
        effect_heal(w, healing_by_item(g));
        note_acted_on(w);
        again = true;
    } while (another_target(g));
    set_missed(g, false);
    return true;
}

// $C1:D4AD
/* An item cures each target, and heals it too where it says so. */
static bool item_cure(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    take_actor(g, carry);
    bool again = false;
    do {
        take_target(g, again);
        uint8_t how = item_healing_byte(g, battle_action_detail(g), ITEM_POWER);
        ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, how));
        effect_cure(g, (how & CURE_REVIVES) != 0);
        if (how & CURE_HEALS)
            effect_heal(w, healing_by_item(g));
        note_acted_on(w);
        again = true;
    } while (another_target(g));
    set_missed(g, false);
    return true;
}

// $C1:D4D6
/* An item gives each target a status. */
static bool item_status(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    take_actor(g, carry);
    bool again = false;
    do {
        take_target(g, again);
        uint8_t item = battle_action_detail(g);
        uint8_t which = item_healing_byte(g, item, ITEM_HEALS), bits = item_healing_byte(g, item, ITEM_AMOUNT);
        ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, which));
        ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_2, bits));
        effect_inflict_status(g, which, bits, &w->machine_v);
        set_missed(g, false);
        note_acted_on(w);
        again = true;
    } while (another_target(g));
    return true;
}

// $C1:D4F3
/* The target loses its condition's bit 6. */
enum { CONDITION_BIT_LOST = 0x40 };

static bool lose_condition_bit(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    take_target(g, carry);
    set_effect_target_stat(g, STAT_CONDITION, effect_target_stat(g, STAT_CONDITION) & (uint8_t)~CONDITION_BIT_LOST);
    set_missed(g, false);
    return false;
}

// $C1:D370, $C1:D48F
static bool nothing(EffectWork *w, bool carry)
{
    (void)w;
    return carry;
}

/* ---- The kinds' table ---------------------------------------------------------------------- */

typedef bool (*EffectKind)(EffectWork *w, bool carry);

typedef struct {
    EffectKind work;
    uint32_t routine;
} KindRow;

// $C1:D9D7
/* The kinds from 9 to $28 do nothing, as 4 does: their rows are empty. */

static const KindRow kinds[EFFECT_KINDS] = {
    [0x00] = { restore, 0xC1D221 },
    [0x01] = { cure, 0xC1D23E },
    [0x02] = { inflict, 0xC1D267 },
    [0x03] = { hit, 0xC1D2DE },
    [0x05] = { drain, 0xC1D371 },
    [0x06] = { steal, 0xC1D3BF },
    [0x07] = { restore_showing_hp, 0xC1D3F4 },
    [0x08] = { hit_repeatedly, 0xC1D431 },
    [0x29] = { item_restore, 0xC1D490 },
    [0x2A] = { item_cure, 0xC1D4AD },
    [0x2B] = { item_status, 0xC1D4D6 },
    [0x2C] = { lose_condition_bit, 0xC1D4F3 },
};

bool effect_run_kind(EffectWork *w, uint8_t kind, bool carry)
{
    const KindRow *row = &kinds[kind];
    if (!row->work)
        return nothing(w, carry);
    bool carry_left = carry;
    TIMED_AS(w->g, row->routine, carry_left = row->work(w, carry));
    return carry_left;
}

/* ---- Working the action out ---------------------------------------------------------------- */

// $C1:EB0E
static void take_effect_flags(GameState *g)
{
    set_battle_effect_flags(g, 0, record(g, RECORD_FLAGS));
    set_battle_effect_flags(g, 1, record(g, RECORD_MORE_FLAGS));
}

// $C1:EB68
/* The hits' numbers added up: each set's enemies' numbers, from the `sets`th
 * down, added into the set before, so that the first set shows them all; a
 * miss in any shows as one, otherwise the kind is the last one added. The sum
 * is at most 9999. */
enum { FIRST_ENEMY_PLACE = 0x0C, SET_BYTES = 0x2C, RECORD_BYTES = 4, MOST_SHOWN = 9999 };

static uint16_t number_kind_word(const GameState *g, unsigned record)
{
    if (record < BATTLE_NUMBER_RECORDS_COUNT)
        return (uint16_t)(battle_number_records_kind(g, record) | battle_number_records_last_byte(g, record) << 8);
    return (uint16_t)(battle_number_records_kind_unchecked(g, record) | battle_number_records_last_byte_unchecked(g, record) << 8);
}
static void set_number_kind_word(GameState *g, unsigned record, uint16_t v)
{
    if (record < BATTLE_NUMBER_RECORDS_COUNT) {
        set_battle_number_records_kind(g, record, (uint8_t)v);
        set_battle_number_records_last_byte(g, record, (uint8_t)(v >> 8));
    } else {
        set_battle_number_records_kind_unchecked(g, record, (uint8_t)v);
        set_battle_number_records_last_byte_unchecked(g, record, (uint8_t)(v >> 8));
    }
}
static uint16_t number_value(const GameState *g, unsigned record)
{
    return record < BATTLE_NUMBER_RECORDS_COUNT ? battle_number_records_value(g, record)
                                                : battle_number_records_value_unchecked(g, record);
}
static void set_number_value(GameState *g, unsigned record, uint16_t v)
{
    if (record < BATTLE_NUMBER_RECORDS_COUNT)
        set_battle_number_records_value(g, record, v);
    else
        set_battle_number_records_value_unchecked(g, record, v);
}

static bool add_up_hits(GameState *g, uint8_t sets, bool carry)
{
    bool overflow = false;
    for (uint8_t place = FIRST_ENEMY_PLACE; place < SET_BYTES; place = (uint8_t)(place + RECORD_BYTES)) {
        for (uint8_t set = sets; set != 0; set--) {
            unsigned from = (effect_times(g, set, SET_BYTES, carry) + place) / RECORD_BYTES, to = from - SET_BYTES / RECORD_BYTES;
            overflow = overflow16(number_value(g, from), number_value(g, to));
            set_number_value(g, to, (uint16_t)(number_value(g, to) + number_value(g, from)));
            uint16_t kind = number_kind_word(g, from), before = number_kind_word(g, to);
            if (kind == 0 || before == NUMBER_MISS) {
                carry = true;
            } else if (kind == NUMBER_MISS) {
                set_number_kind_word(g, to, kind);
                carry = true;
            } else {
                carry = before > NUMBER_MISS;
                set_number_kind_word(g, to, kind);
            }
        }
        carry = false;
    }
    for (uint8_t place = FIRST_ENEMY_PLACE; place < SET_BYTES; place = (uint8_t)(place + RECORD_BYTES))
        if (number_value(g, place / RECORD_BYTES) >= MOST_SHOWN)
            set_number_value(g, place / RECORD_BYTES, MOST_SHOWN);
    ORIGINAL_EFFECT(battle_leave_word(g, 0x10, SET_BYTES));
    ORIGINAL_EFFECT(battle_leave_word(g, 0x12, sets));
    return overflow;
}

/* The work's start: the damage table clear, nothing done yet. The clearing
 * leaves the carry set, which the first multiply takes. */
static void start_work(GameState *g, ActionTable table, ActionRecord *action)
{
    TIMED_AS(g, 0xFDACFD, battle_clear_damage_table(g));
    set_battle_effect_total(g, 0);
    uint16_t place = effect_times(g, battle_action_detail(g), ACTION_RECORD_BYTES, true);
    set_battle_action_record_place(g, place);
    set_battle_damage_hit(g, 0);
    set_battle_effects_worked(g, 0);
    set_battle_effect_sure(g, 0);
    set_battle_effect_tally(g, 0);
    *action = action_record(g, table, battle_action_detail(g));
    if (action->flags & ACTION_SURE)
        set_battle_effect_sure(g, ACTION_SURE);
    set_battle_effect_target_count(g, battle_action_target_count(g));
}

/* The effect's record copied where the kinds read it, its modifier and flags
 * taken; an action that can't miss makes its effect so. A tech's record says
 * whether it is quiet. */
enum { SURE_NOW = 0x80 | ACTION_SURE };

static void take_effect(GameState *g, ActionTable table, const ActionRecord *action)
{
    set_battle_effect_record_place(g, effect_times(g, action->effect, EFFECT_RECORD_BYTES, false));
    for (unsigned n = 0; n < EFFECT_RECORD_BYTES; n++)
        set_battle_effect_record(g, n, effect_record_byte(g, table, action->effect, n));
    set_battle_effect_modifier(g, action->modifier);
    take_effect_flags(g);
    if (battle_effect_sure(g) & SURE_NOW) {
        set_battle_effect_flags(g, 0, effect_flags(g) | CANT_MISS);
        set_battle_effect_sure(g, battle_effect_sure(g) & 0x7F);
    }
    if (table == ENEMY_TECHS)
        set_battle_action_quiet(g, action->elements);
    set_battle_effect_hit_test(g, 0);
    for (unsigned slot = 0; slot < BATTLE_EFFECT_GIVEN_STATUS_COUNT; slot++)
        set_battle_effect_given_status(g, slot, 0);
}

void effect_work_out_enemy_action(EffectWork *w, ActionTable table)
{
    GameState *g = w->g;
    ActionRecord action;
    start_work(g, table, &action);
    w->machine_v = false;
    set_battle_effect_number(g, action.effect);
    bool carry = true;
    if (action.effect != NO_EFFECT) {
        take_effect(g, table, &action);
        uint8_t kind = record(g, RECORD_KIND);
        w->machine_y = BATTLE_EFFECT_GIVEN_STATUS_COUNT;
        if (kind >= EFFECT_KINDS) {
            w->routine = kind;
            if (!w->host->call(w, table == ENEMY_TECHS ? CALL_UNLISTED_TECH_KIND : CALL_UNLISTED_ATTACK_KIND)) {
                w->stopped = true;
                return;
            }
            carry = w->carry_back;
        } else {
            effect_step_in(w, table == ENEMY_TECHS ? STEP_DISPATCH_TECH : STEP_DISPATCH_ATTACK);
            carry = effect_run_kind(w, kind, false);
            effect_step_out(w);
            if (w->stopped)
                return;
        }
        if ((battle_effect_missed(g) & MISSED) && (action.flags & ACTION_STOPS_AT_MISS))
            goto noted;
    }
    set_battle_damage_hit(g, (uint8_t)(battle_damage_hit(g) + 1));
    set_battle_effects_worked(g, (uint8_t)(battle_effects_worked(g) + 1));
    uint8_t added = action.flags & ACTION_HITS_ADDED;
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, added));
    if (added)
        w->machine_v = add_up_hits(g, added, carry);
noted:
    set_battle_action_actor_bits(g, 0);
    set_battle_action_tech_hits(g, 0);
    set_battle_action_elements(g, 0);
    set_battle_action_step(g, 0);
}

// $FD:AAD2, $FD:AB01
void effect_note_enemy_action(GameState *g, ActionTable table, bool carry)
{
    set_battle_action_record_place(g, effect_times(g, battle_action_detail(g), ACTION_RECORD_BYTES, carry));
    ActionRecord action = action_record(g, table, battle_action_detail(g));
    set_battle_action_actor_bits(g, action.marks);
    set_battle_action_tech_hits(g, battle_action_detail(g));
    set_battle_action_elements(g, action.elements);
    set_battle_action_step(g, 0);
}

/* ---- The machine's side -------------------------------------------------------------------- */

/* Where each step of the work was called from in the original: its JSR's
 * return, as the call pushes it. */
static const uint16_t step_returns[] = {
    [STEP_DISPATCH_TECH] = 0xD895,     [STEP_DISPATCH_ATTACK] = 0xD99B, [STEP_HIT_AGAIN] = 0xD464,
    [STEP_APPLY_DAMAGE] = 0xD337,      [STEP_MODIFIERS] = 0xDCFF,       [STEP_ACTION_MODIFIER] = 0xEA7C,
    [STEP_WEAPON_MODIFIER] = 0xEA94,   [STEP_STEAL_EFFECT] = 0xD3CA,    [STEP_STEAL_IN_HIT] = 0xD34E,
    [STEP_STEAL_MODIFIER] = 0xEA22,    [STEP_MODIFIER_ROUTINE] = 0xEB47,
};

/* The controller's reading saves Y on the stack and hands the flags back as
 * it found them (the carry going on into the work); the item's giving reads
 * the carry (set by the test for no item before it). */
enum { READS_Y_AND_CARRY = DC_A | DC_X | DC_N | DC_V | DC_Z, READS_CARRY = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z };

/* A kind or a modifier's routine past its table is called where the
 * original's table points past its end, as its JSR through the table makes
 * the call: the kind doubled in X (A cleared), the routine doubled in A and X
 * in a byte, with the registers as the original has them; nothing of them is
 * declared unread, the callee being whatever the bytes there are. */
enum { TECH_DISPATCH = 0xC1D893, ATTACK_DISPATCH = 0xC1D999, MODIFIER_DISPATCH = 0xC1EB45 };
enum { KIND_TABLE = 0xC1D9D7, MODIFIER_TABLE = 0xC1FA61, THIS_BANK = 0xC10000 };

static bool call_past_table(EffectWork *w, uint32_t site, uint32_t entry, uint16_t x, uint16_t a, bool carry)
{
    Snes *s = w->host_data;
    uint32_t routine = THIS_BANK | rom_read16(w->g, entry);
    s->cpu.x = x;
    s->cpu.a = a;
    s->cpu.y = w->machine_y;
    m_set_flag(s, CPU_FLAG_C, carry);
    bool back = battle_call(s, site, routine, 0);
    if (back)
        w->carry_back = m_flag(s, CPU_FLAG_C);
    return back;
}

static bool call_original(EffectWork *w, EffectCall call)
{
    Snes *s = w->host_data;
    unsigned depth = w->depth < EFFECT_STEPS_MAX ? w->depth : EFFECT_STEPS_MAX;
    for (unsigned n = 0; n < depth; n++)
        m_push16(s, step_returns[w->steps[n]]);
    bool back;
    switch (call) {
    case CALL_READ_CONTROLLER:
        s->cpu.y = w->machine_y;
        m_set_flag(s, CPU_FLAG_C, w->carry_back);
        back = battle_call_long(s, 0xC1EE27, 0xC28002, READS_Y_AND_CARRY);
        if (back)
            w->carry_back = m_flag(s, CPU_FLAG_C);
        break;
    case CALL_GIVE_STOLEN_ITEM:
        m_set_flag(s, CPU_FLAG_C, true);
        back = battle_call_long(s, 0xC1EA5D, 0xFDB655, READS_CARRY);
        break;
    case CALL_UNLISTED_TECH_KIND:
    case CALL_UNLISTED_ATTACK_KIND: {
        uint16_t doubled = (uint16_t)(w->routine << 1);
        back = call_past_table(w, call == CALL_UNLISTED_TECH_KIND ? TECH_DISPATCH : ATTACK_DISPATCH, KIND_TABLE + doubled, doubled,
                               0, false);
        break;
    }
    default: {
        uint8_t doubled = (uint8_t)(w->routine << 1);
        back = call_past_table(w, MODIFIER_DISPATCH, MODIFIER_TABLE + doubled, doubled, doubled, (w->routine & 0x80) != 0);
        break;
    }
    }
    if (back)
        for (unsigned n = 0; n < depth; n++)
            m_pull16(s);
    return back;
}

static const EffectHost machine_host = { call_original };

static EffectWork work_for(Snes *s)
{
    return (EffectWork){ .g = state_of(s), .host = &machine_host, .host_data = s, .machine_y = s->cpu.y };
}

static void work_out_entry(Snes *s, ActionTable table)
{
    EffectWork w = work_for(s);
    effect_work_out_enemy_action(&w, table);
    if (w.stopped)
        return;
    /* the showing after it takes the overflow flag along */
    m_set_flag(s, CPU_FLAG_V, w.machine_v);
    m_rts(s);
}

void battle_work_out_enemy_tech(Snes *s) { work_out_entry(s, ENEMY_TECHS); }
void battle_work_out_enemy_attack(Snes *s) { work_out_entry(s, ENEMY_ATTACKS); }

/* Each kind as its own routine, for the party's code that runs them: the
 * carry in and out as the original's, A's high byte clear, and Y as it
 * leaves it (read after a hit). */
static void kind_entry(Snes *s, uint8_t kind)
{
    EffectWork w = work_for(s);
    bool carry = effect_run_kind(&w, kind, m_flag(s, CPU_FLAG_C));
    if (w.stopped)
        return;
    m_set_flag(s, CPU_FLAG_C, carry);
    s->cpu.a &= 0x00FF;
    s->cpu.y = w.machine_y;
    m_rts(s);
}

/* The kinds work on the actor and the targets listed as combatants, one or
 * more of them, as the game hands them over. */
bool battle_effect_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t count = battle_action_target_count(g);
    bool named = battle_actor(g) < BATTLE_COMBATANTS && count != 0 && count <= BATTLE_ACTION_TARGETS_COUNT;
    for (uint8_t place = 0; named && place < count; place++)
        named = battle_action_targets(g, place) < BATTLE_COMBATANTS;
    vars_peek(false);
    return battle_on_page(s) && named;
}

void battle_effect_restore(Snes *s) { kind_entry(s, 0x00); }
void battle_effect_cure(Snes *s) { kind_entry(s, 0x01); }
void battle_effect_inflict(Snes *s) { kind_entry(s, 0x02); }
void battle_effect_hit(Snes *s) { kind_entry(s, 0x03); }
void battle_effect_drain(Snes *s) { kind_entry(s, 0x05); }
void battle_effect_steal(Snes *s) { kind_entry(s, 0x06); }
void battle_effect_restore_showing_hp(Snes *s) { kind_entry(s, 0x07); }
void battle_effect_hit_repeatedly(Snes *s) { kind_entry(s, 0x08); }
void battle_effect_item_restore(Snes *s) { kind_entry(s, 0x29); }
void battle_effect_item_cure(Snes *s) { kind_entry(s, 0x2A); }
void battle_effect_item_status(Snes *s) { kind_entry(s, 0x2B); }
void battle_effect_lose_condition_bit(Snes *s) { kind_entry(s, 0x2C); }

/* The notes of the enemy's action, as the enemy's turn calls them (the carry
 * it enters with goes into the multiply's leftovers). */
void battle_note_enemy_tech(Snes *s)
{
    effect_note_enemy_action(state_of(s), ENEMY_TECHS, m_flag(s, CPU_FLAG_C));
    m_rtl(s);
}
void battle_note_enemy_attack(Snes *s)
{
    effect_note_enemy_action(state_of(s), ENEMY_ATTACKS, m_flag(s, CPU_FLAG_C));
    m_rtl(s);
}
