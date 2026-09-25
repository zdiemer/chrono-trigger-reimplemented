/* Battle: the modifiers an attack or a tech applies to a hit, and stealing
 * (battle/effect_parts.h).
 *
 * A modifier is a routine and two numbers ($CC:2A05): a critical hit, an
 * element, a chance of a status, of death, of leaving the target 1 HP, a
 * scale by the fallen party members, and the rest; the action names one, and
 * the attacker's weapon may name another. The routines are a C table, a row
 * each (the original's $C1:FA61). Each works on the hit's amount as it stands
 * (battle_effect_amount) and returns the carry it leaves, which the original's
 * next division takes in. */
#include "battle/effect_parts.h"

#include "battle/combatant.h"
#include "battle/random.h"

enum { STAT_CRITICAL_RATE = 0x45, STAT_KIND_BITS = 0x1A, STAT_WEAPON_STATUS = 0x46, STAT_WEAPON_MODIFIER = 0x47,
       STAT_STEALS_BETTER = 0x2A, STAT_KIND = 0x19, STAT_FIRST_RESISTANCE = 0x3F };

enum { PERCENT = 100, MOST_DAMAGE = 9999 };

/* The flags the modifiers set: a critical hit (battle_action_flags), and in
 * the action's state, the hit leaving 1 HP and the target absorbing it. */
enum { CRITICAL = 0x80, LEAVES_ONE_HP = 0x01, ABSORBED = 0x40, ACTION_MISSED = 0x40 };

static uint16_t amount(const GameState *g) { return battle_effect_amount(g); }
static void set_amount(GameState *g, uint16_t v) { set_battle_effect_amount(g, v); }

typedef struct {
    EffectWork *w;
    uint8_t first, second;
    bool carry;
} Modifier;

/* A draw: the original's works with 8-bit index registers, which clears
 * their high bytes (effect_work.h, machine_y). */
static uint8_t draw(Modifier m, uint8_t limit)
{
    m.w->machine_y &= 0x00FF;
    return battle_random(m.w->g, 0, limit);
}

/* A draw of a percent at most the chance (with the carry of the compare). */
typedef struct {
    bool within;
    bool carry;
} Chance;

static Chance chance(Modifier m, uint8_t percent)
{
    uint8_t drawn = draw(m, PERCENT);
    return (Chance){ drawn <= percent, drawn >= percent };
}

/* ---- The modifiers ------------------------------------------------------------------------- */


// $C1:ED89
/* A critical hit: a percent from the critical hits' own draws at most the
 * attacker's critical rate, the damage times the second number. */
static bool critical_hit(Modifier m)
{
    GameState *g = m.w->g;
    uint8_t rate = effect_actor_stat(g, STAT_CRITICAL_RATE);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_4, rate));
    m.w->machine_y &= 0x00FF;
    uint8_t drawn = battle_random_other(g, 0, PERCENT);
    if (drawn > rate)
        return true;
    set_amount(g, effect_times(g, amount(g), m.second, drawn >= rate));
    set_battle_action_flags(g, battle_action_flags(g) | CRITICAL);
    return false;
}

// $C1:EDB9
static bool nothing(Modifier m) { return m.carry; }

// $C1:EDBA
/* The damage times the first number over the second. */
static bool scaled(Modifier m)
{
    GameState *g = m.w->g;
    set_amount(g, effect_over(g, effect_times(g, amount(g), m.first, m.carry), m.second, false).quotient);
    return false;
}

// $C1:EDDA
/* Against a target of the kinds the first number names, the damage times
 * half the second. */
static bool against_kind(Modifier m)
{
    GameState *g = m.w->g;
    if (!(effect_target_stat(g, STAT_KIND_BITS) & m.first))
        return m.carry;
    uint16_t product = effect_times(g, amount(g), m.second, m.carry);
    set_amount(g, (uint16_t)(product >> 1));
    return (product & 1) != 0;
}

// $C1:EDFD
/* The damage times one more than the party members out of the fight. */
static bool for_the_fallen(Modifier m)
{
    GameState *g = m.w->g;
    uint8_t out = 0;
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (battle_combatant_fighting(g, member) == COMBATANT_NONE)
            out++;
    set_battle_party_out_count(g, out);
    set_amount(g, effect_times(g, (uint8_t)(out + 1), amount(g), true));
    return false;
}

// $C1:EE27
/* By the play time's seconds, read as the controller is: on the tens, about a
 * tenth; 1-2 past, half; 3-4, as it is; 5-6, half of what the direct page's
 * first word holds added; 7-8, double; 9, triple. */
static bool by_the_clock(Modifier m)
{
    GameState *g = m.w->g;
    m.w->carry_back = m.carry;
    if (!m.w->host->call(m.w, CALL_READ_CONTROLLER)) {
        m.w->stopped = true;
        return m.carry;
    }
    BattleLongQuotient q = effect_over(g, play_time_seconds(g), 0x0A, m.w->carry_back);
    uint8_t past = (uint8_t)q.remainder;
    uint16_t a = amount(g);
    if (past == 0) {
        set_amount(g, effect_about_a_tenth(g, a, &m.w->machine_y));
        return true;
    }
    if (past < 3) {
        set_amount(g, (uint16_t)(a >> 1));
        return (a & 1) != 0;
    }
    if (past < 5)
        return false;
    if (past < 7) {
        uint16_t first_word = (uint16_t)(battle_low_scratch(g, 0) | battle_low_scratch(g, 1) << 8);
        uint32_t sum = (uint32_t)(first_word >> 1) + a;
        set_amount(g, (uint16_t)sum);
        return sum > 0xFFFF;
    }
    if (past < 9) {
        set_amount(g, (uint16_t)(a << 1));
        return (a & 0x8000) != 0;
    }
    uint32_t tripled = (uint32_t)(uint16_t)(a << 1) + a;
    set_amount(g, (uint16_t)tripled);
    return tripled > 0xFFFF;
}

/* A status given the target. */
static bool give_status(Modifier m, uint8_t bits)
{
    GameState *g = m.w->g;
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, 1));
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_2, bits));
    m.w->first_left = effect_inflict_status(g, 1, bits, &m.w->machine_v);
    return effect_inflicting_carry(1);
}

// $C1:EE9F
/* A random status of the first byte: a draw of 0-100 picks one in bands; one
 * past the bands gives the draw itself as the bits. */
static bool random_status(Modifier m)
{
    GameState *g = m.w->g;
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, PERCENT));
    uint8_t drawn = draw(m, PERCENT + 1);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, drawn));
    uint8_t bits = drawn < 0x28 ? 0x04 : drawn < 0x41 ? 0x80 : drawn < 0x5A ? 0x02 : drawn < 0x64 ? 0x40 : drawn;
    return give_status(m, bits);
}

// $C1:EED0
/* Four in five, against a target of the kinds the first number names, the
 * status bits of the second. */
static bool status_against_kind(Modifier m)
{
    GameState *g = m.w->g;
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, 0));
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, 0));
    m.w->first_left = 0;
    Chance c = chance(m, 0x50);
    if (!c.within || !(effect_target_stat(g, STAT_KIND_BITS) & m.first))
        return c.carry;
    return give_status(m, m.second);
}

// $C1:EEF7
/* A chance of the first number of the status bits of the second. */
static bool status_by_chance(Modifier m)
{
    GameState *g = m.w->g;
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_1, 0));
    ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_2, 0));
    m.w->first_left = 0;
    Chance c = chance(m, m.first);
    if (!c.within)
        return c.carry;
    return give_status(m, m.second);
}

// $C1:EF14
/* A chance of leaving the target 1 HP. */
static bool leave_one_hp(Modifier m)
{
    GameState *g = m.w->g;
    Chance c = chance(m, m.first);
    if (!c.within)
        return c.carry;
    set_amount(g, (uint16_t)(effect_target_stat_word(g, STAT_HP) - 1));
    set_battle_action_state(g, battle_action_state(g) | LEAVES_ONE_HP);
    return c.carry;
}

// $C1:EF39
/* The action's element (the first of its element bits, from the top) against
 * the target's resistance to it: the damage times four over the
 * resistance's low six bits (less two where its bit 6 is set); bit 7 set, the
 * target absorbs it. With no element nothing changes. */
static bool element(Modifier m)
{
    GameState *g = m.w->g;
    uint8_t elements = battle_action_elements(g);
    set_battle_wide_math(g, 4, elements);
    int which = elements & 0x80 ? 0 : elements & 0x40 ? 1 : elements & 0x20 ? 2 : elements & 0x10 ? 3 : -1;
    if (which < 0)
        return m.carry;
    set_battle_element_resisted(g, 0, (uint8_t)which);
    set_battle_element_resisted(g, 2, 4);
    uint8_t resistance = effect_target_stat(g, (uint16_t)(STAT_FIRST_RESISTANCE + which));
    if (resistance & 0x80)
        set_battle_action_state(g, battle_action_state(g) | ABSORBED);
    uint8_t divisor = resistance & 0x3F;
    bool carry = false;
    if (resistance & 0x40) {
        carry = divisor >= 2;
        divisor = (uint8_t)(divisor - 2);
    }
    set_battle_element_resisted(g, 1, divisor);
    /* the scale is set in the second number's low byte alone */
    uint16_t scale = (uint16_t)(battle_wide_math(g, 3) << 8 | 4);
    uint16_t product = effect_times(g, amount(g), scale, carry);
    set_amount(g, effect_over(g, product, divisor, false).quotient);
    return false;
}

// $C1:EFBC
static bool scaled_element(Modifier m)
{
    m.carry = scaled(m);
    return element(m);
}

// $C1:EFC3
/* A chance (the first number) that a steal takes the rare item: its answer
 * left in the third argument cell, which the steal reads. */
static bool steal_rare_chance(Modifier m)
{
    GameState *g = m.w->g;
    battle_keep_byte(g, EFFECT_ARG_3, 0);
    uint8_t drawn = draw(m, PERCENT);
    if (drawn < m.first)
        battle_keep_byte(g, EFFECT_ARG_3, 1);
    return drawn >= m.first;
}

// $C1:F036
/* Bit 7 of the first number: the target's HP restored in full, shown; bit
 * 6: its statuses cured (as a cure does, less the rest). */
static bool restore_or_cure(Modifier m)
{
    GameState *g = m.w->g;
    bool carry = m.carry;
    if (m.first & 0x80) {
        set_amount(g, effect_target_stat_word(g, STAT_MAX_HP));
        effect_record_damage(g, effect_target(g), DAMAGE_HEALS, carry);
        set_battle_effect_tally(g, 0);
        uint8_t place = effect_target_number_carrying(g, &carry, &m.w->machine_v);
        set_effect_number(g, place, (uint16_t)-amount(g), NUMBER_HP_RESTORED);
    }
    if (m.first & 0x40) {
        set_effect_target_stat(g, STAT_CONDITION, effect_target_stat(g, STAT_CONDITION) & 0xC0);
        set_effect_target_stat(g, STAT_STATUS_1, 0);
        set_effect_target_stat(g, STAT_STATUS_2, effect_target_stat(g, STAT_STATUS_2) & 0xC2);
    }
    return carry;
}

// $C1:F095
/* A chance of halving the target's HP, not against the kinds that shrug off
 * death. */
enum { SHRUGS_OFF_DEATH = 0x80 };

static bool halve_hp(Modifier m)
{
    GameState *g = m.w->g;
    if (effect_target_stat(g, STAT_KIND) & SHRUGS_OFF_DEATH)
        return m.carry;
    Chance c = chance(m, m.first);
    if (!c.within)
        return c.carry;
    uint16_t hp = effect_target_stat_word(g, STAT_HP);
    set_amount(g, (uint16_t)(hp >> 1));
    return (hp & 1) != 0;
}

// $C1:F0BD
/* The bits of the second number taken from the target's stat the first names. */
static bool take_stat_bits(Modifier m)
{
    GameState *g = m.w->g;
    uint8_t kept = (uint8_t)~m.second;
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_5, kept));
    set_effect_target_stat(g, m.first, effect_target_stat(g, m.first) & kept);
    return false;
}

// $C1:F0D9
/* The damage times the last digit of the attacker's HP, over the second
 * number (the HP divided by ten with whatever high word the arithmetic left). */
static bool by_last_digit(Modifier m)
{
    GameState *g = m.w->g;
    BattleLongQuotient q = effect_over(g, effect_actor_stat_word(g, STAT_HP), 0x0A, m.carry);
    uint16_t product = effect_times(g, q.remainder, amount(g), m.carry);
    set_amount(g, effect_over(g, product, m.second, false).quotient);
    return false;
}

// $C1:F10D
/* The actor (second number 0) or the target is beaten outright: its HP 0,
 * the target marked beaten. */
enum { MEMBERS_MARKED_LOW = 2, THIRD_MEMBER = 2 };

/* The mark is set by battle slot from the first two members' marks: the third
 * member's is the one that says any enemy is beaten. */
static void mark_beaten_outright(GameState *g, uint8_t slot)
{
    if (slot < MEMBERS_MARKED_LOW)
        set_battle_beaten_marks_low(g, slot, 1);
    else if (slot == THIRD_MEMBER)
        set_battle_enemy_beaten_any(g, 1);
    else if ((unsigned)(slot - BATTLE_FIRST_ENEMY) < BATTLE_ENEMY_BEATEN_COUNT)
        set_battle_enemy_beaten(g, (unsigned)(slot - BATTLE_FIRST_ENEMY), 1);
    else
        set_battle_enemy_beaten_unchecked(g, (unsigned)(slot - BATTLE_FIRST_ENEMY), 1);
    set_battle_enemy_beaten_any(g, 1);
}

static bool beat_outright(Modifier m)
{
    GameState *g = m.w->g;
    uint8_t slot = m.second == 0 ? battle_actor(g) : effect_target(g);
    if (m.second != 0) {
        mark_beaten_outright(g, slot);
        m.w->machine_y = slot;
    }
    set_combatant_stat_byte(g, slot, STAT_HP, 0);
    set_combatant_stat_byte(g, slot, STAT_HP + 1, 0);
    return m.carry;
}

// $C1:F132
/* A chance (the first number) of death, not against the kinds that shrug it
 * off; where it doesn't come, the target is taken out of the action's targets
 * (as bits) and the hit does nothing. Either way, with no first target left
 * the next of the others is taken, or the action marked as missed. */
static bool death(Modifier m)
{
    GameState *g = m.w->g;
    bool killed = false;
    bool carry = m.carry;
    if (!(effect_target_stat(g, STAT_KIND) & SHRUGS_OFF_DEATH)) {
        Chance c = chance(m, m.first);
        carry = c.carry;
        killed = c.within;
    }
    (void)carry;
    if (killed) {
        set_effect_target_stat(g, STAT_HP, 0);
        set_effect_target_stat(g, STAT_HP + 1, 0);
    } else {
        set_battle_target_bits_kept(g, battle_action_target_bits(g) | battle_action_other_targets(g));
        uint16_t bit = effect_target(g) < 16 ? (uint16_t)(0x8000u >> effect_target(g)) : 0;
        uint16_t mask = (uint16_t)~bit;
        battle_set_wide_math_quotient(g, mask);
        set_battle_action_target_bits(g, battle_action_target_bits(g) & mask);
        set_battle_action_other_targets(g, battle_action_other_targets(g) & mask);
        set_amount(g, 0);
    }
    if (battle_action_target_bits(g) == 0) {
        uint16_t others = battle_action_other_targets(g);
        if (others == 0) {
            set_battle_action_extra(g, battle_action_extra(g) | ACTION_MISSED);
        } else {
            for (uint16_t bit = 0x8000; bit != 0; bit >>= 1)
                if (others & bit) {
                    set_battle_action_target_bits(g, bit);
                    set_battle_action_other_targets(g, others ^ bit);
                    break;
                }
        }
    }
    /* the compares its end makes leave the carry set */
    return true;
}

// $C1:F1C7
/* A critical hit doubles the damage, or with the second number 1, makes it
 * 9999. */
static bool critical_bonus(Modifier m)
{
    GameState *g = m.w->g;
    if (!(battle_action_flags(g) & CRITICAL))
        return m.carry;
    if (m.second == 1) {
        set_amount(g, MOST_DAMAGE);
        return true;
    }
    set_amount(g, effect_times(g, amount(g), 2, m.second >= 1));
    return false;
}

// $C1:F1EF
/* The bits of the second number given the target's stat the first names. */
static bool give_stat_bits(Modifier m)
{
    GameState *g = m.w->g;
    set_effect_target_stat(g, m.first, effect_target_stat(g, m.first) | m.second);
    return false;
}

typedef bool (*ModifierRoutine)(Modifier m);

// $C1:FA61
static const ModifierRoutine routines[] = {
    critical_hit, nothing, scaled, against_kind, for_the_fallen, by_the_clock, random_status, status_against_kind,
    status_by_chance, leave_one_hp, element, scaled_element, steal_rare_chance, restore_or_cure, halve_hp,
    take_stat_bits, by_last_digit, beat_outright, death, critical_bonus, give_stat_bits,
};

enum { MODIFIER_ROUTINES = sizeof routines / sizeof routines[0] };

// $C1:EB1C
/* The modifier the action is worked out with (battle_effect_modifier). */
static bool apply_modifier(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    uint8_t number = battle_effect_modifier(g);
    effect_times(g, number, 3, carry);
    ModifierRecord r = modifier_record(g, number);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_4, r.first));
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_5, r.second));
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_6, r.routine));
    if (r.routine >= MODIFIER_ROUTINES) {
        w->routine = r.routine;
        if (!w->host->call(w, CALL_UNLISTED_MODIFIER)) {
            w->stopped = true;
            return false;
        }
        return w->carry_back;
    }
    Modifier m = { w, r.first, r.second, (r.routine & 0x80) != 0 };
    effect_step_in(w, STEP_MODIFIER_ROUTINE);
    bool carry_left = routines[r.routine](m);
    effect_step_out(w);
    return carry_left;
}

// $C1:EA6F
/* The action's modifier, where it has one, then the weapon's where the
 * effect says so and the weapon's modifier byte asks for it (bit 0); the
 * action's is put back after. */
enum { WITH_WEAPON_MODIFIER = 0x80, WEAPON_MODIFIER_ON = 0x01 };

bool effect_apply_modifiers(EffectWork *w, bool carry)
{
    GameState *g = w->g;
    uint8_t own = battle_effect_modifier(g);
    set_battle_action_modifier_kept(g, own);
    if (own != 0) {
        effect_step_in(w, STEP_ACTION_MODIFIER);
        carry = apply_modifier(w, carry);
        effect_step_out(w);
        if (w->stopped)
            return carry;
    }
    if (battle_effect_flags(g, 1) & WITH_WEAPON_MODIFIER) {
        uint8_t switches = effect_actor_stat(g, STAT_WEAPON_MODIFIER), weapons = effect_actor_stat(g, STAT_WEAPON_STATUS);
        ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_5, switches));
        ORIGINAL_EFFECT(battle_leave_word(g, EFFECT_ARG_6, weapons));
        set_battle_effect_modifier(g, weapons);
        if (switches & WEAPON_MODIFIER_ON) {
            effect_step_in(w, STEP_WEAPON_MODIFIER);
            carry = apply_modifier(w, carry);
            effect_step_out(w);
            if (w->stopped)
                return carry;
        }
    }
    set_battle_effect_modifier(g, battle_action_modifier_kept(g));
    return carry;
}

// $C1:E9D5
/* A try at stealing: the chance, tripled where the attacker has the thief's
 * stat ($A5); where it comes, the rare item where the steal's modifier's
 * chance says so and the enemy still has it, or else the common one; the item
 * is given the party by the original's code. */
enum { STEALS_BETTER = 0xA5, RARE_LEFT = 0x80, COMMON_LEFT = 0x40, ENEMY_RECORD_BYTES = 7 };

static uint8_t steal_marks(const GameState *g, uint8_t slot)
{
    return slot < BATTLE_STEAL_MARKS_COUNT ? battle_steal_marks(g, slot) : battle_steal_marks_unchecked(g, slot);
}
static void set_steal_marks(GameState *g, uint8_t slot, uint8_t v)
{
    if (slot < BATTLE_STEAL_MARKS_COUNT)
        set_battle_steal_marks(g, slot, v);
    else
        set_battle_steal_marks_unchecked(g, slot, v);
}

uint8_t effect_steal(EffectWork *w, uint8_t chance_of)
{
    GameState *g = w->g;
    set_battle_steal_outcome(g, 0);
    if (effect_actor_stat(g, STAT_STEALS_BETTER) == STEALS_BETTER) {
        chance_of = (uint8_t)(chance_of * 3);
        ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, chance_of));
    }
    w->machine_y &= 0x00FF;
    uint8_t drawn = battle_random(g, 0, PERCENT);
    if (drawn > chance_of) {
        ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, STEAL_MISSED));
        set_battle_steal_outcome(g, STEAL_MISSED);
        return STEAL_MISSED;
    }
    uint8_t slot = effect_target(g);
    ORIGINAL_EFFECT(battle_leave_word(g, 0x0C, slot));
    uint16_t record = effect_times(g, combatant_id(g, slot), ENEMY_RECORD_BYTES, drawn >= chance_of);
    battle_keep_stat_offset_word(g, record);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_6, battle_effect_modifier(g)));
    effect_step_in(w, STEP_STEAL_MODIFIER);
    apply_modifier(w, false);
    effect_step_out(w);
    if (w->stopped)
        return 0;
    /* the modifier's chance of the rare item is what it leaves in the third
     * argument cell (as the original reads it, whatever modifier it was) */
    bool rare_chance_won = battle_low_scratch(g, EFFECT_ARG_3) != 0;
    uint8_t marks = steal_marks(g, slot), item;
    if (rare_chance_won && (marks & RARE_LEFT)) {
        set_steal_marks(g, slot, marks & (uint8_t)~RARE_LEFT);
        set_battle_steal_outcome(g, (uint8_t)(battle_steal_outcome(g) + 1));
        item = enemy_rare_item(g, record);
    } else if (marks & COMMON_LEFT) {
        set_battle_steal_outcome(g, (uint8_t)(battle_steal_outcome(g) + 1));
        set_steal_marks(g, slot, marks & (uint8_t)~COMMON_LEFT);
        item = enemy_common_item(g, record);
    } else {
        item = 0;
    }
    if (item == 0) {
        ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, STEAL_NOTHING));
        set_battle_steal_outcome(g, STEAL_NOTHING);
        return STEAL_NOTHING;
    }
    /* the original's code that gives the item takes it in $06 */
    battle_keep_byte(g, 0x06, item);
    if (!w->host->call(w, CALL_GIVE_STOLEN_ITEM)) {
        w->stopped = true;
        return 0;
    }
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, STEAL_TOOK));
    return STEAL_TOOK;
}
