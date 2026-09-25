/* Battle: the actions of the enemies' scripts (battle/enemy_actions.h).
 *
 * Choosing an action is C, a row a byte in the table at the end of the
 * choosing; carrying it out runs beside the original's code for its attacks
 * and techs, its messages and its bringing enemies in (battle/enemy_turn.c),
 * and what an action does between those calls is here. Each reads its
 * operands at its place in the script, and moves the place the script is read
 * at (battle_script_place) as the original does. */
#include "battle/enemy_actions.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/enemy_scripts.h"
#include "battle/entries.h"
#include "battle/random.h"
#include "battle/rules_data.h"
#include "battle/targets.h"
#include "battle/timers.h"
#include "charge.h"
#include "original_effects.h"
#include "vars.h"

/* ---- What they share ------------------------------------------------------------------------ */

static uint8_t operand(const GameState *g, ScriptPlace at, unsigned n) { return enemy_script_byte(g, (ScriptPlace)(at + n)); }

static void leave_byte(GameState *g, unsigned at, uint8_t v) { set_battle_low_scratch(g, at, v); }
static void leave_word(GameState *g, unsigned at, uint16_t v)
{
    set_battle_low_scratch(g, at, (uint8_t)v);
    set_battle_low_scratch(g, at + 1, (uint8_t)(v >> 8));
}

/* The enemy whose script chooses, and the one whose turn it is: the same
 * enemy, which the original names in two cells. */
static uint8_t choosing(const GameState *g) { return enemy_at_work(g); }
static uint8_t acting(const GameState *g) { return battle_enemy_acting(g); }
static uint8_t actor(const GameState *g) { return battle_actor(g); }

static void select_by(GameState *g, ScriptPlace at)
{
    read_script_at(g, (ScriptPlace)(at + 1));
    TIMED_AS(g, 0xC1AC14, battle_select_candidates(g, operand(g, at, 1)));
}

/* The one target: the candidates and the action's targets hold it alone. */
static void only_target(GameState *g, uint8_t slot)
{
    set_battle_candidates(g, 0, slot);
    set_battle_action_targets(g, 0, slot);
    set_battle_candidate_count(g, 1);
    set_battle_action_target_count(g, 1);
}

/* An action aimed at the party lets the party counter it. */
static void note_party_hit(GameState *g, uint8_t first)
{
    if (battle_slot_is_party(first))
        set_battle_action_hit_party(g, true);
}

/* The pose an action is announced with, as the effect code sorts it: 4 for
 * poses 1 and 2, $D for 3 and 4, the pose itself for any other. */
static uint8_t pose_kind(uint8_t pose) { return pose == 1 || pose == 2 ? 0x04 : pose == 3 || pose == 4 ? 0x0D : pose; }

// $C1:ACCE
void enemy_note_pose_kind(GameState *g) { ORIGINAL_EFFECT(leave_byte(g, 0x0E, pose_kind(battle_action_pose(g)))); }

static void keep_pose_kind(GameState *g)
{
    enemy_note_pose_kind(g);
    uint8_t kind = pose_kind(battle_action_pose(g));
    set_battle_action_pose_kind(g, (uint8_t)((battle_action_pose_kind(g) & 0xF0) | kind));
}

/* ---- Choosing ---------------------------------------------------------------------------------- */

/* The enemy faces its target in the pose it announces its action with. */
static void face_target(GameState *g, uint8_t pose)
{
    uint8_t enemy = choosing(g);
    set_battle_action_pose(g, pose);
    set_battle_enemy_animation(g, enemy, pose);
    set_battle_enemy_target(g, enemy, battle_candidates(g, 0));
}

// $C1:9810
/* A pose: the enemy picks whom it shows it to by its selector, and takes the
 * pose. */
static void choose_pose(GameState *g, ScriptPlace at)
{
    read_script_at(g, (ScriptPlace)(at + 1));
    select_by(g, (ScriptPlace)(at + 1));
    read_script_at(g, (ScriptPlace)(at + 3));
    face_target(g, operand(g, at, 3));
}

/* Nobody to act on: the block's outcome says so, and the enemy has no
 * condition held. */
enum { NO_CONDITION = 0xFF };

static void nobody_to_act_on(GameState *g)
{
    set_battle_enemy_last_condition(g, choosing(g), NO_CONDITION);
    set_battle_script_outcome(g, OUTCOME_NOBODY_TO_ACT_ON);
}

// $C1:983A
/* An attack or a tech: whom it is aimed at, by its selector, the first by the
 * enemies' drawn order where several are picked. An attack's target and pose
 * are the enemy's (battle_enemy_target); a tech's target is its foe. */
static void choose_aim(GameState *g, ScriptPlace at)
{
    read_script_at(g, (ScriptPlace)(at + 1));
    select_by(g, (ScriptPlace)(at + 1));
    uint8_t count = battle_candidates_held(g);
    if (count == 0 || (count != 1 && !battle_take_first_by_enemy_order(g))) {
        nobody_to_act_on(g);
        return;
    }
    if (battle_action_kind(g) == ACTION_TECH) {
        set_battle_foes(g, choosing(g), battle_candidates(g, 0));
        read_script_at(g, (ScriptPlace)(at + 4));
        return;
    }
    read_script_at(g, (ScriptPlace)(at + 3));
    ORIGINAL_EFFECT(leave_word(g, 0x0C, choosing(g)));
    face_target(g, operand(g, at, 3));
}

// $C1:98C5
/* One of the four actions after this, a quarter chance each: the draw leaves
 * the one drawn as the block's actions' place, where the choosing takes it
 * up (battle/enemy_scripts.c). */
enum { QUARTERS_LIMIT = 100, FIRST_QUARTER = 0x19, SECOND_QUARTER = 0x32, THIRD_QUARTER = 0x4B };

static uint8_t draw_quarter(GameState *g, ScriptPlace at, uint8_t *stepped_over)
{
    ScriptPlace place = (ScriptPlace)(at + 1);
    set_battle_script_actions_place(g, place);
    uint8_t drawn = battle_random(g, 0, QUARTERS_LIMIT);
    ORIGINAL_EFFECT(leave_byte(g, 0x0A, drawn));
    static const uint8_t past[] = { FIRST_QUARTER, SECOND_QUARTER, THIRD_QUARTER };
    for (unsigned q = 0; q < sizeof past && drawn >= past[q]; q++) {
        *stepped_over = enemy_script_byte(g, place);
        place = (ScriptPlace)(place + enemy_action_length(g, *stepped_over));
        set_battle_script_actions_place(g, place);
    }
    read_script_at(g, place);
    keep_pose_kind(g);
    return drawn;
}

static void draw_one_of_four(GameState *g, ScriptPlace at)
{
    uint8_t stepped_over = 0;
    draw_quarter(g, at, &stepped_over);
}

/* A few actions, chosen, forgive the block's failing: it counts as held. */
static void forgive(GameState *g, ScriptPlace at)
{
    (void)at;
    set_battle_script_outcome(g, OUTCOME_HELD);
}

// $C1:9967
static void note_number(GameState *g, ScriptPlace at) { set_battle_action_number(g, operand(g, at, 1)); }

// $C1:9981
/* Bringing back beaten enemies needs some: an enemy that was in the battle,
 * is out of the fight, and isn't waiting in reserve. */
enum { IN_RESERVE = 0x80 };

static bool beaten_enemy(const GameState *g, uint8_t enemy)
{
    uint8_t slot = (uint8_t)(BATTLE_FIRST_ENEMY + enemy);
    return battle_combatant_id(g, slot) != COMBATANT_NONE && !combatant_in_fight(g, slot)
           && !(battle_enemy_standing(g, enemy) & IN_RESERVE);
}

static void need_beaten(GameState *g, ScriptPlace at)
{
    (void)at;
    uint8_t beaten = 0;
    for (uint8_t enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        if (beaten_enemy(g, enemy))
            beaten++;
    ORIGINAL_EFFECT(leave_word(g, 0x10, BATTLE_ENEMIES));
    ORIGINAL_EFFECT(leave_word(g, 0x0C, beaten));
    if (beaten == 0)
        set_battle_script_outcome(g, OUTCOME_NOBODY_TO_ACT_ON);
}

typedef void (*ChooseAction)(GameState *g, ScriptPlace at);

// $C1:B85F
/* The other actions need nothing chosen. Each choice is timed as the
 * original's routine for it (in bank C1; the three that forgive are alike). */
typedef struct {
    ChooseAction choose;
    uint16_t routine;
} ChoiceRow;

static const ChoiceRow choosing_by_action[ACTION_KINDS] = {
    [ACTION_POSE] = { choose_pose, 0x9810 },
    [ACTION_ATTACK] = { choose_aim, 0x983A },
    [ACTION_TECH] = { choose_aim, 0x983A },
    [ACTION_ONE_OF_FOUR] = { draw_one_of_four, 0x98C5 },
    [ACTION_CHANGE_INTO] = { forgive, 0x9962 },
    [ACTION_LEAVE] = { note_number, 0x9967 },
    [ACTION_SET_STAT] = { forgive, 0x9962 },
    [ACTION_BRING_BACK] = { need_beaten, 0x9981 },
    [ACTION_SET_STATS] = { forgive, 0x9962 },
    [ACTION_SET_STATS_THEN_TECH] = { forgive, 0x9962 },
    [ACTION_ADD_TO_TURN_WAIT] = { forgive, 0x9962 },
    [ACTION_ADD_TO_STATS] = { forgive, 0x9962 },
    [ACTION_ADD_TO_STATS_THEN_TECH] = { forgive, 0x9962 },
    [ACTION_CALL_IN] = { need_beaten, 0x9981 },
};

uint16_t enemy_action_choice_routine(uint8_t action)
{
    return action < ACTION_KINDS && choosing_by_action[action].choose ? choosing_by_action[action].routine : 0;
}

void enemy_choose_action(GameState *g, uint8_t action)
{
    if (action >= ACTION_KINDS || !choosing_by_action[action].choose)
        return;
    const ChoiceRow *row = &choosing_by_action[action];
    TIMED_AS(g, 0xC10000u | row->routine, row->choose(g, battle_script_place(g)));
}

/* ---- The action record ------------------------------------------------------------------------ */

// $C1:ACF2
void enemy_record_action(GameState *g)
{
    set_battle_action_source(g, 0);
    set_battle_action_actor(g, actor(g));
    set_battle_action_type(g, battle_action_kind(g));
    set_battle_action_what(g, battle_action_number(g));
}

/* The attack's and the tech's marks in the acting enemy's bits. */
enum { BITS_ATTACK = 0x80, BITS_TECH = 0x40, ATTACK_HITS = 4 };

// $C1:AC89
/* For a tech, the byte beside them is what the tech's targeting ($C1:C1DD,
 * the original's still) left in the direct page's $0E, which the original
 * takes up there. */
void enemy_note_actor_bits(GameState *g, uint8_t enemy)
{
    uint8_t bits = (uint8_t)(battle_action_actor_bits(g) | (uint8_t)(enemy + BATTLE_FIRST_ENEMY));
    set_battle_action_step(g, 0);
    switch (battle_action_kind(g)) {
    case ACTION_POSE:
        set_battle_action_tech_hits(g, 0);
        break;
    case ACTION_ATTACK:
        bits |= BITS_ATTACK;
        set_battle_action_tech_hits(g, 0);
        set_battle_attack_hits(g, ATTACK_HITS);
        break;
    case ACTION_TECH:
        bits |= BITS_TECH;
        set_battle_action_tech_hits(g, battle_low_scratch(g, 0x0E));
        break;
    default:
        break;
    }
    set_battle_action_actor_bits(g, bits);
    ORIGINAL_EFFECT(leave_byte(g, 0x10, bits));
}

/* ---- The attack ---------------------------------------------------------------------------- */

/* The attack whose detail is the enemy's own stat block byte $1B. */
enum { ATTACK_WITH_OWN_DETAIL = 1, STAT_ATTACK_DETAIL = 0x1B, ATTACKING = 0x02 };

// $C1:99BE
void enemy_attack_begins(GameState *g, ScriptPlace at)
{
    set_battle_action_detail(g, 0);
    set_battle_action_state(g, battle_action_state(g) | ATTACKING);
    uint8_t target = battle_enemy_target(g, acting(g));
    only_target(g, target);
    note_party_hit(g, target);
    battle_mark_first_target(g);
    read_script_at(g, (ScriptPlace)(at + 1));
    uint8_t number = operand(g, at, 1);
    ORIGINAL_EFFECT(leave_byte(g, 0x0E, number));
    set_battle_action_number(g, number);
    if (number == ATTACK_WITH_OWN_DETAIL)
        set_battle_action_detail(g, combatant_stat_byte(g, (uint8_t)(BATTLE_FIRST_ENEMY + acting(g)), STAT_ATTACK_DETAIL));
    read_script_at(g, (ScriptPlace)(at + 2));
    set_battle_action_pose(g, operand(g, at, 2));
}

void enemy_attack_ends(GameState *g)
{
    enemy_note_actor_bits(g, acting(g));
    enemy_note_pose_kind(g);
    set_battle_action_follow_up(g, FOLLOW_WITH_EFFECT);
}

/* ---- The tech ------------------------------------------------------------------------------- */

/* A further target, by the selector at the operand given: the tech's first
 * target stays first among the candidates. */
static uint8_t further_target(GameState *g, ScriptPlace at, unsigned n)
{
    if (operand(g, at, n) == 0) {
        read_script_at(g, (ScriptPlace)(at + n));
        return NO_CANDIDATE;
    }
    uint8_t first = battle_candidates(g, 0);
    ORIGINAL_EFFECT(leave_byte(g, 0x10, first));
    select_by(g, (ScriptPlace)(at + n - 1));
    uint8_t further = battle_candidates(g, 0);
    set_battle_candidates(g, 0, first);
    return further;
}

/* What the further targets are, for the effect code: a stats-then-tech
 * action doesn't check for none, and reads past the table there. */
static uint8_t fighting_as_or_none(const GameState *g, uint8_t slot, bool none_checked)
{
    return none_checked && slot == NO_CANDIDATE ? NO_CANDIDATE : combatant_fighting_as(g, slot);
}

// $C1:9A3D, $C1:A04C
bool enemy_tech_begins(GameState *g, ScriptPlace at, bool none_checked)
{
    set_battle_action_state(g, battle_action_state(g) & (uint8_t)~ATTACKING);
    read_script_at(g, (ScriptPlace)(at + 1));
    uint8_t tech = operand(g, at, 1);
    ORIGINAL_EFFECT(leave_byte(g, 0x0E, tech));
    set_battle_action_number(g, tech);
    set_battle_action_detail(g, tech);
    read_script_at(g, (ScriptPlace)(at + 2));
    only_target(g, battle_foes(g, acting(g)));
    set_battle_action_further_targets(g, 0, NO_CANDIDATE);
    set_battle_action_further_targets(g, 1, NO_CANDIDATE);
    set_battle_action_further_targets(g, 0, further_target(g, at, 3));
    set_battle_action_further_targets(g, 1, further_target(g, at, 4));
    set_battle_action_further_ids(g, 0, fighting_as_or_none(g, battle_action_further_targets(g, 0), none_checked));
    set_battle_action_further_ids(g, 1, fighting_as_or_none(g, battle_action_further_targets(g, 1), none_checked));
    set_battle_first_candidate(g, battle_candidates(g, 0));
    return operand(g, at, 3) != 0 || operand(g, at, 4) != 0;
}

/* The tech's targeting ($C1:C1DD, the original's still) says, through the
 * cell a party member's turn uses too (battle_member_acts_for_itself), when
 * it can't be used. Otherwise the targets it worked out are the
 * candidates. */
bool enemy_tech_aimed(GameState *g)
{
    if (battle_member_acts_for_itself(g)) {
        set_battle_script_outcome(g, OUTCOME_NOBODY_TO_ACT_ON);
        return false;
    }
    for (unsigned place = 0; place < BATTLE_CANDIDATES_COUNT; place++)
        set_battle_candidates(g, place, battle_action_targets(g, place));
    set_battle_candidate_count(g, battle_action_target_count(g));
    note_party_hit(g, battle_action_targets(g, 0));
    battle_mark_first_target(g);
    battle_mark_other_targets(g);
    return true;
}

void enemy_tech_ends(GameState *g)
{
    enemy_note_actor_bits(g, acting(g));
    enemy_note_pose_kind(g);
}

/* ---- Stats ----------------------------------------------------------------------------------- */

/* The scripts name a stat by its offset in the stat block; an amount with
 * bit 7 set is taken away, the rest added, and the stat stays within a
 * byte. */
enum { TAKE_AWAY = 0x80 };

static uint8_t changed_by(uint8_t stat, uint8_t amount)
{
    if (!(amount & TAKE_AWAY))
        return stat + amount > 0xFF ? 0xFF : (uint8_t)(stat + amount);
    amount &= (uint8_t)~TAKE_AWAY;
    return stat >= amount ? (uint8_t)(stat - amount) : 0;
}

/* $C1:9D72 takes away the same way but drops to 0 wherever what is left has
 * its top bit set, as its test is. */
static uint8_t changed_by_signed(uint8_t stat, uint8_t amount)
{
    if (!(amount & TAKE_AWAY))
        return stat + amount > 0xFF ? 0xFF : (uint8_t)(stat + amount);
    uint8_t left = (uint8_t)(stat - (amount & (uint8_t)~TAKE_AWAY));
    return (left & 0x80) ? 0 : left;
}

static void set_own_stat(GameState *g, uint8_t offset, uint8_t v) { set_combatant_stat_byte_by_place(g, actor(g), offset, v); }
static uint8_t own_stat(const GameState *g, uint8_t offset) { return combatant_stat_byte_by_place(g, actor(g), offset); }

/* Whether adding `amount` to `stat` carries past a byte, or taking it away
 * leaves something: the carry the original's change leaves. */
static bool change_carries(uint8_t stat, uint8_t amount)
{
    if (!(amount & TAKE_AWAY))
        return stat + amount > 0xFF;
    return stat >= (uint8_t)(amount & (uint8_t)~TAKE_AWAY);
}

/* The pairs of (stat, value) a stats action gives, from `first` on. */
static void set_stats(GameState *g, ScriptPlace at, unsigned first, unsigned pairs, bool adding)
{
    for (unsigned p = 0; p < pairs; p++) {
        uint8_t offset = operand(g, at, first + 2 * p), value = operand(g, at, first + 2 * p + 1);
        ORIGINAL_EFFECT(leave_word(g, 0x0E, offset));
        if (adding)
            ORIGINAL_EFFECT(leave_byte(g, 0x10, value & (uint8_t)~TAKE_AWAY));
        set_own_stat(g, offset, adding ? changed_by(own_stat(g, offset), value) : value);
    }
}

/* Setting stats keeps the message as a word, its high byte 0; adding keeps
 * it as a byte. */
static void keep_message(GameState *g, uint8_t message, bool as_word)
{
    set_battle_action_message(g, message);
    if (as_word)
        set_battle_action_message_high(g, 0);
}

bool enemy_set_stats_before_tech(GameState *g, ScriptPlace at, bool adding)
{
    enum { FIRST = 5, PAIRS = 5, LAST = FIRST + 2 * (PAIRS - 1) };
    keep_message(g, operand(g, at, 15), true);
    set_stats(g, at, FIRST, PAIRS - 1, adding);
    bool carries = adding && change_carries(own_stat(g, operand(g, at, LAST)), operand(g, at, LAST + 1));
    set_stats(g, at, LAST, 1, adding);
    return carries;
}

/* ---- Leaving, dying, changing, coming back -------------------------------------------------- */

enum { CONDITION_KNOCKED_OUT = 0x80, STAT_HP = 0x03, STAT_CONDITION = 0x1D };

/* The enemy is carried out as the effect code's tech number... and then marked
 * as the kind of action the turn shows. */
enum { SHOWN_AS_LEAVING = 7, SHOWN_AS_CHANGING = 5 };

static void knock_out_self(GameState *g)
{
    set_own_stat(g, STAT_CONDITION, (uint8_t)(own_stat(g, STAT_CONDITION) | CONDITION_KNOCKED_OUT));
    set_battle_combatant_fighting(g, actor(g), COMBATANT_NONE);
}

// $C1:9C6F
static void leave(GameState *g, ScriptPlace at)
{
    set_battle_action_number(g, operand(g, at, 1));
    set_battle_action_kind(g, ACTION_TECH);
    enemy_record_action(g);
    knock_out_self(g);
    set_battle_action_kind(g, SHOWN_AS_LEAVING);
    set_battle_action_follow_up(g, FOLLOW_WITH_SHOWING);
}

// $C1:9CB3
static void die(GameState *g, ScriptPlace at)
{
    battle_clear_candidates(g);
    set_battle_action_number(g, operand(g, at, 1));
    set_battle_action_message(g, operand(g, at, 2));
    set_battle_action_kind(g, ACTION_TECH);
    enemy_record_action(g);
    set_own_stat(g, STAT_HP, 0);
    set_own_stat(g, STAT_HP + 1, 0);
    knock_out_self(g);
    set_battle_combatant_id(g, actor(g), COMBATANT_NONE);
    set_battle_candidates(g, 0, actor(g));
    set_battle_candidate_count(g, 1);
}

/* ---- Actions the turn carries out with no call but a message's ------------------------------ */

// $C1:9D1B
static void set_stat(GameState *g, ScriptPlace at)
{
    uint8_t offset = operand(g, at, 1), value = operand(g, at, 2), or_in = operand(g, at, 3);
    set_battle_action_message(g, operand(g, at, 4));
    ORIGINAL_EFFECT(leave_word(g, 0x0E, offset));
    ORIGINAL_EFFECT(leave_word(g, 0x10, value));
    ORIGINAL_EFFECT(leave_word(g, 0x12, or_in));
    set_own_stat(g, offset, or_in ? (uint8_t)(own_stat(g, offset) | value) : value);
}

// $C1:9D72
static void add_to_stat(GameState *g, ScriptPlace at)
{
    uint8_t offset = operand(g, at, 1), amount = operand(g, at, 2);
    set_battle_action_message(g, operand(g, at, 3));
    ORIGINAL_EFFECT(leave_word(g, 0x0E, offset));
    ORIGINAL_EFFECT(leave_word(g, 0x10, (amount & TAKE_AWAY) ? amount & (uint8_t)~TAKE_AWAY : amount));
    uint8_t slot = actor(g);
    set_combatant_stat_byte(g, slot, offset, changed_by_signed(combatant_stat_byte(g, slot, offset), amount));
}

// $C1:9DCE
/* The enemy's own flags: the first of the operand's bits set, from the top,
 * says what: start its count over, take away or add a flag of the stat block
 * of the combatant in the battle slot of its enemy number (as the original
 * indexes it), start the battle's frame count over, or count its count up. */
enum {
    FLAG_NOTHING = 0x80, FLAG_COUNT_OVER = 0x40, FLAG_CLEAR = 0x20, FLAG_NOTHING_2 = 0x10,
    FLAG_FRAMES_OVER = 0x08, FLAG_COUNT_UP = 0x04, FLAG_SET = 0x02, STAT_FLAG = 0x02,
};

static void flags(GameState *g, ScriptPlace at)
{
    uint8_t which = operand(g, at, 1), enemy = acting(g);
    ORIGINAL_EFFECT(leave_byte(g, 0x0E, which));
    uint8_t condition = combatant_stat_byte(g, enemy, STAT_CONDITION);
    if (which & FLAG_NOTHING)
        return;
    if (which & FLAG_COUNT_OVER)
        set_battle_enemy_script_counter(g, enemy, 0);
    else if (which & FLAG_CLEAR)
        set_combatant_stat_byte(g, enemy, STAT_CONDITION, condition & (uint8_t)~STAT_FLAG);
    else if (which & FLAG_NOTHING_2)
        return;
    else if (which & FLAG_FRAMES_OVER)
        set_battle_frames(g, battle_frames(g) & 0xFF000000u);
    else if (which & FLAG_COUNT_UP)
        set_battle_enemy_script_counter(g, enemy, (uint8_t)(battle_enemy_script_counter(g, enemy) + 1));
    else if (which & FLAG_SET)
        set_combatant_stat_byte(g, enemy, STAT_CONDITION, condition | STAT_FLAG);
}


// $C1:9F5A, $C1:A188
static void set_four_stats(GameState *g, ScriptPlace at, bool adding)
{
    keep_message(g, operand(g, at, 9), !adding);
    set_stats(g, at, 1, 4, adding);
}

// $C1:A14E
/* Speed up, or slow down, the combatant the selector picks: its turn wait
 * made shorter or longer by the amount, and kept between 1 and 255. */
static void add_to_turn_wait(GameState *g, ScriptPlace at)
{
    select_by(g, at);
    uint8_t amount = operand(g, at, 2);
    ORIGINAL_EFFECT(leave_byte(g, 0x0E, amount));
    ORIGINAL_EFFECT(set_battle_wide_math(g, 4, amount & (uint8_t)~TAKE_AWAY));
    ORIGINAL_EFFECT(set_battle_wide_math(g, 5, 0));
    uint8_t slot = battle_candidates(g, 0), wait = combatant_turn_wait(g, slot);
    uint8_t changed = changed_by_signed(wait, amount);
    /* what is left with its top bit set is 1 (0 stays 0) */
    if ((amount & TAKE_AWAY) && (uint8_t)(wait - (amount & (uint8_t)~TAKE_AWAY)) & 0x80)
        changed = 1;
    set_combatant_turn_wait(g, slot, changed);
}

void enemy_carry_out(GameState *g, EnemyAction action, ScriptPlace at)
{
    switch (action) {
    case ACTION_LEAVE: leave(g, at); break;
    case ACTION_DIE: die(g, at); break;
    case ACTION_SET_STAT: set_stat(g, at); break;
    case ACTION_ADD_TO_STAT: add_to_stat(g, at); break;
    case ACTION_FLAGS: flags(g, at); break;
    case ACTION_SET_STATS: set_four_stats(g, at, false); break;
    case ACTION_ADD_TO_STATS: set_four_stats(g, at, true); break;
    case ACTION_ADD_TO_TURN_WAIT: add_to_turn_wait(g, at); break;
    default: break;
    }
}

/* ---- Changing into an enemy in reserve ----------------------------------------------------- */

/* The one to change into: an enemy slot whose id is the one the action names,
 * not already fighting as it, standing in the actor's place (or its own),
 * and waiting in reserve. */
static bool may_change_into(const GameState *g, uint8_t slot, uint8_t id)
{
    if (battle_combatant_id(g, slot) != id || battle_combatant_fighting(g, slot) == id)
        return false;
    uint8_t place = battle_places(g, slot);
    return (place == slot || place == actor(g)) && (battle_enemy_standing(g, slot - BATTLE_FIRST_ENEMY) & IN_RESERVE);
}

// $C1:9B8D
bool enemy_change_into(GameState *g, ScriptPlace at, bool *brought_in)
{
    uint8_t id = operand(g, at, 1);
    set_battle_candidates(g, 0, id);
    set_battle_action_number(g, operand(g, at, 2));
    *brought_in = operand(g, at, 3) != 0;
    ORIGINAL_EFFECT(leave_byte(g, 0x12, operand(g, at, 3)));
    set_battle_action_kind(g, ACTION_TECH);
    enemy_record_action(g);
    uint8_t slot = BATTLE_FIRST_ENEMY;
    for (; slot < BATTLE_COMBATANTS; slot++) {
        if (battle_combatant_id(g, slot) == id && battle_combatant_fighting(g, slot) != id)
            ORIGINAL_EFFECT(leave_word(g, 0x0E, slot));
        if (may_change_into(g, slot, id))
            break;
    }
    if (slot == BATTLE_COMBATANTS) {
        set_battle_script_outcome(g, OUTCOME_NOBODY_TO_ACT_ON);
        return false;
    }
    unsigned enemy = slot - BATTLE_FIRST_ENEMY;
    set_battle_enemy_standing(g, enemy, battle_enemy_standing(g, enemy) & (uint8_t)~IN_RESERVE);
    set_battle_combatant_fighting(g, slot, id);
    set_battle_candidates(g, 0, slot);
    set_battle_candidate_count(g, 1);
    battle_mark_first_target(g);
    uint8_t me = actor(g);
    set_battle_combatant_fighting(g, me, COMBATANT_NONE);
    uint8_t mine = battle_places(g, me);
    ORIGINAL_EFFECT(leave_byte(g, 0x0E, battle_places(g, slot)));
    set_battle_places(g, me, battle_places(g, slot));
    set_battle_places(g, slot, mine);
    set_battle_enemy_standing(g, me - BATTLE_FIRST_ENEMY, battle_enemy_standing(g, me - BATTLE_FIRST_ENEMY) | IN_RESERVE);
    if (*brought_in)
        ORIGINAL_EFFECT(leave_word(g, 0x02, (uint16_t)(slot - BATTLE_FIRST_ENEMY)));
    return true;
}

/* Not brought in by the original's code: the enemy takes the actor's HP. */
void enemy_change_takes_hp(GameState *g)
{
    uint8_t slot = battle_candidates(g, 0);
    set_combatant_stat_byte_by_place(g, slot, STAT_HP, own_stat(g, STAT_HP));
    set_combatant_stat_byte_by_place(g, slot, STAT_HP + 1, own_stat(g, STAT_HP + 1));
}

void enemy_change_ends(GameState *g)
{
    set_battle_action_kind(g, SHOWN_AS_CHANGING);
    set_battle_action_follow_up(g, FOLLOW_WITH_SHOWING);
}

/* ---- Bringing back the beaten --------------------------------------------------------------- */

/* How much of its maximum HP an enemy brought back has, by the action's
 * first operand: half, a quarter, or 1; any other, what it had. */
enum { BACK_AT_HALF = 1, BACK_AT_QUARTER = 2, BACK_AT_ONE = 3, STAT_MAX_HP = 0x05, SHOWN_AS_TECH = 2 };

uint8_t enemy_next_to_bring_back(const GameState *g, uint8_t enemy)
{
    while (enemy < BATTLE_ENEMIES && !beaten_enemy(g, enemy))
        enemy++;
    return enemy;
}

// $C1:9E78
void enemy_bring_back_starts(GameState *g, ScriptPlace at)
{
    set_battle_action_message(g, operand(g, at, 3));
    ORIGINAL_EFFECT(leave_byte(g, 0x0E, operand(g, at, 1)));
    ORIGINAL_EFFECT(leave_word(g, 0x12, operand(g, at, 2)));
    battle_clear_candidates(g);
}

/* The enemy is back in the fight with its turn wait, choosing afresh, not yet
 * counted beaten, the `count`th of those brought back; the original's code
 * brings it in. Its place in the formation is set by its enemy number, as the
 * original indexes the places there. */
void enemy_bring_back(GameState *g, uint8_t enemy, uint8_t count)
{
    uint8_t slot = (uint8_t)(BATTLE_FIRST_ENEMY + enemy);
    set_battle_candidates(g, count, slot);
    set_battle_combatant_fighting(g, slot, battle_combatant_id(g, slot));
    set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, battle_turn_wait(g, slot));
    set_battle_enemy_actions_block(g, enemy, 0xFF);
    set_battle_places(g, enemy, enemy);
    set_battle_enemy_beaten_counted(g, enemy, false);
    ORIGINAL_EFFECT(leave_word(g, 0x02, enemy));
    ORIGINAL_EFFECT(leave_word(g, 0x10, enemy));
    ORIGINAL_EFFECT(leave_word(g, 0x0C, (uint16_t)(count + 1)));
}

void enemy_brought_back(GameState *g, uint8_t enemy, ScriptPlace at)
{
    uint8_t slot = (uint8_t)(BATTLE_FIRST_ENEMY + enemy);
    uint16_t most = battle_stats_max_hp(g, slot);
    switch (operand(g, at, 1)) {
    case BACK_AT_HALF: set_battle_stats_hp(g, slot, (uint16_t)(most >> 1)); break;
    case BACK_AT_QUARTER: set_battle_stats_hp(g, slot, (uint16_t)(most >> 2)); break;
    case BACK_AT_ONE: set_battle_stats_hp(g, slot, 1); break;
    default: break;
    }
}

void enemy_bring_back_ends(GameState *g, ScriptPlace at, uint8_t count)
{
    set_battle_candidate_count(g, count);
    set_battle_action_actor(g, actor(g));
    set_battle_action_type(g, SHOWN_AS_TECH);
    set_battle_action_what(g, operand(g, at, 2));
    set_battle_action_extra(g, 0);
    battle_mark_first_target(g);
    battle_mark_other_targets(g);
    ORIGINAL_EFFECT(leave_word(g, 0x10, BATTLE_ENEMIES));
    ORIGINAL_EFFECT(leave_word(g, 0x0C, count));
}

void enemy_call_in_starts(GameState *g, ScriptPlace at)
{
    ORIGINAL_EFFECT(leave_byte(g, 0x0E, operand(g, at, 1)));
    ORIGINAL_EFFECT(leave_word(g, 0x12, operand(g, at, 2)));
    set_battle_action_message(g, operand(g, at, 11));
    battle_clear_candidates(g);
}

// $C1:9B48
/* Choose again, as many times as the action says and one more: each time
 * the choosing reads on four bytes past the action the enemy is at. */
void enemy_choose_again(GameState *g, ScriptPlace at)
{
    uint8_t times = operand(g, at, 1);
    read_script_at(g, (ScriptPlace)(at + 2));
    keep_pose_kind(g);
    do {
        enemy_choose_actions(g);
        read_script_at(g, (ScriptPlace)(battle_enemy_next_action(g, enemy_at_work(g)) + SCRIPT_CONDITION_BYTES));
    } while (!(--times & 0x80));
    ORIGINAL_EFFECT(leave_byte(g, 0x12, times));
}

/* ---- The entries ------------------------------------------------------------------------ */

/* Each action's choosing is called with the script read at the action. */
static void choice_entry(Snes *s, ChooseAction choose)
{
    GameState *g = state_of(s);
    choose(g, battle_script_place(g));
    m_rts(s);
}

#define CHOICE_ENTRY(name, choose) \
    void name(Snes *s) { choice_entry(s, choose); }
CHOICE_ENTRY(battle_enemy_choose_pose, choose_pose)
CHOICE_ENTRY(battle_enemy_choose_aim, choose_aim)
CHOICE_ENTRY(battle_enemy_forgive_block, forgive)
CHOICE_ENTRY(battle_enemy_note_number, note_number)
CHOICE_ENTRY(battle_enemy_need_beaten, need_beaten)

/* The draw doesn't return: it drops its own return address and goes on with
 * the choosing's loop at $C1:8C90 ($C1:8C63's, battle/enemy_scripts.c), with
 * A's high byte clear, which the loop indexes with. */
enum { CHOOSING_GOES_ON = 0xC18C90 };

/* On its way it leaves the place the script is read at wrong, which the loop
 * puts right at once: the byte of the last action it stepped over, or, where
 * it drew the first, the word the random draw leaves in X (the long
 * division's quotient cell, battle/random.c). */
enum { QUOTIENT_CELL = 4 };

void battle_enemy_draw_one_of_four(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t stepped_over = 0;
    uint8_t drawn = draw_quarter(g, battle_script_place(g), &stepped_over);
    uint16_t left = drawn < FIRST_QUARTER
                        ? (uint16_t)(battle_wide_math(g, QUOTIENT_CELL) | battle_wide_math(g, QUOTIENT_CELL + 1) << 8)
                        : stepped_over;
    ORIGINAL_EFFECT(set_battle_script_place(g, left));
    s->cpu.y = m_pull16(s);
    s->cpu.x = left;
    s->cpu.a = battle_action_pose_kind(g);
    m_stop_at_own(s, CHOOSING_GOES_ON, DC_AL | DC_X | DC_Y | DC_FLAGS);
}

void battle_enemy_record_action(Snes *s)
{
    enemy_record_action(state_of(s));
    m_rts(s);
}

/* The acting enemy comes in A. */
void battle_enemy_note_actor_bits(Snes *s)
{
    GameState *g = state_of(s);
    enemy_note_actor_bits(g, (uint8_t)s->cpu.a);
    m_set_a8(s, battle_action_actor_bits(g));
    m_rts(s);
}

/* It leaves the kind in A, and the pose in X. */
void battle_enemy_pose_kind(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t pose = battle_action_pose(g);
    enemy_note_pose_kind(g);
    s->cpu.a = pose_kind(pose);
    s->cpu.x = pose;
    m_rts(s);
}
