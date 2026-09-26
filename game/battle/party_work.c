/* Battle: the party's techs and items worked out (battle/effect_work.h).
 *
 * A party tech is performed by one member, or by two or three together (a
 * combination tech); its record names them and has a part for each, an effect
 * of the party's table with a modifier (battle/effect_data.h). Before the
 * work each member must be able to take part: not kept from techs, and with
 * the MP their part costs (less for the two accessories that cut it). A tech
 * that can't be done does nothing, unless it is a member's attack, which
 * costs nothing. Then each part is worked out in turn by its
 * kind, as an enemy's effect is (battle/effect_work.c), with its member as the
 * actor, and the hits' numbers are added up as the tech says. A member's
 * attack is worked out this way too, as the character's attack tech; unlike a
 * tech, whose targets the player chose, it aims anew at an enemy still in the
 * fight where its own is out.
 *
 * The item a member uses is one kind's work: restore, cure, or give a status.
 * The entries are at the end. */
#include "battle/effect_parts.h"

#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/enemy_actions.h"
#include "battle/entries.h"
#include "battle/random.h"
#include "battle/targets.h"
#include "charge.h"
#include "wram.h"

/* The battle slot a character fights in, none where it isn't in the battle.
 * A part with no member ends the tech: the original reads the slot of the
 * id of none past the table and ends where that byte is none too, which the
 * entry's guard makes sure of (the only place the port reads it). */
enum { NO_SLOT = 0xFF };

static uint8_t slot_of(const GameState *g, uint8_t character) { return battle_character_slot(g, character); }

/* The original finds the tech's record by a multiply, whose cells the
 * battle's next arithmetic takes up (battle/arith.h). */
static void multiply_out_tech_place(GameState *g, uint8_t tech) { (void)effect_times(g, tech, PARTY_TECH_BYTES); }

/* ---- Whether the members can take part ------------------------------------------------------- */

/* Two accessories cut the MP a part costs: to half, and to a quarter, each
 * halving rounded up. */
enum { HALVES_MP_COST = 0xA2, QUARTERS_MP_COST = 0xA3 };

static uint8_t halved_up(uint8_t cost) { return (uint8_t)((cost >> 1) + (cost & 1)); }

// $C1:CBF6
static uint8_t mp_cost_with(uint8_t cost, uint8_t accessory)
{
    if (accessory == HALVES_MP_COST)
        return halved_up(cost);
    if (accessory == QUARTERS_MP_COST)
        return halved_up(halved_up(cost));
    return cost;
}

// $C1:CB48, $C1:CB61, $C1:CB7A
uint8_t effect_part_mp_cost(const GameState *g, const PartyTech *t, unsigned part, uint8_t slot)
{
    return mp_cost_with(party_effect_mp_cost(g, t->effects[part]), battle_member_accessory(g, slot));
}

/* A member can take part where nothing keeps them from techs and they have
 * the MP for their part: more MP than it costs, and not kept from techs by
 * their status; exactly as much, whatever their status (the original's
 * test, kept). */
static bool member_takes_part(const GameState *g, const PartyTech *t, unsigned part, uint8_t slot)
{
    if (battle_member_disabled(g, slot))
        return false;
    uint8_t cost = effect_part_mp_cost(g, t, part, slot);
    uint16_t mp = combatant_mp(g, slot);
    return cost == mp || (cost < mp && !(combatant_stat_byte(g, slot, STAT_STATUS_1) & STATUS_NO_TECHS));
}

/* What the check leaves as it ends: the MP of the last member whose MP it
 * compared, kept in the word the original compares with
 * (battle_tech_mp_compared, which a steal aimed at the party reads as that
 * member's marks), with the multiply the original found the MP's cost by; and
 * as scaffolding, the part it stopped at in the cell the hits repeated count,
 * and that member's slot in the division's remainder. */
enum { WIDE_REMAINDER = 10, NOBODY_COMPARED = 0xFF };

static void leave_check(GameState *g, uint8_t tech, unsigned stopped_at, uint8_t compared)
{
    ORIGINAL_EFFECT(set_battle_hits_left(g, (uint8_t)stopped_at));
    if (compared == NOBODY_COMPARED)
        return;
    ORIGINAL_EFFECT(set_battle_wide_math(g, WIDE_REMAINDER, compared));
    ORIGINAL_EFFECT(set_battle_wide_math(g, WIDE_REMAINDER + 1, 0));
    multiply_out_tech_place(g, tech);
    set_battle_tech_mp_compared(g, combatant_mp(g, compared));
}

// $C1:D74F
/* Whether every member of the tech can take part; the answer kept for the
 * member's turn (battle_tech_blocked). */
enum { ALL_TAKE_PART = 0x00, NOT_ALL_TAKE_PART = 0xFF };

static bool members_can_take_part(GameState *g, uint8_t tech, const PartyTech *t)
{
    unsigned part;
    uint8_t compared = NOBODY_COMPARED;
    bool all = true;
    for (part = 0; part < TECH_MEMBERS && all; part++) {
        uint8_t member = battle_tech_members(g, part);
        if (member == NO_MEMBER)
            continue;
        uint8_t slot = slot_of(g, member);
        if (!battle_member_disabled(g, slot))
            compared = slot;
        all = member_takes_part(g, t, part, slot);
    }
    leave_check(g, tech, all ? TECH_MEMBERS : part - 1, compared);
    set_battle_tech_blocked(g, all ? ALL_TAKE_PART : NOT_ALL_TAKE_PART);
    return all;
}

/* ---- Aiming a member's attack ---------------------------------------------------------------- */

/* The flag the battle's code shares (battle_action_came_to_nothing), which the
 * member's turn reads after the work: set where the attack found no enemy
 * to aim at. */
enum { NOTHING_TO_AIM_AT = 1 };

static bool enemy_in_fight(const GameState *g)
{
    for (uint8_t slot = BATTLE_FIRST_ENEMY; slot < BATTLE_COMBATANTS; slot++)
        if (combatant_in_fight(g, slot))
            return true;
    return false;
}

/* The enemy the attack aims at: the first candidate where it is still in the
 * fight, or the one standing in its place; otherwise one drawn at random from
 * those in the fight. */
static uint8_t living_enemy(GameState *g)
{
    uint8_t first = battle_candidates(g, 0);
    if (first != NO_CANDIDATE) {
        if (combatant_in_fight(g, first))
            return first;
        uint8_t standing = battle_places(g, first);
        if (combatant_in_fight(g, standing))
            return standing;
    }
    uint8_t drawn;
    do
        drawn = battle_random(g, BATTLE_FIRST_ENEMY, BATTLE_COMBATANTS);
    while (!combatant_in_fight(g, drawn));
    return drawn;
}

// $C1:D63F
static bool aim_at_an_enemy(GameState *g)
{
    if (!enemy_in_fight(g)) {
        set_battle_action_came_to_nothing(g, NOTHING_TO_AIM_AT);
        return false;
    }
    uint8_t enemy = living_enemy(g);
    set_battle_candidates(g, 0, enemy);
    set_battle_action_targets(g, 0, enemy);
    TIMED_AS(g, 0xC1AD09, battle_mark_first_target(g));
    return true;
}

/* ---- A part of the tech -------------------------------------------------------------------- */

/* The part's effect copied where the kinds read it; once its member is known
 * to be in the battle, its modifier and flags taken, with the tech's
 * can't-miss and can't-be-turned-away. */
static EffectRecord copy_effect(GameState *g, uint8_t effect)
{
    set_battle_effect_record_place(g, effect_times(g, effect, EFFECT_RECORD_BYTES));
    uint8_t bytes[EFFECT_RECORD_BYTES];
    party_effect_bytes(g, effect, bytes);
    for (unsigned n = 0; n < EFFECT_RECORD_BYTES; n++)
        set_battle_effect_record(g, n, bytes[n]);
    return effect_record_decode(bytes, bytes[0]);
}

static void take_flags(GameState *g, const EffectRecord *e, uint8_t modifier, uint8_t sure)
{
    set_battle_effect_modifier(g, modifier);
    // $C1:EB0E
    set_battle_effect_flags(g, 0, (uint8_t)(e->flags | sure));
    set_battle_effect_flags(g, 1, e->more_flags);
}

/* How the tech marks those it acts on, with its member's slot, and what it is,
 * for the action's showing and the enemies' scripts; an attack's showing is
 * numbered by its effect's damage formula, less one. A tech's further members
 * are its further targets for the showing: none past the first missing, the
 * rest left as they were. */
static void note_the_part(GameState *g, const PartyTech *t, const EffectRecord *e, uint8_t tech, uint8_t actor,
                          bool attack)
{
    set_battle_action_actor_bits(g, (uint8_t)(t->marks | actor));
    set_battle_action_tech_hits(g, attack ? 0 : tech);
    set_battle_action_elements(g, t->elements);
    set_battle_action_step(g, 0);
    if (attack)
        set_battle_action_what(g, (uint8_t)(e->damage.formula - 1));
    for (unsigned further = 0; further < BATTLE_ACTION_FURTHER_TARGETS_COUNT; further++) {
        uint8_t member = battle_tech_members(g, 1 + further);
        set_battle_action_further_targets(g, further, member == NO_MEMBER ? NO_CANDIDATE : slot_of(g, member));
        if (member == NO_MEMBER)
            break;
    }
}

/* The work starts each part with no status given yet and no enemy beaten. */
static void clear_part_marks(GameState *g)
{
    set_battle_effect_hit_test(g, 0);
    for (unsigned slot = 0; slot < BATTLE_EFFECT_GIVEN_STATUS_COUNT; slot++)
        set_battle_effect_given_status(g, slot, 0);
    set_battle_enemy_beaten_any(g, 0);
    for (unsigned enemy = 0; enemy < BATTLE_ENEMY_BEATEN_COUNT; enemy++)
        set_battle_enemy_beaten(g, enemy, 0);
}

/* After a part that missed, the tech stops where a byte of work RAM has bit 7:
 * the original reads it by the tech's record's offset in the wrong bank, where
 * the record's last byte is meant, and so reads whatever the battle's work RAM
 * holds there. Only the original's memory has that byte (docs/TODO.md). */
enum { STOPS_AT_MISS = 0x80, MISREAD_BASE = 0x7EB1F5 };

static bool stops_after_miss(const GameState *g, uint8_t tech)
{
    return (wram_read8(g, MISREAD_BASE + (uint32_t)(tech * PARTY_TECH_BYTES)) & STOPS_AT_MISS)
           && (battle_effect_missed(g) & STOPS_AT_MISS);
}

/* A part worked out: TECH_WORKED_OUT where the tech goes on. */
static TechOutcome work_out_part(GameState *g, Platform *pf, const PartyTech *t, uint8_t tech, unsigned part,
                                 uint8_t sure)
{
    EffectRecord e = copy_effect(g, t->effects[part]);
    uint8_t member = battle_tech_members(g, part);
    uint8_t actor = member == NO_MEMBER ? NO_SLOT : slot_of(g, member);
    if (actor == NO_SLOT)
        return TECH_MEMBER_ABSENT;
    ORIGINAL_EFFECT(set_battle_tech_performer(g, actor));
    take_flags(g, &e, t->modifiers[part], sure);
    set_battle_actor(g, actor);
    if (actor < BATTLE_PARTY_SIZE) {
        bool attack = battle_action_type(g) != ACTION_TECH;
        if (attack && !aim_at_an_enemy(g))
            return TECH_NOTHING_TO_AIM_AT;
        note_the_part(g, t, &e, tech, actor, attack);
    }
    clear_part_marks(g);
    effect_run_kind(g, pf, (EffectKind)e.kind);
    set_battle_effect_tally(g, (uint8_t)(battle_effect_tally(g) + 1));
    return stops_after_miss(g, tech) ? TECH_STOPPED_AT_MISS : TECH_WORKED_OUT;
}

/* ---- The tech ------------------------------------------------------------------------------ */

static uint8_t sure_flags(const PartyTech *t)
{
    return (uint8_t)((t->flags & TECH_CANT_MISS ? EFFECT_CANT_MISS : 0)
                     | (t->flags & TECH_CANT_BE_TURNED_AWAY ? EFFECT_CANT_BE_TURNED_AWAY : 0));
}

/* The action's notes cleared as the work ends, as an enemy's are. */
static void clear_action_notes(GameState *g)
{
    set_battle_action_actor_bits(g, 0);
    set_battle_action_tech_hits(g, 0);
    set_battle_action_elements(g, 0);
    set_battle_action_step(g, 0);
}

static TechOutcome work_out_parts(GameState *g, Platform *pf, const PartyTech *t, uint8_t tech)
{
    set_battle_damage_hit(g, 0);
    set_battle_effects_worked(g, 0);
    set_battle_effect_tally(g, 0);
    uint8_t sure = sure_flags(t);
    set_battle_effect_sure(g, sure);
    uint8_t targets = battle_action_target_count(g);
    set_battle_effect_target_count(g, targets);
    for (unsigned part = 0; part < PARTY_TECH_PARTS; part++) {
        set_battle_action_target_count(g, targets);
        set_battle_effect_number(g, t->effects[part]);
        if (!(t->effects[part] & NO_PART)) {
            TechOutcome outcome = work_out_part(g, pf, t, tech, part, sure);
            if (outcome != TECH_WORKED_OUT)
                return outcome;
        }
        set_battle_damage_hit(g, (uint8_t)(battle_damage_hit(g) + 1));
        set_battle_effects_worked(g, (uint8_t)(battle_effects_worked(g) + 1));
    }
    uint8_t added = t->flags & ACTION_HITS_ADDED;
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_3, added));
    if (added)
        effect_add_up_hits(g, added);
    return TECH_WORKED_OUT;
}

/* An attack (battle_action_choice) goes ahead whatever the MP: it is worked
 * out as the character's attack tech, which costs nothing. */

// $C1:D523
static TechOutcome work_out_tech(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xFDACFD, battle_clear_damage_table(g));
    set_battle_effect_total(g, 0);
    set_battle_steal_outcome(g, 0);
    uint8_t tech = battle_action_detail(g);
    set_battle_action_record_place(g, effect_times(g, tech, PARTY_TECH_BYTES));
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, true));
    PartyTech t = party_tech(g, tech);
    battle_keep_word(g, BATTLE_CLOCK_READS, t.members);
    ORIGINAL_EFFECT(set_battle_tech_members_place(g, (uint16_t)(t.members * TECH_MEMBERS)));
    for (unsigned part = 0; part < TECH_MEMBERS; part++)
        set_battle_tech_members(g, part, tech_member(g, t.members, part));
    TechOutcome outcome = TECH_CANT_BE_DONE;
    if (members_can_take_part(g, tech, &t) || (battle_action_choice(g) & CHOSE_AN_ATTACK))
        outcome = work_out_parts(g, pf, &t, tech);
    clear_action_notes(g);
    return outcome;
}

// $C1:BF46
/* The original leaves where it found the item's use in the direct page's
 * first word, which the clock's modifier may read (battle.h). */

void effect_use_item(GameState *g, Platform *pf)
{
    uint8_t item = battle_action_detail(g);
    uint8_t use = item_use(g, item);
    battle_keep_word(g, BATTLE_CLOCK_READS, item_use_place(item));
    if (use & ITEM_USE_RESTORES)
        effect_run_kind(g, pf, EFFECT_ITEM_RESTORE);
    else if (use & ITEM_USE_CURES)
        effect_run_kind(g, pf, EFFECT_ITEM_CURE);
    else if (use & ITEM_USE_GIVES_STATUS)
        effect_run_kind(g, pf, EFFECT_ITEM_STATUS);
}

// $C1:BF79
/* The member's turn's flag (battle_action_came_to_nothing) is clear as the
 * work starts; the work sets it where an attack had nothing to aim at. */
TechOutcome effect_work_out_member_tech(GameState *g, Platform *pf)
{
    set_battle_action_came_to_nothing(g, 0);
    return work_out_tech(g, pf);
}

/* ---- The entries --------------------------------------------------------------------------- */

/* What the port works out of a tech: its members all in the party's slots
 * (the check of whether they can take part reads the party's tables by
 * them), each part's kind as the port has it for the part's member (battle/
 * effect_work.c), targets that are combatants, and for an attack, a first
 * candidate that is one or none, standing in a place that is one (the aim
 * reads where it stands, and takes whoever stands there). The rest is turned
 * away to the original. */
static bool members_in_party(const GameState *g, const PartyTech *t)
{
    for (unsigned part = 0; part < TECH_MEMBERS; part++) {
        uint8_t member = tech_member(g, t->members, part);
        if (member != NO_MEMBER && (member >= BATTLE_CHARACTER_SLOT_COUNT || slot_of(g, member) >= BATTLE_PARTY_SIZE))
            return false;
    }
    return true;
}

/* A part with no member: the original reads the slot of the id of none, the
 * byte past the character slots, and ends the tech where it is none; any
 * other it takes as the part's member's. */
static bool no_member_ends_the_tech(const GameState *g)
{
    return battle_character_slot_unchecked(g, NO_MEMBER) == NO_SLOT;
}

static bool parts_ported(const GameState *g, const PartyTech *t)
{
    uint8_t sure = sure_flags(t);
    for (unsigned part = 0; part < PARTY_TECH_PARTS; part++) {
        uint8_t effect = t->effects[part];
        if (effect & NO_PART)
            continue;
        uint8_t member = tech_member(g, t->members, part);
        if (member == NO_MEMBER)
            return no_member_ends_the_tech(g);
        uint8_t actor = slot_of(g, member);
        if (actor == NO_SLOT)
            return true;
        EffectRecord e = party_effect_record(g, effect);
        if (!effect_kind_ported(g, actor, e.kind, (uint8_t)(e.flags | sure), e.more_flags, t->modifiers[part]))
            return false;
    }
    return true;
}

static bool aim_ported(const GameState *g)
{
    uint8_t first = battle_candidates(g, 0);
    return battle_action_type(g) == ACTION_TECH || first == NO_CANDIDATE
           || (first < BATTLE_COMBATANTS && battle_places(g, first) < BATTLE_COMBATANTS);
}

static bool member_tech_ported(const GameState *g)
{
    PartyTech t = party_tech(g, battle_action_detail(g));
    return effect_targets_named(g) && members_in_party(g, &t) && parts_ported(g, &t) && aim_ported(g);
}

bool battle_work_out_member_tech_ready(Snes *s)
{
    vars_peek(true);
    bool ready = member_tech_ported(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && ready;
}

/* The work hands back A's high byte clear, and the carry set but where it
 * stopped at a miss: then the carry the part's kind left (battle/entries.h).
 * The member's turn next finds the tech's MP cost by a multiply, which keeps
 * the carry it comes in with in its second number's cell ($C1:BBFA). */
void battle_work_out_member_tech(Snes *s)
{
    GameState *g = state_of(s);
    TechOutcome outcome = effect_work_out_member_tech(g, m_platform(s));
    s->cpu.a &= 0x00FF;
    m_set_flag(s, CPU_FLAG_C, outcome != TECH_STOPPED_AT_MISS || battle_kind_leaves_carry(battle_effect_record(g, 0)));
    m_rts(s);
}

/* An item's kinds are all the port's: the actor and the targets must be
 * combatants. */
static bool item_use_ported(const GameState *g) { return battle_actor(g) < BATTLE_COMBATANTS && effect_targets_named(g); }

bool battle_use_item_ready(Snes *s)
{
    vars_peek(true);
    bool ready = item_use_ported(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && ready;
}

/* It hands back A's high byte clear. */
void battle_use_item(Snes *s)
{
    effect_use_item(state_of(s), m_platform(s));
    s->cpu.a &= 0x00FF;
    m_rts(s);
}
