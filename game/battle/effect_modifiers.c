/* Battle: the modifiers an attack or a tech applies to a hit
 * (battle/effect_parts.h).
 *
 * A modifier is a routine and two numbers ($CC:2A05): a critical hit, an
 * element, a chance of a status, of death, of leaving the target 1 HP, a
 * scale by the fallen party members, and the rest; the action names one, and
 * the attacker's weapon may name another. Each works on the hit's amount as it
 * stands (battle_effect_amount). The record is decoded here into the routine
 * and what its two numbers mean for it, and applied by a switch over the
 * routines, each a function with its numbers named.
 *
 * The clock's modifier runs the frame's input work again, the controller read
 * and the play time's tick, before it reads the seconds: that is the one piece
 * of an effect's work that reaches the host (the platform it is handed). A
 * routine past the table's 21, which no record has, the entries turn away
 * (battle/effect_work.c). */
#include "battle/effect_parts.h"

#include "battle/combatant.h"
#include "battle/random.h"
#include "charge.h"
#include "core/input.h"

enum { PERCENT = 100, MOST_DAMAGE = 9999 };

/* The flags the modifiers set: a critical hit (battle_action_flags), and in
 * the action's state, the hit leaving 1 HP and the target absorbing it. */
enum { CRITICAL = 0x80, LEAVES_ONE_HP = 0x01, ABSORBED = 0x40, ACTION_MISSED = 0x40 };

static uint16_t amount(const GameState *g) { return battle_effect_amount(g); }
static void set_amount(GameState *g, uint16_t v) { set_battle_effect_amount(g, v); }

/* A draw of a percent at most the chance. */
static bool chance_comes(GameState *g, uint8_t percent) { return battle_random(g, 0, PERCENT) <= percent; }

/* ---- The modifiers, decoded ---------------------------------------------------------------- */

typedef enum {
    MOD_CRITICAL_HIT, MOD_NOTHING, MOD_SCALED, MOD_AGAINST_FAMILY, MOD_FOR_THE_FALLEN, MOD_BY_THE_CLOCK,
    MOD_RANDOM_STATUS, MOD_STATUS_AGAINST_FAMILY, MOD_STATUS_BY_CHANCE, MOD_LEAVE_ONE_HP, MOD_ELEMENT,
    MOD_SCALED_ELEMENT, MOD_RARE_ITEM_CHANCE, MOD_RESTORE_OR_CURE, MOD_HALVE_HP, MOD_TAKE_STAT_BITS,
    MOD_BY_LAST_DIGIT, MOD_FELL_OUTRIGHT, MOD_DEATH, MOD_CRITICAL_BONUS, MOD_GIVE_STAT_BITS,
    MODIFIER_ROUTINES,
} ModifierRoutine;

typedef struct {
    ModifierRoutine routine;
    union {
        struct { uint8_t times; } critical;                  /* the damage times this on a critical hit */
        struct { uint8_t times, over; } scaled;              /* the damage times one over the other */
        struct { uint8_t families, times; } against_family;  /* ... times half the second against these */
        struct { uint8_t families, statuses; } status_against_family;
        struct { uint8_t percent, statuses; } status_by_chance;
        struct { uint8_t percent; } chance;                  /* leave one HP, halve HP, death, the rare item */
        struct { uint8_t what; } restore_or_cure;            /* bit 7 the HP restored, bit 6 cured */
        struct { uint8_t stat, bits; } stat_bits;            /* a stat's bits taken or given */
        struct { uint8_t over; } last_digit;
        struct { bool target; } fell;                        /* the target, or the actor */
        struct { bool to_most; } critical_bonus;             /* a critical hit makes the damage 9999, or doubles it */
    };
} Modifier;

static Modifier decode(ModifierRecord r)
{
    Modifier m = { .routine = (ModifierRoutine)r.routine };
    switch (m.routine) {
    case MOD_CRITICAL_HIT: m.critical.times = r.second; break;
    case MOD_SCALED:
    case MOD_SCALED_ELEMENT: m.scaled.times = r.first; m.scaled.over = r.second; break;
    case MOD_AGAINST_FAMILY: m.against_family.families = r.first; m.against_family.times = r.second; break;
    case MOD_STATUS_AGAINST_FAMILY: m.status_against_family.families = r.first; m.status_against_family.statuses = r.second; break;
    case MOD_STATUS_BY_CHANCE: m.status_by_chance.percent = r.first; m.status_by_chance.statuses = r.second; break;
    case MOD_LEAVE_ONE_HP:
    case MOD_HALVE_HP:
    case MOD_DEATH:
    case MOD_RARE_ITEM_CHANCE: m.chance.percent = r.first; break;
    case MOD_RESTORE_OR_CURE: m.restore_or_cure.what = r.first; break;
    case MOD_TAKE_STAT_BITS:
    case MOD_GIVE_STAT_BITS: m.stat_bits.stat = r.first; m.stat_bits.bits = r.second; break;
    case MOD_BY_LAST_DIGIT: m.last_digit.over = r.second; break;
    case MOD_FELL_OUTRIGHT: m.fell.target = r.second != 0; break;
    case MOD_CRITICAL_BONUS: m.critical_bonus.to_most = r.second == 1; break;
    default: break;
    }
    return m;
}

bool effect_modifier_ported(const GameState *g, uint8_t modifier)
{
    uint8_t routine = modifier_record(g, modifier).routine;
    return routine < MODIFIER_ROUTINES;
}

bool effect_modifier_is_charm_chance(const GameState *g, uint8_t modifier)
{
    return modifier_record(g, modifier).routine == MOD_RARE_ITEM_CHANCE;
}

/* ---- The routines ---------------------------------------------------------------------------- */

// $C1:ED89
/* A critical hit: a percent from the critical hits' own draws at most the
 * attacker's critical rate. */
static void critical_hit(GameState *g, uint8_t times)
{
    uint8_t rate = effect_actor_stat(g, STAT_CRITICAL_RATE);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_4, rate));
    if (battle_random_other(g, 0, PERCENT) > rate)
        return;
    set_amount(g, effect_times(g, amount(g), times));
    set_battle_action_flags(g, battle_action_flags(g) | CRITICAL);
}

// $C1:EDBA
static void scaled(GameState *g, uint8_t times, uint8_t over)
{
    set_amount(g, effect_over(g, effect_times(g, amount(g), times), over).quotient);
}

// $C1:EDDA
/* Against a target of the families named, the damage times half the second
 * number. */
static void against_family(GameState *g, uint8_t families, uint8_t times)
{
    if (effect_target_stat(g, STAT_FAMILY) & families)
        set_amount(g, (uint16_t)(effect_times(g, amount(g), times) >> 1));
}

// $C1:EDFD
/* The damage times one more than the party members out of the fight. */
static void for_the_fallen(GameState *g)
{
    uint8_t out = 0;
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (battle_combatant_fighting(g, member) == COMBATANT_NONE)
            out++;
    set_battle_party_out_count(g, out);
    set_amount(g, effect_times(g, (uint8_t)(out + 1), amount(g)));
}

// $C1:EE27
/* The damage by the play time. The frame's input work runs again first (the
 * controller read, the button assignments and the clock's tick, $C2:8002),
 * then the last digit of the seconds -- over the long arithmetic's leftover
 * high word as well, which the modifier's own multiply leaves clear -- scales
 * it: 0 to about a tenth, 1 or 2 half, 3 or 4 as it is, 5 or 6 by half of the
 * word the direct page's first cell holds added (the original loads that, not
 * the damage: the game plays it so), 7 or 8 doubled, 9 trebled. Where the
 * console is still reading the controller, or the reset buttons are held, the
 * original waits or resets inside the work; the port goes on with the sets
 * and the clock as they are (docs/TODO.md). */
enum { DIGITS = 10 };

static void by_the_clock(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC284D2, input_take_frame(g, pf));
    uint16_t digit = effect_over(g, play_time_seconds(g), DIGITS).remainder;
    uint16_t damage = amount(g);
    if (digit == 0)
        set_amount(g, effect_about_a_tenth(g, damage));
    else if (digit < 3)
        set_amount(g, (uint16_t)(damage >> 1));
    else if (digit < 5)
        return;
    else if (digit < 7) {
        uint16_t first = (uint16_t)(battle_low_scratch(g, BATTLE_CLOCK_READS) | battle_low_scratch(g, BATTLE_CLOCK_READS + 1) << 8);
        set_amount(g, (uint16_t)((first >> 1) + damage));
    } else if (digit < 9)
        set_amount(g, (uint16_t)(damage << 1));
    else
        set_amount(g, (uint16_t)(damage * 3));
}

// $C1:EFC3
/* The chance of an enemy's charm, which a steal takes where it comes: a
 * percent drawn below the modifier's number. The original hands the outcome
 * back in the direct page's third cell. */
static bool charm_chance(GameState *g, uint8_t percent)
{
    bool comes = battle_random(g, 0, PERCENT) < percent;
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, comes ? 1 : 0));
    return comes;
}

/* A status of the first status byte given the target. */
static void give_status(GameState *g, uint8_t bits)
{
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, 1));
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_2, bits));
    effect_inflict_status(g, 1, bits);
}

// $C1:EE9F
/* A random status: a draw of 0-100 picks one in bands -- confusion, stop,
 * sleep, poison -- and a draw past the bands gives the draw itself as the
 * bits. */
static void random_status(GameState *g)
{
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, PERCENT));
    uint8_t drawn = battle_random(g, 0, PERCENT + 1);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, drawn));
    uint8_t bits = drawn < 0x28   ? STATUS_CONFUSED
                   : drawn < 0x41 ? STATUS_STOP
                   : drawn < 0x5A ? STATUS_ASLEEP
                   : drawn < 0x64 ? STATUS_POISON
                                  : drawn;
    give_status(g, bits);
}

// $C1:EED0
/* Four in five, against a target of the families named, the statuses. */
static void status_against_family(GameState *g, uint8_t families, uint8_t statuses)
{
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, 0));
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, 0));
    if (chance_comes(g, 0x50) && (effect_target_stat(g, STAT_FAMILY) & families))
        give_status(g, statuses);
}

// $C1:EEF7
static void status_by_chance(GameState *g, uint8_t percent, uint8_t statuses)
{
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, 0));
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, 0));
    if (chance_comes(g, percent))
        give_status(g, statuses);
}

// $C1:EF14
/* A chance of leaving the target 1 HP. */
static void leave_one_hp(GameState *g, uint8_t percent)
{
    if (!chance_comes(g, percent))
        return;
    set_amount(g, (uint16_t)(effect_target_stat_word(g, STAT_HP) - 1));
    set_battle_action_state(g, battle_action_state(g) | LEAVES_ONE_HP);
}

// $C1:EF39
/* The action's element (the first of its element bits, from the top) against
 * the target's affinity for it: the damage times the scale over the
 * affinity's low six bits (two less where its bit 6 is set); bit 7 set, the
 * target absorbs it. With no element nothing changes.
 *
 * The scale is 4, set in the second number's cell a byte at a time: its high
 * byte is what the arithmetic before left there -- after the hit's division
 * by 256, which comes just before the modifiers, 1 -- so the scale the game
 * uses is 260, as a rule, and the damage comes out 65 times the element's
 * divisor over. */
enum { ELEMENT_SCALE = 4, TWO_LESS = 0x40, AFFINITY_BITS = 0x3F, ABSORBS = 0x80 };

static void element(GameState *g)
{
    uint8_t elements = battle_action_elements(g);
    set_battle_wide_math(g, 4, elements);
    int which = elements & 0x80 ? 0 : elements & 0x40 ? 1 : elements & 0x20 ? 2 : elements & 0x10 ? 3 : -1;
    if (which < 0)
        return;
    set_battle_element_resisted(g, 0, (uint8_t)which);
    set_battle_element_resisted(g, 2, ELEMENT_SCALE);
    uint8_t affinity = effect_target_stat(g, (uint16_t)(STAT_ELEMENT_AFFINITY + which));
    if (affinity & ABSORBS)
        set_battle_action_state(g, battle_action_state(g) | ABSORBED);
    uint8_t divisor = affinity & AFFINITY_BITS;
    if (affinity & TWO_LESS)
        divisor = (uint8_t)(divisor - 2);
    set_battle_element_resisted(g, 1, divisor);
    uint16_t scale = (uint16_t)(battle_wide_math(g, 3) << 8 | ELEMENT_SCALE);
    set_amount(g, effect_over(g, effect_times(g, amount(g), scale), divisor).quotient);
}

// $C1:F036
/* Bit 7 of the first number: the target's HP restored in full, shown; bit
 * 6: its statuses cured (as a cure does, less the rest). */
enum { RESTORES_HP_IN_FULL = 0x80, CURES = 0x40, CONDITION_KEPT = 0xC0, STATUS_2_KEPT = 0xC2 };

static void restore_or_cure(GameState *g, uint8_t what)
{
    if (what & RESTORES_HP_IN_FULL) {
        uint16_t most = effect_target_stat_word(g, STAT_MAX_HP);
        set_amount(g, most);
        effect_record_damage(g, effect_target(g), DAMAGE_HEALS);
        set_battle_effect_tally(g, 0);
        set_effect_number(g, effect_target_number(g), most, NUMBER_HP_RESTORED);
    }
    if (what & CURES) {
        set_effect_target_stat(g, STAT_CONDITION, effect_target_stat(g, STAT_CONDITION) & CONDITION_KEPT);
        set_effect_target_stat(g, STAT_STATUS_1, 0);
        set_effect_target_stat(g, STAT_STATUS_2, effect_target_stat(g, STAT_STATUS_2) & STATUS_2_KEPT);
    }
}

// $C1:F095
/* A chance of halving the target's HP, not against those that can't be
 * felled outright. */
enum { CANT_BE_FELLED = 0x80 };

static void halve_hp(GameState *g, uint8_t percent)
{
    if (effect_target_stat(g, STAT_KIND) & CANT_BE_FELLED)
        return;
    if (chance_comes(g, percent))
        set_amount(g, (uint16_t)(effect_target_stat_word(g, STAT_HP) >> 1));
}

// $C1:F0BD, $C1:F1EF
/* Bits of a stat of the target's taken away, or given. */
static void take_stat_bits(GameState *g, uint8_t stat, uint8_t bits)
{
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_5, (uint8_t)~bits));
    set_effect_target_stat(g, stat, effect_target_stat(g, stat) & (uint8_t)~bits);
}

static void give_stat_bits(GameState *g, uint8_t stat, uint8_t bits)
{
    set_effect_target_stat(g, stat, effect_target_stat(g, stat) | bits);
}

// $C1:F0D9
/* The damage times the last digit of the attacker's HP, over the second
 * number. */
static void by_last_digit(GameState *g, uint8_t over)
{
    uint16_t digit = effect_over(g, effect_actor_stat_word(g, STAT_HP), 10).remainder;
    set_amount(g, effect_over(g, effect_times(g, digit, amount(g)), over).quotient);
}

// $C1:F10D
/* The actor or the target felled outright, its HP 0; the target is marked
 * beaten by its battle slot among the marks from $AEB0, where the first two
 * party members' land before the enemies' and the third's is the mark that
 * any enemy is beaten. */
enum { MEMBERS_MARKED_LOW = 2, THIRD_MEMBER = 2 };

static void mark_felled(GameState *g, uint8_t slot)
{
    if (slot < MEMBERS_MARKED_LOW)
        set_battle_beaten_marks_low(g, slot, 1);
    else if (slot != THIRD_MEMBER && (unsigned)(slot - BATTLE_FIRST_ENEMY) < BATTLE_ENEMY_BEATEN_COUNT)
        set_battle_enemy_beaten(g, (unsigned)(slot - BATTLE_FIRST_ENEMY), 1);
    else if (slot != THIRD_MEMBER)
        set_battle_enemy_beaten_unchecked(g, (unsigned)(slot - BATTLE_FIRST_ENEMY), 1);
    set_battle_enemy_beaten_any(g, 1);
}

static void fell_outright(GameState *g, bool target)
{
    uint8_t slot = target ? effect_target(g) : battle_actor(g);
    if (target)
        mark_felled(g, slot);
    set_combatant_stat_byte(g, slot, STAT_HP, 0);
    set_combatant_stat_byte(g, slot, STAT_HP + 1, 0);
}

// $C1:F132
/* A chance of death, not against those that can't be felled outright. Where
 * it doesn't come, the target is taken out of the action's targets (as bits)
 * and the hit does nothing. Either way, with no first target left the next of
 * the others is taken, or the action is marked as missed. */
static void death(GameState *g, uint8_t percent)
{
    bool dies = !(effect_target_stat(g, STAT_KIND) & CANT_BE_FELLED) && chance_comes(g, percent);
    if (dies) {
        set_effect_target_stat(g, STAT_HP, 0);
        set_effect_target_stat(g, STAT_HP + 1, 0);
    } else {
        set_battle_target_bits_kept(g, battle_action_target_bits(g) | battle_action_other_targets(g));
        uint16_t bit = effect_target(g) < 16 ? (uint16_t)(0x8000u >> effect_target(g)) : 0;
        uint16_t spared = (uint16_t)~bit;
        battle_set_wide_math_quotient(g, spared);
        set_battle_action_target_bits(g, battle_action_target_bits(g) & spared);
        set_battle_action_other_targets(g, battle_action_other_targets(g) & spared);
        set_amount(g, 0);
    }
    if (battle_action_target_bits(g) != 0)
        return;
    uint16_t others = battle_action_other_targets(g);
    if (others == 0) {
        set_battle_action_extra(g, battle_action_extra(g) | ACTION_MISSED);
        return;
    }
    for (uint16_t bit = 0x8000; bit != 0; bit >>= 1)
        if (others & bit) {
            set_battle_action_target_bits(g, bit);
            set_battle_action_other_targets(g, others ^ bit);
            return;
        }
}

// $C1:F1C7
/* A critical hit doubles the damage, or makes it 9999. */
static void critical_bonus(GameState *g, bool to_most)
{
    if (!(battle_action_flags(g) & CRITICAL))
        return;
    set_amount(g, to_most ? MOST_DAMAGE : effect_times(g, amount(g), 2));
}

/* ---- Applying them -------------------------------------------------------------------------- */

static void apply(GameState *g, Platform *pf, Modifier m)
{
    switch (m.routine) {
    case MOD_CRITICAL_HIT: critical_hit(g, m.critical.times); break;
    case MOD_SCALED: scaled(g, m.scaled.times, m.scaled.over); break;
    case MOD_AGAINST_FAMILY: against_family(g, m.against_family.families, m.against_family.times); break;
    case MOD_FOR_THE_FALLEN: for_the_fallen(g); break;
    case MOD_BY_THE_CLOCK: by_the_clock(g, pf); break;
    case MOD_RANDOM_STATUS: random_status(g); break;
    case MOD_STATUS_AGAINST_FAMILY:
        status_against_family(g, m.status_against_family.families, m.status_against_family.statuses);
        break;
    case MOD_STATUS_BY_CHANCE:
        status_by_chance(g, m.status_by_chance.percent, m.status_by_chance.statuses);
        break;
    case MOD_LEAVE_ONE_HP: leave_one_hp(g, m.chance.percent); break;
    case MOD_ELEMENT: element(g); break;
    case MOD_SCALED_ELEMENT:
        scaled(g, m.scaled.times, m.scaled.over);
        element(g);
        break;
    case MOD_RARE_ITEM_CHANCE: charm_chance(g, m.chance.percent); break;
    case MOD_RESTORE_OR_CURE: restore_or_cure(g, m.restore_or_cure.what); break;
    case MOD_HALVE_HP: halve_hp(g, m.chance.percent); break;
    case MOD_TAKE_STAT_BITS: take_stat_bits(g, m.stat_bits.stat, m.stat_bits.bits); break;
    case MOD_BY_LAST_DIGIT: by_last_digit(g, m.last_digit.over); break;
    case MOD_FELL_OUTRIGHT: fell_outright(g, m.fell.target); break;
    case MOD_DEATH: death(g, m.chance.percent); break;
    case MOD_CRITICAL_BONUS: critical_bonus(g, m.critical_bonus.to_most); break;
    case MOD_GIVE_STAT_BITS: give_stat_bits(g, m.stat_bits.stat, m.stat_bits.bits); break;
    default: break; /* nothing */
    }
}

// $C1:EB1C
/* The modifier the action is worked out with (battle_effect_modifier). The
 * original hands the routine its numbers in the direct page, and finds the
 * record by a multiply. */
static Modifier take_modifier(GameState *g)
{
    uint8_t number = battle_effect_modifier(g);
    effect_times(g, number, 3);
    ModifierRecord r = modifier_record(g, number);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_4, r.first));
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_5, r.second));
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_6, r.routine));
    return decode(r);
}

static void apply_modifier(GameState *g, Platform *pf) { apply(g, pf, take_modifier(g)); }

bool effect_charm_comes(GameState *g) { return charm_chance(g, take_modifier(g).chance.percent); }

// $C1:EA6F
/* The action's modifier, where it has one, then the weapon's where the
 * effect says so and the weapon's switch is on; the action's is put back
 * after. */
enum { WEAPON_MODIFIER_ON = 0x01 };

void effect_apply_modifiers(GameState *g, Platform *pf)
{
    uint8_t own = battle_effect_modifier(g);
    set_battle_action_modifier_kept(g, own);
    if (own != 0)
        apply_modifier(g, pf);
    if (battle_effect_flags(g, 1) & EFFECT_WITH_WEAPON_MODIFIER) {
        uint8_t on = effect_actor_stat(g, STAT_WEAPON_MODIFIER_ON), weapons = effect_actor_stat(g, STAT_WEAPON_MODIFIER);
        ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_5, on));
        ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_6, weapons));
        set_battle_effect_modifier(g, weapons);
        if (on & WEAPON_MODIFIER_ON)
            apply_modifier(g, pf);
    }
    set_battle_effect_modifier(g, battle_action_modifier_kept(g));
}
