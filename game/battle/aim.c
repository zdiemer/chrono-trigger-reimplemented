/* Battle: whom an action aims at.
 *
 * Before a tech or an item is worked out, it is aimed ($C1:C1DD): a party
 * member's as its turn carries it out (battle/member_turn.c), an enemy's tech
 * as its script's turn does (battle/enemy_turn.c). The caller has named the
 * target the actor chose (battle_first_candidate); aiming turns that choice,
 * and the action's own records (battle/aim_data.h), into the action's
 * targets (battle_action_targets and their count). It goes in three steps:
 *
 *   1. Whether the action can be made at all: the actor must be able to take
 *      part, and so must a party tech's partners, or the enemy an enemy's
 *      tech brings in with it.
 *   2. The targets the action's aim mode takes (Take): the one chosen, or
 *      whoever took its place; everyone on a side; one foe, drawn at random
 *      where the chosen one has fallen; one that is knocked out, for a revive.
 *   3. Its area rules, where it has any: each places an area on the
 *      combatants -- around the tech's first member, around the target,
 *      around the actor and reaching toward the target -- and the original's
 *      area search (the battle's step 7, $C1:1FDD, still a call-out) finds
 *      who stands in it, who become the targets. Then the targets are settled
 *      as the mode says (Finish): the chosen one added, those out of the
 *      fight taken off the list.
 *
 * An action that can't be made, or whose area finds nobody, comes to nothing
 * (battle_action_came_to_nothing), and its turn skips the rest; a party
 * tech's area that finds nobody is shown first, as the tech's "no target"
 * showing.
 *
 * The original works in the direct page's cells as it goes, and some of what
 * it leaves there is read after it as game input (battle.h): the cells from
 * $00 to $0E are kept plainly, as it leaves them; the rest of its working
 * cells are scaffolding. The entry is at the end. */
#include "battle/aim_data.h"
#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/effect_data.h"
#include "battle/effect_parts.h"
#include "battle/effect_work.h"
#include "battle/enemy_actions.h"
#include "battle/entries.h"
#include "battle/random.h"
#include "battle/rules_data.h"
#include "charge.h"
#include "original_effects.h"
#include "port/native.h"
#include "vars.h"

enum { NO_TARGET = 0xFF };

/* ---- How a mode aims ---------------------------------------------------------------------------- */

/* What an aim mode takes. The modes are the records' bytes; several bytes
 * aim alike, and a byte not listed takes the chosen target as it is. */
typedef enum {
    TAKE_CHOSEN,          /* the chosen target, or whoever took its place */
    TAKE_ONE_STANDING,    /* the same, which must not be knocked out */
    TAKE_OWN_SIDE,        /* everyone on the actor's side in the fight */
    TAKE_OTHER_SIDE,      /* everyone on the other side in the fight */
    TAKE_WHOLE_PARTY,     /* the three party places, in the fight or not */
    TAKE_ONE_FALLEN,      /* the chosen target, which must be knocked out: a revive */
    TAKE_FOE,             /* a foe: the chosen one, or one drawn at random where it has fallen */
    TAKE_BY_ROLL,         /* a foe or a side, by a roll */
    TAKE_LISTED,          /* the targets as the caller listed them */
    TAKE_FOE_FOR_AREA,    /* a foe, for the area rules to work from */
    TAKE_AREA,            /* nothing: the area rules find them all */
    TAKE_CHOSEN_AND_AREA, /* the chosen target, and the area's finds after it */
} Take;

/* How the targets are settled once the area rules are done. The values are
 * the original's, which it leaves in a cell (scaffolding). */
typedef enum {
    FINISH_CHOSEN = 0,          /* the first target is the chosen one, or whoever took its place */
    FINISH_LISTED = 1,          /* as listed, those out of the fight taken off */
    FINISH_CHOSEN_FOE = 2,      /* as FINISH_CHOSEN */
    FINISH_AREA = 3,            /* as the area found them, those out taken off */
    FINISH_AREA_AND_CHOSEN = 4, /* the area's finds and the chosen one after them, even where the area found nobody */
    FINISH_KEEP_FALLEN = 5,     /* as listed, the knocked out kept */
} Finish;

typedef struct {
    uint8_t mode;
    Take take;
    Finish finish;
} AimRow;

static const AimRow AIM_MODES[] = {
    { 0x00, TAKE_ONE_STANDING, FINISH_CHOSEN },
    { 0x05, TAKE_ONE_STANDING, FINISH_CHOSEN },
    { 0x06, TAKE_ONE_STANDING, FINISH_CHOSEN },
    { 0x01, TAKE_OWN_SIDE, FINISH_LISTED },
    { 0x08, TAKE_OTHER_SIDE, FINISH_LISTED },
    { 0x04, TAKE_WHOLE_PARTY, FINISH_KEEP_FALLEN },
    { 0x03, TAKE_ONE_FALLEN, FINISH_KEEP_FALLEN },
    { 0x07, TAKE_FOE, FINISH_CHOSEN },
    { 0x09, TAKE_BY_ROLL, FINISH_LISTED },
    { 0x0A, TAKE_LISTED, FINISH_LISTED },
    { 0x0B, TAKE_FOE_FOR_AREA, FINISH_CHOSEN_FOE },
    { 0x0C, TAKE_FOE_FOR_AREA, FINISH_CHOSEN_FOE },
    { 0x0D, TAKE_FOE_FOR_AREA, FINISH_CHOSEN_FOE },
    { 0x0E, TAKE_FOE_FOR_AREA, FINISH_CHOSEN_FOE },
    { 0x0F, TAKE_FOE_FOR_AREA, FINISH_CHOSEN_FOE },
    { 0x12, TAKE_FOE_FOR_AREA, FINISH_CHOSEN_FOE },
    { 0x1A, TAKE_FOE_FOR_AREA, FINISH_CHOSEN_FOE },
    { 0x10, TAKE_AREA, FINISH_AREA },
    { 0x11, TAKE_AREA, FINISH_AREA },
    { 0x13, TAKE_AREA, FINISH_AREA },
    { 0x14, TAKE_AREA, FINISH_AREA },
    { 0x15, TAKE_AREA, FINISH_AREA },
    { 0x1B, TAKE_AREA, FINISH_AREA },
    { 0x32, TAKE_CHOSEN_AND_AREA, FINISH_AREA_AND_CHOSEN },
};

static AimRow aim_row(uint8_t mode)
{
    for (unsigned n = 0; n < sizeof AIM_MODES / sizeof AIM_MODES[0]; n++)
        if (AIM_MODES[n].mode == mode)
            return AIM_MODES[n];
    return (AimRow){ mode, TAKE_CHOSEN, FINISH_CHOSEN };
}

/* ---- The cells the original leaves --------------------------------------------------------------- */

/* The direct page's cells the targeting works in. Those up to $0E are read
 * after it as game input (battle.h), so they are kept plainly, each as the
 * original leaves it; those past it are scaffolding. */
enum {
    LEFT_MODE = 0x00,        /* the aim mode, then the tech's first member's party place */
    LEFT_SECOND = 0x02,      /* the tech's second member's party place */
    LEFT_PARTY_END = 0x04,   /* a word: where the party's places end; then a byte: the third member's place, the rule's placing */
    LEFT_CHOSEN = 0x06,      /* the chosen target, as the area rules start */
    LEFT_SIZE = 0x08,        /* the area's size */
    LEFT_AMONG_PARTY = 0x0A, /* 1 where the area is among the party's places */
    LEFT_AREA = 0x0C,        /* the area rules' set (an item's, its caller's), then the actor */
    LEFT_FLAG = 0x0E,        /* the size byte's top bit */
    LEFT_ENEMIES_END = 0x10, /* a word: where the enemies' places end; then a byte: where the next rule is */
    LEFT_RULE = 0x12,        /* the rule being placed */
    LEFT_RULES_LEFT = 0x14,  /* the rules still to place */
};

static void keep(GameState *g, unsigned cell, uint8_t v) { battle_keep_byte(g, cell, v); }

/* The aim as it is worked out: the action's records, and the side its area
 * is among, which each way of taking targets sets as it goes. */
typedef struct {
    ActionAim aim;
    AimRow row;
    bool among_party;
} Aiming;

static void set_among_party(GameState *g, Aiming *a, bool party)
{
    a->among_party = party;
    keep(g, LEFT_AMONG_PARTY, party);
}

/* ---- Who can take part ------------------------------------------------------------------------- */

/* Two of the second status byte's bits, whose meaning isn't worked out, keep
 * a combatant out of an action as the statuses that keep it from acting do. */
enum { STATUS2_KEEP_OUT_OF_ACTIONS = 0x18, STATUS1_KEEP_OUT_OF_ACTIONS = STATUS_ASLEEP | STATUS_CONFUSED | STATUS_STOP };

/* Whether a combatant has none of the statuses that keep it out of an
 * action. Any slot is read as the original reads the stat blocks. */
static bool free_to_act(const GameState *g, uint8_t slot)
{
    uint8_t fourth = (uint8_t)(combatant_stat_byte(g, slot, STAT_STATUS_4) | combatant_stat_byte(g, slot, STAT_KEPT_STATUS_4));
    return !(combatant_stat_byte(g, slot, STAT_STATUS_1) & STATUS1_KEEP_OUT_OF_ACTIONS)
           && !(combatant_stat_byte(g, slot, STAT_STATUS_2) & STATUS2_KEEP_OUT_OF_ACTIONS) && !(fourth & STATUS4_KEEPS_FROM_ACTING);
}

static bool knocked_out(const GameState *g, uint8_t slot)
{
    return (combatant_stat_byte(g, slot, STAT_CONDITION) & CONDITION_KNOCKED_OUT) != 0;
}

static bool can_take_part(const GameState *g, uint8_t slot) { return !knocked_out(g, slot) && free_to_act(g, slot); }

/* The actor: an enemy the action has beaten still makes its action (its
 * last, as it falls), knocked out or not. */
static bool actor_can_act(const GameState *g, uint8_t actor)
{
    bool beaten = actor >= BATTLE_FIRST_ENEMY && battle_enemy_beaten(g, (unsigned)(actor - BATTLE_FIRST_ENEMY));
    return (beaten || !knocked_out(g, actor)) && free_to_act(g, actor);
}

/* A party tech's partners, its second and third members, each where it has
 * one in the party: the first who has none ends the test. */
static bool partners_can_act(const GameState *g)
{
    for (unsigned part = 1; part < TECH_MEMBERS; part++) {
        uint8_t member = battle_tech_members(g, part);
        if (member == NO_MEMBER)
            return true;
        uint8_t slot = battle_character_slot(g, member);
        if (slot == NO_TARGET)
            return true;
        if (!can_take_part(g, slot))
            return false;
    }
    return true;
}

/* The enemy an enemy's tech brings in with it (its first further target).
 * The original tests it a second time where the second further target's test
 * belongs, and never looks at that one. */
static bool enemy_brought_in_can_act(const GameState *g)
{
    uint8_t first = battle_action_further_targets(g, 0);
    return first == NO_TARGET || can_take_part(g, first);
}

/* ---- The action's aim --------------------------------------------------------------------------- */

static bool actor_in_party(const GameState *g) { return battle_actor(g) < BATTLE_PARTY_SIZE; }

/* Whether the party member acting chose an item, as its action record says
 * (whose place its caller hands the original: BATTLE_MEMBER_CELL). */
static bool member_uses_item(const GameState *g) { return (battle_member_actions_choice_flags(g, battle_actor(g)) & CHOSE_AN_ITEM) != 0; }

/* The action's aim: an enemy tech's, an item's by its number (its area rules
 * are what the caller leaves for it, battle_cell_word: none), a party
 * tech's. */
static ActionAim action_aim(const GameState *g)
{
    uint8_t detail = battle_action_detail(g);
    if (!actor_in_party(g))
        return enemy_tech_aim(g, detail);
    if (member_uses_item(g))
        return (ActionAim){ item_aim_mode(g, (uint8_t)(detail & 0x7F)), (uint8_t)battle_cell_word(g, LEFT_AREA) };
    return party_tech_aim(g, detail);
}

/* Step 1: whether the action can be made. The cells it leaves on the way:
 * the sides' ends, the side, the area rules' set (an item leaves its
 * caller's), and the mode. */
static bool can_be_made(GameState *g, Aiming *a)
{
    uint8_t actor = battle_actor(g);
    if (!actor_can_act(g, actor))
        return false;
    battle_keep_word(g, LEFT_PARTY_END, BATTLE_PARTY_SIZE);
    ORIGINAL_EFFECT(battle_leave_word(g, LEFT_ENEMIES_END, BATTLE_COMBATANTS));
    bool party = actor < BATTLE_PARTY_SIZE, item = party && member_uses_item(g);
    set_among_party(g, a, !party);
    if (!item)
        keep(g, LEFT_AREA, a->aim.area);
    keep(g, LEFT_MODE, a->aim.mode);
    if (!party)
        return enemy_brought_in_can_act(g);
    return item || partners_can_act(g);
}

/* ---- Step 2: the targets ------------------------------------------------------------------------- */

static bool in_fight(const GameState *g, uint8_t slot) { return combatant_fighting_as(g, slot) != COMBATANT_NONE; }

static void set_target(GameState *g, unsigned place, uint8_t slot) { set_battle_action_targets(g, place, slot); }

/* The only target, the chosen one. */
static void only_target(GameState *g, uint8_t slot)
{
    set_target(g, 0, slot);
    set_battle_action_target_count(g, 1);
}

// $C1:C8F7
/* The combatant an action meant for `slot` falls on: `slot` while it is in
 * the fight, and otherwise whoever now stands in its place. */
static uint8_t standing_in_for(const GameState *g, uint8_t slot)
{
    if (slot == NO_TARGET || in_fight(g, slot))
        return slot;
    return battle_places(g, slot);
}

static uint8_t chosen_target(const GameState *g) { return standing_in_for(g, battle_first_candidate(g)); }

/* Everyone in the fight from `first` up to `end`, as the targets (`first`
 * looked at whatever `end` is); false where nobody is. */
static bool take_everyone(GameState *g, uint8_t first, uint8_t end)
{
    uint8_t count = 0, slot = first;
    do
        if (in_fight(g, slot))
            set_target(g, count++, slot);
    while (++slot < end);
    if (count == 0)
        return false;
    set_battle_action_target_count(g, count);
    return true;
}

/* A side: the actor's own, or the other. The area is said to be among the
 * party's places where the actor is a party member, for both: the original
 * sets it so whichever side the mode takes. `party_end` is where the party's
 * places end, which a roll can set. */
static bool take_side(GameState *g, Aiming *a, bool own_side, uint8_t party_end)
{
    bool party = actor_in_party(g);
    set_among_party(g, a, party);
    return own_side == party ? take_everyone(g, 0, party_end) : take_everyone(g, BATTLE_FIRST_ENEMY, BATTLE_COMBATANTS);
}

// $C1:C82D
/* The chosen foe, the first candidate the caller named: where it has left the
 * fight, another drawn at random from those in it (in a demo battle, the
 * first enemy in it, so that the demo plays the same), which becomes the
 * chosen one. False where the actor's foes are all out. */
static bool take_foe(GameState *g, Aiming *a)
{
    set_battle_action_came_to_nothing(g, 0);
    bool party = actor_in_party(g);
    uint8_t first = party ? BATTLE_FIRST_ENEMY : 0, end = party ? BATTLE_COMBATANTS : BATTLE_PARTY_SIZE;
    uint8_t slot = first;
    while (slot < end && !in_fight(g, slot))
        slot++;
    if (slot == end) {
        set_battle_action_came_to_nothing(g, 1);
        return false;
    }
    if (in_fight(g, battle_first_candidate(g)))
        return true;
    if (party && battle_is_demo(g)) {
        set_battle_action_target_count(g, 0);
        for (slot = first; !in_fight(g, slot); slot++)
            ;
    } else {
        do
            TIMED_AS(g, 0xC1AF22, slot = battle_random(g, first, end));
        while (!in_fight(g, slot));
    }
    set_battle_first_candidate(g, slot);
    only_target(g, slot);
    set_among_party(g, a, !party);
    return true;
}

/* A target taken by a roll, which isn't random: the original draws a number
 * and then reads the roll from the arithmetic's cell the draw puts back as it
 * found it, where the tech members' lookup at the start left its record's
 * number (battle/party_work.c), so each item that aims this way rolls the
 * same every time. By the roll: a foe, the actor's side, a foe, the other
 * side, or nothing. On the way it leaves the buttons the player holds (the
 * repeating ones' low byte, or their high), $FF or 0, and the row's number;
 * the actor's side taken this way ends the party's places at that byte of
 * buttons. */
enum { ROLL_LIMIT = 100, REPEATING_BUTTONS = 2 };

typedef struct {
    uint8_t below;
    bool foe, own_side, high_buttons;
    uint8_t chosen_cell, size_cell;
} RollRow;

static const RollRow ROLLS[] = {
    { 8, true, false, false, 0xFF, 1 },
    { 12, false, true, false, 0, 2 },
    { 18, true, false, true, 0xFF, 3 },
    { 20, false, false, true, 0, 4 },
};

static uint8_t buttons_byte(const GameState *g, bool high)
{
    uint16_t buttons = input_mapped(g, REPEATING_BUTTONS);
    return (uint8_t)(high ? buttons >> 8 : buttons);
}

/* *done: the foe is the whole aim, with nothing after it. */
static bool take_by_roll(GameState *g, Aiming *a, bool *done)
{
    TIMED_AS(g, 0xC1AF22, (void)battle_random(g, 0, ROLL_LIMIT));
    uint8_t roll = (uint8_t)battle_wide_math_quotient(g);
    for (unsigned n = 0; n < sizeof ROLLS / sizeof ROLLS[0]; n++) {
        const RollRow *r = &ROLLS[n];
        if (roll >= r->below)
            continue;
        uint8_t buttons = buttons_byte(g, r->high_buttons);
        keep(g, LEFT_PARTY_END, buttons);
        keep(g, LEFT_CHOSEN, r->chosen_cell);
        keep(g, LEFT_SIZE, r->size_cell);
        if (r->foe) {
            *done = true;
            return take_foe(g, a);
        }
        return take_side(g, a, r->own_side, buttons);
    }
    return false;
}

/* A revive's target, which must be knocked out; an enemy one must not be in
 * reserve, which the original asks of the enemies' table by the target's
 * battle slot, as if it were its enemy number, reading past the eight for the
 * last three. */
static bool fallen_in_reserve(const GameState *g, uint8_t slot)
{
    uint8_t standing = slot < BATTLE_ENEMY_STANDING_COUNT ? battle_enemy_standing(g, slot) : battle_enemy_standing_unchecked(g, slot);
    return (standing & ENEMY_IN_RESERVE) != 0;
}

static bool take_one_fallen(GameState *g, Aiming *a)
{
    uint8_t slot = chosen_target(g);
    set_target(g, 0, slot);
    if (!knocked_out(g, slot))
        return false;
    set_battle_action_target_count(g, 1);
    bool party = slot < BATTLE_PARTY_SIZE;
    if (!party && fallen_in_reserve(g, slot))
        return false;
    set_among_party(g, a, party);
    return true;
}

static bool take_one_standing(GameState *g, Aiming *a)
{
    set_among_party(g, a, actor_in_party(g));
    uint8_t slot = chosen_target(g);
    set_target(g, 0, slot);
    if (knocked_out(g, slot))
        return false;
    set_battle_action_target_count(g, 1);
    return true;
}

static bool take_whole_party(GameState *g)
{
    for (uint8_t slot = 0; slot < BATTLE_PARTY_SIZE; slot++)
        set_target(g, slot, slot);
    set_battle_action_target_count(g, BATTLE_PARTY_SIZE);
    return true;
}

/* Step 2. False where the action comes to nothing; *done where it is aimed
 * with nothing more to do. */
static bool take_by_mode(GameState *g, Aiming *a, bool *done)
{
    switch (a->row.take) {
    case TAKE_ONE_STANDING:
        return take_one_standing(g, a);
    case TAKE_OWN_SIDE:
        return take_side(g, a, true, BATTLE_PARTY_SIZE);
    case TAKE_OTHER_SIDE:
        return take_side(g, a, false, BATTLE_PARTY_SIZE);
    case TAKE_WHOLE_PARTY:
        return take_whole_party(g);
    case TAKE_ONE_FALLEN:
        return take_one_fallen(g, a);
    case TAKE_FOE:
    case TAKE_FOE_FOR_AREA:
        return take_foe(g, a);
    case TAKE_BY_ROLL:
        return take_by_roll(g, a, done);
    case TAKE_CHOSEN_AND_AREA:
        only_target(g, chosen_target(g));
        set_among_party(g, a, true);
        return true;
    case TAKE_CHOSEN:
    case TAKE_LISTED:
    case TAKE_AREA:
        break;
    }
    return true;
}

/* ---- Step 3: the area rules ------------------------------------------------------------------------ */

/* Where an area is placed: the combatants the rule's placing anchors it on,
 * and what of the rule it passes on. */
typedef enum { ANCHOR_NONE, ANCHOR_FIRST_MEMBER, ANCHOR_SECOND_MEMBER, ANCHOR_CHOSEN, ANCHOR_ACTOR, ANCHOR_SIZE } Anchor;

typedef struct {
    bool side_is_first_member; /* the side is the first member's party place rather than the area's side */
    Anchor centre, toward;
    bool size, flag;
} AreaPlacing;

enum { AREA_PLACINGS = 7 };

static const AreaPlacing PLACINGS[AREA_PLACINGS] = {
    { false, ANCHOR_FIRST_MEMBER, ANCHOR_CHOSEN, true, true }, /* from the tech's performer toward the target */
    { false, ANCHOR_CHOSEN, ANCHOR_NONE, true, false },        /* around the target */
    { false, ANCHOR_SIZE, ANCHOR_NONE, false, false },         /* on the combatant the size names */
    { true, ANCHOR_CHOSEN, ANCHOR_SECOND_MEMBER, false, false }, /* around the target toward the second member, on the first's side */
    { false, ANCHOR_SECOND_MEMBER, ANCHOR_NONE, true, false }, /* around the tech's second member */
    { false, ANCHOR_ACTOR, ANCHOR_NONE, true, false },         /* around the actor */
    { false, ANCHOR_ACTOR, ANCHOR_CHOSEN, true, true },        /* from the actor toward the target */
};

static bool placing_needs_members(const AreaPlacing *p)
{
    return p->side_is_first_member || p->centre == ANCHOR_FIRST_MEMBER || p->centre == ANCHOR_SECOND_MEMBER
           || p->toward == ANCHOR_FIRST_MEMBER || p->toward == ANCHOR_SECOND_MEMBER;
}

/* What the area rules are placed with: a party tech's members' places, the
 * chosen target, the actor. */
typedef struct {
    uint8_t first_member, second_member, chosen, actor;
} Anchors;

// $C1:C74C
/* The party places of a party tech's members, which the original looks up
 * again with the tech's members (its multiply entered with the carry clear)
 * and leaves for the effect as the further targets. An enemy's tech has none:
 * its rules are placed without them. */
static Anchors gather_anchors(GameState *g)
{
    Anchors a = { 0, 0, battle_first_candidate(g), battle_actor(g) };
    if (actor_in_party(g)) {
        effect_take_tech_members(g, battle_action_detail(g));
        uint8_t places[TECH_MEMBERS];
        places[0] = battle_character_slot(g, battle_tech_members(g, 0));
        for (unsigned part = 1; part < TECH_MEMBERS; part++) {
            uint8_t member = battle_tech_members(g, part);
            places[part] = member == NO_MEMBER ? NO_MEMBER : battle_character_slot(g, member);
            set_battle_action_further_targets(g, part - 1, places[part]);
        }
        a.first_member = places[0];
        a.second_member = places[1];
        keep(g, LEFT_MODE, places[0]);
        keep(g, LEFT_SECOND, places[1]);
        keep(g, LEFT_PARTY_END, places[2]);
    }
    keep(g, LEFT_CHOSEN, a.chosen);
    keep(g, LEFT_AREA, a.actor);
    return a;
}

static uint8_t anchor(const Anchors *a, Anchor which, uint8_t size)
{
    switch (which) {
    case ANCHOR_FIRST_MEMBER:
        return a->first_member;
    case ANCHOR_SECOND_MEMBER:
        return a->second_member;
    case ANCHOR_CHOSEN:
        return a->chosen;
    case ANCHOR_ACTOR:
        return a->actor;
    case ANCHOR_SIZE:
        return size;
    case ANCHOR_NONE:
        break;
    }
    return 0;
}

/* Where the original is in its rules: the next rule's place in the set (past
 * its count), the rule's number, the rules left, and the set's place, whose
 * high byte the rule's search flag takes. */
static void leave_rule_reached(GameState *g, uint16_t rules, uint8_t rule, uint8_t left)
{
    ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_ENEMIES_END, (uint8_t)(1 + rule * AREA_RULE_BYTES)));
    ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_RULE, rule));
    ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_RULES_LEFT, left));
    ORIGINAL_EFFECT(set_battle_aim_rules(g, (uint16_t)(rules & 0x00FF)));
}

static void leave_rules_placed(GameState *g, uint8_t count)
{
    ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_ENEMIES_END, (uint8_t)(1 + count * AREA_RULE_BYTES)));
    ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_RULE, count));
    ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_RULES_LEFT, 0));
}

/* The set's one rule's area, placed for the area search. */
static void place_area(GameState *g, const Aiming *a, const Anchors *at, const AreaRule *r)
{
    set_battle_aim_rule_shape(g, r->shape);
    keep(g, LEFT_PARTY_END, (uint8_t)(r->placing | (r->searches_for_target ? AREA_RULE_TOP_BIT : 0)));
    keep(g, LEFT_SIZE, r->size);
    keep(g, LEFT_FLAG, r->flag);
    const AreaPlacing *p = &PLACINGS[r->placing];
    set_battle_area_search_among_party(g, p->side_is_first_member ? at->first_member : a->among_party);
    set_battle_area_search_centre(g, anchor(at, p->centre, r->size));
    set_battle_area_search_toward(g, anchor(at, p->toward, r->size));
    set_battle_area_search_size(g, p->size ? r->size : 0);
    set_battle_area_search_flag(g, p->flag ? r->flag : 0);
}

/* An enemy waiting in reserve isn't taken from the area's finds. The area
 * search ends its finds with $FF, which the original's test takes as a
 * combatant past the enemies, reading the byte that far on: where that byte's
 * top bit is set, the end is passed over as a reserve is. */
static bool in_reserve(const GameState *g, uint8_t slot)
{
    if (slot < BATTLE_FIRST_ENEMY)
        return false;
    unsigned enemy = (unsigned)(slot - BATTLE_FIRST_ENEMY);
    uint8_t standing = enemy < BATTLE_ENEMY_STANDING_COUNT ? battle_enemy_standing(g, enemy) : battle_enemy_standing_unchecked(g, enemy);
    return (standing & ENEMY_IN_RESERVE) != 0;
}

/* The finds become the targets and the candidates, place for place, those in
 * reserve passed over (their places keep what they held). The original
 * finds where the rule's finds start by a multiply, by the rule's number --
 * the first's, at the list's start -- and walks them with the product's
 * cell, which it leaves past the last place. */
static void take_finds(GameState *g)
{
    uint8_t taken = 0;
    unsigned place = (uint16_t)effect_times(g, 0, BATTLE_AREA_FOUND_COUNT);
    for (; place < BATTLE_AREA_FOUND_COUNT; place++) {
        uint8_t slot = battle_area_found(g, place);
        if (in_reserve(g, slot))
            continue;
        set_target(g, place, slot);
        set_battle_candidates(g, place, slot);
        if (slot != NO_TARGET)
            taken++;
    }
    battle_set_wide_math_quotient(g, (uint16_t)place);
    set_battle_candidate_count(g, taken);
    set_battle_action_target_count(g, taken);
}

static bool area_found_nobody(const GameState *g) { return battle_area_found(g, 0) == NO_TARGET && battle_area_found(g, 1) == NO_TARGET; }

/* A party tech whose area found nobody is shown as its "no target" showing,
 * by how many members it has, aimed at the first enemy's place; the showing
 * is what the record says. The original asks only the top bit of the third
 * member, which a member's none has. */
enum { NO_TARGET_ALONE = 0x7B, NO_TARGET_PAIR = 0x7C, NO_TARGET_TRIPLE = 0x7D, FIRST_ENEMY_BIT = 0x1000, NO_MEMBER_BIT = 0x80 };

static uint8_t record_no_target_showing(GameState *g)
{
    set_battle_action_actor(g, battle_actor(g));
    set_battle_action_type(g, ACTION_TECH);
    set_battle_action_extra(g, 0);
    set_battle_action_target_bits(g, FIRST_ENEMY_BIT);
    uint8_t what = !(battle_tech_members(g, 2) & NO_MEMBER_BIT) ? NO_TARGET_TRIPLE
                   : battle_tech_members(g, 1) != NO_MEMBER ? NO_TARGET_PAIR
                                                            : NO_TARGET_ALONE;
    set_battle_action_what(g, what);
    return what;
}

/* The chosen target after the area's finds, in the first free place. */
static void add_chosen_after_finds(GameState *g)
{
    for (uint8_t place = 0; place < BATTLE_ACTION_TARGETS_COUNT; place++)
        if (battle_action_targets(g, place) == NO_TARGET) {
            set_target(g, place, battle_first_candidate(g));
            set_battle_action_target_count(g, (uint8_t)(battle_action_target_count(g) + 1));
            return;
        }
}

/* Take those out of the fight off the targets, closing up behind them. The
 * original's walk is kept as it is: where it meets a place with no target it
 * steps past it and closes up over the next place, so a gap takes the
 * target after it off the list too; and the list's last place is never
 * closed over, so closing up repeats it. */
enum { TARGETS_CLOSED_UP_TO = BATTLE_ACTION_TARGETS_COUNT - 1 };

static void drop_those_out(GameState *g)
{
    unsigned place = 0;
    while (place < BATTLE_ACTION_TARGETS_COUNT) {
        uint8_t slot = battle_action_targets(g, place);
        if (slot == NO_TARGET)
            place++;
        else if (in_fight(g, slot)) {
            place++;
            continue;
        }
        if (place >= TARGETS_CLOSED_UP_TO)
            return;
        for (unsigned at = place; at < TARGETS_CLOSED_UP_TO; at++)
            set_target(g, at, battle_action_targets(g, at + 1));
    }
}

/* The targets counted up to the first place with none; false where every
 * place has one. */
static bool count_targets(GameState *g)
{
    set_battle_action_target_count(g, 0);
    for (uint8_t place = 0; place < BATTLE_ACTION_TARGETS_COUNT; place++)
        if (battle_action_targets(g, place) == NO_TARGET) {
            set_battle_action_target_count(g, place);
            return true;
        }
    return false;
}

/* The targets settled after the area rules: false where the action comes to
 * nothing. */
static bool settle_targets(GameState *g, Finish finish)
{
    if (finish == FINISH_AREA_AND_CHOSEN)
        add_chosen_after_finds(g);
    if (finish != FINISH_KEEP_FALLEN)
        drop_those_out(g);
    return count_targets(g);
}

/* ---- The entry ---------------------------------------------------------------------------------- */

static void came_to_nothing(GameState *g) { set_battle_action_came_to_nothing(g, 1); }

/* The area search's finds, as the port takes them: combatants, or none.
 * Others stop the port at the search's return, and the original carries
 * on. */
enum { SEARCH_AREA = 0xC1C741, SEARCH_AREA_CALL = 0xC1C5FB, SHOWING = 0xC1AC57 };

static bool finds_fit(Snes *s)
{
    const GameState *g = state_of(s);
    for (unsigned place = 0; place < BATTLE_AREA_FOUND_COUNT; place++) {
        uint8_t slot = battle_area_found(g, place);
        if (slot != NO_TARGET && slot >= BATTLE_COMBATANTS) {
            battle_stop_after_call(s, SEARCH_AREA_CALL);
            return false;
        }
    }
    return true;
}

/* Rule `rule`'s area searched, and its finds taken. The carry the search
 * hands back goes into the multiply that finds where its finds start. */
static bool search_by_rule(Snes *s, const Aiming *a, const Anchors *at, uint16_t rules, const AreaRule *rule)
{
    GameState *g = state_of(s);
    place_area(g, a, at, rule);
    leave_rule_reached(g, rules, 0, 1);
    /* A clear, as the original's loads leave it: the battle's steps pick
     * their step by the whole of A */
    s->cpu.a = 0;
    if (!battle_run(s, SEARCH_AREA_CALL, SEARCH_AREA, battle_search_area) || !finds_fit(s))
        return false;
    bool carry = m_flag(s, CPU_FLAG_C);
    take_finds(g);
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, carry));
    return true;
}

/* How the aim ends for the entry: stopped, where the original carries on;
 * aimed; or aimed after the "no target" showing. */
typedef enum { AIM_STOPPED, AIM_DONE, AIM_SHOWN } AimEnd;

static AimEnd nothing_shown(Snes *s)
{
    if (!actor_in_party(state_of(s))) {
        came_to_nothing(state_of(s));
        return AIM_DONE;
    }
    s->cpu.a = record_no_target_showing(state_of(s));
    if (!battle_run(s, 0xC1C6A3, SHOWING, battle_start_action_showing))
        return AIM_STOPPED;
    came_to_nothing(state_of(s));
    return AIM_SHOWN;
}

/* Step 3, for an action with area rules: every set the ROM has holds one
 * rule (the entry's guard takes no more), and a set of none places nothing. */
static AimEnd aim_by_area(Snes *s, const Aiming *a)
{
    GameState *g = state_of(s);
    uint16_t rules = area_rules_place(g, a->aim.area);
    uint8_t count = area_rule_count(g, rules);
    ORIGINAL_EFFECT(set_battle_aim_rules(g, rules));
    Anchors anchors = gather_anchors(g);
    AreaRule rule = area_rule(g, rules, 0);
    if (count != 0 && !search_by_rule(s, a, &anchors, rules, &rule))
        return AIM_STOPPED;
    leave_rules_placed(g, count);
    if (a->row.finish != FINISH_AREA_AND_CHOSEN && area_found_nobody(g))
        return nothing_shown(s);
    if (!settle_targets(g, a->row.finish))
        came_to_nothing(g);
    return AIM_DONE;
}

static AimEnd aim(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_action_came_to_nothing(g, 0);
    effect_take_tech_members(g, battle_action_detail(g));
    /* the lookup's multiply is entered with the caller's carry, which it
     * leaves in a cell */
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, m_flag(s, CPU_FLAG_C)));
    Aiming a = { action_aim(g), { 0, TAKE_CHOSEN, FINISH_CHOSEN }, false };
    a.row = aim_row(a.aim.mode);
    if (!can_be_made(g, &a)) {
        came_to_nothing(g);
        return AIM_DONE;
    }
    /* a roll's way leaves its finish before it rolls; every other leaves
     * none until it has taken its targets */
    ORIGINAL_EFFECT(set_battle_aim_finish(g, a.row.take == TAKE_BY_ROLL ? a.row.finish : FINISH_CHOSEN));
    bool done = false;
    if (!take_by_mode(g, &a, &done)) {
        came_to_nothing(g);
        return AIM_DONE;
    }
    ORIGINAL_EFFECT(set_battle_aim_finish(g, a.row.finish));
    if (done)
        return AIM_DONE;
    if (a.row.finish == FINISH_CHOSEN || a.row.finish == FINISH_CHOSEN_FOE)
        set_target(g, 0, chosen_target(g));
    ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_RULE, 0));
    set_battle_action_came_to_nothing(g, 0);
    if (a.aim.area == NO_AREA)
        return AIM_DONE;
    return aim_by_area(s, &a);
}

/* ---- What the port aims ------------------------------------------------------------------------- */

/* An actor in the battle; a party member's action record where its caller
 * hands it, and for an item no area rules; a chosen target that is a
 * combatant (or none, but for a revive); a tech's members in the party's
 * table; and area rules as the ROM's are: one a set, each the area search's,
 * placed one of the seven ways, an enemy's tech's by a way that needs no
 * party members. A roll for the actor's side bounded by more buttons than
 * the party has places reads past them; the ROM's items never roll that, and
 * an enemy never rolls. */
static bool rules_fit(const GameState *g, uint8_t area, bool party)
{
    if (area == NO_AREA)
        return true;
    uint16_t rules = area_rules_place(g, area);
    uint8_t count = area_rule_count(g, rules);
    if (count > 1)
        return false;
    for (unsigned n = 0; n < count; n++) {
        AreaRule r = area_rule(g, rules, n);
        if (r.searches_for_target || r.placing >= AREA_PLACINGS || (!party && placing_needs_members(&PLACINGS[r.placing])))
            return false;
    }
    return true;
}

static bool members_fit(const GameState *g, uint8_t tech)
{
    PartyTech t = party_tech(g, tech);
    for (unsigned part = 0; part < TECH_MEMBERS; part++) {
        uint8_t member = tech_member(g, t.members, part);
        if ((part == 0 || member != NO_MEMBER) && member >= BATTLE_CHARACTER_SLOT_COUNT)
            return false;
    }
    return true;
}

static bool roll_fits(const GameState *g, bool party)
{
    if (!party)
        return false;
    uint8_t roll = party_tech(g, battle_action_detail(g)).members;
    return roll < ROLLS[0].below || roll >= ROLLS[1].below || buttons_byte(g, false) <= BATTLE_PARTY_SIZE;
}

static bool aim_fits(const GameState *g)
{
    uint8_t actor = battle_actor(g);
    if (actor >= BATTLE_COMBATANTS)
        return false;
    bool party = actor < BATTLE_PARTY_SIZE;
    if (party && battle_cell_word(g, BATTLE_MEMBER_CELL) != battle_member_record_place(g, actor))
        return false;
    bool item = party && member_uses_item(g);
    if (item && battle_cell_word(g, LEFT_AREA) != NO_AREA)
        return false;
    if (party && !item && !members_fit(g, battle_action_detail(g)))
        return false;
    ActionAim aim = action_aim(g);
    Take take = aim_row(aim.mode).take;
    uint8_t chosen = battle_first_candidate(g);
    if (chosen != NO_TARGET ? chosen >= BATTLE_COMBATANTS : take == TAKE_ONE_FALLEN)
        return false;
    if (take == TAKE_BY_ROLL && !roll_fits(g, party))
        return false;
    return rules_fit(g, aim.area, party);
}

bool battle_aim_action_ready(Snes *s)
{
    vars_peek(true);
    bool fits = aim_fits(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && fits;
}

// $C1:C1DD
/* A's high byte goes back clear, as the original's last loads leave it, but
 * after the "no target" showing, which hands back its own. */
void battle_aim_action(Snes *s)
{
    AimEnd end = aim(s);
    if (end == AIM_STOPPED)
        return;
    if (end == AIM_DONE)
        s->cpu.a &= 0x00FF;
    m_rts(s);
}

// $C1:C741
/* The area search, the battle's step 7, for the shape its caller hands it in
 * a cell (battle_aim_rule_shape). The carry the search leaves goes back to
 * the caller, whose multiply after it takes it. It is entered with A's high
 * byte clear, which the steps' dispatch reads with the step. */
enum { AREA_SEARCH_CALL = 0xC1C748, STEP_READS_A = DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

void battle_search_area(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_area_shape(g, battle_aim_rule_shape(g));
    m_set_a8(s, BATTLE_STEP_SEARCH_AREA);
    /* dc-evidence: ct_fuzz --poison-dc battle_search_area, seed 1: 5000 states,
       5000 call-outs scrambled, 0 mismatched */
    if (battle_call(s, AREA_SEARCH_CALL, BATTLE_STEP, STEP_READS_A))
        m_rts(s);
}
