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
#include "battle/random.h"
#include "battle/setup_data.h"
#include "battle/timers.h"
#include "charge.h"
#include "original_effects.h"
#include "port/native.h"
#include "vars.h"

/* The battle's steps read A alone. */
enum { STEP_READS_A = DC_X | DC_Y | DC_FLAGS };

/* The battle's step `number`, from the JSR at `site`; A's high byte is clear
 * where the original's code before it leaves it so. */
static bool step(Snes *s, uint32_t site, BattleStep number, bool a_high_clear)
{
    if (a_high_clear)
        s->cpu.a = number;
    else
        m_set_a8(s, number);
    /* the overflow flag the step saves with the flags across its frame waits
     * is read by nothing after (dc-evidence: tools/v_readers.py C1:0003: V
     * dead, its tables' indexes at most $FE (A's high byte known clear, on the
     * battle's direct page $0000); ct_dev --poison-set V at each site over the
     * library, calls with V inverted: C1B43B 0, C1B7D7 0, C1B7F7 0, C1B800 0,
     * C1B809 0; 0 differing; a site under 100 rests on the static result) */
    return battle_call(s, site, BATTLE_STEP, STEP_READS_A);
}

/* The member a step is about, in the cell the steps read it from. */
static void set_step_member(GameState *g, uint8_t member) { set_battle_effect_sprite_attributes(g, member); }

/* A member's attack on the action's first target ($C1:BFAA), called from
 * bank $FD through the long call at $C1:FDC7. */
enum { ATTACK_FROM_BANK_FD = 0xC1FDC7 };
static bool attack(Snes *s, uint32_t site)
{
    return battle_run_long(s, site, ATTACK_FROM_BANK_FD, battle_member_attacks_from_bank_fd);
}

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
static bool attack_for_it(Snes *s, uint32_t site, uint8_t member, uint8_t target)
{
    GameState *g = state_of(s);
    set_battle_action_targets(g, 0, target);
    set_battle_action_choice(g, CHOSE_AN_ATTACK);
    set_battle_actor(g, member);
    return attack(s, site);
}

static void note_acted_for_itself(GameState *g)
{
    set_battle_member_acts_for_itself(g, (uint8_t)(battle_member_acts_for_itself(g) + 1));
}

// $FD:AB30
/* The statuses are the ones of the stat block the caller names, the
 * member's own as the game calls it. */
static bool status_turn(Snes *s, uint8_t member, uint8_t statuses_of)
{
    GameState *g = state_of(s);
    set_battle_member_turn_mark(g, 0);
    set_battle_member_acts_for_itself(g, 0);
    if (driven_to_fight(g, statuses_of)) {
        if (member < BATTLE_PARTY_SIZE && !attack_for_it(s, 0xFDAB6E, member, random_enemy(g)))
            return false;
    } else if (confused(g, statuses_of)) {
        if (!attack_for_it(s, 0xFDAB9A, member, random_other(g, member)))
            return false;
    } else {
        return true;
    }
    note_acted_for_itself(g);
    return true;
}

// $C1:B3F9
static bool cancel_if_unable(Snes *s, uint8_t member)
{
    GameState *g = state_of(s);
    if (!combatant_cant_act(g, member) || battle_member_ready(g, member) == 0)
        return true;
    set_battle_member_ready(g, member, 0);
    set_step_member(g, member);
    set_battle_low_scratch(g, BATTLE_ITEM_RETURNING_FOR_AT, member);
    set_battle_low_scratch(g, BATTLE_ITEM_RETURNING_FOR_AT + 1, 0);
    set_battle_timer_flags_byte(g, BATTLE_TIMER_TURN, member, 0);
    set_battle_member_actions_state(g, member, 0);
    if (!step(s, 0xC1B43B, BATTLE_STEP_CALLED_OFF, true))
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

static bool counter_attack(Snes *s, uint8_t member, bool *attacker_out)
{
    GameState *g = state_of(s);
    set_battle_actor(g, member);
    set_battle_action_targets(g, 0, (uint8_t)battle_turn_slot(g));
    if (!attack(s, 0xFDACC9))
        return false;
    *attacker_out = (combatant_stat_byte(g, battle_action_targets(g, 0), STAT_CONDITION) & CONDITION_KNOCKED_OUT) != 0;
    return true;
}

// $FD:AC6E
static bool counter_the_attack(Snes *s)
{
    GameState *g = state_of(s);
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
            else if (!counter_attack(s, member, &attacker_out))
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
    set_battle_low_scratch(g, BATTLE_ITEM_RETURNING_FOR_AT, member);
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

/* A member waiting with its action has its menu closed, by the step its
 * caller's code runs for it ($C1:B961, from $FD:A937). */
enum { CLOSE_MENU_SITE = 0xFDA937, CLOSE_MENU = 0xC1B961 };

// $FD:A8FE
/* A member of a combination called off, which waited with its action: its
 * action forgotten and its menu closed. The original's code clears A first. */
static bool call_off_waiting_member(Snes *s, uint8_t member)
{
    GameState *g = state_of(s);
    s->cpu.a = 0;
    if (!battle_member_ready(g, member))
        return true;
    forget_combination(g, member);
    set_step_member(g, member);
    return battle_run_long(s, CLOSE_MENU_SITE, CLOSE_MENU, battle_member_called_off_step);
}

/* Each member is seen to by its own call. */
enum { CALL_OFF_WAITING_MEMBER = 0xFDA8FE };
static const uint32_t call_off_sites[BATTLE_PARTY_SIZE] = { 0xFDA966, 0xFDA972, 0xFDA97E };

// $FD:A95F
/* The combination called off while its members waited with their actions. */
static bool call_off_while_waiting(Snes *s)
{
    GameState *g = state_of(s);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        if (!battle_combo_called_off(g, member))
            continue;
        set_battle_low_scratch(g, BATTLE_ITEM_RETURNING_FOR_AT, member);
        if (!battle_run(s, call_off_sites[member], CALL_OFF_WAITING_MEMBER, battle_call_off_waiting_member))
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
static bool see_to_knocked_out(Snes *s)
{
    GameState *g = state_of(s);
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
    if (battle_enemy_beaten_any(g) && !battle_is_demo(g) && !step(s, 0xC1B7D7, BATTLE_STEP_ENEMIES_BEATEN, true))
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

/* A demo battle's members are ready with an action from the start; the demo's
 * records name it. */
enum { DEMO_READY = 0x81, DEMO_ACTION_STATE = 0x41 };

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
enum { DEMO_GAUGE_FULL = 0x30, RECORD_BYTES = 7 };

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
    for (unsigned at = 0; at < BATTLE_PARTY_SIZE * RECORD_BYTES; at++)
        set_battle_member_actions_record_byte(g, at / RECORD_BYTES, at % RECORD_BYTES, demo_party_actions_byte(g, demo, at));
}

static const uint32_t stand_down_sites[BATTLE_PARTY_SIZE] = { 0xC1B7F7, 0xC1B800, 0xC1B809 };

// $C1:B7F2
static bool stand_down(Snes *s)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_step_member(state_of(s), member);
        if (!step(s, stand_down_sites[member], BATTLE_STEP_CALLED_OFF, member == 0))
            return false;
    }
    return true;
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

void battle_member_status_turn(Snes *s)
{
    if (status_turn(s, (uint8_t)s->cpu.y, (uint8_t)battle_slot_of_stat_block(s->cpu.x)))
        m_rtl(s);
}

/* The original finds the member's stat block with A's high byte under the
 * member's number, clear as its callers leave it. */
bool battle_cancel_if_unable_ready(Snes *s) { return battle_on_page_a_high_clear(s) && s->cpu.x < BATTLE_PARTY_SIZE; }
void battle_cancel_if_unable(Snes *s)
{
    if (cancel_if_unable(s, (uint8_t)s->cpu.x))
        m_rts(s);
}

bool battle_party_counters_ready(Snes *s) { return peek(s, members_hit_are_combatants); }
void battle_party_counters(Snes *s)
{
    if (counter_the_attack(s))
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

void battle_call_off_waiting_member(Snes *s)
{
    if (call_off_waiting_member(s, battle_low_scratch(state_of(s), BATTLE_ITEM_RETURNING_FOR_AT)))
        m_rts(s);
}

void battle_call_off_waiting_combination(Snes *s)
{
    if (call_off_while_waiting(s))
        m_rtl(s);
}

void battle_call_off_combination_entry(Snes *s)
{
    battle_call_off_combination(state_of(s));
    m_rtl(s);
}

bool battle_see_to_knocked_out_enemies_ready(Snes *s) { return peek(s, reaction_targets_are_combatants); }
void battle_see_to_knocked_out_enemies(Snes *s)
{
    if (see_to_knocked_out(s))
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

void battle_party_stands_down(Snes *s)
{
    if (stand_down(s))
        m_rts(s);
}
