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
 * The entries and their guards are at the end. */
#include "battle/effect_parts.h"

#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/random.h"
#include "charge.h"
#include "port/native.h"

/* The effect being worked out, as the work copies it (battle_effect_record). */
static EffectRecord effect_in_work(const GameState *g, EffectKind kind)
{
    uint8_t bytes[EFFECT_RECORD_BYTES];
    for (unsigned n = 0; n < EFFECT_RECORD_BYTES; n++)
        bytes[n] = battle_effect_record(g, n);
    return effect_record_decode(bytes, (uint8_t)kind);
}

/* The effect's flags as the work goes (battle_effect_flags): taken from the
 * record, and made to pass a hit's tests where the action can't miss. */
static uint8_t effect_flags(const GameState *g) { return battle_effect_flags(g, 0); }

/* $80 once the effect missed the target it was at. */
enum { MISSED = 0x80, LANDED = 0x00 };

static void set_missed(GameState *g, bool missed) { set_battle_effect_missed(g, missed ? MISSED : LANDED); }

/* ---- Going through the targets ------------------------------------------------------------- */

/* The work keeps its combatants as stat blocks' offsets, which it works out
 * by multiplying (so the arithmetic's cells keep what that leaves). */
enum { STAT_BLOCK_BYTES = 0x80 };

// $C1:E9A3
static void take_actor(GameState *g) { set_battle_effect_actor_block(g, effect_times(g, STAT_BLOCK_BYTES, battle_actor(g))); }

// $C1:E9B8
/* The target the work is at: the last of the action's targets left. */
static void take_target(GameState *g)
{
    uint8_t target = effect_listed_target(g);
    set_battle_effect_target(g, target);
    set_battle_effect_target_block(g, effect_times(g, STAT_BLOCK_BYTES, target));
}

/* The next target, counting the targets left down in a byte: false once none
 * is. */
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
enum { NOTE_BYTES = 4 };

static void note_acted_on(GameState *g)
{
    unsigned slot = (uint8_t)(effect_listed_target(g) * NOTE_BYTES) / NOTE_BYTES;
    uint32_t acted = (uint32_t)battle_action_actor_bits(g) | (uint32_t)battle_action_tech_hits(g) << 8
                     | (uint32_t)battle_action_elements(g) << 16 | (uint32_t)battle_action_step(g) << 24;
    if (slot < BATTLE_CHOSEN_ACTIONS_COUNT)
        set_battle_chosen_actions(g, slot, acted);
    else
        set_battle_chosen_actions_unchecked(g, slot, acted);
}

/* A kind's work on each of the action's targets in turn, noted after each.
 * (The original takes each target after the first with the carry the note
 * leaves, set, which the multiply leaves in its cell.) */
typedef void TargetWork(GameState *g, const EffectRecord *e);

static void each_target(GameState *g, const EffectRecord *e, TargetWork *work)
{
    take_actor(g);
    bool first = true;
    do {
        take_target(g);
        ORIGINAL_EFFECT(battle_leave_multiply_carry(g, !first));
        work(g, e);
        note_acted_on(g);
        first = false;
    } while (another_target(g));
}

/* ---- The numbers the kinds take ------------------------------------------------------------ */

/* The kinds hand their numbers on in the direct page's cells, which the port
 * leaves as the original's loaders do: scaffolding. */
static void leave_numbers(GameState *g, uint8_t first, uint8_t second)
{
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, first));
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, second));
}

/* The record's second and third bytes, as a kind loads them ($C1:D14F). */
static void leave_record_numbers(GameState *g) { leave_numbers(g, battle_effect_record(g, 1), battle_effect_record(g, 2)); }

// $C1:D1B5
static void leave_misses(GameState *g, const EffectRecord *e) { ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, e->misses)); }

/* The effect missed the target: dodged, or the attacker's miss. */
static void miss(GameState *g, const EffectRecord *e, bool dodged)
{
    leave_misses(g, e);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_4, dodged ? 1 : 0));
    effect_missed(g, e->misses, dodged);
    set_missed(g, true);
}

// $C1:D132
/* Healing by the actor's magic. */
static Healing healing_by_magic(GameState *g, uint8_t multiple, uint8_t restores)
{
    Healing h = { effect_actor_stat(g, STAT_MAGIC), multiple, restores };
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

/* ---- The kinds ----------------------------------------------------------------------------- */

// $C1:D221
/* Restore each target's HP or MP by the actor's magic. */
static void restore_target(GameState *g, const EffectRecord *e)
{
    effect_heal(g, healing_by_magic(g, e->restore.multiple, e->restore.restores));
}

static void restore(GameState *g, const EffectRecord *e)
{
    each_target(g, e, restore_target);
    set_missed(g, false);
}

// $C1:D23E
/* Cure each target, reviving it where the effect says so, and heal it too. */
static void cure_target(GameState *g, const EffectRecord *e)
{
    leave_record_numbers(g);
    effect_cure(g, (e->cure.how & CURE_REVIVES) != 0);
    if (e->cure.how & CURE_HEALS)
        effect_heal(g, healing_by_magic(g, (uint8_t)(e->cure.how & MULTIPLE_BITS), e->cure.restores));
}

static void cure(GameState *g, const EffectRecord *e)
{
    each_target(g, e, cure_target);
    set_missed(g, false);
}

// $C1:D267
/* Give each target a status, where the effect can't miss or a percent drawn
 * is at most its chance: the record's base chance, and the actor's magic over
 * its divisor. */
enum { PERCENT = 100 };

static bool status_lands(GameState *g, const EffectRecord *e)
{
    uint8_t base = e->landing.base_chance, divisor = e->landing.magic_divisor;
    leave_numbers(g, base, divisor);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_3, base));
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_4, divisor));
    uint8_t part = (uint8_t)effect_over(g, effect_actor_stat(g, STAT_MAGIC), divisor).quotient;
    ORIGINAL_EFFECT(battle_leave_add_overflow8(part, base));
    uint8_t chance = (uint8_t)(part + base);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, chance));
    if (battle_random(g, 0, PERCENT) <= chance)
        return true;
    /* the original tells a dodge by its fourth cell, which holds the divisor */
    leave_misses(g, e);
    effect_missed(g, e->misses, divisor == 1);
    set_missed(g, true);
    return false;
}

static void inflict_on_target(GameState *g, const EffectRecord *e)
{
    if (!(effect_flags(g) & EFFECT_CANT_MISS) && !status_lands(g, e))
        return;
    leave_record_numbers(g);
    effect_inflict_status(g, e->inflict.status_byte, e->inflict.statuses);
    set_missed(g, false);
}

static void inflict(GameState *g, const EffectRecord *e) { each_target(g, e, inflict_on_target); }

// $C1:D2DE
/* A hit on one target: turned away by its kind, or missing it, unless the
 * effect says it can't be; its damage worked out and applied; then its chance
 * of a status. (A hit that steals is the original's.) */
static void hit_target(GameState *g, const EffectRecord *e)
{
    if (!(effect_flags(g) & EFFECT_CANT_BE_TURNED_AWAY) && effect_turned_away(g, e->kinds_turned_away)) {
        miss(g, e, true);
        return;
    }
    if (!(effect_flags(g) & EFFECT_CANT_MISS) && !effect_lands(g, e->aim_stat, e->evasion_stat)) {
        miss(g, e, false);
        return;
    }
    leave_numbers(g, e->damage.formula, e->damage.defence_stat);
    effect_apply_hit(g, effect_hit_amounts(g, e));
    if (effect_flags(g) & EFFECT_GIVES_STATUS) {
        leave_record_numbers(g);
        effect_status_chance(g, e->hit.chance, e->hit.statuses);
    }
    set_missed(g, false);
}

/* The action hits each of its targets in turn, unless it stops at its first
 * (battle_action_state bit 1, an enemy's attack). The count of targets left
 * is kept from going below 0. */
enum { STOPS_AT_FIRST = 0x02 };

static void hit(GameState *g, const EffectRecord *e)
{
    take_actor(g);
    for (bool first = true;; first = false) {
        take_target(g);
        ORIGINAL_EFFECT(battle_leave_multiply_carry(g, !first));
        hit_target(g, e);
        note_acted_on(g);
        if (battle_action_state(g) & STOPS_AT_FIRST)
            return;
        int8_t left = (int8_t)(battle_action_target_count(g) - 1);
        set_battle_action_target_count(g, left < 0 ? 0 : (uint8_t)left);
        if (left <= 0)
            return;
    }
}

// $C1:D371
/* Drain each target: its HP where the effect says so, its MP where it says
 * so, a share of its most; the HP's numbers shown in the first set, the MP's
 * in the next. */
static void drain_target(GameState *g, const EffectRecord *e)
{
    leave_record_numbers(g);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_5, e->drain.what));
    set_battle_effect_tally(g, 0);
    if (e->drain.what & DRAINS_HP) {
        ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, DRAIN_HP));
        effect_drain(g, DRAIN_HP, e->drain.share);
        set_battle_effect_tally(g, (uint8_t)(battle_effect_tally(g) + 1));
    }
    if (e->drain.what & DRAINS_MP) {
        ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, DRAIN_MP));
        ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, 0x14));
        effect_drain(g, DRAIN_MP, e->drain.share);
    }
}

static void drain(GameState *g, const EffectRecord *e)
{
    leave_record_numbers(g);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_6, e->drain.what));
    each_target(g, e, drain_target);
    set_missed(g, false);
}

// $C1:D3F4
/* Restore the targets, and show the actor's HP as restored in the first set
 * of numbers. */
static void restore_showing_hp(GameState *g, const EffectRecord *e)
{
    TIMED_AS(g, 0xC1D221, restore(g, e));
    uint16_t hp = effect_actor_stat_word(g, STAT_HP);
    set_battle_effect_amount(g, hp);
    effect_record_damage(g, battle_actor(g), DAMAGE_PLAIN);
    set_battle_effect_tally(g, 0);
    set_effect_number(g, effect_target_number(g), hp, NUMBER_HP_RESTORED);
    set_missed(g, false);
}

// $C1:D431
/* Hit the targets again and again, the record's number of times: once one
 * hit lands the rest can't miss. Each hit's numbers are a set of their own;
 * the damage of them all is recorded for the last target. */
static void hit_repeatedly(GameState *g, const EffectRecord *e)
{
    leave_record_numbers(g);
    set_battle_hits_left(g, e->repeated.hits);
    set_battle_effect_tally(g, 0);
    set_battle_damage_hit(g, 0);
    set_missed(g, true);
    set_battle_effect_target_count(g, battle_action_target_count(g));
    do {
        if (battle_effect_missed(g) == LANDED)
            set_battle_effect_flags(g, 0, EFFECT_CANT_MISS);
        set_battle_action_target_count(g, battle_effect_target_count(g));
        set_battle_effect_modifier_kept(g, battle_effect_modifier(g));
        TIMED_AS(g, 0xC1D2DE, hit(g, e));
        set_battle_effect_modifier(g, battle_effect_modifier_kept(g));
        set_battle_effect_tally(g, (uint8_t)(battle_effect_tally(g) + 1));
        set_battle_hits_left(g, (uint8_t)(battle_hits_left(g) - 1));
    } while (battle_hits_left(g) != 0);
    set_battle_effect_amount(g, battle_effect_total(g));
    effect_record_damage(g, battle_effect_target(g), DAMAGE_PLAIN);
    /* made with the carry of the count's compare, set */
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, true));
    note_acted_on(g);
}

// $C1:D490
/* An item restores each target's HP or MP. */
static void item_restore_target(GameState *g, const EffectRecord *e)
{
    (void)e;
    effect_heal(g, healing_by_item(g));
}

static void item_restore(GameState *g, const EffectRecord *e)
{
    each_target(g, e, item_restore_target);
    set_missed(g, false);
}

// $C1:D4AD
/* An item cures each target, and heals it too where it says so. */
static void item_cure_target(GameState *g, const EffectRecord *e)
{
    (void)e;
    uint8_t how = item_healing_byte(g, battle_action_detail(g), ITEM_POWER);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, how));
    effect_cure(g, (how & CURE_REVIVES) != 0);
    if (how & CURE_HEALS)
        effect_heal(g, healing_by_item(g));
}

static void item_cure(GameState *g, const EffectRecord *e)
{
    each_target(g, e, item_cure_target);
    set_missed(g, false);
}

// $C1:D4D6
/* An item gives each target a status. */
static void item_status_target(GameState *g, const EffectRecord *e)
{
    (void)e;
    uint8_t item = battle_action_detail(g);
    uint8_t which = item_healing_byte(g, item, ITEM_HEALS), bits = item_healing_byte(g, item, ITEM_AMOUNT);
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, which));
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_2, bits));
    effect_inflict_status(g, which, bits);
    set_missed(g, false);
}

static void item_status(GameState *g, const EffectRecord *e) { each_target(g, e, item_status_target); }

// $C1:D4F3
/* The target loses its condition's bit 6. */
enum { CONDITION_BIT_LOST = 0x40 };

static void lose_condition_bit(GameState *g)
{
    take_target(g);
    set_effect_target_stat(g, STAT_CONDITION, effect_target_stat(g, STAT_CONDITION) & (uint8_t)~CONDITION_BIT_LOST);
    set_missed(g, false);
}

// $C1:D9D7
/* The kinds, each timed as its own routine. Those from 9 to $28 and 4 do
 * nothing; the steal is the original's. */
void effect_run_kind(GameState *g, EffectKind kind)
{
    EffectRecord e = effect_in_work(g, kind);
    switch (kind) {
    case EFFECT_RESTORE: TIMED_AS(g, 0xC1D221, restore(g, &e)); break;
    case EFFECT_CURE: TIMED_AS(g, 0xC1D23E, cure(g, &e)); break;
    case EFFECT_INFLICT: TIMED_AS(g, 0xC1D267, inflict(g, &e)); break;
    case EFFECT_HIT: TIMED_AS(g, 0xC1D2DE, hit(g, &e)); break;
    case EFFECT_DRAIN: TIMED_AS(g, 0xC1D371, drain(g, &e)); break;
    case EFFECT_RESTORE_SHOWING_HP: TIMED_AS(g, 0xC1D3F4, restore_showing_hp(g, &e)); break;
    case EFFECT_HIT_REPEATEDLY: TIMED_AS(g, 0xC1D431, hit_repeatedly(g, &e)); break;
    case EFFECT_ITEM_RESTORE: TIMED_AS(g, 0xC1D490, item_restore(g, &e)); break;
    case EFFECT_ITEM_CURE: TIMED_AS(g, 0xC1D4AD, item_cure(g, &e)); break;
    case EFFECT_ITEM_STATUS: TIMED_AS(g, 0xC1D4D6, item_status(g, &e)); break;
    case EFFECT_LOSE_CONDITION_BIT: TIMED_AS(g, 0xC1D4F3, lose_condition_bit(g)); break;
    default: break;
    }
}

/* ---- Working the action out ---------------------------------------------------------------- */

// $C1:EB68
/* The hits' numbers added up: each set's enemies' numbers, from the `sets`th
 * down, added into the set before, so that the first set shows them all. A
 * miss stays a miss; otherwise the kind is the last one added. The sum is at
 * most 9999. */
enum { FIRST_ENEMY_PLACE = 0x0C, SET_BYTES = 0x2C, RECORD_BYTES = 4, MOST_SHOWN = 9999, NO_NUMBER = 0 };

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

/* A number added into the one before it: the kind moves with it unless the
 * one before is a miss, or it has none. */
static void add_number_into(GameState *g, unsigned from, unsigned to)
{
    ORIGINAL_EFFECT(battle_leave_add_overflow16(number_value(g, from), number_value(g, to)));
    set_number_value(g, to, (uint16_t)(number_value(g, to) + number_value(g, from)));
    uint16_t kind = number_kind_word(g, from), before = number_kind_word(g, to);
    if (kind != NO_NUMBER && (kind == NUMBER_MISS || before != NUMBER_MISS))
        set_number_kind_word(g, to, kind);
}

/* What the original's compares leave as they choose, which its next set's
 * multiply takes in its carry (scaffolding: the last one's shows). */
static bool compares_leave_carry(const GameState *g, unsigned from, unsigned to)
{
    uint16_t kind = number_kind_word(g, from), before = number_kind_word(g, to);
    return kind == NO_NUMBER || kind == NUMBER_MISS || before >= NUMBER_MISS;
}

static void add_up_hits(GameState *g, uint8_t sets)
{
    bool compared = false, carry_into_last = false;
    for (uint8_t place = FIRST_ENEMY_PLACE; place < SET_BYTES; place = (uint8_t)(place + RECORD_BYTES))
        for (uint8_t set = sets; set != 0; set--) {
            ORIGINAL_EFFECT(carry_into_last = set != sets && compared);
            unsigned from = (effect_times(g, set, SET_BYTES) + place) / RECORD_BYTES, to = from - SET_BYTES / RECORD_BYTES;
            ORIGINAL_EFFECT(compared = compares_leave_carry(g, from, to));
            add_number_into(g, from, to);
        }
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, carry_into_last));
    for (uint8_t place = FIRST_ENEMY_PLACE; place < SET_BYTES; place = (uint8_t)(place + RECORD_BYTES))
        if (number_value(g, place / RECORD_BYTES) >= MOST_SHOWN)
            set_number_value(g, place / RECORD_BYTES, MOST_SHOWN);
    ORIGINAL_EFFECT(battle_leave_word(g, 0x10, SET_BYTES));
    ORIGINAL_EFFECT(battle_leave_word(g, 0x12, sets));
}

/* The work's start: the damage table clear, nothing done yet. The clearing
 * leaves the carry set, which the record's multiply takes. */
static ActionRecord start_work(GameState *g, ActionTable table)
{
    TIMED_AS(g, 0xFDACFD, battle_clear_damage_table(g));
    set_battle_effect_total(g, 0);
    set_battle_action_record_place(g, effect_times(g, battle_action_detail(g), ACTION_RECORD_BYTES));
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, true));
    set_battle_damage_hit(g, 0);
    set_battle_effects_worked(g, 0);
    set_battle_effect_sure(g, 0);
    set_battle_effect_tally(g, 0);
    ActionRecord action = action_record(g, table, battle_action_detail(g));
    if (action.flags & ACTION_SURE)
        set_battle_effect_sure(g, ACTION_SURE);
    set_battle_effect_target_count(g, battle_action_target_count(g));
    set_battle_effect_number(g, action.effect);
    ORIGINAL_EFFECT(battle_leave_overflow(false)); /* the record's place added up */
    return action;
}

/* The effect's record copied where the kinds read it, its modifier and flags
 * taken; an action that can't miss makes its effect so. A tech's record says
 * whether it is quiet. */
enum { SURE_NOW = 0x80 | ACTION_SURE };

static void take_effect(GameState *g, ActionTable table, const ActionRecord *action)
{
    set_battle_effect_record_place(g, effect_times(g, action->effect, EFFECT_RECORD_BYTES));
    for (unsigned n = 0; n < EFFECT_RECORD_BYTES; n++)
        set_battle_effect_record(g, n, effect_record_byte(g, table, action->effect, n));
    set_battle_effect_modifier(g, action->modifier);
    // $C1:EB0E
    set_battle_effect_flags(g, 0, battle_effect_record(g, 7));
    set_battle_effect_flags(g, 1, battle_effect_record(g, 8));
    if (battle_effect_sure(g) & SURE_NOW) {
        set_battle_effect_flags(g, 0, effect_flags(g) | EFFECT_CANT_MISS);
        set_battle_effect_sure(g, battle_effect_sure(g) & 0x7F);
    }
    if (table == ENEMY_TECHS)
        set_battle_action_quiet(g, action->elements);
    set_battle_effect_hit_test(g, 0);
    for (unsigned slot = 0; slot < BATTLE_EFFECT_GIVEN_STATUS_COUNT; slot++)
        set_battle_effect_given_status(g, slot, 0);
}

void effect_work_out_enemy_action(GameState *g, ActionTable table)
{
    ActionRecord action = start_work(g, table);
    if (action.effect != NO_EFFECT) {
        take_effect(g, table, &action);
        effect_run_kind(g, (EffectKind)battle_effect_record(g, 0));
        if ((battle_effect_missed(g) & MISSED) && (action.flags & ACTION_STOPS_AT_MISS))
            goto noted;
    }
    set_battle_damage_hit(g, (uint8_t)(battle_damage_hit(g) + 1));
    set_battle_effects_worked(g, (uint8_t)(battle_effects_worked(g) + 1));
    uint8_t added = action.flags & ACTION_HITS_ADDED;
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, added));
    if (added)
        add_up_hits(g, added);
noted:
    set_battle_action_actor_bits(g, 0);
    set_battle_action_tech_hits(g, 0);
    set_battle_action_elements(g, 0);
    set_battle_action_step(g, 0);
}

// $FD:AAD2, $FD:AB01
void effect_note_enemy_action(GameState *g, ActionTable table)
{
    set_battle_action_record_place(g, effect_times(g, battle_action_detail(g), ACTION_RECORD_BYTES));
    ActionRecord action = action_record(g, table, battle_action_detail(g));
    set_battle_action_actor_bits(g, action.marks);
    set_battle_action_tech_hits(g, battle_action_detail(g));
    set_battle_action_elements(g, action.elements);
    set_battle_action_step(g, 0);
}

/* ---- The entries --------------------------------------------------------------------------- */

/* What the port works out: a kind in the table but the steal, a hit that
 * doesn't steal, modifiers the port has (battle/effect_modifiers.c), and a
 * drain whose stat offset's high byte is clear -- the original reads the
 * offset as a word from the cell it hands the drain's kind over in, whose
 * high byte ($1B) nothing in the effect code sets and the game leaves clear.
 * The rest is turned away to the original. */
enum { DRAIN_KIND_HIGH = EFFECT_ARG_3 + 1, WEAPON_MODIFIER_ON = 0x01 };

static bool modifiers_ported(const GameState *g, uint8_t own, uint8_t more_flags)
{
    if (own != 0 && !effect_modifier_ported(g, own))
        return false;
    if (!(more_flags & EFFECT_WITH_WEAPON_MODIFIER) || !(effect_actor_stat(g, STAT_WEAPON_MODIFIER_ON) & WEAPON_MODIFIER_ON))
        return true;
    return effect_modifier_ported(g, effect_actor_stat(g, STAT_WEAPON_MODIFIER));
}

static bool kind_ported(const GameState *g, uint8_t kind, uint8_t flags, uint8_t more_flags, uint8_t modifier)
{
    switch (kind) {
    case EFFECT_STEAL: return false;
    case EFFECT_HIT:
    case EFFECT_HIT_REPEATEDLY: return !(flags & EFFECT_STEALS) && modifiers_ported(g, modifier, more_flags);
    case EFFECT_DRAIN: return battle_low_scratch(g, DRAIN_KIND_HIGH) == 0;
    default: return kind < EFFECT_KINDS;
    }
}

/* The actor and the targets listed are combatants, one to eleven of them. */
static bool combatants_named(const GameState *g)
{
    uint8_t count = battle_action_target_count(g);
    bool named = battle_actor(g) < BATTLE_COMBATANTS && count != 0 && count <= BATTLE_ACTION_TARGETS_COUNT;
    for (uint8_t place = 0; named && place < count; place++)
        named = battle_action_targets(g, place) < BATTLE_COMBATANTS;
    return named;
}

static bool action_ported(const GameState *g, ActionTable table)
{
    ActionRecord action = action_record(g, table, battle_action_detail(g));
    if (action.effect == NO_EFFECT)
        return true;
    return kind_ported(g, effect_record_byte(g, table, action.effect, 0), effect_record_byte(g, table, action.effect, 7),
                       effect_record_byte(g, table, action.effect, 8), action.modifier);
}

static bool work_out_ready(Snes *s, ActionTable table)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ready = combatants_named(g) && action_ported(g, table);
    vars_peek(false);
    return battle_on_page(s) && ready;
}

bool battle_work_out_enemy_tech_ready(Snes *s) { return work_out_ready(s, ENEMY_TECHS); }
bool battle_work_out_enemy_attack_ready(Snes *s) { return work_out_ready(s, ENEMY_ATTACKS); }

/* The work hands the overflow of its last add on to the action's showing
 * (battle/overflow.h). */
static void work_out_entry(Snes *s, ActionTable table)
{
    ORIGINAL_EFFECT(battle_forget_overflow());
    effect_work_out_enemy_action(state_of(s), table);
    ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_V, battle_overflow_left(m_flag(s, CPU_FLAG_V))));
    m_rts(s);
}

void battle_work_out_enemy_tech(Snes *s) { work_out_entry(s, ENEMY_TECHS); }
void battle_work_out_enemy_attack(Snes *s) { work_out_entry(s, ENEMY_ATTACKS); }

/* Each kind as its own routine, for the party's code that runs them, on the
 * effect record the party's code copied. */
static bool kind_ready(Snes *s, EffectKind kind)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ready = combatants_named(g)
                 && kind_ported(g, kind, battle_effect_flags(g, 0), battle_effect_flags(g, 1), battle_effect_modifier(g));
    vars_peek(false);
    return battle_on_page(s) && ready;
}

/* The kinds hand back A clear but its low byte, and the carry of the last
 * note's loop (set), or the last number's place, which is found without
 * one; the one that loses a condition's bit, the carry of its multiply
 * (clear), and takes its target with the carry its caller had. A hit leaves Y
 * past the note's bytes, which the party's code reads. */
static void kind_entry(Snes *s, EffectKind kind)
{
    bool carry_in = m_flag(s, CPU_FLAG_C);
    effect_run_kind(state_of(s), kind);
    if (kind == EFFECT_LOSE_CONDITION_BIT)
        ORIGINAL_EFFECT(battle_leave_multiply_carry(state_of(s), carry_in));
    m_set_flag(s, CPU_FLAG_C, kind != EFFECT_RESTORE_SHOWING_HP && kind != EFFECT_LOSE_CONDITION_BIT);
    s->cpu.a &= 0x00FF;
    s->cpu.y = NOTE_BYTES;
    m_rts(s);
}

#define KIND_ENTRY(name, kind) \
    void name(Snes *s) { kind_entry(s, kind); } \
    bool name##_ready(Snes *s) { return kind_ready(s, kind); }
KIND_ENTRY(battle_effect_restore, EFFECT_RESTORE)
KIND_ENTRY(battle_effect_cure, EFFECT_CURE)
KIND_ENTRY(battle_effect_inflict, EFFECT_INFLICT)
KIND_ENTRY(battle_effect_hit, EFFECT_HIT)
KIND_ENTRY(battle_effect_drain, EFFECT_DRAIN)
KIND_ENTRY(battle_effect_restore_showing_hp, EFFECT_RESTORE_SHOWING_HP)
KIND_ENTRY(battle_effect_hit_repeatedly, EFFECT_HIT_REPEATEDLY)
KIND_ENTRY(battle_effect_item_restore, EFFECT_ITEM_RESTORE)
KIND_ENTRY(battle_effect_item_cure, EFFECT_ITEM_CURE)
KIND_ENTRY(battle_effect_item_status, EFFECT_ITEM_STATUS)
KIND_ENTRY(battle_effect_lose_condition_bit, EFFECT_LOSE_CONDITION_BIT)

/* The notes of the enemy's action, as the enemy's turn calls them: the carry
 * they come in with is left in the multiply's cell. */
static void note_entry(Snes *s, ActionTable table)
{
    GameState *g = state_of(s);
    effect_note_enemy_action(g, table);
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, m_flag(s, CPU_FLAG_C)));
    m_rtl(s);
}

void battle_note_enemy_tech(Snes *s) { note_entry(s, ENEMY_TECHS); }
void battle_note_enemy_attack(Snes *s) { note_entry(s, ENEMY_ATTACKS); }
