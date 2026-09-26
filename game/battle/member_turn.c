/* Battle: a party member's chosen action carried out.
 *
 * When a party member has chosen an action in its menu and its turn comes,
 * the main loop has it carried out here ($C1:B967). The member (the first of
 * the party, from the one the loop names, whose action is waiting) and the
 * partners of its combination tech all take part; if any of them can't act
 * now (battle/combatant.h, combatant_cant_act), the tech is called off for
 * them all and what they had queued is dropped (the original's $FD:A93C).
 * Otherwise the action is carried out by what the member chose:
 *
 *   - an attack, aimed at the enemy it chose (or a living one, where that one
 *     is out), worked out as the character's attack tech ($C1:BFAA);
 *   - an item, aimed by the original's targeting ($C1:C1DD), worked out
 *     (battle/party_work.c) and shown;
 *   - a tech, aimed likewise, worked out, its MP paid by each member that
 *     performed it, shown, and a steal's outcome told in a message; then
 *     every member of it waits longer before its next turn, by the tech.
 *
 * Whatever it was, the member's turn timer starts again from its turn wait,
 * and its action record, and its partners', are cleared for the next -- but
 * where the choice is only its bit 4 (CHOICE_HELD; its meaning isn't worked
 * out, and a knocked-out member's choice loses it, battle/combatants.c), the
 * turn leaves them as they are.
 *
 * The showing ($C1:AC57) and the attack are siblings run as the original's
 * JSRs call them; the targeting, the calling off and the messages are the
 * original's still, call-outs from the entry's bridge. The entries are at the
 * end. */
#include "battle/effect_parts.h"

#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/enemy_actions.h"
#include "battle/entries.h"
#include "battle/targets.h"
#include "battle/rules_data.h"
#include "battle/timers.h"
#include "charge.h"
#include "port/native.h"

/* ---- The member and its partners ------------------------------------------------------------ */

/* A member's action waits its turn to be carried out with bit 6 of its record's
 * state; what it chose is the first set of its choice's bits. */
enum { ACTION_WAITING = 0x40 };
enum { CHOSE_AN_ATTACK = 0x80, CHOSE_AN_ITEM = 0x40, CHOSE_A_TECH = 0x20, CHOICE_HELD = 0x10 };

/* A partner's place in either nibble of the record's partners, $F for none,
 * and $FF for no partners at all. */
enum { NO_PARTNER = 0x0F, NO_PARTNERS = 0xFF };

static uint8_t first_partner(uint8_t partners) { return partners & 0x0F; }
static uint8_t second_partner(uint8_t partners) { return (uint8_t)(partners >> 4); }

// $C1:B978
/* The member whose action is carried out: from the one the loop names (the
 * third for any past it) round the party, the first whose action is waiting,
 * which stops waiting. */
static uint8_t take_waiting_member(GameState *g)
{
    uint8_t member = battle_actor(g) < BATTLE_PARTY_SIZE ? battle_actor(g) : BATTLE_PARTY_SIZE - 1;
    while (!(battle_member_actions_state(g, member) & ACTION_WAITING))
        member = (uint8_t)((member + 1) % BATTLE_PARTY_SIZE);
    set_battle_member_actions_state(g, member, (uint8_t)(battle_member_actions_state(g, member) & ~ACTION_WAITING));
    set_battle_actor(g, member);
    return member;
}

/* The member and its partners marked as taking part (battle_combo_called_off,
 * which calls them all off if one can't act). The original keeps the
 * partners in a cell as it marks them (scaffolding). */
static void mark_those_taking_part(GameState *g, uint8_t member)
{
    for (unsigned m = 0; m < BATTLE_PARTY_SIZE; m++)
        set_battle_combo_called_off(g, m, 0);
    set_battle_acting_for_itself(g, member);
    set_battle_combo_called_off(g, member, 1);
    uint8_t partners = battle_member_actions_partners(g, member);
    ORIGINAL_EFFECT(set_battle_member_partners_kept(g, partners));
    if (first_partner(partners) != NO_PARTNER)
        set_battle_combo_called_off(g, first_partner(partners), 1);
    if (second_partner(partners) != NO_PARTNER)
        set_battle_combo_called_off(g, second_partner(partners), 1);
}

// $FD:A8A5
/* Whether all those taking part can act, asked of each in turn until one
 * can't (the original leaves the last answer in a cell: scaffolding). */
static bool all_can_act(GameState *g)
{
    for (unsigned m = 0; m < BATTLE_PARTY_SIZE; m++) {
        if (!battle_combo_called_off(g, m))
            continue;
        bool out = combatant_cant_act(g, m);
        ORIGINAL_EFFECT(set_battle_combo_member_out(g, out));
        if (out)
            return false;
    }
    return true;
}

/* ---- Aiming ------------------------------------------------------------------------------------ */

/* An item or a tech is aimed first at the target the member chose, alone,
 * with the action not stopping at its first target; the original's targeting
 * ($C1:C1DD, a call-out) then finds the rest. The first candidate it keeps
 * is the chosen target too. */
enum { STOPS_AT_FIRST = 0x02, NO_TARGET = 0xFF, ITEM_NUMBER_BASE = 0xBC };

static void aim_at_the_choice(GameState *g, uint8_t member)
{
    set_battle_action_state(g, (uint8_t)(battle_action_state(g) & ~STOPS_AT_FIRST));
    set_battle_action_target_count(g, 1);
    set_battle_action_targets(g, 0, battle_member_actions_target(g, member));
    set_battle_first_candidate(g, battle_member_actions_target(g, member));
}

/* The action's targets become its candidates, as many as it has up to the
 * first none, and are marked for the showing. */
static void targets_to_candidates(GameState *g)
{
    uint8_t count = 0;
    set_battle_candidate_count(g, 0);
    for (uint8_t place = 0; place < BATTLE_ACTION_TARGETS_COUNT; place++) {
        uint8_t target = battle_action_targets(g, place);
        if (target == NO_TARGET)
            break;
        set_battle_candidates(g, place, target);
        set_battle_candidate_count(g, ++count);
        if (count == battle_action_target_count(g))
            break;
    }
    TIMED_AS(g, 0xC1AD09, battle_mark_first_target(g));
    TIMED_AS(g, 0xC1AD35, battle_mark_other_targets(g));
}

/* An action whose first candidate is an enemy has the enemies react. */
static void note_enemy_hit(GameState *g)
{
    if (battle_candidates(g, 0) >= BATTLE_FIRST_ENEMY)
        set_battle_action_hit_enemy(g, 1);
}

/* How the action is recorded for the showing: who performs it, its kind,
 * what it is; before the work, from the member's side (source 0) with
 * nothing more to say, which the work may add to. */
enum { ACTION_ITEM = 0x03 };

static void record_for_showing(GameState *g, uint8_t performer, uint8_t kind, uint8_t what)
{
    set_battle_action_actor(g, performer);
    set_battle_action_type(g, kind);
    set_battle_action_what(g, what);
}

static void record_before_work(GameState *g, uint8_t performer, uint8_t kind, uint8_t what)
{
    set_battle_action_source(g, 0);
    set_battle_action_extra(g, 0);
    record_for_showing(g, performer, kind, what);
}

/* A tech's first member's slot: the member who chose it, whose tech the
 * record's members name (battle/party_work.c), or the last tech's, before
 * this one is worked out. */
static uint8_t tech_performer(const GameState *g) { return battle_character_slot(g, battle_tech_members(g, 0)); }

/* ---- After a tech: the members wait ------------------------------------------------------------ */

/* Each member of a tech waits longer before its next turn, by its turn wait
 * times the tenths the tech says for its part (tech_wait_tenths), at most
 * 255. The original divides the first member's by whatever its multiply left
 * in the divisor's cell -- nothing -- so it adds nothing, and sets only that
 * member's full gauge, not the gauge itself (the game plays it so). */
enum { TENTHS = 10, MOST_WAIT = 0xFF };

/* The member's record starts over as its turn ends: what it chose forgotten. */
static void forget_choice(GameState *g, uint8_t member)
{
    set_battle_member_actions_choice_flags(g, member, 0);
    set_battle_member_actions_tech_aim(g, member, 0);
    set_battle_member_actions_target(g, member, 0);
    set_battle_member_actions_what(g, member, 0);
}

static void member_waits_after_tech(GameState *g, uint8_t tech, unsigned part)
{
    uint8_t slot = battle_character_slot(g, battle_tech_members(g, part));
    if (part == 0) {
        set_battle_member_actions_state(g, slot, 0);
        forget_choice(g, slot);
    }
    set_battle_member_ready(g, slot, 0);
    set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, battle_turn_wait(g, slot));
    battle_adjust_turn_wait(g, slot);
    uint8_t wait = battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot);
    bool carry = true;
    if (wait != MOST_WAIT) {
        uint16_t product = effect_times(g, tech_wait_tenths(g, tech, part), wait);
        uint8_t more = (uint8_t)effect_over(g, product, part == 0 ? 0 : TENTHS).quotient;
        carry = wait + more > MOST_WAIT;
        wait = carry ? MOST_WAIT : (uint8_t)(wait + more);
        set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, wait);
    }
    if (part != 0)
        set_battle_gauge_wait(g, slot, wait);
    set_battle_gauge_wait_full(g, slot, wait);
    if (battle_timer_flags_byte(g, BATTLE_TIMER_TURN, slot)) {
        battle_restart_timer(g, BATTLE_TIMER_TURN, slot);
        set_battle_member_ready(g, slot, 0);
        /* the record's place is found by a multiply, entered with the
         * carry of the wait's sum */
        (void)effect_times(g, 7, slot);
        ORIGINAL_EFFECT(battle_leave_multiply_carry(g, carry));
        set_battle_member_actions_state(g, slot, 0);
    }
}

// $C1:BDB3
static void members_wait_after_tech(GameState *g)
{
    uint8_t tech = battle_action_detail(g);
    for (unsigned part = 0; part < TECH_MEMBERS; part++) {
        if (part > 0 && battle_tech_members(g, part) == NO_MEMBER)
            return;
        member_waits_after_tech(g, tech, part);
    }
}

/* ---- The turn's end ------------------------------------------------------------------------------ */

// $C1:BCF8
/* The acting member's record and its partners' start over: what it chose
 * forgotten, and the partnership undone (each partner's choice too). */
static void end_turn(GameState *g)
{
    uint8_t member = battle_acting_for_itself(g);
    set_battle_actor(g, member);
    forget_choice(g, member);
    uint8_t partners = battle_member_actions_partners(g, member);
    if (partners != NO_PARTNERS) {
        /* the original keeps the partners in the multiply's first cell */
        set_battle_wide_math(g, 0, partners);
        set_battle_member_actions_choice_flags(g, first_partner(partners), 0);
        set_battle_member_actions_partners(g, first_partner(partners), NO_PARTNERS);
        if (second_partner(partners) != NO_PARTNER) {
            set_battle_member_actions_choice_flags(g, second_partner(partners), 0);
            set_battle_member_actions_partners(g, second_partner(partners), NO_PARTNERS);
        }
    }
    set_battle_member_actions_partners(g, member, NO_PARTNERS);
    ORIGINAL_EFFECT(set_battle_member_turn_running(g, 0));
}

// $C1:BCE1
/* A member that acted alone: its turn timer starts again from its turn wait,
 * running if it was, and its record starts over. */
static void member_turn_ends(GameState *g)
{
    uint8_t member = battle_actor(g);
    set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, member, battle_turn_wait(g, member));
    battle_adjust_turn_wait(g, member);
    if (battle_timer_flags_byte(g, BATTLE_TIMER_TURN, member))
        battle_restart_timer(g, BATTLE_TIMER_TURN, member);
    end_turn(g);
}

/* ---- Paying for a tech ---------------------------------------------------------------------------- */

// $C1:CC71
/* A member of the tech pays the MP of its part, cut by its accessory. The
 * original charges any member past the second from the third's MP (the
 * game's members are the first three), and keeps the cost where a tech's hits
 * are noted ($0E, battle.h). The carry it leaves is whether the MP covered
 * the cost (set where there was no member). */
static bool pay_for_part(GameState *g, const PartyTech *t, unsigned part)
{
    uint8_t member = battle_tech_members(g, part);
    if (member == NO_MEMBER)
        return true;
    uint8_t slot = battle_character_slot(g, member);
    uint8_t payer = slot < BATTLE_PARTY_SIZE - 1 ? slot : BATTLE_PARTY_SIZE - 1;
    uint8_t cost = effect_part_mp_cost(g, t, part, payer);
    battle_keep_word(g, BATTLE_TECH_HITS_LEFT, cost);
    uint16_t mp = (uint16_t)(combatant_stat_byte(g, payer, STAT_MP) | combatant_stat_byte(g, payer, STAT_MP + 1) << 8);
    uint16_t left = (uint16_t)(mp - cost);
    set_combatant_stat_byte(g, payer, STAT_MP, (uint8_t)left);
    set_combatant_stat_byte(g, payer, STAT_MP + 1, (uint8_t)(left >> 8));
    return mp >= cost;
}

/* Every member pays; the original finds each part's cost by a multiply, the
 * last entered with the carry the second member's paying left. */
static void pay_for_tech(GameState *g)
{
    uint8_t tech = battle_action_detail(g);
    PartyTech t = party_tech(g, tech);
    pay_for_part(g, &t, 0);
    bool carry = pay_for_part(g, &t, 1);
    (void)effect_times(g, tech, PARTY_TECH_BYTES);
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, carry));
    pay_for_part(g, &t, 2);
}

/* ---- The entries ------------------------------------------------------------------------------- */

/* The call-outs: the original's targeting, which reads the carry the code
 * before leaves; the calling off, the messages, and the battle's step. */
enum {
    AIM = 0xC1C1DD, CALL_OFF = 0xFDA93C, SHOW_MESSAGE = 0xCD0021, BATTLE_STEP = 0xC10003,
    SHOWING = 0xC1AC57, MEMBER_ATTACKS = 0xC1BFAA,
};

/* Where the turn calls the item's and the tech's working out, and the
 * attack the character's attack tech's: the port stops there, leaving them
 * to the original, where it doesn't work them out (their entries' guards'
 * tests, asked once the targeting has run). */
enum { USE_ITEM_CALL = 0xC1BB0C, WORK_OUT_TECH_CALL = 0xC1BBD4, ATTACK_TECH_CALL = 0xC1C006 };
enum {
    READS_CARRY = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z,
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_FLAGS,
    READS_A = DC_X | DC_Y | DC_FLAGS,
};

/* The action shown ($C1:AC57, a sibling), from `site`: it takes A's high byte
 * along to the battle's step, clear here as the original's code leaves it. */
static bool show_action(Snes *s, uint32_t site)
{
    s->cpu.a &= 0x00FF;
    return battle_run(s, site, SHOWING, battle_start_action_showing);
}

/* The messages a steal's outcome tells: the item taken (named in its
 * variable), and nothing to take. */
enum { MESSAGE_ITEM_TAKEN = 0x03, MESSAGE_NOTHING_TO_TAKE = 0x0D, OUTCOME_NOTHING = 0xFF };

static bool show_message(Snes *s, uint32_t site, uint8_t message)
{
    m_set_a8(s, message);
    return battle_call_long(s, site, SHOW_MESSAGE, READS_A);
}

// $C1:BC21
static bool tell_steal(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t outcome = battle_steal_outcome(g);
    if (outcome == 0)
        return true;
    if (outcome & 0x80)
        return outcome != OUTCOME_NOTHING || show_message(s, 0xC1BC46, MESSAGE_NOTHING_TO_TAKE);
    set_battle_message_item(g, battle_low_scratch(g, BATTLE_ITEM_TAKEN));
    if (!show_message(s, 0xC1BC30, MESSAGE_ITEM_TAKEN))
        return false;
    set_battle_member_kept_item_waiting(g, battle_actor(g), 1);
    return true;
}

/* The tech's members the turn reads before the tech is worked out, which are
 * the last tech's (the enemies' techs share the last two places, by their
 * ids): the first's slot for the showing, and where the turn then waits by
 * them (a tech the targeting found nothing for), each up to the first none,
 * whose slots index the party's tables. */
static bool last_tech_fits(const GameState *g, bool waits)
{
    for (unsigned part = 0; part < (waits ? TECH_MEMBERS : 1); part++) {
        uint8_t character = battle_tech_members(g, part);
        if (part > 0 && character == NO_MEMBER)
            return true;
        if (character >= BATTLE_CHARACTER_SLOT_COUNT
            || (waits && battle_character_slot(g, character) >= BATTLE_PARTY_SIZE))
            return false;
    }
    return true;
}

/* The original's targeting, from `site`; where what it left is past what the
 * port aims with (a count of targets past the list, or a target past the
 * combatants), or the last tech's members the turn goes on to read aren't the
 * party's, the port stops at the call's return and the original carries on. */
static bool aim(Snes *s, uint32_t site, bool carry, bool tech)
{
    m_set_flag(s, CPU_FLAG_C, carry);
    if (!battle_call(s, site, AIM, READS_CARRY))
        return false;
    GameState *g = state_of(s);
    bool nothing_found = battle_member_acts_for_itself(g) != 0;
    bool fits = battle_action_target_count(g) <= BATTLE_ACTION_TARGETS_COUNT
                && ((!tech && nothing_found) || last_tech_fits(g, tech && nothing_found));
    for (uint8_t place = 0; fits && place < battle_action_target_count(g); place++) {
        uint8_t target = battle_action_targets(g, place);
        if (target == NO_TARGET)
            break;
        fits = target < BATTLE_COMBATANTS;
    }
    if (!fits)
        m_stop_at_own(s, site + BATTLE_JSR_SIZE, DC_A | DC_X | DC_Y | DC_FLAGS);
    return fits;
}

// $C1:BA83
/* An item: its number is the record's, past the items' first ($BC). */
static bool use_item(Snes *s, uint8_t member)
{
    GameState *g = state_of(s);
    for (uint8_t place = 0; place < BATTLE_ACTION_TARGETS_COUNT; place++)
        set_battle_action_targets(g, place, NO_TARGET);
    ORIGINAL_EFFECT(battle_leave_word(g, 0x0C, 0));
    aim_at_the_choice(g, member);
    uint8_t what = battle_member_actions_what(g, member);
    set_battle_action_detail(g, (uint8_t)(what - ITEM_NUMBER_BASE));
    set_battle_action_what(g, (uint8_t)(what - ITEM_NUMBER_BASE));
    /* the targeting takes the carry of the item's number's subtraction */
    if (!aim(s, 0xC1BABE, what >= ITEM_NUMBER_BASE, false))
        return false;
    if (!battle_member_acts_for_itself(g)) {
        record_before_work(g, tech_performer(g), ACTION_ITEM, what);
        battle_keep_byte(g, BATTLE_CLOCK_READS, what);
        targets_to_candidates(g);
        if (!effect_item_use_ported(g)) {
            m_stop_at_own(s, USE_ITEM_CALL, DC_A | DC_X | DC_Y | DC_FLAGS);
            return false;
        }
        TIMED_AS(g, 0xC1BF46, effect_use_item(g, m_platform(s)));
        record_for_showing(g, battle_actor(g), ACTION_ITEM, (uint8_t)(battle_action_detail(g) + ITEM_NUMBER_BASE));
        set_battle_action_extra(g, 0);
        if (!show_action(s, 0xC1BB26))
            return false;
        TIMED_AS(g, 0xFDACEE, battle_clear_numbers_shown(g));
    }
    note_enemy_hit(g);
    TIMED_AS(g, 0xC1BCE1, member_turn_ends(g));
    return true;
}

// $C1:BB49
/* A tech, where the member's record aims it as one the turn carries out. */
enum { AIMS_AT_ONE = 1, AIMS_AT_ALL = 3 };

static bool perform_tech(Snes *s, uint8_t member)
{
    GameState *g = state_of(s);
    uint8_t aim_kind = battle_member_actions_tech_aim(g, member);
    if (aim_kind != AIMS_AT_ONE && aim_kind != AIMS_AT_ALL) {
        TIMED_AS(g, 0xC1BCE1, member_turn_ends(g));
        return true;
    }
    aim_at_the_choice(g, member);
    uint8_t tech = battle_member_actions_what(g, member);
    set_battle_action_detail(g, tech);
    /* the targeting takes the carry of the test of the record's aim, set */
    if (!aim(s, 0xC1BB88, true, true))
        return false;
    bool performed = false;
    if (!battle_member_acts_for_itself(g)) {
        record_before_work(g, tech_performer(g), ACTION_TECH, tech);
        targets_to_candidates(g);
        if (!effect_member_tech_ported(g)) {
            m_stop_at_own(s, WORK_OUT_TECH_CALL, DC_A | DC_X | DC_Y | DC_FLAGS);
            return false;
        }
        effect_work_out_member_tech(g, m_platform(s));
        performed = !battle_member_acts_for_itself(g) && !battle_tech_blocked(g);
    }
    if (performed) {
        record_for_showing(g, tech_performer(g), ACTION_TECH, battle_action_detail(g));
        pay_for_tech(g);
        if (!show_action(s, 0xC1BC1E) || !tell_steal(s))
            return false;
        note_enemy_hit(g);
        TIMED_AS(g, 0xFDACEE, battle_clear_numbers_shown(g));
    }
    TIMED_AS(g, 0xC1BDB3, members_wait_after_tech(g));
    end_turn(g);
    return true;
}

enum { RECORD_PLACE_CELL = 0x06 };

static bool carry_out_action(Snes *s)
{
    GameState *g = state_of(s);
    ORIGINAL_EFFECT(set_battle_member_turn_running(g, 1));
    uint8_t member = take_waiting_member(g);
    mark_those_taking_part(g, member);
    if (!all_can_act(g)) {
        if (!battle_call_long(s, 0xC1BA2C, CALL_OFF, READS_NOTHING))
            return false;
        ORIGINAL_EFFECT(set_battle_member_turn_running(g, 0));
        return true;
    }
    battle_keep_word(g, BATTLE_CLOCK_READS, 0);
    ORIGINAL_EFFECT(set_battle_member_turn_mark(g, 0));
    /* the record's place, in the cell a steal later leaves its item in */
    ORIGINAL_EFFECT(battle_leave_word(g, RECORD_PLACE_CELL, battle_member_record_place(g, member)));
    uint8_t choice = battle_member_actions_choice_flags(g, member);
    set_battle_action_choice(g, choice);
    if (choice & CHOSE_AN_ATTACK) {
        set_battle_action_targets(g, 0, battle_member_actions_target(g, member));
        if (!battle_run(s, 0xC1BA7D, MEMBER_ATTACKS, battle_member_attacks))
            return false;
    } else if (choice & CHOSE_AN_ITEM)
        return use_item(s, member);
    else if (choice & CHOSE_A_TECH)
        return perform_tech(s, member);
    else if (choice & CHOICE_HELD)
        return true;
    TIMED_AS(g, 0xC1BCE1, member_turn_ends(g));
    return true;
}

/* What the port carries out: a waiting member in the party (the original
 * looks round the party until it finds one), partners in the party's places,
 * and records reached as the ROM places them. */
bool battle_carry_out_member_action_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool waiting = false, partners_fit = true;
    for (unsigned m = 0; m < BATTLE_PARTY_SIZE; m++) {
        if (battle_member_actions_state(g, m) & ACTION_WAITING)
            waiting = true;
        uint8_t partners = battle_member_actions_partners(g, m);
        if (partners != NO_PARTNERS
            && (first_partner(partners) >= BATTLE_PARTY_SIZE
                || (second_partner(partners) != NO_PARTNER && second_partner(partners) >= BATTLE_PARTY_SIZE)))
            partners_fit = false;
    }
    vars_peek(false);
    return battle_on_page(s) && waiting && partners_fit;
}

void battle_carry_out_member_action(Snes *s)
{
    if (carry_out_action(s))
        m_rts(s);
}

// $C1:BFAA
/* A member's attack: aimed at its first target alone, stopping there,
 * recorded as an attack, worked out as the character's attack tech ($CC:2583,
 * by the character fighting in the slot), and shown. A member out of the
 * fight does nothing. */
enum { NO_FURTHER_TARGET = 0xFF };

static void set_up_attack(GameState *g)
{
    set_battle_action_state(g, (uint8_t)(battle_action_state(g) | STOPS_AT_FIRST));
    set_battle_action_what(g, 0);
    set_battle_action_target_bits(g, 0);
    set_battle_action_other_targets(g, 0);
    set_battle_action_flags(g, 0);
    set_battle_action_further_targets(g, 0, NO_FURTHER_TARGET);
    set_battle_action_further_targets(g, 1, NO_FURTHER_TARGET);
    set_battle_action_source(g, 0);
    set_battle_action_actor(g, battle_actor(g));
    set_battle_action_type(g, ACTION_ATTACK);
    set_battle_candidates(g, 0, battle_action_targets(g, 0));
    set_battle_action_target_count(g, 1);
    set_battle_candidate_count(g, 1);
    /* the marking hands back the none it found first, which the attack keeps */
    bool none_first = battle_candidates(g, 0) == NO_CANDIDATE;
    TIMED_AS(g, 0xC1AD09, battle_mark_first_target(g));
    set_battle_action_extra(g, none_first ? NO_CANDIDATE : 0);
}

static bool attack(Snes *s)
{
    GameState *g = state_of(s);
    set_up_attack(g);
    uint8_t fighting = combatant_fighting_as(g, battle_actor(g));
    if (fighting != COMBATANT_NONE) {
        set_battle_action_detail(g, character_attack_tech(g, fighting));
        if (!effect_member_tech_ported(g)) {
            m_stop_at_own(s, ATTACK_TECH_CALL, DC_A | DC_X | DC_Y | DC_FLAGS);
            return false;
        }
        effect_work_out_member_tech(g, m_platform(s));
        if (!battle_member_acts_for_itself(g)) {
            if (!show_action(s, 0xC1C00E))
                return false;
            note_enemy_hit(g);
            TIMED_AS(g, 0xFDACEE, battle_clear_numbers_shown(g));
        }
    }
    set_battle_action_state(g, (uint8_t)(battle_action_state(g) & ~STOPS_AT_FIRST));
    return true;
}

/* The member attacking is in the party's slots. */
bool battle_member_attacks_ready(Snes *s)
{
    vars_peek(true);
    bool member = battle_actor(state_of(s)) < BATTLE_PARTY_SIZE;
    vars_peek(false);
    return battle_on_page(s) && member;
}

/* It hands back A's high byte clear. */
void battle_member_attacks(Snes *s)
{
    if (!attack(s))
        return;
    s->cpu.a &= 0x00FF;
    m_rts(s);
}

/* The main loop's end of a member's turn whose status picked its action,
 * entered with A's high byte clear, which the original indexes by. */
bool battle_end_member_turn_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t partners = battle_member_actions_partners(g, battle_acting_for_itself(g));
    bool fits = battle_actor(g) < BATTLE_PARTY_SIZE && battle_acting_for_itself(g) < BATTLE_PARTY_SIZE
                && (partners == NO_PARTNERS
                    || (first_partner(partners) < BATTLE_PARTY_SIZE
                        && (second_partner(partners) == NO_PARTNER || second_partner(partners) < BATTLE_PARTY_SIZE)));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && fits;
}

void battle_end_member_turn(Snes *s)
{
    member_turn_ends(state_of(s));
    m_rts(s);
}

/* A tech's members' waits, entered with its members in the party's slots. */
bool battle_members_wait_after_tech_ready(Snes *s)
{
    vars_peek(true);
    bool fits = last_tech_fits(state_of(s), true);
    vars_peek(false);
    return battle_on_page(s) && fits;
}

void battle_members_wait_after_tech(Snes *s)
{
    members_wait_after_tech(state_of(s));
    m_rts(s);
}

// $C1:B961
/* The battle's step 2, for the calling off of a member's action ($FD:A937). */
enum { STEP_MEMBER_CALLED_OFF = 2 };

void battle_member_called_off_step(Snes *s)
{
    m_set_a8(s, STEP_MEMBER_CALLED_OFF);
    /* the step's overflow flag is the caller's, passed on */
    if (battle_call(s, 0xC1B963, BATTLE_STEP, DC_X | DC_Y | DC_N | DC_Z | DC_C))
        m_rtl(s);
}
