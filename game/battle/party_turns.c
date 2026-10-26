/* Battle: what decides a party member's turn besides the player's choice.
 *
 * A member's statuses can take its turn out of the player's hands. A member
 * driven to fight (the fourth status byte's top bit, which also keeps it from
 * choosing) attacks an enemy at random on each of its turns, and a confused
 * member attacks anyone but itself at random. A member whose status keeps it
 * from acting loses the action it had chosen and was waiting with, its item
 * put back.
 *
 * When an enemy's action hits the party, each member it hit that is fit to
 * answer may counter, if its accessory counters: by the accessory's chance in
 * a hundred, it attacks the enemy back, or (for the other kind) its next turn
 * comes at once.
 *
 * A combination tech whose members can't all take part is called off for
 * every member in it (battle/menu_window.c marks them): each one's chosen
 * action and its partners are forgotten, its item put back, and its turn's
 * wait starts again; one that was waiting with its action has its menu closed
 * (the battle's step 2).
 *
 * After an action, the enemies it knocked out that have no last reaction to
 * make (a reaction section that starts by asking whether it was beaten) are
 * counted beaten and shown so at once. A demo battle's party takes its
 * actions from the demo. As the battle ends, the party stands down: each
 * member's menu closes in turn.
 *
 * The decisions are made on the game's state; the battle's steps and a
 * member's attack run between them, and the functions that reach those are
 * the bridges at the end of the file's sections. */
#include "battle/party_turns.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/enemy_scripts.h"
#include "battle/inventory.h"
#include "battle/program.h"
#include "battle/steps.h"
#include "battle/random.h"
#include "battle/setup_data.h"
#include "battle/timers.h"
#include "charge.h"
#include "original_effects.h"
#include "port/native.h"
#include "vars.h"

/* The routines the party's turns call: a member's attack on the action's
 * first target ($C1:BFAA, through the long call at $C1:FDC7, battle/
 * member_turn.c) for a member driven to fight, confused, or countering; the
 * battle's steps -- an action called off (step 2), the enemies beaten seen to
 * (step 6), each member's menu closed as the party stands down -- and a
 * waiting member's menu closed ($C1:B961), and a member of a combination
 * called off while it waited ($FD:A8FE, below). The program makes them as C,
 * and the machine's entries from the original's calls. */
typedef enum {
    PARTY_ATTACK_DRIVEN, PARTY_ATTACK_CONFUSED, PARTY_COUNTER_ATTACK,
    PARTY_CALLED_OFF_STEP, PARTY_BEATEN_STEP, PARTY_STAND_DOWN_STEP,
    PARTY_CLOSE_THE_MENU, PARTY_CALL_OFF_WAITING_MEMBER,
} PartyCall;
typedef bool (*PartyCalls)(void *ctx, PartyCall call, uint8_t member);

static uint8_t draw(GameState *g, uint8_t lowest, uint8_t limit)
{
    uint8_t drawn = 0;
    TIMED_AS(g, 0xC1AF22, drawn = battle_random(g, lowest, limit));
    return drawn;
}

/* ---- What the statuses decide ------------------------------------------------------------- */

/* The status that drives a member to fight: it keeps it from choosing, and it
 * attacks an enemy at random on its own. */
static bool driven_to_fight(const GameState *g, uint8_t member)
{
    return (combatant_statuses(g, member) >> STATUS_BYTE_4_SHIFT & STATUS4_KEEPS_FROM_ACTING) != 0;
}

static bool confused(const GameState *g, uint8_t member) { return (battle_stats_status(g, member) & STATUS_CONFUSED) != 0; }

/* An enemy at random, one in the fight. */
static uint8_t random_enemy(GameState *g)
{
    uint8_t slot;
    do
        slot = draw(g, BATTLE_FIRST_ENEMY, BATTLE_COMBATANTS);
    while (!combatant_in_fight(g, slot));
    return slot;
}

/* Anyone in the fight at random, but the member itself (which the original
 * keeps in a cell to compare). */
enum { CONFUSED_MEMBER_AT = 0x12 };
static uint8_t random_other(GameState *g, uint8_t member)
{
    ORIGINAL_EFFECT(battle_leave_word(g, CONFUSED_MEMBER_AT, member));
    uint8_t slot;
    do
        slot = draw(g, 0, BATTLE_COMBATANTS);
    while (slot == member || !combatant_in_fight(g, slot));
    return slot;
}

/* The member attacks `target` as though it had chosen to. */
static bool attack_for_it(GameState *g, PartyCall attack, uint8_t member, uint8_t target, PartyCalls call, void *ctx)
{
    set_battle_action_targets(g, 0, target);
    set_battle_action_choice(g, CHOSE_AN_ATTACK);
    set_battle_actor(g, member);
    return call(ctx, attack, member);
}

static void note_acted_for_itself(GameState *g)
{
    set_battle_member_acts_for_itself(g, (uint8_t)(battle_member_acts_for_itself(g) + 1));
}

// $FD:AB30
/* The statuses are the ones of the stat block the caller names, the
 * member's own as the game calls it. */
static bool status_turn(GameState *g, uint8_t member, uint8_t statuses_of, PartyCalls call, void *ctx)
{
    set_battle_member_turn_mark(g, 0);
    set_battle_member_acts_for_itself(g, 0);
    if (driven_to_fight(g, statuses_of)) {
        if (member < BATTLE_PARTY_SIZE && !attack_for_it(g, PARTY_ATTACK_DRIVEN, member, random_enemy(g), call, ctx))
            return false;
    } else if (confused(g, statuses_of)) {
        if (!attack_for_it(g, PARTY_ATTACK_CONFUSED, member, random_other(g, member), call, ctx))
            return false;
    } else {
        return true;
    }
    note_acted_for_itself(g);
    return true;
}

/* The member's chosen action, waiting its turn, is dropped: it is no longer
 * ready, its turn's timer stops and its record's state is cleared. (The
 * original keeps the member whose item goes back in a cell.) */
static void drop_waiting_action(GameState *g, uint8_t member)
{
    set_battle_member_ready(g, member, MEMBER_NO_ACTION);
    set_battle_step_member(g, member);
    ORIGINAL_EFFECT(battle_leave_word(g, BATTLE_ITEM_RETURNING_FOR_AT, member));
    set_battle_timer_flags_byte(g, BATTLE_TIMER_TURN, member, 0);
    set_battle_member_actions_state(g, member, MEMBER_NO_ACTION);
}

// $C1:B3F9
/* A member whose status keeps it from acting loses the action it waited
 * with: its menu closes (the battle's step) and its item goes back. */
static bool cancel_if_unable(GameState *g, uint8_t member, PartyCalls call, void *ctx)
{
    if (!combatant_cant_act(g, member) || battle_member_ready(g, member) == MEMBER_NO_ACTION)
        return true;
    drop_waiting_action(g, member);
    if (!call(ctx, PARTY_CALLED_OFF_STEP, member))
        return false;
    TIMED_AS(g, 0xC1B6D1, battle_return_chosen_item(g, member));
    return true;
}

/* ---- Counters ------------------------------------------------------------------------------ */

/* A member counters when its accessory does (its record's bit $40), and is
 * fit to: not knocked out, nor stopped, asleep, confused, blind or kept from
 * techs. The accessory's chance is its second byte, in a hundred; with its
 * first byte's top bit the member attacks back, else its turn comes next. */
enum { ACCESSORY_COUNTERS = 0x40, COUNTER_ATTACKS = 0x80, CHANCES = 100, TURN_AT_ONCE = 1 };
enum { UNFIT_TO_COUNTER = STATUS_STOP | STATUS_NO_TECHS | STATUS_CONFUSED | STATUS_ASLEEP | STATUS_BLIND };

static bool counters(const GameState *g, uint8_t member)
{
    return !(combatant_stat_byte(g, member, STAT_CONDITION) & CONDITION_KNOCKED_OUT)
           && !(combatant_stat_byte(g, member, STAT_STATUS_1) & UNFIT_TO_COUNTER)
           && (combatant_stat_byte(g, member, STAT_ACCESSORY_RECORD) & ACCESSORY_COUNTERS);
}

/* The original keeps the member's stat block's place in a cell. */
enum { COUNTERING_BLOCK_AT = 0x0C };

/* The members it hit are the action's first three targets. */
enum { MEMBERS_HIT = BATTLE_PARTY_SIZE };

static bool counter_attack(GameState *g, uint8_t member, bool *attacker_out, PartyCalls call, void *ctx)
{
    set_battle_actor(g, member);
    set_battle_action_targets(g, 0, (uint8_t)battle_turn_slot(g));
    if (!call(ctx, PARTY_COUNTER_ATTACK, member))
        return false;
    *attacker_out = (combatant_stat_byte(g, battle_action_targets(g, 0), STAT_CONDITION) & CONDITION_KNOCKED_OUT) != 0;
    return true;
}

// $FD:AC6E
static bool counter_the_attack(GameState *g, PartyCalls call, void *ctx)
{
    if (battle_action_hit_party(g)) {
        bool attacker_out = false;
        for (uint8_t entry = 0; entry < MEMBERS_HIT && !attacker_out; entry++) {
            set_battle_reaction_entry(g, entry);
            uint8_t member = battle_reaction_targets(g, entry);
            if (member == COMBATANT_NONE)
                continue;
            ORIGINAL_EFFECT(battle_leave_word(g, COUNTERING_BLOCK_AT, (uint16_t)(member * BATTLE_STAT_BLOCK_BYTES)));
            if (!counters(g, member) || draw(g, 0, CHANCES) >= combatant_stat_byte(g, member, STAT_ACCESSORY_SECOND))
                continue;
            if (!(combatant_stat_byte(g, member, STAT_ACCESSORY_FIRST) & COUNTER_ATTACKS))
                set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, member, TURN_AT_ONCE);
            else if (!counter_attack(g, member, &attacker_out, call, ctx))
                return false;
        }
        if (!attacker_out)
            set_battle_reaction_entry(g, MEMBERS_HIT);
    }
    set_battle_action_hit_party(g, false);
    return true;
}

/* The original reads the members hit as stat blocks by the whole slot, so a
 * target past the combatants (none, for an enemy's action on the party, which
 * leaves its first three targets party members or none) is the original's. */
static bool members_hit_are_combatants(const GameState *g)
{
    for (unsigned entry = 0; entry < MEMBERS_HIT; entry++) {
        uint8_t member = battle_reaction_targets(g, entry);
        if (member != COMBATANT_NONE && member >= BATTLE_COMBATANTS)
            return false;
    }
    return true;
}

/* ---- Calling a combination off ------------------------------------------------------------- */

/* The member's combination is off: its item put back, its choice and its
 * partners forgotten, and its turn's wait started again. */
static void forget_combination(GameState *g, uint8_t member)
{
    ORIGINAL_EFFECT(battle_leave_byte(g, BATTLE_ITEM_RETURNING_FOR_AT, member));
    TIMED_AS(g, 0xC1B6D1, battle_return_chosen_item(g, member));
    set_battle_member_actions_state(g, member, 0);
    set_battle_member_actions_choice_flags(g, member, 0);
    set_battle_member_actions_partners(g, member, NO_PARTNERS);
    uint8_t wait = battle_turn_wait(g, member);
    set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, member, wait);
    set_battle_gauge_wait(g, member, wait);
    set_battle_gauge_wait_full(g, member, wait);
    set_battle_member_ready(g, member, 0);
    set_battle_timer_flags_byte(g, BATTLE_TIMER_TURN, member, 0);
}

// $FD:A93C, $FD:A8CE
void battle_call_off_combination(GameState *g)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (battle_combo_called_off(g, member))
            forget_combination(g, member);
}

// $FD:A8FE
/* A member of a combination called off, which waited with its action: its
 * action forgotten and its menu closed (the step its caller's code runs for
 * it). */
static bool call_off_waiting_member(GameState *g, uint8_t member, PartyCalls call, void *ctx)
{
    if (!battle_member_ready(g, member))
        return true;
    forget_combination(g, member);
    set_battle_step_member(g, member);
    return call(ctx, PARTY_CLOSE_THE_MENU, member);
}

// $FD:A95F
/* The combination called off while its members waited with their actions. */
static bool call_off_while_waiting(GameState *g, PartyCalls call, void *ctx)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        if (!battle_combo_called_off(g, member))
            continue;
        set_battle_low_scratch(g, BATTLE_ITEM_RETURNING_FOR_AT, member);
        if (!call(ctx, PARTY_CALL_OFF_WAITING_MEMBER, member))
            return false;
    }
    return true;
}

/* ---- The enemies an action knocked out ------------------------------------------------------- */

/* An enemy knocked out whose reactions start by asking whether it was beaten
 * has a last reaction to make; the others are counted beaten now. */
enum { WAS_BEATEN = 0x20 };

static bool has_last_reaction(GameState *g, uint8_t slot)
{
    ScriptPlace reactions = 0;
    TIMED_AS(g, 0xC1B488, reactions = enemy_reactions_start(g, battle_enemy_turn(g), battle_combatant_id(g, slot)));
    set_battle_script_place(g, reactions);
    return enemy_script_byte(g, reactions) == WAS_BEATEN;
}

static bool knocked_out_unseen(const GameState *g, uint8_t slot)
{
    unsigned enemy = slot - BATTLE_FIRST_ENEMY;
    return !battle_enemy_beaten_counted(g, enemy) && !combatant_in_fight(g, slot)
           && !(battle_enemy_standing(g, enemy) & ENEMY_IN_RESERVE);
}

/* The enemy is counted beaten, and shown so with the others now; the action's
 * own beaten won't show it again. */
static void count_beaten_now(GameState *g, unsigned enemy)
{
    set_battle_enemy_beaten(g, enemy, 1);
    set_battle_enemy_beaten_counted(g, enemy, 1);
    set_battle_enemy_beaten_any(g, true);
    set_battle_enemy_beaten_before(g, enemy, 0);
}

/* The original keeps the target it is at in a cell. */
enum { TARGET_AT = 0x10 };

// $C1:B762
static bool see_to_knocked_out(GameState *g, PartyCalls call, void *ctx)
{
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        set_battle_enemy_beaten_before(g, enemy, battle_enemy_beaten(g, enemy));
        set_battle_enemy_beaten(g, enemy, 0);
    }
    set_battle_enemy_beaten_any(g, false);
    for (uint8_t entry = 0; entry < BATTLE_ENEMIES; entry++) {
        set_battle_reaction_entry(g, entry);
        uint8_t slot = battle_reaction_targets(g, entry);
        if (slot < BATTLE_FIRST_ENEMY || slot == COMBATANT_NONE)
            continue;
        ORIGINAL_EFFECT(battle_leave_word(g, TARGET_AT, slot));
        if (knocked_out_unseen(g, slot) && !has_last_reaction(g, slot))
            count_beaten_now(g, slot - BATTLE_FIRST_ENEMY);
    }
    set_battle_reaction_entry(g, BATTLE_ENEMIES);
    if (battle_enemy_beaten_any(g) && !battle_is_demo(g) && !call(ctx, PARTY_BEATEN_STEP, 0))
        return false;
    uint8_t any = 0;
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        uint8_t beaten = battle_enemy_beaten_before(g, enemy);
        set_battle_enemy_beaten(g, enemy, beaten);
        any |= beaten;
    }
    set_battle_enemy_beaten_any(g, any);
    return true;
}

/* The reactions' targets past the combatants (none is $FF) are the
 * original's: it reads the tables after them by the slot. */
static bool reaction_targets_are_combatants(const GameState *g)
{
    for (unsigned entry = 0; entry < BATTLE_ENEMIES; entry++) {
        uint8_t slot = battle_reaction_targets(g, entry);
        if (slot != COMBATANT_NONE && slot >= BATTLE_COMBATANTS)
            return false;
    }
    return true;
}

/* ---- The demo's party, and standing down ----------------------------------------------------- */

/* A demo battle's members are ready with an action from the start, waiting
 * with it, their menus open; the demo's records name the action. */
enum {
    DEMO_READY = MEMBER_ACTION_CHOSEN | MEMBER_MENU_OPEN,
    DEMO_ACTION_STATE = MEMBER_ACTION_WAITING | MEMBER_MENU_OPEN,
};

// $C1:B70E
void battle_demo_party_ready(GameState *g)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_member_ready(g, member, DEMO_READY);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_member_actions_state(g, member, DEMO_ACTION_STATE);
}

/* Every pass of a demo battle the first member's action is next, the
 * members' gauges stand empty, and their records are the demo's again. */
enum { DEMO_GAUGE_FULL = 0x30 };

// $C1:B725
void battle_demo_next_turn(GameState *g)
{
    set_battle_action_queue(g, 0, 0);
    set_battle_action_queue_length(g, 1);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_gauge_wait(g, member, 0);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_gauge_wait_full(g, member, DEMO_GAUGE_FULL);
    uint8_t demo = battle_demo(g);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        for (unsigned at = 0; at < MEMBER_ACTION_RECORD_BYTES; at++)
            set_battle_member_actions_record_byte(g, member, at, demo_member_action_byte(g, demo, member, at));
}

// $C1:B7F2
static bool stand_down(GameState *g, PartyCalls call, void *ctx)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_step_member(g, member);
        if (!call(ctx, PARTY_STAND_DOWN_STEP, member))
            return false;
    }
    return true;
}

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} PartyAsC;

enum { CALL_OFF_WAITING_MEMBER = 0xFDA8FE, MEMBER_ATTACKS = 0xC1FDC7, CLOSE_MENU = 0xC1B961 };

static bool party_as_c(void *ctx, PartyCall call, uint8_t member)
{
    PartyAsC *c = ctx;
    GameState *g = c->game;
    bool done = false;
    switch (call) {
    case PARTY_ATTACK_DRIVEN:
    case PARTY_ATTACK_CONFUSED:
    case PARTY_COUNTER_ATTACK: TIMED_AS(g, MEMBER_ATTACKS, done = battle_member_attacks(g, c->pf)); return done;
    case PARTY_CALLED_OFF_STEP:
    case PARTY_STAND_DOWN_STEP: return battle_step_taken(g, c->pf, BATTLE_STEP_CALLED_OFF);
    case PARTY_BEATEN_STEP: return battle_step_taken(g, c->pf, BATTLE_STEP_ENEMIES_BEATEN);
    case PARTY_CLOSE_THE_MENU:
        TIMED_AS(g, CLOSE_MENU, done = battle_step_taken(g, c->pf, BATTLE_STEP_CALLED_OFF));
        return done;
    default:
        TIMED_AS(g, CALL_OFF_WAITING_MEMBER, done = call_off_waiting_member(g, member, party_as_c, ctx));
        return done;
    }
}

/* Each with its guard's state half: the member a party member, the members
 * hit and the reactions' targets combatants or none. */
bool battle_member_status_turn(GameState *g, Platform *pf, uint8_t member)
{
    PartyAsC c = { g, pf };
    return status_turn(g, member, member, party_as_c, &c);
}

bool battle_cancel_if_unable(GameState *g, Platform *pf, uint8_t member)
{
    if (member >= BATTLE_PARTY_SIZE)
        return battle_needs_the_machine(g, 0xC1B3F9, "a member past the party");
    PartyAsC c = { g, pf };
    return cancel_if_unable(g, member, party_as_c, &c);
}

bool battle_party_counters(GameState *g, Platform *pf)
{
    if (!members_hit_are_combatants(g))
        return battle_needs_the_machine(g, 0xFDAC6E, "a member hit past the combatants");
    PartyAsC c = { g, pf };
    return counter_the_attack(g, party_as_c, &c);
}

bool battle_see_to_knocked_out_enemies(GameState *g, Platform *pf)
{
    if (!reaction_targets_are_combatants(g))
        return battle_needs_the_machine(g, 0xC1B762, "a reaction's target past the combatants");
    PartyAsC c = { g, pf };
    return see_to_knocked_out(g, party_as_c, &c);
}

bool battle_party_stands_down(GameState *g, Platform *pf)
{
    PartyAsC c = { g, pf };
    return stand_down(g, party_as_c, &c);
}

bool battle_call_off_waiting_combination(GameState *g, Platform *pf)
{
    PartyAsC c = { g, pf };
    return call_off_while_waiting(g, party_as_c, &c);
}

/* ---- On the machine ------------------------------------------------------------------------ */

/* The battle's steps read A alone: the overflow flag they save with the flags
 * across their frame waits is read by nothing after (tools/v_readers.py
 * C1:0003), which each call's dc-evidence shows over the states its routine
 * is fuzzed with. A step indexes its table with the whole of A, so A's high
 * byte is part of what the ask hands it (battle_take_step puts the step's
 * number in the low byte) and can't be declared dead: it is clear where the
 * original's code before the step clears it, and otherwise what the step
 * before left there. */
enum { STEP_READS_A = DC_X | DC_Y | DC_FLAGS };

static void clear_a_high_byte(Snes *s) { s->cpu.a &= 0x00FF; }

/* Each member is stood down, and seen to while it waited, from its own call. */
static const uint32_t stand_down_sites[BATTLE_PARTY_SIZE] = { 0xC1B7F7, 0xC1B800, 0xC1B809 };
static const uint32_t call_off_sites[BATTLE_PARTY_SIZE] = { 0xFDA966, 0xFDA972, 0xFDA97E };

static bool party_on_the_machine(void *ctx, PartyCall call, uint8_t member)
{
    Snes *s = ctx;
    switch (call) {
    case PARTY_ATTACK_DRIVEN: return battle_run_long(s, 0xFDAB6E, MEMBER_ATTACKS, battle_member_attacks_from_bank_fd);
    case PARTY_ATTACK_CONFUSED: return battle_run_long(s, 0xFDAB9A, MEMBER_ATTACKS, battle_member_attacks_from_bank_fd);
    case PARTY_COUNTER_ATTACK: return battle_run_long(s, 0xFDACC9, MEMBER_ATTACKS, battle_member_attacks_from_bank_fd);
    case PARTY_CALLED_OFF_STEP:
        clear_a_high_byte(s);
        /* dc-evidence: ct_fuzz --poison-dc battle_cancel_if_unable, seed 1: 5000
         * states, 4904 call-outs scrambled, 0 mismatched */
        return battle_take_step(s, 0xC1B43B, BATTLE_STEP_CALLED_OFF, STEP_READS_A);
    case PARTY_BEATEN_STEP:
        clear_a_high_byte(s);
        /* dc-evidence: ct_fuzz --poison-dc battle_see_to_knocked_out_enemies:knocked_out, seed 1:
         * 5000 states, 4954 call-outs scrambled, 0 mismatched */
        return battle_take_step(s, 0xC1B7D7, BATTLE_STEP_ENEMIES_BEATEN, STEP_READS_A);
    case PARTY_STAND_DOWN_STEP:
        if (member == 0)
            clear_a_high_byte(s);
        /* dc-evidence: ct_fuzz --poison-dc battle_party_stands_down, seed 1: 5000
         * states, 15000 call-outs scrambled, 0 mismatched */
        return battle_take_step(s, stand_down_sites[member], BATTLE_STEP_CALLED_OFF, STEP_READS_A);
    case PARTY_CLOSE_THE_MENU: return battle_run_long(s, 0xFDA937, CLOSE_MENU, battle_member_called_off_step);
    default:
        return battle_run(s, call_off_sites[member], CALL_OFF_WAITING_MEMBER, battle_call_off_waiting_member_entry);
    }
}

/* ---- The entries ------------------------------------------------------------------------- */

/* Each as the original's code calls it: the member in Y and its stat block's
 * place in X for its status turn, the member in X for its waiting action, in
 * its cell for its call off; the others take nothing. */
static bool peek(Snes *s, bool (*fits)(const GameState *))
{
    vars_peek(true);
    bool ok = fits(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && ok;
}

bool battle_member_status_turn_ready(Snes *s)
{
    return battle_on_page(s) && battle_names_stat_block(s->cpu.x) && battle_names_combatant(s->cpu.y);
}

void battle_member_status_turn_entry(Snes *s)
{
    if (status_turn(state_of(s), (uint8_t)s->cpu.y, (uint8_t)battle_slot_of_stat_block(s->cpu.x), party_on_the_machine, s))
        m_rtl(s);
}

/* The original finds the member's stat block with A's high byte under the
 * member's number, clear as its callers leave it. */
bool battle_cancel_if_unable_ready(Snes *s) { return battle_on_page_a_high_clear(s) && s->cpu.x < BATTLE_PARTY_SIZE; }
void battle_cancel_if_unable_entry(Snes *s)
{
    if (cancel_if_unable(state_of(s), (uint8_t)s->cpu.x, party_on_the_machine, s))
        m_rts(s);
}

bool battle_party_counters_ready(Snes *s) { return peek(s, members_hit_are_combatants); }
void battle_party_counters_entry(Snes *s)
{
    if (counter_the_attack(state_of(s), party_on_the_machine, s))
        m_rtl(s);
}

/* The member is in the cell its caller leaves it in. */
bool battle_call_off_waiting_member_ready(Snes *s)
{
    vars_peek(true);
    uint8_t member = battle_low_scratch(state_of(s), BATTLE_ITEM_RETURNING_FOR_AT);
    vars_peek(false);
    return battle_on_page(s) && member < BATTLE_PARTY_SIZE;
}

/* The original's code clears A first. */
void battle_call_off_waiting_member_entry(Snes *s)
{
    s->cpu.a = 0;
    if (call_off_waiting_member(state_of(s), battle_low_scratch(state_of(s), BATTLE_ITEM_RETURNING_FOR_AT),
                                party_on_the_machine, s))
        m_rts(s);
}

void battle_call_off_waiting_combination_entry(Snes *s)
{
    if (call_off_while_waiting(state_of(s), party_on_the_machine, s))
        m_rtl(s);
}

void battle_call_off_combination_entry(Snes *s)
{
    battle_call_off_combination(state_of(s));
    m_rtl(s);
}

bool battle_see_to_knocked_out_enemies_ready(Snes *s) { return peek(s, reaction_targets_are_combatants); }
void battle_see_to_knocked_out_enemies_entry(Snes *s)
{
    if (see_to_knocked_out(state_of(s), party_on_the_machine, s))
        m_rts(s);
}

/* The demo's party ready leaves the record's state in A. */
void battle_demo_party_ready_entry(Snes *s)
{
    battle_demo_party_ready(state_of(s));
    m_set_a8(s, DEMO_ACTION_STATE);
    m_rts(s);
}

void battle_demo_next_turn_entry(Snes *s)
{
    battle_demo_next_turn(state_of(s));
    m_rts(s);
}

void battle_party_stands_down_entry(Snes *s)
{
    if (stand_down(state_of(s), party_on_the_machine, s))
        m_rts(s);
}
