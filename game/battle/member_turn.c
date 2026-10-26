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
 *   - an item, aimed (battle/aim.c), worked out (battle/party_work.c) and
 *     shown;
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
 * The targeting ($C1:C1DD), the showing ($C1:AC57) and the attack are
 * siblings run as the original's JSRs call them; the calling off and the
 * messages are the original's still, call-outs from the entry's bridge. The
 * entries are at the end. */
#include "battle/effect_parts.h"

#include "battle/bridge.h"
#include "battle/program.h"
#include "battle/steps.h"
#include "battle/combatant.h"
#include "battle/enemy_actions.h"
#include "battle/entries.h"
#include "battle/message.h"
#include "battle/party_turns.h"
#include "battle/targets.h"
#include "battle/rules_data.h"
#include "battle/timers.h"
#include "charge.h"
#include "port/native.h"

/* ---- The member and its partners ------------------------------------------------------------ */

// $C1:B978
/* The member whose action is carried out: from the one the loop names (the
 * third for any past it) round the party, the first whose action is waiting,
 * which stops waiting. */
static uint8_t take_waiting_member(GameState *g)
{
    uint8_t member = battle_actor(g) < BATTLE_PARTY_SIZE ? battle_actor(g) : BATTLE_PARTY_SIZE - 1;
    while (!(battle_member_actions_state(g, member) & MEMBER_ACTION_WAITING))
        member = (uint8_t)((member + 1) % BATTLE_PARTY_SIZE);
    set_battle_member_actions_state(g, member, (uint8_t)(battle_member_actions_state(g, member) & ~MEMBER_ACTION_WAITING));
    set_battle_actor(g, member);
    return member;
}

/* The member and its partners marked as taking part (battle_members_taking_part,
 * which the calling off drops the queued actions of if one can't act). The
 * original keeps the partners in a cell as it marks them (scaffolding). */
static void mark_those_taking_part(GameState *g, uint8_t member)
{
    for (unsigned m = 0; m < BATTLE_PARTY_SIZE; m++)
        set_battle_members_taking_part(g, m, 0);
    set_battle_turn_member(g, member);
    set_battle_members_taking_part(g, member, 1);
    uint8_t partners = battle_member_actions_partners(g, member);
    ORIGINAL_EFFECT(set_battle_member_partners_kept(g, partners));
    if (first_partner(partners) != NO_PARTNER)
        set_battle_members_taking_part(g, first_partner(partners), 1);
    if (second_partner(partners) != NO_PARTNER)
        set_battle_members_taking_part(g, second_partner(partners), 1);
}

// $FD:A8A5
/* Whether all those taking part can act, asked of each in turn until one
 * can't (the original leaves the last answer in a cell: scaffolding). */
static bool all_can_act(GameState *g)
{
    for (unsigned m = 0; m < BATTLE_PARTY_SIZE; m++) {
        if (!battle_members_taking_part(g, m))
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
 * with the action not stopping at its first target; the targeting
 * (battle/aim.c) then finds the rest. The first candidate it keeps is the
 * chosen target too. */
enum { NO_TARGET = 0xFF };

static void aim_at_the_choice(GameState *g, uint8_t member)
{
    set_battle_action_state(g, (uint8_t)(battle_action_state(g) & ~ACTION_STOPS_AT_FIRST));
    set_battle_action_target_count(g, 1);
    set_battle_action_targets(g, 0, battle_member_actions_target(g, member));
    set_battle_first_candidate(g, battle_member_actions_target(g, member));
}

/* The action's targets become its candidates, as many as it has up to the
 * first none, and are marked for the showing. */
static void targets_to_candidates(GameState *g)
{
    uint8_t count = 0;
    while (count < BATTLE_ACTION_TARGETS_COUNT && battle_action_targets(g, count) != NO_TARGET) {
        set_battle_candidates(g, count, battle_action_targets(g, count));
        if (++count == battle_action_target_count(g))
            break;
    }
    set_battle_candidate_count(g, count);
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
enum { TENTHS = 10, FIRST_MEMBERS_DIVISOR = 0, MOST_WAIT = 0xFF };

static uint8_t wait_divisor(unsigned part) { return part == 0 ? FIRST_MEMBERS_DIVISOR : TENTHS; }

/* A wait lengthened, at most the longest; and whether it ran past it. */
static uint8_t lengthened(uint8_t wait, uint8_t more, bool *ran_past)
{
    *ran_past = wait + more > MOST_WAIT;
    return *ran_past ? MOST_WAIT : (uint8_t)(wait + more);
}

/* The member's record starts over as its turn ends: what it chose forgotten. */
static void forget_choice(GameState *g, uint8_t member)
{
    set_battle_member_actions_choice_flags(g, member, 0);
    set_battle_member_actions_command(g, member, 0);
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
    uint8_t wait = battle_restart_turn_wait(g, slot);
    bool ran_past = true;
    if (wait != MOST_WAIT) {
        uint16_t product = effect_times(g, tech_wait_tenths(g, tech, part), wait);
        uint8_t more = (uint8_t)effect_over(g, product, wait_divisor(part)).quotient;
        wait = lengthened(wait, more, &ran_past);
        set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, wait);
    }
    if (part != 0)
        set_battle_gauge_wait(g, slot, wait);
    set_battle_gauge_wait_full(g, slot, wait);
    if (battle_timer_running(g, BATTLE_TIMER_TURN, slot)) {
        battle_restart_timer(g, BATTLE_TIMER_TURN, slot);
        set_battle_member_ready(g, slot, 0);
        (void)effect_times(g, MEMBER_ACTION_RECORD_BYTES, slot);
        ORIGINAL_EFFECT(battle_leave_multiply_carry(g, ran_past));
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
/* The original keeps the partners in the multiply's first number's cell as it
 * undoes them, which the battle's arithmetic takes up where a caller leaves
 * it as it is (battle/arith.h): kept plainly. */
static void keep_partners_in_first_number(GameState *g, uint8_t partners) { set_battle_wide_math(g, 0, partners); }

static void end_turn(GameState *g)
{
    uint8_t member = battle_turn_member(g);
    set_battle_actor(g, member);
    forget_choice(g, member);
    uint8_t partners = battle_member_actions_partners(g, member);
    if (partners != NO_PARTNERS) {
        keep_partners_in_first_number(g, partners);
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
    (void)battle_restart_turn_wait(g, member);
    if (battle_timer_running(g, BATTLE_TIMER_TURN, member))
        battle_restart_timer(g, BATTLE_TIMER_TURN, member);
    end_turn(g);
}

/* ---- Paying for a tech ---------------------------------------------------------------------------- */

// $C1:CC71
/* A member of the tech pays the MP of its part, cut by its accessory, and the
 * cost is kept where a tech's hits are noted ($0E, battle.h). (The original
 * charges a member past the second from the third's MP; the members are the
 * party's, whose slots never are.) */
static void pay_for_part(GameState *g, const PartyTech *t, unsigned part)
{
    uint8_t member = battle_tech_members(g, part);
    if (member == NO_MEMBER)
        return;
    uint8_t slot = battle_character_slot(g, member);
    uint8_t cost = effect_part_mp_cost(g, t, part, slot);
    battle_keep_word(g, BATTLE_TECH_HITS_LEFT, cost);
    set_combatant_mp(g, slot, (uint16_t)(combatant_mp(g, slot) - cost));
}

/* The original finds each part's cost by a multiply (its record's place), the
 * last of them entered with the carry of the second member's paying: whether
 * its MP covered the cost, which what it has left and the cost say (set where
 * there is no second member). Scaffolding. */
static void leave_payment_carry(GameState *g, const PartyTech *t)
{
    uint8_t member = battle_tech_members(g, 1);
    bool covered = true;
    if (member != NO_MEMBER) {
        uint8_t slot = battle_character_slot(g, member);
        covered = combatant_mp(g, slot) + effect_part_mp_cost(g, t, 1, slot) <= 0xFFFF;
    }
    battle_leave_multiply_carry(g, covered);
}

/* Every member pays for its part. */
static void pay_for_tech(GameState *g)
{
    uint8_t tech = battle_action_detail(g);
    PartyTech t = party_tech(g, tech);
    (void)effect_times(g, tech, PARTY_TECH_BYTES);
    for (unsigned part = 0; part < TECH_MEMBERS; part++)
        pay_for_part(g, &t, part);
    ORIGINAL_EFFECT(leave_payment_carry(g, &t));
}

/* ---- The turn, as one sequence --------------------------------------------------------------- */

/* The turn's calls, each where the original makes it: the targeting
 * ($C1:C1DD, battle/aim.c), which takes the carry the code before leaves, and
 * where what it left is past what the port reads, the original's to go on
 * from; an item used ($C1:BF46, battle/party_work.c), a tech worked out
 * ($C1:BF79), the action shown ($C1:AC57), the messages a steal tells
 * ($CD:0021), and the attack ($C1:BFAA, below). */
typedef enum {
    MEMBER_ATTACK, MEMBER_AIM_ITEM, MEMBER_AIM_TECH, MEMBER_ITEM_AIMED_PAST, MEMBER_TECH_AIMED_PAST, MEMBER_USE_ITEM,
    MEMBER_WORK_OUT_TECH, MEMBER_WORK_OUT_ATTACK, MEMBER_SHOW_ITEM, MEMBER_SHOW_TECH, MEMBER_SHOW_ATTACK,
    MEMBER_ITEM_TAKEN, MEMBER_NOTHING_TO_TAKE,
} MemberCall;
typedef bool (*MemberCalls)(void *ctx, MemberCall call, bool carry);

/* The messages a steal's outcome tells: the item taken (named in its
 * variable), and nothing to take. */
enum { MESSAGE_ITEM_TAKEN = 0x03, MESSAGE_NOTHING_TO_TAKE = 0x0D };

// $C1:BC21
static bool tell_steal(GameState *g, MemberCalls call, void *ctx)
{
    StealOutcome outcome = effect_last_steal(g);
    if (outcome == STEAL_NOT_TRIED || outcome == STEAL_MISSED)
        return true;
    if (outcome == STEAL_NOTHING)
        return call(ctx, MEMBER_NOTHING_TO_TAKE, false);
    set_battle_message_item(g, battle_item_taken(g));
    if (!call(ctx, MEMBER_ITEM_TAKEN, false))
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

/* What the targeting left that the port aims with: no more targets than the
 * list holds, each a combatant up to the first none. */
static bool targets_fit(const GameState *g)
{
    uint8_t count = battle_action_target_count(g);
    if (count > BATTLE_ACTION_TARGETS_COUNT)
        return false;
    for (uint8_t place = 0; place < count && battle_action_targets(g, place) != NO_TARGET; place++)
        if (battle_action_targets(g, place) >= BATTLE_COMBATANTS)
            return false;
    return true;
}

/* The last tech's members the turn goes on to read: an item that came to
 * nothing reads none; anything else reads the first's slot, and a tech that
 * came to nothing waits by them all. */
static bool members_read_fit(const GameState *g, bool tech)
{
    bool nothing = battle_action_came_to_nothing(g) != 0;
    return (!tech && nothing) || last_tech_fits(g, tech && nothing);
}

/* The targeting, with the carry its lookup of the tech's members leaves in
 * a cell; where what it left is past what the port reads, the original goes
 * on. */
static bool aim(GameState *g, bool carry, bool tech, MemberCalls call, void *ctx)
{
    if (!call(ctx, tech ? MEMBER_AIM_TECH : MEMBER_AIM_ITEM, carry))
        return false;
    if (targets_fit(g) && members_read_fit(g, tech))
        return true;
    (void)call(ctx, tech ? MEMBER_TECH_AIMED_PAST : MEMBER_ITEM_AIMED_PAST, false);
    return false;
}

// $C1:BA83
/* An item: its number is the record's, past the items' first ($BC). Its area
 * rules are none, which the targeting reads in a cell its caller clears. */
enum { ITEM_TARGETING_CELL = 0x0C };
static bool use_item(GameState *g, uint8_t member, MemberCalls call, void *ctx)
{
    for (uint8_t place = 0; place < BATTLE_ACTION_TARGETS_COUNT; place++)
        set_battle_action_targets(g, place, NO_TARGET);
    battle_keep_word(g, ITEM_TARGETING_CELL, 0);
    aim_at_the_choice(g, member);
    uint8_t what = battle_member_actions_what(g, member);
    set_battle_action_detail(g, (uint8_t)(what - BATTLE_FIRST_ITEM_NUMBER));
    set_battle_action_what(g, (uint8_t)(what - BATTLE_FIRST_ITEM_NUMBER));
    /* the targeting takes the carry of the item's number's subtraction */
    if (!aim(g, what >= BATTLE_FIRST_ITEM_NUMBER, false, call, ctx))
        return false;
    if (!battle_action_came_to_nothing(g)) {
        /* the item's performer is recorded first as the last tech's first
         * member's slot, as the original has it, and the member's after */
        record_before_work(g, tech_performer(g), ACTION_ITEM, what);
        battle_keep_byte(g, BATTLE_CLOCK_READS, what);
        targets_to_candidates(g);
        if (!call(ctx, MEMBER_USE_ITEM, false))
            return false;
        record_for_showing(g, battle_actor(g), ACTION_ITEM, (uint8_t)(battle_action_detail(g) + BATTLE_FIRST_ITEM_NUMBER));
        set_battle_action_extra(g, 0);
        if (!call(ctx, MEMBER_SHOW_ITEM, false))
            return false;
        TIMED_AS(g, 0xFDACEE, battle_clear_numbers_shown(g));
    }
    note_enemy_hit(g);
    TIMED_AS(g, 0xC1BCE1, member_turn_ends(g));
    return true;
}

// $C1:BB49
/* A tech, where the member's record says the member chose Tech from its menu
 * (battle_menu_command), or 3, which the menu never sets. */
enum { COMMAND_TECH = 1, COMMAND_NO_MENU_SETS = 3 };

static bool perform_tech(GameState *g, uint8_t member, MemberCalls call, void *ctx)
{
    uint8_t command = battle_member_actions_command(g, member);
    if (command != COMMAND_TECH && command != COMMAND_NO_MENU_SETS) {
        TIMED_AS(g, 0xC1BCE1, member_turn_ends(g));
        return true;
    }
    aim_at_the_choice(g, member);
    uint8_t tech = battle_member_actions_what(g, member);
    set_battle_action_detail(g, tech);
    /* the targeting takes the carry of the test of the record's aim, set */
    if (!aim(g, true, true, call, ctx))
        return false;
    bool performed = false;
    if (!battle_action_came_to_nothing(g)) {
        record_before_work(g, tech_performer(g), ACTION_TECH, tech);
        targets_to_candidates(g);
        if (!call(ctx, MEMBER_WORK_OUT_TECH, false))
            return false;
        performed = !battle_action_came_to_nothing(g) && !battle_tech_blocked(g);
    }
    if (performed) {
        record_for_showing(g, tech_performer(g), ACTION_TECH, battle_action_detail(g));
        pay_for_tech(g);
        if (!call(ctx, MEMBER_SHOW_TECH, false) || !tell_steal(g, call, ctx))
            return false;
        note_enemy_hit(g);
        TIMED_AS(g, 0xFDACEE, battle_clear_numbers_shown(g));
    }
    TIMED_AS(g, 0xC1BDB3, members_wait_after_tech(g));
    end_turn(g);
    return true;
}

// $C1:B967
static bool carry_out_action(GameState *g, MemberCalls call, void *ctx)
{
    ORIGINAL_EFFECT(set_battle_member_turn_running(g, 1));
    uint8_t member = take_waiting_member(g);
    mark_those_taking_part(g, member);
    if (!all_can_act(g)) {
        TIMED_AS(g, 0xFDA93C, battle_call_off_combination(g));
        ORIGINAL_EFFECT(set_battle_member_turn_running(g, 0));
        return true;
    }
    battle_keep_word(g, BATTLE_CLOCK_READS, 0);
    ORIGINAL_EFFECT(set_battle_member_turn_mark(g, 0));
    /* the targeting finds the member's action record through it (battle.h) */
    battle_keep_word(g, BATTLE_MEMBER_CELL, battle_member_record_place(g, member));
    uint8_t choice = battle_member_actions_choice_flags(g, member);
    set_battle_action_choice(g, choice);
    if (choice & CHOSE_AN_ATTACK) {
        set_battle_action_targets(g, 0, battle_member_actions_target(g, member));
        if (!call(ctx, MEMBER_ATTACK, false))
            return false;
    } else if (choice & CHOSE_AN_ITEM)
        return use_item(g, member, call, ctx);
    else if (choice & CHOSE_A_TECH)
        return perform_tech(g, member, call, ctx);
    else if (choice & CHOICE_HELD)
        return true;
    TIMED_AS(g, 0xC1BCE1, member_turn_ends(g));
    return true;
}

// $C1:BFAA
/* A member's attack: aimed at its first target alone, stopping there,
 * recorded as an attack, worked out as the character's attack tech ($CC:2583,
 * by the character fighting in the slot), and shown. A member out of the
 * fight does nothing. */
enum { NO_FURTHER_TARGET = 0xFF };

static void set_up_attack(GameState *g)
{
    set_battle_action_state(g, (uint8_t)(battle_action_state(g) | ACTION_STOPS_AT_FIRST));
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

static bool attack(GameState *g, MemberCalls call, void *ctx)
{
    set_up_attack(g);
    uint8_t fighting = combatant_fighting_as(g, battle_actor(g));
    if (fighting != COMBATANT_NONE) {
        set_battle_action_detail(g, character_attack_tech(g, fighting));
        if (!call(ctx, MEMBER_WORK_OUT_ATTACK, false))
            return false;
        if (!battle_action_came_to_nothing(g)) {
            if (!call(ctx, MEMBER_SHOW_ATTACK, false))
                return false;
            note_enemy_hit(g);
            TIMED_AS(g, 0xFDACEE, battle_clear_numbers_shown(g));
        }
    }
    set_battle_action_state(g, (uint8_t)(battle_action_state(g) & ~ACTION_STOPS_AT_FIRST));
    return true;
}

/* The guards' state halves: a waiting member in the party (the original
 * looks round the party until it finds one), partners in the party's places,
 * and records reached as the ROM places them; the attacker in the party's
 * slots. */
static bool action_fits(const GameState *g)
{
    bool waiting = false, partners_fit = true;
    for (unsigned m = 0; m < BATTLE_PARTY_SIZE; m++) {
        if (battle_member_actions_state(g, m) & MEMBER_ACTION_WAITING)
            waiting = true;
        uint8_t partners = battle_member_actions_partners(g, m);
        if (partners != NO_PARTNERS
            && (first_partner(partners) >= BATTLE_PARTY_SIZE
                || (second_partner(partners) != NO_PARTNER && second_partner(partners) >= BATTLE_PARTY_SIZE)))
            partners_fit = false;
    }
    return waiting && partners_fit;
}

static bool attacker_fits(const GameState *g) { return battle_actor(g) < BATTLE_PARTY_SIZE; }

/* ---- On the machine ------------------------------------------------------------------------ */

enum {
    AIM = 0xC1C1DD, SHOW_MESSAGE = 0xCD0021, SHOWING = 0xC1AC57, MEMBER_ATTACKS = 0xC1BFAA,
    USE_ITEM = 0xC1BF46, WORK_OUT_TECH = 0xC1BF79,
};

/* Each call from the original's own: the targeting with the carry it is
 * handed, the showing taking A's high byte along to the battle's step,
 * clear here as the original's code leaves it, the messages with theirs in
 * A. */
static const uint32_t member_sites[] = {
    [MEMBER_ATTACK] = 0xC1BA7D, [MEMBER_AIM_ITEM] = 0xC1BABE, [MEMBER_AIM_TECH] = 0xC1BB88,
    [MEMBER_ITEM_AIMED_PAST] = 0xC1BABE, [MEMBER_TECH_AIMED_PAST] = 0xC1BB88, [MEMBER_USE_ITEM] = 0xC1BB0C,
    [MEMBER_WORK_OUT_TECH] = 0xC1BBD4, [MEMBER_WORK_OUT_ATTACK] = 0xC1C006, [MEMBER_SHOW_ITEM] = 0xC1BB26,
    [MEMBER_SHOW_TECH] = 0xC1BC1E, [MEMBER_SHOW_ATTACK] = 0xC1C00E, [MEMBER_ITEM_TAKEN] = 0xC1BC30,
    [MEMBER_NOTHING_TO_TAKE] = 0xC1BC46,
};

static bool member_on_the_machine(void *ctx, MemberCall call, bool carry)
{
    Snes *s = ctx;
    uint32_t site = member_sites[call];
    switch (call) {
    case MEMBER_ATTACK: return battle_run(s, site, MEMBER_ATTACKS, battle_member_attacks_entry);
    case MEMBER_AIM_ITEM:
    case MEMBER_AIM_TECH:
        m_set_flag(s, CPU_FLAG_C, carry);
        return battle_run(s, site, AIM, battle_aim_action_entry);
    case MEMBER_ITEM_AIMED_PAST:
    case MEMBER_TECH_AIMED_PAST: battle_stop_after_call(s, site); return false;
    case MEMBER_USE_ITEM: return battle_run(s, site, USE_ITEM, battle_use_item_entry);
    case MEMBER_WORK_OUT_TECH:
    case MEMBER_WORK_OUT_ATTACK: return battle_run(s, site, WORK_OUT_TECH, battle_work_out_member_tech_entry);
    case MEMBER_SHOW_ITEM:
    case MEMBER_SHOW_TECH:
    case MEMBER_SHOW_ATTACK:
        s->cpu.a &= 0x00FF;
        return battle_run(s, site, SHOWING, battle_start_action_showing);
    case MEMBER_ITEM_TAKEN:
        m_set_a8(s, MESSAGE_ITEM_TAKEN);
        return battle_run_long(s, site, SHOW_MESSAGE, battle_show_a_battle_message_entry);
    default:
        m_set_a8(s, MESSAGE_NOTHING_TO_TAKE);
        return battle_run_long(s, site, SHOW_MESSAGE, battle_show_a_battle_message_entry);
    }
}

/* ---- The entries ------------------------------------------------------------------------------- */

bool battle_carry_out_member_action_ready(Snes *s)
{
    vars_peek(true);
    bool fits = action_fits(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && fits;
}

void battle_carry_out_member_action_entry(Snes *s)
{
    if (carry_out_action(state_of(s), member_on_the_machine, s))
        m_rts(s);
}

/* The member attacking is in the party's slots. */
bool battle_member_attacks_ready(Snes *s)
{
    vars_peek(true);
    bool member = attacker_fits(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && member;
}

/* It hands back A's high byte clear. */
void battle_member_attacks_entry(Snes *s)
{
    if (!attack(state_of(s), member_on_the_machine, s))
        return;
    s->cpu.a &= 0x00FF;
    m_rts(s);
}

// $C1:FDC7
/* The attack as bank $FD's code calls it, through a long call: a member's
 * status turn and the party's counters (battle/party_turns.c). */
void battle_member_attacks_from_bank_fd(Snes *s)
{
    if (battle_run(s, 0xC1FDC7, MEMBER_ATTACKS, battle_member_attacks_entry))
        m_rtl(s);
}

/* The main loop's end of a member's turn whose status picked its action,
 * entered with A's high byte clear, which the original indexes by. */
static bool member_turn_end_fits(const GameState *g)
{
    uint8_t partners = battle_member_actions_partners(g, battle_turn_member(g));
    return battle_actor(g) < BATTLE_PARTY_SIZE && battle_turn_member(g) < BATTLE_PARTY_SIZE
           && (partners == NO_PARTNERS
               || (first_partner(partners) < BATTLE_PARTY_SIZE
                   && (second_partner(partners) == NO_PARTNER || second_partner(partners) < BATTLE_PARTY_SIZE)));
}

bool battle_end_member_turn_ready(Snes *s)
{
    vars_peek(true);
    bool fits = member_turn_end_fits(state_of(s));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && fits;
}

void battle_end_member_turn_entry(Snes *s)
{
    member_turn_ends(state_of(s));
    m_rts(s);
}

bool battle_end_member_turn(GameState *g)
{
    if (!member_turn_end_fits(g))
        return battle_needs_the_machine(g, 0xC1BCE1, "a member's turn's end with partners past the party");
    member_turn_ends(g);
    return true;
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
void battle_member_called_off_step(Snes *s)
{
    /* the overflow flag the step saves with the flags across its frame waits
     * is read by nothing after (tools/v_readers.py C1:0003). dc-evidence:
     * ct_fuzz --poison-dc battle_member_called_off_step, seed 1: 5000 states,
     * 5000 call-outs scrambled, 0 mismatched */
    if (battle_take_step(s, 0xC1B963, BATTLE_STEP_CALLED_OFF, DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C))
        m_rtl(s);
}

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} MemberAsC;

/* Each with its guard's state half where it has one. */
static bool member_as_c(void *ctx, MemberCall call, bool carry)
{
    MemberAsC *c = ctx;
    GameState *g = c->game;
    bool done = false;
    switch (call) {
    case MEMBER_ATTACK: TIMED_AS(g, MEMBER_ATTACKS, done = battle_member_attacks(g, c->pf)); return done;
    case MEMBER_AIM_ITEM:
    case MEMBER_AIM_TECH: TIMED_AS(g, AIM, done = battle_aim_action(g, c->pf, carry)); return done;
    case MEMBER_ITEM_AIMED_PAST:
    case MEMBER_TECH_AIMED_PAST: return battle_needs_the_machine(g, member_sites[call], "an aim past what the port reads");
    case MEMBER_USE_ITEM: TIMED_AS(g, USE_ITEM, done = battle_use_item(g, c->pf)); return done;
    case MEMBER_WORK_OUT_TECH:
    case MEMBER_WORK_OUT_ATTACK: TIMED_AS(g, WORK_OUT_TECH, done = battle_work_out_member_tech(g, c->pf)); return done;
    case MEMBER_SHOW_ITEM:
    case MEMBER_SHOW_TECH:
    case MEMBER_SHOW_ATTACK: TIMED_AS(g, SHOWING, done = battle_action_shown(g, c->pf)); return done;
    case MEMBER_ITEM_TAKEN: TIMED_AS(g, SHOW_MESSAGE, done = battle_show_a_battle_message(g, c->pf, MESSAGE_ITEM_TAKEN)); return done;
    default: TIMED_AS(g, SHOW_MESSAGE, done = battle_show_a_battle_message(g, c->pf, MESSAGE_NOTHING_TO_TAKE)); return done;
    }
}

bool battle_member_attacks(GameState *g, Platform *pf)
{
    if (!attacker_fits(g))
        return battle_needs_the_machine(g, MEMBER_ATTACKS, "an attacker past the party");
    MemberAsC c = { g, pf };
    return attack(g, member_as_c, &c);
}

bool battle_carry_out_member_action(GameState *g, Platform *pf)
{
    if (!action_fits(g))
        return battle_needs_the_machine(g, 0xC1B967, "a member's action the port doesn't carry out");
    MemberAsC c = { g, pf };
    return carry_out_action(g, member_as_c, &c);
}
