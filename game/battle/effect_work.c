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
 * landing and damage), battle/effect_statuses.c, battle/effect_modifiers.c
 * and battle/effect_steal.c. What the port works out is checked at the end. */
#include "battle/effect_parts.h"

#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/random.h"
#include "charge.h"

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

/* A target after the first, taken as the first is. */
static void take_next_target(GameState *g)
{
    take_target(g);
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

/* A kind's work on each of the action's targets in turn, noted after each. */
typedef void TargetWork(GameState *g, const EffectRecord *e);

static void each_target(GameState *g, const EffectRecord *e, TargetWork *work)
{
    take_actor(g);
    take_target(g);
    for (;;) {
        work(g, e);
        note_acted_on(g);
        if (!another_target(g))
            return;
        take_next_target(g);
    }
}

/* The record's second and third bytes, as a kind loads them ($C1:D14F). */
static void charge_record_numbers(GameState *g) { charge_legacy_accesses(g, 2); }

/* The effect missed the target: dodged, or the attacker's miss. */
static void miss(GameState *g, const EffectRecord *e, bool dodged)
{
    effect_missed(g, e->misses, dodged);
    set_missed(g, true);
}

// $C1:D132
/* Healing by the actor's magic. */
static Healing healing_by_magic(GameState *g, uint8_t multiple, uint8_t restores)
{
    Healing h = { effect_actor_stat(g, STAT_MAGIC), multiple, restores };
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
    charge_record_numbers(g);
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
    uint8_t part = (uint8_t)effect_over(g, effect_actor_stat(g, STAT_MAGIC), divisor).quotient;
    uint8_t chance = (uint8_t)(part + base);
    if (battle_random(g, 0, PERCENT) <= chance)
        return true;
    /* the original tells a dodge by its fourth cell, which holds the divisor */
    effect_missed(g, e->misses, divisor == 1);
    set_missed(g, true);
    return false;
}

static void inflict_on_target(GameState *g, const EffectRecord *e)
{
    if (!(effect_flags(g) & EFFECT_CANT_MISS) && !status_lands(g, e))
        return;
    charge_record_numbers(g);
    effect_inflict_status(g, e->inflict.status_byte, e->inflict.statuses);
    set_missed(g, false);
}

static void inflict(GameState *g, const EffectRecord *e) { each_target(g, e, inflict_on_target); }

// $C1:D2DE
/* A hit on one target: turned away by its kind, or missing it, unless the
 * effect says it can't be; its damage worked out and applied; then its chance
 * of a status. (A hit that steals as well is refused.) */
static void hit_target(GameState *g, Platform *pf, const EffectRecord *e)
{
    if (!(effect_flags(g) & EFFECT_CANT_BE_TURNED_AWAY) && effect_turned_away(g, e->kinds_turned_away)) {
        miss(g, e, true);
        return;
    }
    if (!(effect_flags(g) & EFFECT_CANT_MISS) && !effect_lands(g, e->aim_stat, e->evasion_stat)) {
        miss(g, e, false);
        return;
    }
    effect_apply_hit(g, pf, effect_hit_amounts(g, e));
    if (effect_flags(g) & EFFECT_GIVES_STATUS) {
        charge_record_numbers(g);
        effect_status_chance(g, e->hit.chance, e->hit.statuses);
    }
    set_missed(g, false);
}

/* The action hits each of its targets in turn, unless it stops at its first
 * (battle_action_state bit 1, an enemy's attack). The count of targets left
 * is kept from going below 0. */

static void hit(GameState *g, Platform *pf, const EffectRecord *e)
{
    take_actor(g);
    take_target(g);
    for (;;) {
        hit_target(g, pf, e);
        note_acted_on(g);
        if (battle_action_state(g) & ACTION_STOPS_AT_FIRST)
            return;
        int8_t left = (int8_t)(battle_action_target_count(g) - 1);
        set_battle_action_target_count(g, left < 0 ? 0 : (uint8_t)left);
        if (left <= 0)
            return;
        take_next_target(g);
    }
}

// $C1:D371
/* Drain each target: its HP where the effect says so, its MP where it says
 * so, a share of its most; the HP's numbers shown in the first set, the MP's
 * in the next. */
static void drain_target(GameState *g, const EffectRecord *e)
{
    charge_record_numbers(g);
    set_battle_effect_tally(g, 0);
    if (e->drain.what & DRAINS_HP) {
        effect_drain(g, DRAIN_HP, e->drain.share);
        set_battle_effect_tally(g, (uint8_t)(battle_effect_tally(g) + 1));
    }
    if (e->drain.what & DRAINS_MP) {
        effect_drain(g, DRAIN_MP, e->drain.share);
    }
}

static void drain(GameState *g, const EffectRecord *e)
{
    charge_record_numbers(g);
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

// $C1:D3BF
/* Steal from the target (the action's last listed, the one alone): a miss
 * shows as the attacker's miss, and nothing to take as a miss without a
 * number. */
static void steal(GameState *g, const EffectRecord *e)
{
    take_actor(g);
    take_target(g);
    charge_record_numbers(g);
    StealOutcome outcome = effect_steal(g, e->steal.chance);
    if (outcome == STEAL_MISSED)
        miss(g, e, false);
    else if (outcome == STEAL_NOTHING)
        set_missed(g, true);
    note_acted_on(g);
    set_missed(g, false);
}

// $C1:D431
/* Hit the targets again and again, the record's number of times: once one
 * hit lands the rest can't miss. Each hit's numbers are a set of their own;
 * the damage of them all is recorded for the last target. The original makes
 * the rest sure by setting the effect's flags to "can't miss" alone, so they
 * lose the rest of what the flags gave the first hit (the status chance,
 * passing a barrier or a shield, not being turned away): the game plays it so,
 * and so does the port. */
static void hit_repeatedly(GameState *g, Platform *pf, const EffectRecord *e)
{
    charge_record_numbers(g);
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
        TIMED_AS(g, 0xC1D2DE, hit(g, pf, e));
        set_battle_effect_modifier(g, battle_effect_modifier_kept(g));
        set_battle_effect_tally(g, (uint8_t)(battle_effect_tally(g) + 1));
        set_battle_hits_left(g, (uint8_t)(battle_hits_left(g) - 1));
    } while (battle_hits_left(g) != 0);
    set_battle_effect_amount(g, battle_effect_total(g));
    effect_record_damage(g, battle_effect_target(g), DAMAGE_PLAIN);
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
 * nothing. */
void effect_run_kind(GameState *g, Platform *pf, EffectKind kind)
{
    EffectRecord e = effect_in_work(g, kind);
    switch (kind) {
    case EFFECT_RESTORE: TIMED_AS(g, 0xC1D221, restore(g, &e)); break;
    case EFFECT_CURE: TIMED_AS(g, 0xC1D23E, cure(g, &e)); break;
    case EFFECT_INFLICT: TIMED_AS(g, 0xC1D267, inflict(g, &e)); break;
    case EFFECT_HIT: TIMED_AS(g, 0xC1D2DE, hit(g, pf, &e)); break;
    case EFFECT_DRAIN: TIMED_AS(g, 0xC1D371, drain(g, &e)); break;
    case EFFECT_STEAL: TIMED_AS(g, 0xC1D3BF, steal(g, &e)); break;
    case EFFECT_RESTORE_SHOWING_HP: TIMED_AS(g, 0xC1D3F4, restore_showing_hp(g, &e)); break;
    case EFFECT_HIT_REPEATEDLY: TIMED_AS(g, 0xC1D431, hit_repeatedly(g, pf, &e)); break;
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
    set_number_value(g, to, (uint16_t)(number_value(g, to) + number_value(g, from)));
    uint16_t kind = number_kind_word(g, from), before = number_kind_word(g, to);
    if (kind != NO_NUMBER && (kind == NUMBER_MISS || before != NUMBER_MISS))
        set_number_kind_word(g, to, kind);
}

/* Each later set's numbers added into the first's, enemy place by place, and
 * each capped at what can be shown. */
void effect_add_up_hits(GameState *g, uint8_t sets)
{
    charge_legacy_accesses(g, 2);
    for (uint8_t place = FIRST_ENEMY_PLACE; place < SET_BYTES; place = (uint8_t)(place + RECORD_BYTES))
        for (uint8_t set = sets; set != 0; set--) {
            unsigned from = (effect_times(g, set, SET_BYTES) + place) / RECORD_BYTES, to = from - SET_BYTES / RECORD_BYTES;
            add_number_into(g, from, to);
        }
    for (uint8_t place = FIRST_ENEMY_PLACE; place < SET_BYTES; place = (uint8_t)(place + RECORD_BYTES))
        if (number_value(g, place / RECORD_BYTES) >= MOST_SHOWN)
            set_number_value(g, place / RECORD_BYTES, MOST_SHOWN);
}

/* The work's start: the damage table clear, nothing done yet. */
static ActionRecord start_work(GameState *g, ActionTable table)
{
    TIMED_AS(g, 0xFDACFD, battle_clear_damage_table(g));
    set_battle_effect_total(g, 0);
    set_battle_action_record_place(g, effect_times(g, battle_action_detail(g), ACTION_RECORD_BYTES));
    set_battle_damage_hit(g, 0);
    set_battle_effects_worked(g, 0);
    set_battle_effect_sure(g, 0);
    set_battle_effect_tally(g, 0);
    ActionRecord action = action_record(g, table, battle_action_detail(g));
    if (action.flags & ACTION_SURE)
        set_battle_effect_sure(g, ACTION_SURE);
    set_battle_effect_target_count(g, battle_action_target_count(g));
    set_battle_effect_number(g, action.effect);
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

void effect_work_out_enemy_action(GameState *g, Platform *pf, ActionTable table)
{
    ActionRecord action = start_work(g, table);
    if (action.effect != NO_EFFECT) {
        take_effect(g, table, &action);
        effect_run_kind(g, pf, (EffectKind)battle_effect_record(g, 0));
        if ((battle_effect_missed(g) & MISSED) && (action.flags & ACTION_STOPS_AT_MISS))
            goto noted;
    }
    set_battle_damage_hit(g, (uint8_t)(battle_damage_hit(g) + 1));
    set_battle_effects_worked(g, (uint8_t)(battle_effects_worked(g) + 1));
    uint8_t added = action.flags & ACTION_HITS_ADDED;
    if (added)
        effect_add_up_hits(g, added);
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

/* ---- What the port works out -------------------------------------------------------------- */

/* What the port works out: a kind in the table; a hit that doesn't steal as
 * well (no record has one: its chance would be whatever the hit's work left
 * in the battle's first work cell), with modifiers the port has
 * (battle/effect_modifiers.c); a steal by a party member, whose modifier is
 * a charm's chance (as both of the party's steals are: another routine would
 * leave the charm to what it leaves in the work cells); and a drain whose
 * stat offset's high byte is clear -- the original reads the offset as a
 * word from the cell it hands the drain's kind over in, whose high byte
 * ($1B) nothing in the effect code sets and the game leaves clear. The rest
 * is refused. */
enum { DRAIN_KIND_HIGH = EFFECT_ARG_3 + 1, WEAPON_MODIFIER_ON = 0x01 };

static bool modifiers_ported(const GameState *g, uint8_t actor, uint8_t own, uint8_t more_flags)
{
    if (own != 0 && !effect_modifier_ported(g, own))
        return false;
    if (!(more_flags & EFFECT_WITH_WEAPON_MODIFIER)
        || !(combatant_stat_byte(g, actor, STAT_WEAPON_MODIFIER_ON) & WEAPON_MODIFIER_ON))
        return true;
    return effect_modifier_ported(g, combatant_stat_byte(g, actor, STAT_WEAPON_MODIFIER));
}

bool effect_kind_ported(const GameState *g, uint8_t actor, uint8_t kind, uint8_t flags, uint8_t more_flags,
                        uint8_t modifier)
{
    switch (kind) {
    case EFFECT_STEAL: return actor < BATTLE_PARTY_SIZE && effect_modifier_is_charm_chance(g, modifier);
    case EFFECT_HIT:
    case EFFECT_HIT_REPEATEDLY: return !(flags & EFFECT_STEALS) && modifiers_ported(g, actor, modifier, more_flags);
    case EFFECT_DRAIN: return battle_low_scratch(g, DRAIN_KIND_HIGH) == 0;
    default: return kind < EFFECT_KINDS;
    }
}

/* The targets listed are combatants, one to eleven of them. */
bool effect_targets_named(const GameState *g)
{
    uint8_t count = battle_action_target_count(g);
    bool named = count != 0 && count <= BATTLE_ACTION_TARGETS_COUNT;
    for (uint8_t place = 0; named && place < count; place++)
        named = battle_action_targets(g, place) < BATTLE_COMBATANTS;
    return named;
}

/* A hit whose targeting counted nobody still lands once: the work takes its
 * one target at the count less one, in a byte, so the list's index $FF
 * ($7E:AE8D, well past its eleven places: effect_listed_target), and the
 * hit's count of targets left stops at nothing. An enemy's tech in Geno Dome
 * after Atropos does it (the motherbrain route, where that cell held 0). The
 * port takes it so where that cell names a combatant; the kinds that count
 * their targets down without stopping would go round the whole byte, and are
 * left to the original. */

static bool takes_targets(const GameState *g, uint8_t kind)
{
    if (battle_action_target_count(g) == 0 && (kind == EFFECT_HIT || kind == EFFECT_HIT_REPEATEDLY))
        return effect_listed_target(g) < BATTLE_COMBATANTS;
    return effect_targets_named(g);
}

static bool combatants_taken(const GameState *g, uint8_t kind)
{
    return battle_actor(g) < BATTLE_COMBATANTS && takes_targets(g, kind);
}

static bool action_ported(const GameState *g, ActionTable table)
{
    ActionRecord action = action_record(g, table, battle_action_detail(g));
    if (action.effect == NO_EFFECT)
        return battle_actor(g) < BATTLE_COMBATANTS && effect_targets_named(g);
    uint8_t kind = effect_record_byte(g, table, action.effect, 0);
    return combatants_taken(g, kind)
           && effect_kind_ported(g, battle_actor(g), kind, effect_record_byte(g, table, action.effect, 7),
                                 effect_record_byte(g, table, action.effect, 8), action.modifier);
}

bool battle_enemy_action_worked_out(const GameState *g, bool tech) { return action_ported(g, tech ? ENEMY_TECHS : ENEMY_ATTACKS); }

void battle_work_out_enemy_action(GameState *g, Platform *pf, bool tech)
{
    effect_work_out_enemy_action(g, pf, tech ? ENEMY_TECHS : ENEMY_ATTACKS);
}
