/* Battle: an enemy's turn -- carrying out the action its script chose.
 *
 * On its turn an enemy carries out the action it is at in its chosen block
 * (battle_enemy_next_action), where that is still the one its script chose
 * (battle_enemy_block_actions), and moves on to the next; reacting, it carries
 * out its reaction's action at once. The action does its part (battle/
 * enemy_actions.c) and says what the turn does after it: work its effect out
 * on its targets and show it, only show it, or nothing more. Its effect is
 * worked out in C (battle/effect_work.c); a tech's targeting, messages, an
 * enemy's stat block loaded as it comes in and the showing are routines of
 * their own, which the machine's side of the turn calls as the original's
 * code and the program's side as C.
 *
 * After the turn the enemy has nothing chosen, and where its action was aimed
 * at an enemy (and doesn't say otherwise) the enemies react. */
#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/program.h"
#include "battle/steps.h"
#include "battle/combatant.h"
#include "battle/effect_work.h"
#include "battle/enemy_actions.h"
#include "battle/enemy_scripts.h"
#include "battle/fight.h"
#include "battle/entries.h"
#include "battle/message.h"
#include "battle/random.h"
#include "battle/rules_data.h"
#include "battle/targets.h"
#include "charge.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The turn, as C ------------------------------------------------------------------------ */

static uint8_t acting(const GameState *g) { return battle_enemy_acting(g); }
static bool reacting(const GameState *g) { return battle_action_hit_enemy(g) != 0; }

/* How far the turn has read the script: it starts at the action, and each
 * action's turn reads its operands. This is the turn's result as much as the
 * choosing's: a reaction's search for its next block goes on from it
 * (battle/main_loop.c), and so does the original's calling in. */
static void script_read_to(GameState *g, ScriptPlace at) { set_battle_script_place(g, at); }

/* The turn starts with the action record blank, its further targets none. */
static void start_turn(GameState *g)
{
    uint8_t enemy = acting(g);
    set_battle_script_outcome(g, OUTCOME_HELD);
    set_battle_enemy_turn(g, enemy);
    set_battle_actor(g, (uint8_t)(BATTLE_FIRST_ENEMY + enemy));
    set_battle_action_what(g, 0);
    set_battle_action_extra(g, 0);
    set_battle_action_target_bits(g, 0);
    set_battle_action_other_targets(g, 0);
    set_battle_action_flags(g, 0);
    set_battle_action_further_targets(g, 0, NO_CANDIDATE);
    set_battle_action_further_targets(g, 1, NO_CANDIDATE);
}

/* What the turn does. */
typedef enum { NOTHING_TO_DO, CARRY_OUT, MOVE_ON } TurnStart;

/* The action the turn carries out, read where it is: none where the enemy
 * has no block chosen, or its actions are done. An enemy whose next action
 * is no longer the one its script chose only moves on past it. */
static TurnStart action_to_carry_out(GameState *g)
{
    uint8_t enemy = acting(g);
    ScriptPlace at;
    if (reacting(g)) {
        at = battle_script_actions_place(g);
    } else {
        set_battle_enemy_acts_anyway(g, enemy, false);
        if (battle_enemy_actions_block(g, enemy) == 0xFF)
            return NOTHING_TO_DO;
        at = battle_enemy_next_action(g, enemy);
        script_read_to(g, at);
        if (at != battle_enemy_block_actions(g, enemy))
            return MOVE_ON;
    }
    script_read_to(g, at);
    uint8_t action = enemy_script_byte(g, at);
    set_battle_action_kind(g, action);
    return action < SCRIPT_ACTIONS ? CARRY_OUT : NOTHING_TO_DO;
}

/* The enemy moves on to its next action. */
static void move_on(GameState *g)
{
    if (reacting(g) || battle_script_outcome(g) != OUTCOME_HELD)
        return;
    uint8_t enemy = acting(g);
    ScriptPlace at = battle_enemy_next_action(g, enemy);
    set_battle_script_actions_place(g, at);
    set_battle_enemy_next_action(g, enemy, (ScriptPlace)(at + enemy_action_length(g, enemy_script_byte(g, at))));
}

/* The turn's end: the enemy has nothing chosen, and the numbers shown over the
 * combatants are cleared. An action on an enemy has the enemies react, unless
 * it says they don't. */
enum { NO_REACTIONS = 0x08 };

static void end_turn(GameState *g)
{
    set_battle_enemy_action_chosen(g, acting(g), false);
    if (!reacting(g))
        set_battle_enemy_turn(g, 0);
    TIMED_AS(g, 0xFDACEE, battle_clear_numbers_shown(g));
    uint8_t first = battle_candidates(g, 0);
    if (first >= BATTLE_FIRST_ENEMY && first != NO_CANDIDATE && !(battle_action_quiet(g) & NO_REACTIONS)) {
        set_battle_action_hit_enemy(g, true);
        set_battle_action_quiet(g, 0);
    }
}

/* The effect's first target: the one the action aimed at, where it is still in
 * the fight. An enemy that is gone ends the turn (the enemy stops, and has
 * no condition held); with no one, or a party member gone, another member in
 * the fight is drawn -- where none is, the turn ends there, before its end. */
typedef enum { TARGET_TAKEN, TARGET_GONE, NO_ONE_LEFT } FirstTarget;

enum { MEMBER_DRAW_LIMIT = 0x30, MEMBER_DRAW_SHIFT = 4, ENEMY_ANIMATION_HALTED = 4 };

static bool fighting(const GameState *g, uint8_t slot) { return combatant_fighting_as(g, slot) != COMBATANT_NONE; }

/* A party member in the fight, drawn by thirds of 0-47; false where none is. */
static bool draw_member(GameState *g, uint8_t *member)
{
    if (!fighting(g, 0) && !fighting(g, 1) && !fighting(g, 2))
        return false;
    do
        *member = (uint8_t)(battle_random(g, 0, MEMBER_DRAW_LIMIT) >> MEMBER_DRAW_SHIFT);
    while (!fighting(g, *member));
    set_battle_action_targets(g, 0, *member);
    return true;
}

static FirstTarget take_first_target(GameState *g)
{
    uint8_t target = battle_action_targets(g, 0);
    set_battle_candidates(g, 0, target);
    bool gone = target == NO_CANDIDATE || !fighting(g, target);
    if (gone && target != NO_CANDIDATE && !battle_slot_is_party(target)) {
        set_battle_enemy_animation(g, acting(g), ENEMY_ANIMATION_HALTED);
        set_battle_enemy_last_condition(g, acting(g), NO_CONDITION);
        return TARGET_GONE;
    }
    if (gone && !draw_member(g, &target))
        return NO_ONE_LEFT;
    set_battle_candidates(g, 0, target);
    battle_mark_first_target(g);
    set_battle_effect_flags(g, 0, 0);
    set_battle_effect_flags(g, 1, 0);
    return TARGET_TAKEN;
}

/* Whether the action, its effect worked out, is shown: not where it had no
 * targets, and not where the actor is knocked out (unless it beat an enemy)
 * or can't act. */
enum { CANT_ACT = 0x82 };

static bool effect_shown(GameState *g)
{
    set_battle_enemy_last_condition(g, acting(g), NO_CONDITION);
    if (battle_candidate_count(g) == 0)
        return false;
    uint8_t actor = battle_action_actor(g);
    if (!battle_enemy_beaten(g, acting(g)) && (combatant_stat_byte(g, actor, STAT_CONDITION) & CONDITION_KNOCKED_OUT))
        return false;
    return !(combatant_stat_byte(g, actor, STAT_STATUS_1) & CANT_ACT);
}

/* ---- The actions' turns ---------------------------------------------------------------------- */

/* The routines an action's turn calls, each where the original makes it: the
 * tech's body ($C1:9A3D, below), its targeting ($C1:C1DD, battle/aim.c), an
 * enemy's message ($CD:0033, battle/message.c), an enemy coming into the
 * battle loaded ($FD:B438, battle/fight_enemies.c), and enemies called in
 * ($FD:A990, below). `arg` is the message or the enemy; the targeting takes
 * the carry the code before it leaves. */
typedef enum {
    ACT_TECH_BODY,
    ACT_AIM_TECH, ACT_AIM_SET_STATS_THEN_TECH, ACT_AIM_ADD_TO_STATS_THEN_TECH,
    ACT_MESSAGE_TECH, ACT_MESSAGE_SET_STATS_THEN_TECH, ACT_MESSAGE_ADD_TO_STATS_THEN_TECH, ACT_MESSAGE_SET_STAT,
    ACT_MESSAGE_ADD_TO_STAT, ACT_MESSAGE_FLAGS, ACT_MESSAGE_MESSAGE, ACT_MESSAGE_SET_STATS, ACT_MESSAGE_ADD_TO_STATS,
    ACT_MESSAGE_DIE, ACT_MESSAGE_BRING_BACK, ACT_MESSAGE_CALL_IN,
    ACT_ARRIVAL_CHANGE_INTO, ACT_ARRIVAL_BRING_BACK, ACT_ARRIVAL_CALL_IN, ACT_CALL_IN,
} ActCall;
typedef bool (*ActCalls)(void *ctx, ActCall call, uint8_t arg, bool carry);

/* A message, where the action has one. */
static bool show_message(ActCall at, uint8_t message, ActCalls call, void *ctx)
{
    return message == 0 || call(ctx, at, message, false);
}

static void follow_up(GameState *g, ActionFollowUp f) { set_battle_action_follow_up(g, (uint8_t)f); }

// $C1:99B8
static bool pose_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    (void)at;
    (void)call;
    (void)ctx;
    follow_up(g, FOLLOW_WITH_NOTHING);
    return true;
}

// $C1:99BE
static bool attack_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    (void)call;
    (void)ctx;
    enemy_attack_begins(g, at);
    /* the attack noted for its targets takes the carry the original's test
     * of the attack's number leaves: set for a number past the one that takes
     * the enemy's own detail */
    TIMED_AS(g, 0xFDAB01, effect_note_enemy_action(g, ENEMY_ATTACKS));
    /* its multiply takes the carry of the test of the attack's number: set for
     * a number past the one that takes the enemy's own detail */
    enemy_attack_ends(g);
    return true;
}

static bool tech_after_targets(GameState *g, ActCall aim, ActCall message_at, bool stats_first, bool aim_carry,
                               uint8_t message, ActCalls call, void *ctx)
{
    if (!call(ctx, aim, 0, aim_carry))
        return false;
    if (stats_first && !battle_member_acts_for_itself(g))
        set_battle_action_kind(g, ACTION_TECH);
    charge_legacy_accesses(g, 1);
    if (enemy_tech_aimed(g)) {
        TIMED_AS(g, 0xFDAAD2, effect_note_enemy_action(g, ENEMY_TECHS));
        /* its multiply takes the carry of the marking of the targets */
        enemy_tech_ends(g);
        if (!show_message(message_at, stats_first ? battle_action_message(g) : message, call, ctx))
            return false;
    }
    follow_up(g, FOLLOW_WITH_EFFECT);
    return true;
}

static bool tech_body(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    enemy_tech_begins(g, battle_enemy_turn(g), at, true);
    /* the targeting takes the carry of the test for no third target */
    bool no_third = battle_action_further_targets(g, 1) == NO_CANDIDATE;
    return tech_after_targets(g, ACT_AIM_TECH, ACT_MESSAGE_TECH, false, no_third, enemy_tech_at(g, at).message, call, ctx);
}

// $C1:9A39
static bool tech_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    (void)g;
    (void)at;
    return call(ctx, ACT_TECH_BODY, 0, false);
}

/* Whether adding `amount` to `stat` carries past a byte, or taking it away
 * leaves something: the carry the original's change leaves. */
enum { TAKE_AWAY = 0x80 };

static bool change_carries(uint8_t stat, uint8_t amount)
{
    if (!(amount & TAKE_AWAY))
        return stat + amount > 0xFF;
    return stat >= (uint8_t)(amount & (uint8_t)~TAKE_AWAY);
}

/* The stats set or added to first, then the tech (its message the one they
 * kept). The targeting after takes the carry a further target's selection
 * leaves (set), or where there is none the last stat change's. */
static bool stats_before_tech(GameState *g, ScriptPlace at, bool adding)
{
    enemy_keep_message_before_tech(g, at);
    bool change_carry = false;
    for (unsigned n = 0; n < STAT_CHANGES_BEFORE_TECH; n++) {
        StatChange change = enemy_stat_change_before_tech(g, at, n);
        uint8_t before = combatant_stat_byte_by_place(g, battle_actor(g), change.offset);
        change_carry = adding && change_carries(before, change.value);
        enemy_change_own_stat(g, change, adding);
    }
    EnemyTech tech = enemy_tech_at(g, at);
    enemy_tech_begins(g, battle_enemy_turn(g), at, false);
    return tech.further[0] != SELECT_NOBODY || tech.further[1] != SELECT_NOBODY || change_carry;
}

// $C1:9FD2
static bool set_stats_then_tech_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    bool aim_carry = stats_before_tech(g, at, false);
    return tech_after_targets(g, ACT_AIM_SET_STATS_THEN_TECH, ACT_MESSAGE_SET_STATS_THEN_TECH, true, aim_carry, 0, call, ctx);
}

// $C1:A20B
static bool add_to_stats_then_tech_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    bool aim_carry = stats_before_tech(g, at, true);
    return tech_after_targets(g, ACT_AIM_ADD_TO_STATS_THEN_TECH, ACT_MESSAGE_ADD_TO_STATS_THEN_TECH, true, aim_carry, 0,
                              call, ctx);
}

// $C1:9B48
/* The choosing is a machine entry's here: it is taken from the cells and
 * handed back, the script read as far as it read. */
static bool choose_again_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    (void)call;
    (void)ctx;
    ScriptChoosing c = enemy_choosing_in_cells(g);
    enemy_choose_again(g, &c, at);
    enemy_hand_back_choosing(g, &c);
    return true;
}

// $C1:9B8D
static bool change_into_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    if (enemy_change_into(g, at) == CHANGED_TO_BRING_IN
        && !call(ctx, ACT_ARRIVAL_CHANGE_INTO, (uint8_t)(battle_candidates(g, 0) - BATTLE_FIRST_ENEMY), false))
        return false;
    enemy_change_ends(g);
    return true;
}

/* The actions that do their part and show their message. */

static bool carry_out_with_message(GameState *g, EnemyAction action, ActCall message_at, ScriptPlace at, ActCalls call,
                                   void *ctx)
{
    uint8_t message = enemy_carry_out(g, battle_enemy_turn(g), action, at);
    if (!show_message(message_at, message, call, ctx))
        return false;
    follow_up(g, FOLLOW_WITH_NOTHING);
    return true;
}

static bool set_stat_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    return carry_out_with_message(g, ACTION_SET_STAT, ACT_MESSAGE_SET_STAT, at, call, ctx);
}
static bool add_to_stat_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    return carry_out_with_message(g, ACTION_ADD_TO_STAT, ACT_MESSAGE_ADD_TO_STAT, at, call, ctx);
}
static bool flags_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    return carry_out_with_message(g, ACTION_FLAGS, ACT_MESSAGE_FLAGS, at, call, ctx);
}
static bool message_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    return carry_out_with_message(g, ACTION_MESSAGE, ACT_MESSAGE_MESSAGE, at, call, ctx);
}
static bool set_stats_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    return carry_out_with_message(g, ACTION_SET_STATS, ACT_MESSAGE_SET_STATS, at, call, ctx);
}
static bool add_to_stats_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    return carry_out_with_message(g, ACTION_ADD_TO_STATS, ACT_MESSAGE_ADD_TO_STATS, at, call, ctx);
}

// $C1:9C6F
static bool leave_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    (void)call;
    (void)ctx;
    enemy_carry_out(g, battle_enemy_turn(g), ACTION_LEAVE, at);
    return true;
}

// $C1:9CB3
static bool die_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    if (!show_message(ACT_MESSAGE_DIE, enemy_carry_out(g, battle_enemy_turn(g), ACTION_DIE, at), call, ctx))
        return false;
    battle_mark_first_target(g);
    follow_up(g, FOLLOW_WITH_SHOWING);
    return true;
}

// $C1:A14E
static bool add_to_turn_wait_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    (void)call;
    (void)ctx;
    enemy_carry_out(g, battle_enemy_turn(g), ACTION_ADD_TO_TURN_WAIT, at);
    follow_up(g, FOLLOW_WITH_NOTHING);
    return true;
}

// $C1:9E78
static bool bring_back_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    enemy_bring_back_starts(g, at);
    uint8_t count = 0;
    for (uint8_t enemy = enemy_next_to_bring_back(g, 0); enemy < BATTLE_ENEMIES; enemy = enemy_next_to_bring_back(g, (uint8_t)(enemy + 1))) {
        enemy_bring_back(g, enemy, count++);
        if (!call(ctx, ACT_ARRIVAL_BRING_BACK, enemy, false))
            return false;
        enemy_brought_back(g, enemy, at);
    }
    enemy_bring_back_ends(g, at, count);
    if (!show_message(ACT_MESSAGE_BRING_BACK, battle_action_message(g), call, ctx))
        return false;
    follow_up(g, FOLLOW_WITH_SHOWING);
    return true;
}

// $FD:A990
/* Enemies called in: the caller sets four of its own stats, then every enemy
 * of the formation out of the fight (beaten, or never in it, and not waiting
 * in reserve) comes in, loaded as at the fight's start, with the HP the last
 * stat set says (enemy_call_in_sets_its_stats), and the action is shown as its
 * tech on them. The script is read at the action, as the turn leaves it. */
static bool call_enemies_in(GameState *g, ActCalls call, void *ctx)
{
    ScriptPlace at = battle_script_place(g);
    uint8_t how = enemy_call_in_sets_its_stats(g, at);
    uint8_t count = 0;
    for (uint8_t enemy = enemy_next_to_bring_back(g, 0); enemy < BATTLE_ENEMIES; enemy = enemy_next_to_bring_back(g, (uint8_t)(enemy + 1))) {
        enemy_bring_back(g, enemy, count++);
        if (!call(ctx, ACT_ARRIVAL_CALL_IN, enemy, false))
            return false;
        enemy_called_in(g, enemy, how);
    }
    enemy_call_in_ends(g, at, count);
    return true;
}

// $C1:A396
static bool call_in_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    enemy_call_in_starts(g, at);
    if (!call(ctx, ACT_CALL_IN, 0, false))
        return false;
    battle_mark_first_target(g);
    battle_mark_other_targets(g);
    if (!show_message(ACT_MESSAGE_CALL_IN, battle_action_message(g), call, ctx))
        return false;
    follow_up(g, FOLLOW_WITH_SHOWING);
    return true;
}

static bool nothing_turn(GameState *g, ScriptPlace at, ActCalls call, void *ctx)
{
    (void)g;
    (void)at;
    (void)call;
    (void)ctx;
    return true;
}

/* Each action's turn is its own routine in the original's table, with the
 * script read at the action. How far each reads the script: an attack its
 * number and pose, a tech (in its body) its number and selectors, a change to
 * a turn wait its selector; the others move the place no further (choosing
 * again moves it itself). */
enum { READS_NOTHING_MORE = 0, ATTACK_READS = 2, TECH_READS = 4, TURN_WAIT_READS = 1 };

typedef bool (*ActionTurn)(GameState *g, ScriptPlace at, ActCalls call, void *ctx);

static bool run_action_turn(GameState *g, ActionTurn turn, uint8_t reads, ActCalls call, void *ctx)
{
    ScriptPlace at = battle_script_place(g);
    script_read_to(g, (ScriptPlace)(at + reads));
    return turn(g, at, call, ctx);
}

/* The actions' turns, by action, with the original's routine for each, whose
 * time is its own; the actions with no turn share one, as their routines do
 * nothing alike. Each is an entry on the machine (below). */
typedef struct {
    ActionTurn turn;
    uint8_t reads;
    uint32_t routine;
} ActionTurnRow;

static const ActionTurnRow turns[ACTION_KINDS] = {
    [ACTION_POSE] = { pose_turn, READS_NOTHING_MORE, 0xC199B8 },
    [ACTION_ATTACK] = { attack_turn, ATTACK_READS, 0xC199BE },
    [ACTION_TECH] = { tech_turn, READS_NOTHING_MORE, 0xC19A39 },
    [0x03] = { nothing_turn, READS_NOTHING_MORE, 0xC19B46 },
    [ACTION_ONE_OF_FOUR] = { nothing_turn, READS_NOTHING_MORE, 0xC19B46 },
    [ACTION_CHOOSE_AGAIN] = { choose_again_turn, READS_NOTHING_MORE, 0xC19B48 },
    [0x06] = { nothing_turn, READS_NOTHING_MORE, 0xC19B46 },
    [ACTION_CHANGE_INTO] = { change_into_turn, READS_NOTHING_MORE, 0xC19B8D },
    [0x08] = { nothing_turn, READS_NOTHING_MORE, 0xC19B46 },
    [ACTION_LEAVE] = { leave_turn, READS_NOTHING_MORE, 0xC19C6F },
    [ACTION_DIE] = { die_turn, READS_NOTHING_MORE, 0xC19CB3 },
    [ACTION_SET_STAT] = { set_stat_turn, READS_NOTHING_MORE, 0xC19D1B },
    [ACTION_ADD_TO_STAT] = { add_to_stat_turn, READS_NOTHING_MORE, 0xC19D72 },
    [ACTION_FLAGS] = { flags_turn, READS_NOTHING_MORE, 0xC19DCE },
    [0x0E] = { nothing_turn, READS_NOTHING_MORE, 0xC19B46 },
    [ACTION_MESSAGE] = { message_turn, READS_NOTHING_MORE, 0xC19E63 },
    [ACTION_BRING_BACK] = { bring_back_turn, READS_NOTHING_MORE, 0xC19E78 },
    [ACTION_SET_STATS] = { set_stats_turn, READS_NOTHING_MORE, 0xC19F5A },
    [ACTION_SET_STATS_THEN_TECH] = { set_stats_then_tech_turn, TECH_READS, 0xC19FD2 },
    [ACTION_ADD_TO_TURN_WAIT] = { add_to_turn_wait_turn, TURN_WAIT_READS, 0xC1A14E },
    [ACTION_ADD_TO_STATS] = { add_to_stats_turn, READS_NOTHING_MORE, 0xC1A188 },
    [ACTION_ADD_TO_STATS_THEN_TECH] = { add_to_stats_then_tech_turn, TECH_READS, 0xC1A20B },
    [ACTION_CALL_IN] = { call_in_turn, READS_NOTHING_MORE, 0xC1A396 },
};

/* The scripts hold no action past the table, and the turn's guard
 * (battle_enemy_turn_ready) takes none: the original would take a word past
 * its table for a routine. One would do nothing here. */
static const ActionTurnRow no_turn = { nothing_turn, READS_NOTHING_MORE, 0xC19B46 };

static const ActionTurnRow *turn_of(uint8_t action) { return action < ACTION_KINDS ? &turns[action] : &no_turn; }

/* ---- The turn ---------------------------------------------------------------------------- */

/* The turn's calls: the action's turn (above), its effect worked out on the
 * action's targets (battle/effect_work.c: the tech's, or anything else's as an
 * attack's), and the action shown ($C1:AC57, below). */
typedef enum { TURN_ACTION, TURN_EFFECT, TURN_SHOW_THE_EFFECT, TURN_SHOW } TurnCall;
typedef bool (*TurnCalls)(void *ctx, TurnCall call);

// $C1:8CF9
static bool enemy_turn(GameState *g, TurnCalls call, void *ctx)
{
    start_turn(g);
    TurnStart start = action_to_carry_out(g);
    if (start == CARRY_OUT) {
        if (!call(ctx, TURN_ACTION))
            return false;
        if (battle_script_outcome(g) != OUTCOME_HELD) {
            end_turn(g);
            return true;
        }
        switch (battle_action_follow_up(g)) {
        case FOLLOW_WITH_EFFECT:
            enemy_record_action(g);
            switch (take_first_target(g)) {
            case NO_ONE_LEFT: return true;
            case TARGET_GONE: end_turn(g); return true;
            case TARGET_TAKEN: break;
            }
            if (!call(ctx, TURN_EFFECT))
                return false;
            if (!effect_shown(g)) {
                end_turn(g);
                return true;
            }
            if (!call(ctx, TURN_SHOW_THE_EFFECT))
                return false;
            break;
        case FOLLOW_WITH_SHOWING:
            if (!call(ctx, TURN_SHOW))
                return false;
            break;
        default:
            break;
        }
        start = MOVE_ON;
    }
    if (start == MOVE_ON)
        move_on(g);
    end_turn(g);
    return true;
}

enum { TECH_BODY = 0xC19A3D, AIM = 0xC1C1DD, ENEMY_MESSAGE = 0xCD0033, ARRIVAL = 0xFDB438, CALL_IN = 0xFDA990 };
enum { ARRIVING_ENEMY_CELL = 0x02 };

/* An entry and its guard, which knows the action it carries out. */
/* The actions' entries, by action. */

/* The action's dispatch, which the original makes with the action doubled in
 * X and A. */
enum { DISPATCH = 0xC18D88 };

/* The action shown, from the turn's JSR at $C1:8E47, with A's high byte as
 * the test of the actor's statuses leaves it, clear. */
enum { SHOWING = 0xC1AC57 };

/* The action is shown, the battle's step 4 asked for from the original's
 * routine for it ($C1:BFA4), whose return the ask is made under, and its hits
 * land (battle/damage.c). The step takes A's high byte along, as the caller
 * left it, and the overflow flag, which it keeps with the flags across its
 * frame waits. */

/* The calling in alone, as the call-in's turn calls it (with JSL): the
 * battle's page and data bank, as each of its arrivals' guards takes them,
 * and the effect code's tech number in the cell the original reads it from,
 * where the turn's start leaves it (enemy_call_in_starts). */
enum { TECH_NUMBER_CELL = 0x12, TECH_NUMBER_OPERAND = 2 };

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} EnemyTurnContext;

/* The action turns' calls, each with its guard's state half: the
 * targeting's, an aim the port makes; the message's, one the menus' text code
 * types; an arrival, an enemy the fight's start loads; the calling in, each
 * of its arrivals'. */
static bool act_out(void *ctx, ActCall call, uint8_t arg, bool carry)
{
    EnemyTurnContext *c = ctx;
    GameState *g = c->game;
    bool done = false;
    switch (call) {
    case ACT_TECH_BODY:
        if (!enemy_action_entry_fits(g, ACTION_TECH))
            return battle_contract_broken(g, TECH_BODY, "an enemy's tech the port doesn't take");
        TIMED_AS(g, TECH_BODY, done = run_action_turn(g, tech_body, TECH_READS, act_out, ctx));
        return done;
    case ACT_AIM_TECH:
    case ACT_AIM_SET_STATS_THEN_TECH:
    case ACT_AIM_ADD_TO_STATS_THEN_TECH: TIMED_AS(g, AIM, done = battle_aim_action(g, c->pf, carry)); return done;
    case ACT_ARRIVAL_CHANGE_INTO:
    case ACT_ARRIVAL_BRING_BACK:
    case ACT_ARRIVAL_CALL_IN:
        /* the enemy's stat block filled from its record, as at the fight's
         * start. fight_load_enemy_ready also wants the enemy in the cell $02,
         * which the original's arrivals store from X just before the call
         * ($C1:9C2E, $C1:9ED8, $FD:AA2C), so it holds by construction; its
         * state half is the enemy being one of the eight */
        if (arg >= BATTLE_ENEMIES)
            return battle_contract_broken(g, ARRIVAL, "an enemy coming into the battle past the eight");
        TIMED_AS(g, ARRIVAL, fight_load_enemy(g, arg));
        return true;
    case ACT_CALL_IN:
        /* the calling in's guard's state half, the effect code's tech number
         * where the original reads it, holds by construction: the turn's
         * start leaves it there */
        TIMED_AS(g, CALL_IN, done = call_enemies_in(g, act_out, ctx));
        return done;
    default: TIMED_AS(g, ENEMY_MESSAGE, done = battle_show_an_enemy_message(g, c->pf, arg)); return done;
    }
}

static bool take_a_part_of_the_turn(void *ctx, TurnCall call)
{
    EnemyTurnContext *c = ctx;
    GameState *g = c->game;
    bool done = false;
    switch (call) {
    case TURN_ACTION: {
        const ActionTurnRow *row = turn_of(battle_action_kind(g));
        if (!enemy_action_entry_fits(g, battle_action_kind(g)))
            return battle_contract_broken(g, row->routine, "an enemy's action the port doesn't take");
        TIMED_AS(g, row->routine, done = run_action_turn(g, row->turn, row->reads, act_out, ctx));
        return done;
    }
    case TURN_EFFECT: {
        bool tech = battle_action_kind(g) == ACTION_TECH;
        if (!battle_enemy_action_worked_out(g, tech))
            return battle_contract_broken(g, tech ? 0xC1D7C4 : 0xC1D8D1, "an enemy's effect the port doesn't work out");
        TIMED_AS(g, tech ? 0xC1D7C4 : 0xC1D8D1, battle_work_out_enemy_action(g, c->pf, tech));
        return true;
    }
    default: TIMED_AS(g, SHOWING, done = battle_action_shown(g, c->pf)); return done;
    }
}

/* The turn's guard's state half (battle_enemy_turn_ready's). */
bool battle_enemy_takes_turn(GameState *g, Platform *pf)
{
    if (!battle_enemy_turn_fits(g))
        return battle_contract_broken(g, 0xC18CF9, "an enemy's turn the port doesn't take");
    EnemyTurnContext c = { g, pf };
    return enemy_turn(g, take_a_part_of_the_turn, &c);
}

/* The action shown: the battle's step 4, and its hits landing. */
bool battle_action_shown(GameState *g, Platform *pf)
{
    if (!battle_step_taken(g, pf, BATTLE_STEP_SHOW_ACTION))
        return false;
    TIMED_AS(g, 0xC1EC7F, battle_apply_damage(g));
    return true;
}
