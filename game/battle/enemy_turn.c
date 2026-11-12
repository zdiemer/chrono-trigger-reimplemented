/* Battle: an enemy's turn -- carrying out the action its script chose.
 *
 * On its turn an enemy carries out the action it is at in its chosen block
 * (battle_enemy_next_action), where that is still the one its script chose
 * (battle_enemy_block_actions), and moves on to the next; reacting, it carries
 * out its reaction's action at once. The action does its part (battle/
 * enemy_actions.c) and says what the turn does after it: work its effect out
 * on its targets and show it, only show it, or nothing more. Its effect is
 * worked out by battle/effect_work.c; a tech's targeting, messages, an
 * enemy's stat block loaded as it comes in and the showing are routines of
 * their own, which the turn calls.
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

/* ---- The turn's start and end ----------------------------------------------------------------- */

static uint8_t acting(const GameState *g) { return battle_enemy_acting(g); }
static bool reacting(const GameState *g) { return battle_action_hit_enemy(g) != 0; }

/* How far the turn has read the script: it starts at the action, and each
 * action's turn reads its operands. This is the turn's result as much as the
 * choosing's: a reaction's search for its next block goes on from it
 * (battle/main_loop.c), and so does the calling in. */
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

/* ---- The routines the turns call ------------------------------------------------------------- */

enum { TECH_BODY = 0xC19A3D, AIM = 0xC1C1DD, ENEMY_MESSAGE = 0xCD0033, ARRIVAL = 0xFDB438, CALL_IN = 0xFDA990 };

/* The action shown ($C1:AC57), called from the turn at $C1:8E47. */
enum { SHOWING = 0xC1AC57 };

/* A tech's targeting ($C1:C1DD, battle/aim.c). */
static bool aim(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, AIM, done = battle_aim_action(g, pf));
    return done;
}

/* An enemy's message ($CD:0033, battle/message.c), where the action has
 * one. */
static bool show_message(GameState *g, Platform *pf, uint8_t message)
{
    if (message == 0)
        return true;
    bool done = false;
    TIMED_AS(g, ENEMY_MESSAGE, done = battle_show_an_enemy_message(g, pf, message));
    return done;
}

/* An enemy coming into the battle ($FD:B438, battle/fight_enemies.c): its
 * stat block filled from its record, as at the fight's start, for one of the
 * eight enemies (refused past them). */
static bool load_the_arrival(GameState *g, uint8_t enemy)
{
    if (enemy >= BATTLE_ENEMIES)
        return battle_contract_broken(g, ARRIVAL, "an enemy coming into the battle past the eight");
    TIMED_AS(g, ARRIVAL, fight_load_enemy(g, enemy));
    return true;
}

static void follow_up(GameState *g, ActionFollowUp f) { set_battle_action_follow_up(g, (uint8_t)f); }

/* ---- The actions' turns ---------------------------------------------------------------------- */

/* Each action's turn is its own routine in the original's table, with the
 * script read at the action. How far each reads the script: an attack its
 * number and pose, a tech (in its body) its number and selectors, a change to
 * a turn wait its selector; the others move the place no further (choosing
 * again moves it itself). */
enum { READS_NOTHING_MORE = 0, ATTACK_READS = 2, TECH_READS = 4, TURN_WAIT_READS = 1 };

typedef bool (*ActionTurn)(GameState *g, Platform *pf, ScriptPlace at);

/* The script read at the action as far as `reads`, and the turn from there;
 * the read is the turn's routine's work. */
static bool run_action_turn(GameState *g, Platform *pf, ActionTurn turn, uint8_t reads)
{
    ScriptPlace at = battle_script_place(g);
    script_read_to(g, (ScriptPlace)(at + reads));
    return turn(g, pf, at);
}

// $C1:99B8
static bool pose_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    (void)pf;
    (void)at;
    follow_up(g, FOLLOW_WITH_NOTHING);
    return true;
}

// $C1:99BE
static bool attack_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    (void)pf;
    enemy_attack_begins(g, at);
    TIMED_AS(g, 0xFDAB01, effect_note_enemy_action(g, ENEMY_ATTACKS));
    enemy_attack_ends(g);
    return true;
}

static bool tech_after_targets(GameState *g, Platform *pf, bool stats_first, uint8_t message)
{
    if (!aim(g, pf))
        return false;
    if (stats_first && !battle_member_acts_for_itself(g))
        set_battle_action_kind(g, ACTION_TECH);
    charge_accesses(g, 1);
    if (enemy_tech_aimed(g)) {
        TIMED_AS(g, 0xFDAAD2, effect_note_enemy_action(g, ENEMY_TECHS));
        enemy_tech_ends(g);
        if (!show_message(g, pf, stats_first ? battle_action_message(g) : message))
            return false;
    }
    follow_up(g, FOLLOW_WITH_EFFECT);
    return true;
}

/* The original reads whether a third target is selected, to hand the
 * targeting a flag it no longer reads: the read's time is kept. */
static bool tech_body(GameState *g, Platform *pf, ScriptPlace at)
{
    enemy_tech_begins(g, battle_enemy_turn(g), at, true);
    charge_accesses(g, 1);
    return tech_after_targets(g, pf, false, enemy_tech_at(g, at).message);
}

// $C1:9A39
/* The tech's body ($C1:9A3D), refused where the port doesn't take the
 * enemy's tech. */
static bool tech_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    (void)at;
    if (!enemy_action_turn_fits(g, ACTION_TECH))
        return battle_contract_broken(g, TECH_BODY, "an enemy's tech the port doesn't take");
    bool done = false;
    TIMED_AS(g, TECH_BODY, done = run_action_turn(g, pf, tech_body, TECH_READS));
    return done;
}

/* The stats set or added to first, then the tech (its message the one they
 * kept). The original reads each stat before it changes it, and the tech's
 * further targets, to hand the targeting a flag it no longer reads (a further
 * target selected, or the last change's carry past a byte): the reads' time
 * is kept, the actor and its stat each change and four of the script's
 * bytes. */
enum { TECH_BYTES_READ_FOR_THE_FLAG = 4 };

static void stats_before_tech(GameState *g, ScriptPlace at, bool adding)
{
    enemy_keep_message_before_tech(g, at);
    for (unsigned n = 0; n < STAT_CHANGES_BEFORE_TECH; n++) {
        StatChange change = enemy_stat_change_before_tech(g, at, n);
        charge_accesses(g, 2);
        enemy_change_own_stat(g, change, adding);
    }
    charge_rom_accesses(g, TECH_BYTES_READ_FOR_THE_FLAG);
    enemy_tech_begins(g, battle_enemy_turn(g), at, false);
}

// $C1:9FD2
static bool set_stats_then_tech_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    stats_before_tech(g, at, false);
    return tech_after_targets(g, pf, true, 0);
}

// $C1:A20B
static bool add_to_stats_then_tech_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    stats_before_tech(g, at, true);
    return tech_after_targets(g, pf, true, 0);
}

// $C1:9B48
/* The choosing again is taken from the cells and handed back, the script
 * read as far as it read. */
static bool choose_again_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    (void)pf;
    ScriptChoosing c = enemy_choosing_in_cells(g);
    enemy_choose_again(g, &c, at);
    enemy_hand_back_choosing(g, &c);
    return true;
}

// $C1:9B8D
static bool change_into_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    (void)pf;
    if (enemy_change_into(g, at) == CHANGED_TO_BRING_IN
        && !load_the_arrival(g, (uint8_t)(battle_candidates(g, 0) - BATTLE_FIRST_ENEMY)))
        return false;
    enemy_change_ends(g);
    return true;
}

/* The actions that do their part and show their message. */

static bool carry_out_with_message(GameState *g, Platform *pf, EnemyAction action, ScriptPlace at)
{
    uint8_t message = enemy_carry_out(g, battle_enemy_turn(g), action, at);
    if (!show_message(g, pf, message))
        return false;
    follow_up(g, FOLLOW_WITH_NOTHING);
    return true;
}

static bool set_stat_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    return carry_out_with_message(g, pf, ACTION_SET_STAT, at);
}
static bool add_to_stat_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    return carry_out_with_message(g, pf, ACTION_ADD_TO_STAT, at);
}
static bool flags_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    return carry_out_with_message(g, pf, ACTION_FLAGS, at);
}
static bool message_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    return carry_out_with_message(g, pf, ACTION_MESSAGE, at);
}
static bool set_stats_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    return carry_out_with_message(g, pf, ACTION_SET_STATS, at);
}
static bool add_to_stats_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    return carry_out_with_message(g, pf, ACTION_ADD_TO_STATS, at);
}

// $C1:9C6F
static bool leave_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    (void)pf;
    enemy_carry_out(g, battle_enemy_turn(g), ACTION_LEAVE, at);
    return true;
}

// $C1:9CB3
static bool die_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    if (!show_message(g, pf, enemy_carry_out(g, battle_enemy_turn(g), ACTION_DIE, at)))
        return false;
    battle_mark_first_target(g);
    follow_up(g, FOLLOW_WITH_SHOWING);
    return true;
}

// $C1:A14E
static bool add_to_turn_wait_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    (void)pf;
    enemy_carry_out(g, battle_enemy_turn(g), ACTION_ADD_TO_TURN_WAIT, at);
    follow_up(g, FOLLOW_WITH_NOTHING);
    return true;
}

// $C1:9E78
static bool bring_back_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    enemy_bring_back_starts(g, at);
    uint8_t count = 0;
    for (uint8_t enemy = enemy_next_to_bring_back(g, 0); enemy < BATTLE_ENEMIES; enemy = enemy_next_to_bring_back(g, (uint8_t)(enemy + 1))) {
        enemy_bring_back(g, enemy, count++);
        if (!load_the_arrival(g, enemy))
            return false;
        enemy_brought_back(g, enemy, at);
    }
    enemy_bring_back_ends(g, at, count);
    if (!show_message(g, pf, battle_action_message(g)))
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
static bool call_enemies_in(GameState *g)
{
    ScriptPlace at = battle_script_place(g);
    uint8_t how = enemy_call_in_sets_its_stats(g, at);
    uint8_t count = 0;
    for (uint8_t enemy = enemy_next_to_bring_back(g, 0); enemy < BATTLE_ENEMIES; enemy = enemy_next_to_bring_back(g, (uint8_t)(enemy + 1))) {
        enemy_bring_back(g, enemy, count++);
        if (!load_the_arrival(g, enemy))
            return false;
        enemy_called_in(g, enemy, how);
    }
    enemy_call_in_ends(g, at, count);
    return true;
}

// $C1:A396
static bool call_in_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    enemy_call_in_starts(g, at);
    bool done = false;
    TIMED_AS(g, CALL_IN, done = call_enemies_in(g));
    if (!done)
        return false;
    battle_mark_first_target(g);
    battle_mark_other_targets(g);
    if (!show_message(g, pf, battle_action_message(g)))
        return false;
    follow_up(g, FOLLOW_WITH_SHOWING);
    return true;
}

static bool nothing_turn(GameState *g, Platform *pf, ScriptPlace at)
{
    (void)g;
    (void)pf;
    (void)at;
    return true;
}

/* The actions' turns, by action, with the original's routine for each, whose
 * time is its own; the actions with no turn share one, as their routines do
 * nothing alike. */
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

/* The scripts hold no action past the table, and the turn's check
 * (enemy_action_turn_fits) takes none: the original would take a word past
 * its table for a routine. One would do nothing here. */
static const ActionTurnRow no_turn = { nothing_turn, READS_NOTHING_MORE, 0xC19B46 };

static const ActionTurnRow *turn_of(uint8_t action) { return action < ACTION_KINDS ? &turns[action] : &no_turn; }

/* The action's turn, refused where the port doesn't take the action, with
 * the script read at the action as far as the action reads it. */
static bool run_the_action_turn(GameState *g, Platform *pf)
{
    const ActionTurnRow *row = turn_of(battle_action_kind(g));
    if (!enemy_action_turn_fits(g, battle_action_kind(g)))
        return battle_contract_broken(g, row->routine, "an enemy's action the port doesn't take");
    bool done = false;
    TIMED_AS(g, row->routine, done = run_action_turn(g, pf, row->turn, row->reads));
    return done;
}

/* ---- The turn ---------------------------------------------------------------------------- */

/* The action's effect worked out on its targets (battle/effect_work.c: the
 * tech's, or anything else's as an attack's), refused where the port doesn't
 * work it out. */
static bool work_the_effect_out(GameState *g, Platform *pf)
{
    bool tech = battle_action_kind(g) == ACTION_TECH;
    if (!battle_enemy_action_worked_out(g, tech))
        return battle_contract_broken(g, tech ? 0xC1D7C4 : 0xC1D8D1, "an enemy's effect the port doesn't work out");
    TIMED_AS(g, tech ? 0xC1D7C4 : 0xC1D8D1, battle_work_out_enemy_action(g, pf, tech));
    return true;
}

static bool show_the_action(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, SHOWING, done = battle_action_shown(g, pf));
    return done;
}

// $C1:8CF9
static bool enemy_turn(GameState *g, Platform *pf)
{
    start_turn(g);
    TurnStart start = action_to_carry_out(g);
    if (start == CARRY_OUT) {
        if (!run_the_action_turn(g, pf))
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
            if (!work_the_effect_out(g, pf))
                return false;
            if (!effect_shown(g)) {
                end_turn(g);
                return true;
            }
            if (!show_the_action(g, pf))
                return false;
            break;
        case FOLLOW_WITH_SHOWING:
            if (!show_the_action(g, pf))
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

/* Refused where the port doesn't take the turn (battle_enemy_turn_fits). */
bool battle_enemy_takes_turn(GameState *g, Platform *pf)
{
    if (!battle_enemy_turn_fits(g))
        return battle_contract_broken(g, 0xC18CF9, "an enemy's turn the port doesn't take");
    return enemy_turn(g, pf);
}

/* The action shown: the battle's step 4, and its hits landing. */
bool battle_action_shown(GameState *g, Platform *pf)
{
    if (!battle_step_taken(g, pf, BATTLE_STEP_SHOW_ACTION))
        return false;
    TIMED_AS(g, 0xC1EC7F, battle_apply_damage(g));
    return true;
}
