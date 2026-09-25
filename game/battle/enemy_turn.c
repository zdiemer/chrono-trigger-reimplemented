/* Battle: an enemy's turn -- carrying out the action its script chose.
 *
 * On its turn an enemy carries out the action it is at in its chosen block
 * (battle_enemy_next_action), where that is still the one its script chose
 * (battle_enemy_block_actions), and moves on to the next; reacting, it carries
 * out its reaction's action at once. The action does its part (battle/
 * enemy_actions.c) and says what the turn does after it: work its effect out
 * on its targets and show it, only show it, or nothing more. Its effect is
 * worked out in C (battle/effect_work.c); a tech's targeting, messages,
 * bringing enemies in and the showing are the original's code still: this is
 * the machine's side of the turn, which calls them and runs the rest as C.
 *
 * After the turn the enemy has nothing chosen, and where its action was aimed
 * at an enemy (and doesn't say otherwise) the enemies react. */
#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/effect_work.h"
#include "battle/enemy_actions.h"
#include "battle/enemy_scripts.h"
#include "battle/entries.h"
#include "battle/random.h"
#include "battle/rules_data.h"
#include "battle/targets.h"
#include "charge.h"
#include "original_effects.h"
#include "port/native.h"
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

static void end_turn(Snes *s)
{
    GameState *g = state_of(s);
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
    if (target != NO_CANDIDATE)
        ORIGINAL_EFFECT(battle_leave_word(g, 0x06, target));
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

/* ---- The original's calls -------------------------------------------------------------------- */

/* The calls read nothing of the registers but what each is given. */
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_FLAGS,
    READS_A = DC_X | DC_Y | DC_FLAGS,
    READS_X = DC_A | DC_Y | DC_FLAGS,
    READS_CARRY = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z,
};

/* An enemy that comes into the battle (battle/enemy_actions.c) is set up by
 * the original's code, from the JSL at `site`, with the enemy in X. */
static bool set_up_arrival(Snes *s, uint32_t site, uint8_t enemy)
{
    s->cpu.x = enemy;
    return battle_call_long(s, site, 0xFDB438, READS_X);
}

/* A message, where the action has one. */
static bool show_message(Snes *s, uint32_t site, uint8_t message)
{
    if (message == 0)
        return true;
    m_set_a8(s, message);
    return battle_call_long(s, site, 0xCD0033, READS_A);
}

/* The enemy whose script the turn runs, whose selectors pick for it. */
static uint8_t script_enemy(Snes *s) { return battle_enemy_turn(state_of(s)); }

/* ---- The actions' turns ---------------------------------------------------------------------- */

static void follow_up(Snes *s, ActionFollowUp f) { set_battle_action_follow_up(state_of(s), (uint8_t)f); }

// $C1:99B8
static bool pose_turn(Snes *s, ScriptPlace at)
{
    (void)at;
    follow_up(s, FOLLOW_WITH_NOTHING);
    return true;
}

// $C1:99BE
static bool attack_turn(Snes *s, ScriptPlace at)
{
    GameState *g = state_of(s);
    enemy_attack_begins(g, at);
    /* the attack noted for its targets takes the carry the original's test
     * of the attack's number leaves: set for a number past the one that takes
     * the enemy's own detail */
    TIMED_AS(g, 0xFDAB01, effect_note_enemy_action(g, ENEMY_ATTACKS));
    /* its multiply takes the carry of the test of the attack's number: set for
     * a number past the one that takes the enemy's own detail */
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, battle_action_number(g) > 1));
    enemy_attack_ends(g);
    return true;
}

/* A tech carried out: its targets, its targeting by the original's code, the
 * tech noted for its targets (battle/effect_work.c), its message. The actions
 * that change stats first give their message apart and say no further target
 * is none only by its id. The targeting and the noting read the carry the
 * code before them leaves: the targeting, `aim_carry` (worked out by the
 * caller); the noting, the carry of marking the targets, which starts from
 * comparing the first with the first enemy's slot. */
static bool tech_after_targets(Snes *s, uint32_t aim_site, uint32_t message_site, bool stats_first,
                               bool aim_carry, uint8_t message)
{
    GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_C, aim_carry);
    if (!battle_call(s, aim_site, 0xC1C1DD, READS_CARRY))
        return false;
    if (stats_first && !battle_member_acts_for_itself(g))
        set_battle_action_kind(g, ACTION_TECH);
    bool past_party = battle_action_targets(g, 0) >= BATTLE_FIRST_ENEMY;
    if (enemy_tech_aimed(g)) {
        TIMED_AS(g, 0xFDAAD2, effect_note_enemy_action(g, ENEMY_TECHS));
        /* its multiply takes the carry of the marking of the targets */
        ORIGINAL_EFFECT(battle_leave_multiply_carry(g, battle_other_marks_carry(g, battle_first_mark_carry(g, past_party))));
        enemy_tech_ends(g);
        if (!show_message(s, message_site, stats_first ? battle_action_message(g) : message))
            return false;
    }
    follow_up(s, FOLLOW_WITH_EFFECT);
    return true;
}

// $C1:9A3D
static bool tech_body(Snes *s, ScriptPlace at)
{
    GameState *g = state_of(s);
    enemy_tech_begins(g, script_enemy(s), at, true);
    /* the targeting takes the carry of the test for no third target */
    bool no_third = battle_action_further_targets(g, 1) == NO_CANDIDATE;
    return tech_after_targets(s, 0xC19AEB, 0xC19B35, false, no_third, enemy_tech_at(g, at).message);
}

void battle_enemy_tech_body(Snes *s);

// $C1:9A39
static bool tech_turn(Snes *s, ScriptPlace at)
{
    (void)at;
    return battle_run(s, 0xC19A39, 0xC19A3D, battle_enemy_tech_body);
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
static bool stats_before_tech(Snes *s, ScriptPlace at, bool adding)
{
    GameState *g = state_of(s);
    enemy_keep_message_before_tech(g, at);
    bool change_carry = false;
    for (unsigned n = 0; n < STAT_CHANGES_BEFORE_TECH; n++) {
        StatChange change = enemy_stat_change_before_tech(g, at, n);
        uint8_t before = combatant_stat_byte_by_place(g, battle_actor(g), change.offset);
        change_carry = adding && change_carries(before, change.value);
        enemy_change_own_stat(g, change, adding);
    }
    EnemyTech tech = enemy_tech_at(g, at);
    enemy_tech_begins(g, script_enemy(s), at, false);
    return tech.further[0] != SELECT_NOBODY || tech.further[1] != SELECT_NOBODY || change_carry;
}

// $C1:9FD2
static bool set_stats_then_tech_turn(Snes *s, ScriptPlace at)
{
    bool aim_carry = stats_before_tech(s, at, false);
    return tech_after_targets(s, 0xC1A0F2, 0xC1A13D, true, aim_carry, 0);
}

// $C1:A20B
static bool add_to_stats_then_tech_turn(Snes *s, ScriptPlace at)
{
    bool aim_carry = stats_before_tech(s, at, true);
    return tech_after_targets(s, 0xC1A33A, 0xC1A385, true, aim_carry, 0);
}

// $C1:9B48
/* The choosing is a machine entry's here: it is taken from the cells and
 * handed back, the script read as far as it read. */
static bool choose_again_turn(Snes *s, ScriptPlace at)
{
    GameState *g = state_of(s);
    ScriptChoosing c = enemy_choosing_in_cells(g);
    enemy_choose_again(g, &c, at);
    enemy_hand_back_choosing(g, &c);
    return true;
}

// $C1:9B8D
static bool change_into_turn(Snes *s, ScriptPlace at)
{
    GameState *g = state_of(s);
    if (enemy_change_into(g, at) == CHANGED_TO_BRING_IN
        && !set_up_arrival(s, 0xC19C30, (uint8_t)(battle_candidates(g, 0) - BATTLE_FIRST_ENEMY)))
        return false;
    enemy_change_ends(g);
    return true;
}

/* The actions that do their part and show their message. */
static bool carry_out_with_message(Snes *s, EnemyAction action, uint32_t message_site, ScriptPlace at)
{
    uint8_t message = enemy_carry_out(state_of(s), script_enemy(s), action, at);
    if (!show_message(s, message_site, message))
        return false;
    follow_up(s, FOLLOW_WITH_NOTHING);
    return true;
}

static bool set_stat_turn(Snes *s, ScriptPlace at) { return carry_out_with_message(s, ACTION_SET_STAT, 0xC19D68, at); }
static bool add_to_stat_turn(Snes *s, ScriptPlace at) { return carry_out_with_message(s, ACTION_ADD_TO_STAT, 0xC19DC4, at); }
static bool flags_turn(Snes *s, ScriptPlace at) { return carry_out_with_message(s, ACTION_FLAGS, 0xC19E58, at); }
static bool message_turn(Snes *s, ScriptPlace at) { return carry_out_with_message(s, ACTION_MESSAGE, 0xC19E6E, at); }
static bool set_stats_turn(Snes *s, ScriptPlace at) { return carry_out_with_message(s, ACTION_SET_STATS, 0xC19FC8, at); }
static bool add_to_stats_turn(Snes *s, ScriptPlace at) { return carry_out_with_message(s, ACTION_ADD_TO_STATS, 0xC1A201, at); }

// $C1:9C6F
static bool leave_turn(Snes *s, ScriptPlace at)
{
    enemy_carry_out(state_of(s), script_enemy(s), ACTION_LEAVE, at);
    return true;
}

// $C1:9CB3
static bool die_turn(Snes *s, ScriptPlace at)
{
    GameState *g = state_of(s);
    if (!show_message(s, 0xC19D0E, enemy_carry_out(g, script_enemy(s), ACTION_DIE, at)))
        return false;
    battle_mark_first_target(g);
    follow_up(s, FOLLOW_WITH_SHOWING);
    return true;
}

// $C1:A14E
static bool add_to_turn_wait_turn(Snes *s, ScriptPlace at)
{
    enemy_carry_out(state_of(s), script_enemy(s), ACTION_ADD_TO_TURN_WAIT, at);
    follow_up(s, FOLLOW_WITH_NOTHING);
    return true;
}

// $C1:9E78
static bool bring_back_turn(Snes *s, ScriptPlace at)
{
    GameState *g = state_of(s);
    enemy_bring_back_starts(g, at);
    uint8_t count = 0;
    for (uint8_t enemy = enemy_next_to_bring_back(g, 0); enemy < BATTLE_ENEMIES; enemy = enemy_next_to_bring_back(g, (uint8_t)(enemy + 1))) {
        enemy_bring_back(g, enemy, count++);
        if (!set_up_arrival(s, 0xC19EDA, enemy))
            return false;
        enemy_brought_back(g, enemy, at);
    }
    enemy_bring_back_ends(g, at, count);
    if (!show_message(s, 0xC19F50, battle_action_message(g)))
        return false;
    follow_up(s, FOLLOW_WITH_SHOWING);
    return true;
}

// $C1:A396
static bool call_in_turn(Snes *s, ScriptPlace at)
{
    GameState *g = state_of(s);
    enemy_call_in_starts(g, at);
    if (!battle_call_long(s, 0xC1A3B6, 0xFDA990, READS_NOTHING))
        return false;
    battle_mark_first_target(g);
    battle_mark_other_targets(g);
    if (!show_message(s, 0xC1A3C7, battle_action_message(g)))
        return false;
    follow_up(s, FOLLOW_WITH_SHOWING);
    return true;
}

static bool nothing_turn(Snes *s, ScriptPlace at)
{
    (void)s;
    (void)at;
    return true;
}

/* Each action's turn is its own routine in the original's table, called as
 * its dispatch calls it, with the script read at the action: an entry each.
 * How far each reads the script: an attack its number and pose, a tech (in
 * its body) its number and selectors, a change to a turn wait its selector;
 * the others move the place no further (choosing again moves it itself). */
enum { READS_NOTHING_MORE = 0, ATTACK_READS = 2, TECH_READS = 4, TURN_WAIT_READS = 1 };

typedef bool (*ActionTurn)(Snes *s, ScriptPlace at);

static void run_action_entry(Snes *s, ActionTurn turn, uint8_t reads)
{
    GameState *g = state_of(s);
    ScriptPlace at = battle_script_place(g);
    script_read_to(g, (ScriptPlace)(at + reads));
    if (turn(s, at))
        m_rts(s);
}

/* An entry and its guard, which knows the action it carries out. */
#define ACTION_TURN_ENTRY(name, turn, action, reads) \
    void name(Snes *s) { run_action_entry(s, turn, reads); } \
    bool name##_ready(Snes *s) { return battle_script_entry_ready(s, TAKES_ACTION, action); }
ACTION_TURN_ENTRY(battle_enemy_pose_turn, pose_turn, ACTION_POSE, READS_NOTHING_MORE)
ACTION_TURN_ENTRY(battle_enemy_attack_turn, attack_turn, ACTION_ATTACK, ATTACK_READS)
ACTION_TURN_ENTRY(battle_enemy_tech_turn, tech_turn, ACTION_TECH, READS_NOTHING_MORE)
ACTION_TURN_ENTRY(battle_enemy_tech_body, tech_body, ACTION_TECH, TECH_READS)
ACTION_TURN_ENTRY(battle_enemy_choose_again_turn, choose_again_turn, ACTION_CHOOSE_AGAIN, READS_NOTHING_MORE)
ACTION_TURN_ENTRY(battle_enemy_change_into_turn, change_into_turn, ACTION_CHANGE_INTO, READS_NOTHING_MORE)
ACTION_TURN_ENTRY(battle_enemy_leave_turn, leave_turn, ACTION_LEAVE, READS_NOTHING_MORE)
ACTION_TURN_ENTRY(battle_enemy_die_turn, die_turn, ACTION_DIE, READS_NOTHING_MORE)
ACTION_TURN_ENTRY(battle_enemy_set_stat_turn, set_stat_turn, ACTION_SET_STAT, READS_NOTHING_MORE)
ACTION_TURN_ENTRY(battle_enemy_add_to_stat_turn, add_to_stat_turn, ACTION_ADD_TO_STAT, READS_NOTHING_MORE)
ACTION_TURN_ENTRY(battle_enemy_flags_turn, flags_turn, ACTION_FLAGS, READS_NOTHING_MORE)
ACTION_TURN_ENTRY(battle_enemy_message_turn, message_turn, ACTION_MESSAGE, READS_NOTHING_MORE)
ACTION_TURN_ENTRY(battle_enemy_bring_back_turn, bring_back_turn, ACTION_BRING_BACK, READS_NOTHING_MORE)
ACTION_TURN_ENTRY(battle_enemy_set_stats_turn, set_stats_turn, ACTION_SET_STATS, READS_NOTHING_MORE)
ACTION_TURN_ENTRY(battle_enemy_set_stats_then_tech_turn, set_stats_then_tech_turn, ACTION_SET_STATS_THEN_TECH, TECH_READS)
ACTION_TURN_ENTRY(battle_enemy_add_to_turn_wait_turn, add_to_turn_wait_turn, ACTION_ADD_TO_TURN_WAIT, TURN_WAIT_READS)
ACTION_TURN_ENTRY(battle_enemy_add_to_stats_turn, add_to_stats_turn, ACTION_ADD_TO_STATS, READS_NOTHING_MORE)
ACTION_TURN_ENTRY(battle_enemy_add_to_stats_then_tech_turn, add_to_stats_then_tech_turn, ACTION_ADD_TO_STATS_THEN_TECH,
                  TECH_READS)
ACTION_TURN_ENTRY(battle_enemy_call_in_turn, call_in_turn, ACTION_CALL_IN, READS_NOTHING_MORE)

/* The actions with no turn take up nothing of the script. */
void battle_enemy_no_turn(Snes *s) { run_action_entry(s, nothing_turn, READS_NOTHING_MORE); }
bool battle_enemy_no_turn_ready(Snes *s) { return battle_script_entry_ready(s, TAKES_NOTHING, 0); }

/* The actions' turns, by action. */
typedef void (*ActionTurnEntry)(Snes *s);

// $C1:B88D
/* Each action's turn is run as the original's routine for it, whose time is
 * its own; the actions with no turn share one, as their routines do nothing
 * alike. */
typedef struct {
    ActionTurnEntry entry;
    uint32_t routine;
} ActionTurnRow;

static const ActionTurnRow turns[ACTION_KINDS] = {
    [ACTION_POSE] = { battle_enemy_pose_turn, 0xC199B8 },
    [ACTION_ATTACK] = { battle_enemy_attack_turn, 0xC199BE },
    [ACTION_TECH] = { battle_enemy_tech_turn, 0xC19A39 },
    [0x03] = { battle_enemy_no_turn, 0xC19B46 },
    [ACTION_ONE_OF_FOUR] = { battle_enemy_no_turn, 0xC19B46 },
    [ACTION_CHOOSE_AGAIN] = { battle_enemy_choose_again_turn, 0xC19B48 },
    [0x06] = { battle_enemy_no_turn, 0xC19B46 },
    [ACTION_CHANGE_INTO] = { battle_enemy_change_into_turn, 0xC19B8D },
    [0x08] = { battle_enemy_no_turn, 0xC19B46 },
    [ACTION_LEAVE] = { battle_enemy_leave_turn, 0xC19C6F },
    [ACTION_DIE] = { battle_enemy_die_turn, 0xC19CB3 },
    [ACTION_SET_STAT] = { battle_enemy_set_stat_turn, 0xC19D1B },
    [ACTION_ADD_TO_STAT] = { battle_enemy_add_to_stat_turn, 0xC19D72 },
    [ACTION_FLAGS] = { battle_enemy_flags_turn, 0xC19DCE },
    [0x0E] = { battle_enemy_no_turn, 0xC19B46 },
    [ACTION_MESSAGE] = { battle_enemy_message_turn, 0xC19E63 },
    [ACTION_BRING_BACK] = { battle_enemy_bring_back_turn, 0xC19E78 },
    [ACTION_SET_STATS] = { battle_enemy_set_stats_turn, 0xC19F5A },
    [ACTION_SET_STATS_THEN_TECH] = { battle_enemy_set_stats_then_tech_turn, 0xC19FD2 },
    [ACTION_ADD_TO_TURN_WAIT] = { battle_enemy_add_to_turn_wait_turn, 0xC1A14E },
    [ACTION_ADD_TO_STATS] = { battle_enemy_add_to_stats_turn, 0xC1A188 },
    [ACTION_ADD_TO_STATS_THEN_TECH] = { battle_enemy_add_to_stats_then_tech_turn, 0xC1A20B },
    [ACTION_CALL_IN] = { battle_enemy_call_in_turn, 0xC1A396 },
};

/* The action's dispatch, which the original makes with the action doubled in
 * X and A: an action past the table (none in the scripts) is the original's
 * from there. */
enum { DISPATCH = 0xC18D88 };

static bool carry_out_action(Snes *s, uint8_t action)
{
    s->cpu.x = (uint8_t)(action * 2);
    s->cpu.a = (uint8_t)(action * 2);
    if (action >= ACTION_KINDS) {
        m_stop_at_own(s, DISPATCH, DC_Y | DC_FLAGS);
        return false;
    }
    return battle_run(s, DISPATCH, turns[action].routine, turns[action].entry);
}

/* The action shown, from the turn's JSR at $C1:8E47. */
static bool show_action(Snes *s) { return battle_run(s, 0xC18E47, 0xC1AC57, battle_start_action_showing); }

/* ---- The turn ---------------------------------------------------------------------------- */

/* The action's showing starts: the battle's step 4, and the effect's start
 * ($C1:AC57, $C1:BFA4, $C1:EC7F, the original's). */
static bool start_showing(Snes *s)
{
    /* the step takes A's high byte and the overflow flag along, as the caller
     * left them */
    if (!battle_call(s, 0xC1AC57, 0xC1BFA4, DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C))
        return false;
    m_push16(s, 0xAC5C);
    if (!battle_call(s, 0xC1AC85, 0xC1EC7F, READS_NOTHING))
        return false;
    m_pull16(s);
    return true;
}

void battle_start_action_showing(Snes *s)
{
    if (start_showing(s))
        m_rts(s);
}

/* The effect worked out on the action's targets (battle/effect_work.c): the
 * tech's, or anything else's as an attack's. */
static bool work_out_effect(Snes *s)
{
    if (battle_action_kind(state_of(s)) == ACTION_TECH)
        return battle_run(s, 0xC18E15, 0xC1D7C4, battle_work_out_enemy_tech);
    return battle_run(s, 0xC18E10, 0xC1D8D1, battle_work_out_enemy_attack);
}

// $C1:8CF9
static bool enemy_turn(Snes *s)
{
    GameState *g = state_of(s);
    start_turn(g);
    TurnStart start = action_to_carry_out(g);
    if (start == CARRY_OUT) {
        if (!carry_out_action(s, battle_action_kind(g)))
            return false;
        if (battle_script_outcome(g) != OUTCOME_HELD) {
            end_turn(s);
            return true;
        }
        switch (battle_action_follow_up(g)) {
        case FOLLOW_WITH_EFFECT:
            enemy_record_action(g);
            switch (take_first_target(g)) {
            case NO_ONE_LEFT: return true;
            case TARGET_GONE: end_turn(s); return true;
            case TARGET_TAKEN: break;
            }
            if (!work_out_effect(s))
                return false;
            if (!effect_shown(g)) {
                end_turn(s);
                return true;
            }
            /* the test of the actor's statuses leaves A's high byte clear */
            s->cpu.a &= 0x00FF;
            if (!show_action(s))
                return false;
            break;
        case FOLLOW_WITH_SHOWING:
            if (!show_action(s))
                return false;
            break;
        default:
            break;
        }
        start = MOVE_ON;
    }
    if (start == MOVE_ON)
        move_on(g);
    end_turn(s);
    return true;
}

void battle_enemy_takes_turn(Snes *s)
{
    if (enemy_turn(s))
        m_rts(s);
}
