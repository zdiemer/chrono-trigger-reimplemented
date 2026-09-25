/* Battle: an enemy's turn -- carrying out the action its script chose.
 *
 * On its turn an enemy carries out the action it is at in its chosen block
 * (battle_enemy_next_action), where that is still the one its script chose
 * (battle_enemy_block_actions), and moves on to the next; reacting, it carries
 * out its reaction's action at once. The action does its part (battle/
 * enemy_actions.c) and says what the turn does after it: work its effect out
 * on its first target and show it, only show it, or nothing more. Its effect
 * is worked out by the original's code still, as are an attack's and a
 * tech's setting up, a tech's targeting, messages and bringing enemies in:
 * this is the machine's side of the turn, which calls them and runs the
 * rest as C.
 *
 * After the turn the enemy has nothing chosen, and where its action was aimed
 * at an enemy (and doesn't say otherwise) the enemies react. */
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
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
        read_script_at(g, battle_enemy_next_action(g, enemy));
        if (battle_enemy_next_action(g, enemy) != battle_enemy_block_actions(g, enemy))
            return MOVE_ON;
        at = battle_enemy_next_action(g, enemy);
    }
    read_script_at(g, at);
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

enum { MEMBER_DRAW_LIMIT = 0x30, MEMBER_DRAW_SHIFT = 4, ENEMY_ANIMATION_HALTED = 4, NO_CONDITION = 0xFF };

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
    if (target != NO_CANDIDATE) {
        ORIGINAL_EFFECT(set_battle_low_scratch(g, 0x06, target));
        ORIGINAL_EFFECT(set_battle_low_scratch(g, 0x07, 0));
    }
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
    set_battle_hit_counts(g, 0, 0);
    set_battle_hit_counts(g, 1, 0);
    return TARGET_TAKEN;
}

/* Whether the action, its effect worked out, is shown: not where it had no
 * targets, and not where the actor is knocked out (unless it beat an enemy)
 * or can't act. */
enum { CONDITION_KNOCKED_OUT = 0x80, CANT_ACT = 0x82, STAT_CONDITION = 0x1D, STAT_STATUS_1 = 0x1E };

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
    READS_A_HIGH_AND_CARRY = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z,
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

static ScriptPlace place_read(Snes *s) { return battle_script_place(state_of(s)); }

/* ---- The actions' turns ---------------------------------------------------------------------- */

typedef enum { FOLLOW_UP_KEPT = 0xFF } FollowUpKept;

static void follow_up(Snes *s, ActionFollowUp f) { set_battle_action_follow_up(state_of(s), (uint8_t)f); }

// $C1:99B8
static bool pose_turn(Snes *s)
{
    follow_up(s, FOLLOW_WITH_NOTHING);
    return true;
}

// $C1:99BE
static bool attack_turn(Snes *s)
{
    GameState *g = state_of(s);
    enemy_attack_begins(g, place_read(s));
    /* the attack's working out takes A's high byte along (clear here) and the
     * carry the original's test of the attack's number leaves: set for a
     * number past the one that takes the enemy's own detail */
    s->cpu.a &= 0x00FF;
    m_set_flag(s, CPU_FLAG_C, battle_action_number(g) > 1);
    if (!battle_call_long(s, 0xC19A25, 0xFDAB01, READS_A_HIGH_AND_CARRY))
        return false;
    enemy_attack_ends(g);
    return true;
}

/* A tech carried out: its targets, its targeting and its setting up by the
 * original's code, its message. The actions that change stats first give
 * their message apart and say no further target is none only by its id. */
/* The tech's targeting and its setting up are the original's, and read the
 * carry the code before them leaves: the targeting, `aim_carry` (worked out
 * by the caller); the setting up, the carry of marking the targets, which
 * starts from comparing the first with the first enemy's slot, with A's high
 * byte clear. */
static bool tech_after_targets(Snes *s, uint32_t aim_site, uint32_t set_up_site, uint32_t message_site, bool stats_first,
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
        s->cpu.a = 0;
        m_set_flag(s, CPU_FLAG_C, battle_other_marks_carry(g, battle_first_mark_carry(g, past_party)));
        if (!battle_call_long(s, set_up_site, 0xFDAAD2, READS_A_HIGH_AND_CARRY))
            return false;
        enemy_tech_ends(g);
        if (!show_message(s, message_site, stats_first ? battle_action_message(g) : message))
            return false;
    }
    follow_up(s, FOLLOW_WITH_EFFECT);
    return true;
}

// $C1:9A3D
static bool tech_body(Snes *s)
{
    GameState *g = state_of(s);
    ScriptPlace at = place_read(s);
    enemy_tech_begins(g, at, true);
    /* the targeting takes the carry of the test for no third target */
    bool no_third = battle_action_further_targets(g, 1) == NO_CANDIDATE;
    return tech_after_targets(s, 0xC19AEB, 0xC19B1C, 0xC19B35, false, no_third, enemy_script_byte(g, (ScriptPlace)(at + 5)));
}

void battle_enemy_tech_body(Snes *s)
{
    if (tech_body(s))
        m_rts(s);
}

// $C1:9A39
static bool tech_turn(Snes *s) { return battle_run(s, 0xC19A39, 0xC19A3D, battle_enemy_tech_body); }

/* The stats set or added to first, then the tech (its message the one they
 * kept). The targeting after takes the carry a further target's selection
 * leaves (set), or where there is none the stat change's. */
static bool stats_before_tech(Snes *s, bool adding)
{
    GameState *g = state_of(s);
    ScriptPlace at = place_read(s);
    bool change_carry = enemy_set_stats_before_tech(g, at, adding);
    return enemy_tech_begins(g, at, false) || change_carry;
}

// $C1:9FD2
static bool set_stats_then_tech_turn(Snes *s)
{
    bool aim_carry = stats_before_tech(s, false);
    return tech_after_targets(s, 0xC1A0F2, 0xC1A128, 0xC1A13D, true, aim_carry, 0);
}

// $C1:A20B
static bool add_to_stats_then_tech_turn(Snes *s)
{
    bool aim_carry = stats_before_tech(s, true);
    return tech_after_targets(s, 0xC1A33A, 0xC1A370, 0xC1A385, true, aim_carry, 0);
}

// $C1:9B48
static bool choose_again_turn(Snes *s)
{
    enemy_choose_again(state_of(s), place_read(s));
    return true;
}

// $C1:9B8D
static bool change_into_turn(Snes *s)
{
    GameState *g = state_of(s);
    bool brought_in = false;
    if (enemy_change_into(g, place_read(s), &brought_in)) {
        if (!brought_in)
            enemy_change_takes_hp(g);
        else if (!set_up_arrival(s, 0xC19C30, (uint8_t)(battle_candidates(g, 0) - BATTLE_FIRST_ENEMY)))
            return false;
    }
    enemy_change_ends(g);
    return true;
}

/* The actions that do their part and show their message: the message the
 * action kept, or (the flags, a message alone) one of its operands. */
static bool carry_out_with_message(Snes *s, EnemyAction action, uint32_t message_site, unsigned message_operand)
{
    GameState *g = state_of(s);
    ScriptPlace at = place_read(s);
    enemy_carry_out(g, action, at);
    uint8_t message = message_operand ? enemy_script_byte(g, (ScriptPlace)(at + message_operand)) : battle_action_message(g);
    if (!show_message(s, message_site, message))
        return false;
    follow_up(s, FOLLOW_WITH_NOTHING);
    return true;
}

static bool set_stat_turn(Snes *s) { return carry_out_with_message(s, ACTION_SET_STAT, 0xC19D68, 0); }
static bool add_to_stat_turn(Snes *s) { return carry_out_with_message(s, ACTION_ADD_TO_STAT, 0xC19DC4, 0); }
static bool flags_turn(Snes *s) { return carry_out_with_message(s, ACTION_FLAGS, 0xC19E58, 2); }
static bool message_turn(Snes *s) { return carry_out_with_message(s, ACTION_MESSAGE, 0xC19E6E, 1); }
static bool set_stats_turn(Snes *s) { return carry_out_with_message(s, ACTION_SET_STATS, 0xC19FC8, 0); }
static bool add_to_stats_turn(Snes *s) { return carry_out_with_message(s, ACTION_ADD_TO_STATS, 0xC1A201, 0); }

// $C1:9C6F
static bool leave_turn(Snes *s)
{
    enemy_carry_out(state_of(s), ACTION_LEAVE, place_read(s));
    return true;
}

// $C1:9CB3
static bool die_turn(Snes *s)
{
    GameState *g = state_of(s);
    enemy_carry_out(g, ACTION_DIE, place_read(s));
    if (!show_message(s, 0xC19D0E, battle_action_message(g)))
        return false;
    battle_mark_first_target(g);
    follow_up(s, FOLLOW_WITH_SHOWING);
    return true;
}

// $C1:A14E
static bool add_to_turn_wait_turn(Snes *s)
{
    enemy_carry_out(state_of(s), ACTION_ADD_TO_TURN_WAIT, place_read(s));
    follow_up(s, FOLLOW_WITH_NOTHING);
    return true;
}

// $C1:9E78
static bool bring_back_turn(Snes *s)
{
    GameState *g = state_of(s);
    ScriptPlace at = place_read(s);
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
static bool call_in_turn(Snes *s)
{
    GameState *g = state_of(s);
    enemy_call_in_starts(g, place_read(s));
    if (!battle_call_long(s, 0xC1A3B6, 0xFDA990, READS_NOTHING))
        return false;
    battle_mark_first_target(g);
    battle_mark_other_targets(g);
    if (!show_message(s, 0xC1A3C7, battle_action_message(g)))
        return false;
    follow_up(s, FOLLOW_WITH_SHOWING);
    return true;
}

static bool nothing_turn(Snes *s)
{
    (void)s;
    return true;
}

/* Each action's turn is its own routine in the original's table, called as
 * its dispatch calls it: an entry each. */
static void run_action_entry(Snes *s, bool (*turn)(Snes *s))
{
    if (turn(s))
        m_rts(s);
}

#define ACTION_TURN_ENTRY(name, turn) \
    void name(Snes *s) { run_action_entry(s, turn); }
ACTION_TURN_ENTRY(battle_enemy_pose_turn, pose_turn)
ACTION_TURN_ENTRY(battle_enemy_attack_turn, attack_turn)
ACTION_TURN_ENTRY(battle_enemy_tech_turn, tech_turn)
ACTION_TURN_ENTRY(battle_enemy_no_turn, nothing_turn)
ACTION_TURN_ENTRY(battle_enemy_choose_again_turn, choose_again_turn)
ACTION_TURN_ENTRY(battle_enemy_change_into_turn, change_into_turn)
ACTION_TURN_ENTRY(battle_enemy_leave_turn, leave_turn)
ACTION_TURN_ENTRY(battle_enemy_die_turn, die_turn)
ACTION_TURN_ENTRY(battle_enemy_set_stat_turn, set_stat_turn)
ACTION_TURN_ENTRY(battle_enemy_add_to_stat_turn, add_to_stat_turn)
ACTION_TURN_ENTRY(battle_enemy_flags_turn, flags_turn)
ACTION_TURN_ENTRY(battle_enemy_message_turn, message_turn)
ACTION_TURN_ENTRY(battle_enemy_bring_back_turn, bring_back_turn)
ACTION_TURN_ENTRY(battle_enemy_set_stats_turn, set_stats_turn)
ACTION_TURN_ENTRY(battle_enemy_set_stats_then_tech_turn, set_stats_then_tech_turn)
ACTION_TURN_ENTRY(battle_enemy_add_to_turn_wait_turn, add_to_turn_wait_turn)
ACTION_TURN_ENTRY(battle_enemy_add_to_stats_turn, add_to_stats_turn)
ACTION_TURN_ENTRY(battle_enemy_add_to_stats_then_tech_turn, add_to_stats_then_tech_turn)
ACTION_TURN_ENTRY(battle_enemy_call_in_turn, call_in_turn)

/* The actions' turns, by action. */
typedef void (*ActionTurnEntry)(Snes *s);

// $C1:B88D
/* Each action's turn is run as the original's routine for it (in bank C1),
 * whose time is its own; the actions with no turn share one, as their
 * routines do nothing alike. */
typedef struct {
    ActionTurnEntry entry;
    uint16_t routine;
} ActionTurn;

enum { NO_TURN = 0x9B46 };

static const ActionTurn turns[ACTION_KINDS] = {
    [ACTION_POSE] = { battle_enemy_pose_turn, 0x99B8 },
    [ACTION_ATTACK] = { battle_enemy_attack_turn, 0x99BE },
    [ACTION_TECH] = { battle_enemy_tech_turn, 0x9A39 },
    [0x03] = { battle_enemy_no_turn, NO_TURN },
    [ACTION_ONE_OF_FOUR] = { battle_enemy_no_turn, NO_TURN },
    [ACTION_CHOOSE_AGAIN] = { battle_enemy_choose_again_turn, 0x9B48 },
    [0x06] = { battle_enemy_no_turn, NO_TURN },
    [ACTION_CHANGE_INTO] = { battle_enemy_change_into_turn, 0x9B8D },
    [0x08] = { battle_enemy_no_turn, NO_TURN },
    [ACTION_LEAVE] = { battle_enemy_leave_turn, 0x9C6F },
    [ACTION_DIE] = { battle_enemy_die_turn, 0x9CB3 },
    [ACTION_SET_STAT] = { battle_enemy_set_stat_turn, 0x9D1B },
    [ACTION_ADD_TO_STAT] = { battle_enemy_add_to_stat_turn, 0x9D72 },
    [ACTION_FLAGS] = { battle_enemy_flags_turn, 0x9DCE },
    [0x0E] = { battle_enemy_no_turn, NO_TURN },
    [ACTION_MESSAGE] = { battle_enemy_message_turn, 0x9E63 },
    [ACTION_BRING_BACK] = { battle_enemy_bring_back_turn, 0x9E78 },
    [ACTION_SET_STATS] = { battle_enemy_set_stats_turn, 0x9F5A },
    [ACTION_SET_STATS_THEN_TECH] = { battle_enemy_set_stats_then_tech_turn, 0x9FD2 },
    [ACTION_ADD_TO_TURN_WAIT] = { battle_enemy_add_to_turn_wait_turn, 0xA14E },
    [ACTION_ADD_TO_STATS] = { battle_enemy_add_to_stats_turn, 0xA188 },
    [ACTION_ADD_TO_STATS_THEN_TECH] = { battle_enemy_add_to_stats_then_tech_turn, 0xA20B },
    [ACTION_CALL_IN] = { battle_enemy_call_in_turn, 0xA396 },
};

/* The action's dispatch, which the original makes with the action doubled in
 * X and A: an action past the table (none in the scripts) is the original's
 * from there. */
enum { DISPATCH = 0xC18D88 };

uint16_t enemy_action_turn_routine(uint8_t action) { return action < ACTION_KINDS ? turns[action].routine : 0; }

static bool carry_out_action(Snes *s, uint8_t action)
{
    s->cpu.x = (uint8_t)(action * 2);
    s->cpu.a = (uint8_t)(action * 2);
    if (action >= ACTION_KINDS) {
        m_stop_at_own(s, DISPATCH, DC_Y | DC_FLAGS);
        return false;
    }
    return battle_run(s, DISPATCH, 0xC10000u | turns[action].routine, turns[action].entry);
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

/* The effect worked out on the first target: the tech's by $C1:D7C4, anything
 * else's by $C1:D8D1. */
enum { WORKS_OUT_TECHS = 0xC1D7C4, WORKS_OUT_ATTACKS = 0xC1D8D1 };

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
            if (battle_action_kind(g) == ACTION_TECH ? !battle_call(s, 0xC18E15, WORKS_OUT_TECHS, READS_NOTHING)
                                                     : !battle_call(s, 0xC18E10, WORKS_OUT_ATTACKS, READS_NOTHING))
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
