/* Battle: the actions of the enemies' scripts (battle/enemy_actions.h).
 *
 * Choosing an action is C, a row a byte in the table at the end of the
 * choosing: how it is chosen, and which of its operands name selectors. Each
 * choice decodes its operands, and says how far it read the script and what
 * it does to the block's outcome; the choosing (battle/enemy_scripts.c) goes
 * on from that. Carrying an action out runs beside the original's code for
 * its attacks and techs, its messages and its bringing enemies in
 * (battle/enemy_turn.c); what an action does between those calls is here. */
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

static uint8_t byte_at(const GameState *g, ScriptPlace at, unsigned n) { return enemy_script_byte(g, (ScriptPlace)(at + n)); }

/* The enemy whose turn it is, and its battle slot, the actor. */
static uint8_t acting(const GameState *g) { return battle_enemy_acting(g); }
static uint8_t actor(const GameState *g) { return battle_actor(g); }

static void select_by(GameState *g, uint8_t enemy, Selector who)
{
    TIMED_AS(g, 0xC1AC14, battle_select_candidates(g, enemy, who));
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
void enemy_note_pose_kind(GameState *g) { battle_keep_byte(g, BATTLE_TECH_HITS_LEFT, pose_kind(battle_action_pose(g))); }

static void keep_pose_kind(GameState *g)
{
    enemy_note_pose_kind(g);
    uint8_t kind = pose_kind(battle_action_pose(g));
    set_battle_action_pose_kind(g, (uint8_t)((battle_action_pose_kind(g) & 0xF0) | kind));
}

/* ---- Choosing ---------------------------------------------------------------------------------- */

/* What a choice does to the block: nothing, forgives its failing (it counts
 * as held), or finds nobody to act on; and how far it read the script. */
typedef enum { CHOICE_KEEPS, CHOICE_FORGIVES, CHOICE_FINDS_NOBODY } ChoiceOutcome;

typedef struct {
    ChoiceOutcome outcome;
    uint8_t read_to;
} Choice;

static Choice read_to(uint8_t operand) { return (Choice){ CHOICE_KEEPS, operand }; }

/* The enemy faces its target in the pose it announces its action with. */
static void face_target(GameState *g, uint8_t enemy, uint8_t pose)
{
    set_battle_action_pose(g, pose);
    set_battle_enemy_animation(g, enemy, pose);
    set_battle_enemy_target(g, enemy, battle_candidates(g, 0));
}

// $C1:9810
/* A pose: the enemy picks whom it shows it to by its selector, and takes the
 * pose. */
static Choice choose_pose(GameState *g, const ScriptChoosing *c, Selector who, uint8_t pose)
{
    select_by(g, c->enemy, who);
    face_target(g, c->enemy, pose);
    return read_to(3);
}

// $C1:983A
/* An attack or a tech: whom it is aimed at, by its selector, the first by the
 * enemies' drawn order where several are picked. An attack's target and pose
 * are the enemy's (battle_enemy_target); a tech's target is its foe. With
 * nobody to aim at the enemy has no condition held. */
static Choice choose_aim(GameState *g, const ScriptChoosing *c, EnemyAction action, Selector who, uint8_t pose)
{
    select_by(g, c->enemy, who);
    uint8_t count = battle_candidates_held(g);
    if (count == 0 || (count != 1 && !battle_take_first_by_enemy_order(g))) {
        set_battle_enemy_last_condition(g, c->enemy, NO_CONDITION);
        return (Choice){ CHOICE_FINDS_NOBODY, 2 };
    }
    if (action == ACTION_TECH) {
        set_battle_foes(g, c->enemy, battle_candidates(g, 0));
        return read_to(4);
    }
    ORIGINAL_EFFECT(battle_leave_word(g, 0x0C, c->enemy));
    face_target(g, c->enemy, pose);
    return read_to(3);
}

// $C1:9967
static Choice note_number(GameState *g, uint8_t number)
{
    set_battle_action_number(g, number);
    return read_to(0);
}

// $C1:9981
/* Bringing back beaten enemies needs some: an enemy that was in the battle,
 * is out of the fight, and isn't waiting in reserve. */
static bool beaten_enemy(const GameState *g, uint8_t enemy)
{
    uint8_t slot = (uint8_t)(BATTLE_FIRST_ENEMY + enemy);
    return battle_combatant_id(g, slot) != COMBATANT_NONE && !combatant_in_fight(g, slot)
           && !(battle_enemy_standing(g, enemy) & ENEMY_IN_RESERVE);
}

static Choice need_beaten(GameState *g)
{
    uint8_t beaten = 0;
    for (uint8_t enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        if (beaten_enemy(g, enemy))
            beaten++;
    ORIGINAL_EFFECT(battle_leave_word(g, 0x10, BATTLE_ENEMIES));
    ORIGINAL_EFFECT(battle_leave_word(g, 0x0C, beaten));
    return (Choice){ beaten == 0 ? CHOICE_FINDS_NOBODY : CHOICE_KEEPS, 0 };
}

// $C1:98C5
/* One of the four actions after this, a quarter chance each. */
enum { QUARTERS_LIMIT = 100, FIRST_QUARTER = 0x19, SECOND_QUARTER = 0x32, THIRD_QUARTER = 0x4B };

DrawnAction enemy_draw_one_of_four(GameState *g, ScriptPlace at)
{
    static const uint8_t past[] = { FIRST_QUARTER, SECOND_QUARTER, THIRD_QUARTER };
    ScriptPlace place = (ScriptPlace)(at + 1);
    uint8_t drawn = battle_random(g, 0, QUARTERS_LIMIT), quarter = 0;
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x0A, drawn));
    while (quarter < sizeof past && drawn >= past[quarter]) {
        place = (ScriptPlace)(place + enemy_action_length(g, enemy_script_byte(g, place)));
        quarter++;
    }
    keep_pose_kind(g);
    return (DrawnAction){ place, quarter };
}

/* How each action is chosen; the other actions need nothing chosen. */
typedef enum { CHOOSE_NOTHING, CHOOSE_POSE, CHOOSE_AIM, CHOOSE_ONE_OF_FOUR, FORGIVE, NOTE_NUMBER, NEED_BEATEN } ChoiceKind;

typedef struct {
    ChoiceKind choice;
    uint8_t first_selector, last_selector; /* the operands that name selectors (0: none) */
    uint32_t timed_as;                     /* the choosing's routine, whose time it is */
} ActionRow;

// $C1:B85F
static const ActionRow actions[ACTION_KINDS] = {
    [ACTION_POSE] = { CHOOSE_POSE, 2, 2, 0xC19810 },
    [ACTION_ATTACK] = { CHOOSE_AIM, 2, 2, 0xC1983A },
    [ACTION_TECH] = { CHOOSE_AIM, 2, 4, 0xC1983A },
    [ACTION_ONE_OF_FOUR] = { CHOOSE_ONE_OF_FOUR, 0, 0, 0xC198C5 },
    [ACTION_CHANGE_INTO] = { FORGIVE, 0, 0, 0xC19962 },
    [ACTION_LEAVE] = { NOTE_NUMBER, 0, 0, 0xC19967 },
    [ACTION_SET_STAT] = { FORGIVE, 0, 0, 0xC19962 },
    [ACTION_BRING_BACK] = { NEED_BEATEN, 0, 0, 0xC19981 },
    [ACTION_SET_STATS] = { FORGIVE, 0, 0, 0xC19962 },
    [ACTION_SET_STATS_THEN_TECH] = { FORGIVE, 3, 4, 0xC19962 },
    [ACTION_ADD_TO_TURN_WAIT] = { FORGIVE, 1, 1, 0xC19962 },
    [ACTION_ADD_TO_STATS] = { FORGIVE, 0, 0, 0xC19962 },
    [ACTION_ADD_TO_STATS_THEN_TECH] = { FORGIVE, 3, 4, 0xC19962 },
    [ACTION_CALL_IN] = { NEED_BEATEN, 0, 0, 0xC19981 },
};

/* The choice, its operands decoded: a pose's and an aim's selector in the
 * second, the pose in the third; a number in the first. */
static Choice choose(GameState *g, const ScriptChoosing *c, ChoiceKind kind, uint8_t action, ScriptPlace at)
{
    switch (kind) {
    case CHOOSE_POSE: return choose_pose(g, c, byte_at(g, at, 2), byte_at(g, at, 3));
    case CHOOSE_AIM: return choose_aim(g, c, (EnemyAction)action, byte_at(g, at, 2), byte_at(g, at, 3));
    case FORGIVE: return (Choice){ CHOICE_FORGIVES, 0 };
    case NOTE_NUMBER: return note_number(g, byte_at(g, at, 1));
    case NEED_BEATEN: return need_beaten(g);
    case CHOOSE_ONE_OF_FOUR:
    case CHOOSE_NOTHING: break;
    }
    return read_to(0);
}

static void follow_choice(ScriptChoosing *c, Choice r, ScriptPlace at)
{
    c->place = (ScriptPlace)(at + r.read_to);
    if (r.outcome == CHOICE_FORGIVES)
        c->outcome = OUTCOME_HELD;
    else if (r.outcome == CHOICE_FINDS_NOBODY)
        c->outcome = OUTCOME_NOBODY_TO_ACT_ON;
}

void enemy_choose_action(GameState *g, ScriptChoosing *c, uint8_t action, ScriptPlace at)
{
    if (action >= ACTION_KINDS)
        return;
    const ActionRow *row = &actions[action];
    if (row->choice == CHOOSE_NOTHING || row->choice == CHOOSE_ONE_OF_FOUR)
        return;
    Choice r;
    TIMED_AS(g, row->timed_as, r = choose(g, c, row->choice, action, at));
    follow_choice(c, r, at);
}

bool enemy_action_in_tables(const GameState *g, uint8_t action, ScriptPlace at)
{
    if (action >= ACTION_KINDS)
        return false;
    const ActionRow *row = &actions[action];
    for (unsigned operand = row->first_selector; operand != 0 && operand <= row->last_selector; operand++) {
        Selector selector = byte_at(g, at, operand);
        if (!(selector < SELECTORS || (selector & SELECT_CONDITION_TARGET)))
            return false;
    }
    return true;
}

// $C1:9B48
void enemy_choose_again(GameState *g, ScriptChoosing *c, ScriptPlace at)
{
    uint8_t times = byte_at(g, at, 1);
    c->place = (ScriptPlace)(at + 2);
    keep_pose_kind(g);
    do {
        enemy_choose_actions(g, c);
        c->place = (ScriptPlace)(battle_enemy_next_action(g, c->enemy) + SCRIPT_CONDITION_BYTES);
    } while (!(--times & 0x80));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x12, times));
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
/* A tech's hits are what its targeting ($C1:C1DD, the original's still) left
 * for them (battle.h). */
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
        set_battle_action_tech_hits(g, battle_tech_hits_left(g));
        break;
    default:
        break;
    }
    set_battle_action_actor_bits(g, bits);
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x10, bits));
}

/* ---- The attack ---------------------------------------------------------------------------- */

/* An attack as an action names it: its number (1 takes its detail from the
 * enemy's own stat block) and the pose it is announced with. */
enum { ATTACK_WITH_OWN_DETAIL = 1, ATTACKING = 0x02 };

// $C1:99BE
void enemy_attack_begins(GameState *g, ScriptPlace at)
{
    uint8_t number = byte_at(g, at, 1), pose = byte_at(g, at, 2);
    set_battle_action_detail(g, 0);
    set_battle_action_state(g, battle_action_state(g) | ATTACKING);
    uint8_t target = battle_enemy_target(g, acting(g));
    only_target(g, target);
    note_party_hit(g, target);
    battle_mark_first_target(g);
    battle_keep_byte(g, BATTLE_TECH_HITS_LEFT, number);
    set_battle_action_number(g, number);
    if (number == ATTACK_WITH_OWN_DETAIL)
        set_battle_action_detail(g, combatant_stat_byte(g, (uint8_t)(BATTLE_FIRST_ENEMY + acting(g)), STAT_ATTACK_DETAIL));
    set_battle_action_pose(g, pose);
}

void enemy_attack_ends(GameState *g)
{
    enemy_note_actor_bits(g, acting(g));
    enemy_note_pose_kind(g);
    set_battle_action_follow_up(g, FOLLOW_WITH_EFFECT);
}

/* ---- The tech ------------------------------------------------------------------------------- */

EnemyTech enemy_tech_at(const GameState *g, ScriptPlace at)
{
    return (EnemyTech){ byte_at(g, at, 1), { byte_at(g, at, 3), byte_at(g, at, 4) }, byte_at(g, at, 5) };
}

/* A further target, by its selector: the tech's first target stays first
 * among the candidates. */
static uint8_t further_target(GameState *g, uint8_t enemy, Selector who)
{
    if (who == SELECT_NOBODY)
        return NO_CANDIDATE;
    uint8_t first = battle_candidates(g, 0);
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x10, first));
    select_by(g, enemy, who);
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
/* The tech's number is left where its targeting leaves its hits when it has
 * none to give (battle.h). */
void enemy_tech_begins(GameState *g, uint8_t enemy, ScriptPlace at, bool none_checked)
{
    EnemyTech tech = enemy_tech_at(g, at);
    set_battle_action_state(g, battle_action_state(g) & (uint8_t)~ATTACKING);
    battle_keep_tech_hits_left(g, tech.number);
    set_battle_action_number(g, tech.number);
    set_battle_action_detail(g, tech.number);
    only_target(g, battle_foes(g, acting(g)));
    set_battle_action_further_targets(g, 0, NO_CANDIDATE);
    set_battle_action_further_targets(g, 1, NO_CANDIDATE);
    for (unsigned n = 0; n < 2; n++)
        set_battle_action_further_targets(g, n, further_target(g, enemy, tech.further[n]));
    for (unsigned n = 0; n < 2; n++)
        set_battle_action_further_ids(g, n, fighting_as_or_none(g, battle_action_further_targets(g, n), none_checked));
    set_battle_first_candidate(g, battle_candidates(g, 0));
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

/* An amount with bit 7 set is taken away, the rest added, and the stat stays
 * within a byte. */
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

void enemy_change_own_stat(GameState *g, StatChange change, bool adding)
{
    battle_keep_word(g, BATTLE_TECH_HITS_LEFT, change.offset);
    if (adding)
        ORIGINAL_EFFECT(battle_leave_byte(g, 0x10, change.value & (uint8_t)~TAKE_AWAY));
    set_own_stat(g, change.offset, adding ? changed_by(own_stat(g, change.offset), change.value) : change.value);
}

/* The pairs of (stat, value) a stats action gives, from operand `first`. */
static StatChange stat_change_at(const GameState *g, ScriptPlace at, unsigned first, unsigned n)
{
    return (StatChange){ byte_at(g, at, first + 2 * n), byte_at(g, at, first + 2 * n + 1) };
}

/* Setting stats keeps the message as a word, its high byte 0; adding keeps
 * it as a byte. */
static void keep_message(GameState *g, uint8_t message, bool as_word)
{
    set_battle_action_message(g, message);
    if (as_word)
        set_battle_action_message_high(g, 0);
}

enum { FIRST_CHANGE_BEFORE_TECH = 5, MESSAGE_BEFORE_TECH = 15 };

StatChange enemy_stat_change_before_tech(const GameState *g, ScriptPlace at, unsigned n)
{
    return stat_change_at(g, at, FIRST_CHANGE_BEFORE_TECH, n);
}

void enemy_keep_message_before_tech(GameState *g, ScriptPlace at) { keep_message(g, byte_at(g, at, MESSAGE_BEFORE_TECH), true); }

// $C1:9F5A, $C1:A188
/* Four stat changes, then the message. */
enum { FOUR_CHANGES = 4, MESSAGE_AFTER_FOUR = 9 };

static uint8_t change_four_stats(GameState *g, ScriptPlace at, bool adding)
{
    keep_message(g, byte_at(g, at, MESSAGE_AFTER_FOUR), !adding);
    for (unsigned n = 0; n < FOUR_CHANGES; n++)
        enemy_change_own_stat(g, stat_change_at(g, at, 1, n), adding);
    return battle_action_message(g);
}

// $C1:9D1B
/* A stat set, or with `or_in` its bits added, and a message. */
static uint8_t set_stat(GameState *g, uint8_t offset, uint8_t value, uint8_t or_in, uint8_t message)
{
    set_battle_action_message(g, message);
    battle_keep_word(g, BATTLE_TECH_HITS_LEFT, offset);
    ORIGINAL_EFFECT(battle_leave_word(g, 0x10, value));
    ORIGINAL_EFFECT(battle_leave_word(g, 0x12, or_in));
    set_own_stat(g, offset, or_in ? (uint8_t)(own_stat(g, offset) | value) : value);
    return battle_action_message(g);
}

// $C1:9D72
static uint8_t add_to_stat(GameState *g, uint8_t offset, uint8_t amount, uint8_t message)
{
    set_battle_action_message(g, message);
    battle_keep_word(g, BATTLE_TECH_HITS_LEFT, offset);
    ORIGINAL_EFFECT(battle_leave_word(g, 0x10, (amount & TAKE_AWAY) ? amount & (uint8_t)~TAKE_AWAY : amount));
    uint8_t slot = actor(g);
    set_combatant_stat_byte(g, slot, offset, changed_by_signed(combatant_stat_byte(g, slot, offset), amount));
    return battle_action_message(g);
}

/* ---- Leaving, dying, flags, turn waits ------------------------------------------------------ */

/* The enemy is carried out as the effect code's tech number... and then marked
 * as the kind of action the turn shows. */
enum { SHOWN_AS_LEAVING = 7, SHOWN_AS_CHANGING = 5 };

static void knock_out_self(GameState *g)
{
    set_own_stat(g, STAT_CONDITION, (uint8_t)(own_stat(g, STAT_CONDITION) | CONDITION_KNOCKED_OUT));
    set_battle_combatant_fighting(g, actor(g), COMBATANT_NONE);
}

// $C1:9C6F
static void leave(GameState *g, uint8_t number)
{
    set_battle_action_number(g, number);
    set_battle_action_kind(g, ACTION_TECH);
    enemy_record_action(g);
    knock_out_self(g);
    set_battle_action_kind(g, SHOWN_AS_LEAVING);
    set_battle_action_follow_up(g, FOLLOW_WITH_SHOWING);
}

// $C1:9CB3
static uint8_t die(GameState *g, uint8_t number, uint8_t message)
{
    battle_clear_candidates(g);
    set_battle_action_number(g, number);
    set_battle_action_message(g, message);
    set_battle_action_kind(g, ACTION_TECH);
    enemy_record_action(g);
    set_own_stat(g, STAT_HP, 0);
    set_own_stat(g, STAT_HP + 1, 0);
    knock_out_self(g);
    set_battle_combatant_id(g, actor(g), COMBATANT_NONE);
    set_battle_candidates(g, 0, actor(g));
    set_battle_candidate_count(g, 1);
    return battle_action_message(g);
}

// $C1:9DCE
/* The enemy's own flags: the first of the operand's bits set, from the top,
 * says what: nothing; start its count over; take away a flag of the stat
 * block of the combatant in the battle slot of its enemy number (as the
 * original indexes it); nothing again; start the battle's frame count over;
 * count its count up; or set that flag. */
enum {
    FLAG_NOTHING = 0x80, FLAG_COUNT_OVER = 0x40, FLAG_CLEAR = 0x20, FLAG_NOTHING_EITHER = 0x10,
    FLAG_FRAMES_OVER = 0x08, FLAG_COUNT_UP = 0x04, FLAG_SET = 0x02, STAT_FLAG = 0x02,
};

static void flags(GameState *g, uint8_t which)
{
    uint8_t enemy = acting(g);
    battle_keep_byte(g, BATTLE_TECH_HITS_LEFT, which);
    uint8_t condition = combatant_stat_byte(g, enemy, STAT_CONDITION);
    if (which & FLAG_NOTHING)
        return;
    if (which & FLAG_COUNT_OVER)
        set_battle_enemy_script_counter(g, enemy, 0);
    else if (which & FLAG_CLEAR)
        set_combatant_stat_byte(g, enemy, STAT_CONDITION, condition & (uint8_t)~STAT_FLAG);
    else if (which & FLAG_NOTHING_EITHER)
        return;
    else if (which & FLAG_FRAMES_OVER)
        set_battle_frames(g, battle_frames(g) & 0xFF000000u);
    else if (which & FLAG_COUNT_UP)
        set_battle_enemy_script_counter(g, enemy, (uint8_t)(battle_enemy_script_counter(g, enemy) + 1));
    else if (which & FLAG_SET)
        set_combatant_stat_byte(g, enemy, STAT_CONDITION, condition | STAT_FLAG);
}

// $C1:A14E
/* Speed up, or slow down, the combatant the selector picks: its turn wait
 * made shorter or longer by the amount, and kept between 1 and 255. */
static void add_to_turn_wait(GameState *g, uint8_t enemy, Selector who, uint8_t amount)
{
    select_by(g, enemy, who);
    battle_keep_byte(g, BATTLE_TECH_HITS_LEFT, amount);
    ORIGINAL_EFFECT(set_battle_wide_math(g, 4, amount & (uint8_t)~TAKE_AWAY));
    ORIGINAL_EFFECT(set_battle_wide_math(g, 5, 0));
    uint8_t slot = battle_candidates(g, 0), wait = combatant_turn_wait(g, slot);
    uint8_t changed = changed_by_signed(wait, amount);
    /* what is left with its top bit set is 1 (0 stays 0) */
    if ((amount & TAKE_AWAY) && (uint8_t)(wait - (amount & (uint8_t)~TAKE_AWAY)) & 0x80)
        changed = 1;
    set_combatant_turn_wait(g, slot, changed);
}

/* Each decoded from its operands: leaving and dying name the tech number the
 * effect code shows them as, then a message; a stat change its offset, value
 * and message; the flags which, then a message; a turn wait's change a
 * selector and the amount. */
uint8_t enemy_carry_out(GameState *g, uint8_t enemy, EnemyAction action, ScriptPlace at)
{
    uint8_t first = byte_at(g, at, 1), second = byte_at(g, at, 2);
    switch (action) {
    case ACTION_LEAVE: leave(g, first); return 0;
    case ACTION_DIE: return die(g, first, second);
    case ACTION_SET_STAT: return set_stat(g, first, second, byte_at(g, at, 3), byte_at(g, at, 4));
    case ACTION_ADD_TO_STAT: return add_to_stat(g, first, second, byte_at(g, at, 3));
    case ACTION_FLAGS: flags(g, first); return second;
    case ACTION_MESSAGE: return first;
    case ACTION_SET_STATS: return change_four_stats(g, at, false);
    case ACTION_ADD_TO_STATS: return change_four_stats(g, at, true);
    case ACTION_ADD_TO_TURN_WAIT: add_to_turn_wait(g, enemy, first, second); return 0;
    default: return 0;
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
    return (place == slot || place == actor(g)) && (battle_enemy_standing(g, slot - BATTLE_FIRST_ENEMY) & ENEMY_IN_RESERVE);
}

/* Not brought in by the original's code: the enemy takes the actor's HP. */
static void take_actors_hp(GameState *g, uint8_t slot)
{
    set_combatant_stat_byte_by_place(g, slot, STAT_HP, own_stat(g, STAT_HP));
    set_combatant_stat_byte_by_place(g, slot, STAT_HP + 1, own_stat(g, STAT_HP + 1));
}

// $C1:9B8D
/* The action names the id to change into, the effect code's tech number,
 * and whether the original's code brings it in. */
ChangeInto enemy_change_into(GameState *g, ScriptPlace at)
{
    uint8_t id = byte_at(g, at, 1), brought_in = byte_at(g, at, 3);
    set_battle_candidates(g, 0, id);
    set_battle_action_number(g, byte_at(g, at, 2));
    ORIGINAL_EFFECT(battle_leave_byte(g, 0x12, brought_in));
    set_battle_action_kind(g, ACTION_TECH);
    enemy_record_action(g);
    uint8_t slot = BATTLE_FIRST_ENEMY;
    for (; slot < BATTLE_COMBATANTS; slot++) {
        if (battle_combatant_id(g, slot) == id && battle_combatant_fighting(g, slot) != id)
            battle_keep_word(g, BATTLE_TECH_HITS_LEFT, slot);
        if (may_change_into(g, slot, id))
            break;
    }
    if (slot == BATTLE_COMBATANTS) {
        set_battle_script_outcome(g, OUTCOME_NOBODY_TO_ACT_ON);
        return NONE_TO_CHANGE_INTO;
    }
    unsigned enemy = slot - BATTLE_FIRST_ENEMY;
    set_battle_enemy_standing(g, enemy, battle_enemy_standing(g, enemy) & (uint8_t)~ENEMY_IN_RESERVE);
    set_battle_combatant_fighting(g, slot, id);
    set_battle_candidates(g, 0, slot);
    set_battle_candidate_count(g, 1);
    battle_mark_first_target(g);
    uint8_t me = actor(g);
    set_battle_combatant_fighting(g, me, COMBATANT_NONE);
    uint8_t mine = battle_places(g, me);
    battle_keep_byte(g, BATTLE_TECH_HITS_LEFT, battle_places(g, slot));
    set_battle_places(g, me, battle_places(g, slot));
    set_battle_places(g, slot, mine);
    set_battle_enemy_standing(g, me - BATTLE_FIRST_ENEMY, battle_enemy_standing(g, me - BATTLE_FIRST_ENEMY) | ENEMY_IN_RESERVE);
    if (brought_in == 0) {
        take_actors_hp(g, slot);
        return CHANGED;
    }
    ORIGINAL_EFFECT(battle_leave_word(g, 0x02, (uint16_t)(slot - BATTLE_FIRST_ENEMY)));
    return CHANGED_TO_BRING_IN;
}

void enemy_change_ends(GameState *g)
{
    set_battle_action_kind(g, SHOWN_AS_CHANGING);
    set_battle_action_follow_up(g, FOLLOW_WITH_SHOWING);
}

/* ---- Bringing back the beaten --------------------------------------------------------------- */

/* How much of its maximum HP an enemy brought back has, by the action's
 * first operand: half, a quarter, or 1; any other, what it had. The second is
 * the effect code's tech number, the third the message. */
enum { BACK_AT_HALF = 1, BACK_AT_QUARTER = 2, BACK_AT_ONE = 3, SHOWN_AS_TECH = 2 };

uint8_t enemy_next_to_bring_back(const GameState *g, uint8_t enemy)
{
    while (enemy < BATTLE_ENEMIES && !beaten_enemy(g, enemy))
        enemy++;
    return enemy;
}

// $C1:9E78
void enemy_bring_back_starts(GameState *g, ScriptPlace at)
{
    set_battle_action_message(g, byte_at(g, at, 3));
    battle_keep_byte(g, BATTLE_TECH_HITS_LEFT, byte_at(g, at, 1));
    ORIGINAL_EFFECT(battle_leave_word(g, 0x12, byte_at(g, at, 2)));
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
    ORIGINAL_EFFECT(battle_leave_word(g, 0x02, enemy));
    ORIGINAL_EFFECT(battle_leave_word(g, 0x10, enemy));
    ORIGINAL_EFFECT(battle_leave_word(g, 0x0C, (uint16_t)(count + 1)));
}

void enemy_brought_back(GameState *g, uint8_t enemy, ScriptPlace at)
{
    uint8_t slot = (uint8_t)(BATTLE_FIRST_ENEMY + enemy);
    uint16_t most = battle_stats_max_hp(g, slot);
    switch (byte_at(g, at, 1)) {
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
    set_battle_action_what(g, byte_at(g, at, 2));
    set_battle_action_extra(g, 0);
    battle_mark_first_target(g);
    battle_mark_other_targets(g);
    ORIGINAL_EFFECT(battle_leave_word(g, 0x10, BATTLE_ENEMIES));
    ORIGINAL_EFFECT(battle_leave_word(g, 0x0C, count));
}

/* The calling in names what the original's code needs in its first two
 * operands, and its message in the eleventh. */
enum { CALL_IN_MESSAGE = 11 };

void enemy_call_in_starts(GameState *g, ScriptPlace at)
{
    battle_keep_byte(g, BATTLE_TECH_HITS_LEFT, byte_at(g, at, 1));
    ORIGINAL_EFFECT(battle_leave_word(g, 0x12, byte_at(g, at, 2)));
    set_battle_action_message(g, byte_at(g, at, CALL_IN_MESSAGE));
    battle_clear_candidates(g);
}

/* ---- The entries ------------------------------------------------------------------------ */

/* Each action's choosing is called with the choosing in its cells, the script
 * read at the action, and hands it back there. */
static void choice_entry(Snes *s, ChoiceKind kind)
{
    GameState *g = state_of(s);
    ScriptChoosing c = enemy_choosing_in_cells(g);
    ScriptPlace at = c.place;
    follow_choice(&c, choose(g, &c, kind, battle_action_kind(g), at), at);
    enemy_hand_back_choosing(g, &c);
    m_rts(s);
}

/* An entry and its guard, which knows the action it chooses. */
#define CHOICE_ENTRY(name, kind, action) \
    void name(Snes *s) { choice_entry(s, kind); } \
    bool name##_ready(Snes *s) { return battle_script_entry_ready(s, TAKES_ACTION, action); }
CHOICE_ENTRY(battle_enemy_choose_pose, CHOOSE_POSE, ACTION_POSE)
CHOICE_ENTRY(battle_enemy_choose_aim, CHOOSE_AIM, ACTION_ATTACK)
CHOICE_ENTRY(battle_enemy_forgive_block, FORGIVE, ACTION_CHANGE_INTO)
CHOICE_ENTRY(battle_enemy_note_number, NOTE_NUMBER, ACTION_LEAVE)
CHOICE_ENTRY(battle_enemy_need_beaten, NEED_BEATEN, ACTION_BRING_BACK)

/* The draw doesn't return: it drops its own return address and goes on with
 * the choosing's loop at $C1:8C90 ($C1:8C63's, battle/enemy_scripts.c), with
 * A's high byte clear, which the loop indexes with. */
enum { CHOOSING_GOES_ON = 0xC18C90 };

/* On its way it leaves the place the script is read at wrong, which the loop
 * puts right at once: the byte of the last action it stepped over, or, where
 * it drew the first, the word the random draw leaves in X (the long
 * division's quotient cell, battle/random.c). */
enum { QUOTIENT_CELL = 4 };

static uint16_t place_left_by_draw(const GameState *g, ScriptPlace at, uint8_t quarter)
{
    if (quarter == 0)
        return (uint16_t)(battle_wide_math(g, QUOTIENT_CELL) | battle_wide_math(g, QUOTIENT_CELL + 1) << 8);
    ScriptPlace place = (ScriptPlace)(at + 1);
    for (uint8_t q = 1; q < quarter; q++)
        place = (ScriptPlace)(place + enemy_action_length(g, enemy_script_byte(g, place)));
    return enemy_script_byte(g, place);
}

void battle_enemy_draw_one_of_four(Snes *s)
{
    GameState *g = state_of(s);
    ScriptChoosing c = enemy_choosing_in_cells(g);
    ScriptPlace at = c.place;
    DrawnAction drawn = enemy_draw_one_of_four(g, at);
    c.actions_from = drawn.place;
    c.place = place_left_by_draw(g, at, drawn.quarter);
    enemy_hand_back_choosing(g, &c);
    s->cpu.y = m_pull16(s);
    s->cpu.x = c.place;
    s->cpu.a = battle_action_pose_kind(g);
    m_stop_at_own(s, CHOOSING_GOES_ON, DC_AL | DC_X | DC_Y | DC_FLAGS);
}

bool battle_enemy_draw_one_of_four_ready(Snes *s) { return battle_script_entry_ready(s, TAKES_ACTION, ACTION_ONE_OF_FOUR); }

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
