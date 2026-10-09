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
 * The original works in the direct page's first cells as it goes. What it
 * leaves there is worked out once, from what the steps decided
 * (leave_aim_cells): the cells a later reader takes as game input (battle.h)
 * plainly, the rest as scaffolding. The logic is in functions of the game's
 * state; the entry at the end runs the two call-outs, the area search and the
 * "no target" showing. */
#include "battle/aim_data.h"
#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/choosing.h"
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
 * aim alike, and a byte not listed takes nobody itself, the chosen target
 * becoming the first as the targets are settled. */
typedef enum {
    TAKE_NOBODY,          /* nobody: the caller's list, the chosen target or the area's finds stand */
    TAKE_ONE_STANDING,    /* the chosen target, or whoever took its place, which must not be knocked out */
    TAKE_OWN_SIDE,        /* everyone on the actor's side in the fight */
    TAKE_OTHER_SIDE,      /* everyone on the other side in the fight */
    TAKE_WHOLE_PARTY,     /* the three party places, in the fight or not */
    TAKE_ONE_FALLEN,      /* the chosen target, which must be knocked out: a revive */
    TAKE_FOE,             /* a foe: the chosen one, or one drawn at random where it has fallen */
    TAKE_BY_ROLL,         /* a foe or a side, by a roll */
    TAKE_CHOSEN_AND_AREA, /* the chosen target, and the area's finds after it */
} Take;

/* How the targets are settled once the area rules are done. */
typedef enum {
    FINISH_FIRST_IS_CHOSEN, /* the first target is the chosen one, or whoever took its place; those out taken off */
    FINISH_DROP_THOSE_OUT,  /* as they are, those out of the fight taken off */
    FINISH_AREA_AND_CHOSEN, /* the area's finds and the chosen one after them, even where the area found nobody */
    FINISH_KEEP_FALLEN,     /* as they are, the knocked out kept */
} Finish;

/* A mode's row; `left` is the number the original leaves for its finish in
 * a cell nothing reads after it (battle_aim_finish, scaffolding), which tells
 * apart modes that settle alike. */
typedef struct {
    uint8_t mode;
    Take take;
    Finish finish;
    uint8_t left;
} AimRow;

static const AimRow AIM_MODES[] = {
    { 0x00, TAKE_ONE_STANDING, FINISH_FIRST_IS_CHOSEN, 0 },
    { 0x05, TAKE_ONE_STANDING, FINISH_FIRST_IS_CHOSEN, 0 },
    { 0x06, TAKE_ONE_STANDING, FINISH_FIRST_IS_CHOSEN, 0 },
    { 0x01, TAKE_OWN_SIDE, FINISH_DROP_THOSE_OUT, 1 },
    { 0x08, TAKE_OTHER_SIDE, FINISH_DROP_THOSE_OUT, 1 },
    { 0x04, TAKE_WHOLE_PARTY, FINISH_KEEP_FALLEN, 5 },
    { 0x03, TAKE_ONE_FALLEN, FINISH_KEEP_FALLEN, 5 },
    { 0x07, TAKE_FOE, FINISH_FIRST_IS_CHOSEN, 0 },
    { 0x09, TAKE_BY_ROLL, FINISH_DROP_THOSE_OUT, 1 },
    { 0x0A, TAKE_NOBODY, FINISH_DROP_THOSE_OUT, 1 },
    { 0x0B, TAKE_FOE, FINISH_FIRST_IS_CHOSEN, 2 },
    { 0x0C, TAKE_FOE, FINISH_FIRST_IS_CHOSEN, 2 },
    { 0x0D, TAKE_FOE, FINISH_FIRST_IS_CHOSEN, 2 },
    { 0x0E, TAKE_FOE, FINISH_FIRST_IS_CHOSEN, 2 },
    { 0x0F, TAKE_FOE, FINISH_FIRST_IS_CHOSEN, 2 },
    { 0x12, TAKE_FOE, FINISH_FIRST_IS_CHOSEN, 2 },
    { 0x1A, TAKE_FOE, FINISH_FIRST_IS_CHOSEN, 2 },
    { 0x10, TAKE_NOBODY, FINISH_DROP_THOSE_OUT, 3 },
    { 0x11, TAKE_NOBODY, FINISH_DROP_THOSE_OUT, 3 },
    { 0x13, TAKE_NOBODY, FINISH_DROP_THOSE_OUT, 3 },
    { 0x14, TAKE_NOBODY, FINISH_DROP_THOSE_OUT, 3 },
    { 0x15, TAKE_NOBODY, FINISH_DROP_THOSE_OUT, 3 },
    { 0x1B, TAKE_NOBODY, FINISH_DROP_THOSE_OUT, 3 },
    { 0x32, TAKE_CHOSEN_AND_AREA, FINISH_AREA_AND_CHOSEN, 4 },
};

static AimRow aim_row(uint8_t mode)
{
    for (unsigned n = 0; n < sizeof AIM_MODES / sizeof AIM_MODES[0]; n++)
        if (AIM_MODES[n].mode == mode)
            return AIM_MODES[n];
    return (AimRow){ mode, TAKE_NOBODY, FINISH_FIRST_IS_CHOSEN, 0 };
}

/* ---- The aim as it is worked out ----------------------------------------------------------------- */

/* What the area rules are placed with: a party tech's members' places, the
 * chosen target, the actor. */
typedef struct {
    uint8_t first_member, second_member, third_member, chosen, actor;
} Anchors;

typedef struct RollRow RollRow;

/* The aim as it is worked out: the action's records, the side its area is
 * among (which each way of taking targets sets as it goes), and how far the
 * steps went -- whether the action's records were looked at (the actor able
 * to act), the roll taken, the area's anchors gathered and its rule placed,
 * which is what the original's cells hold after it. */
typedef struct {
    ActionAim aim;
    AimRow row;
    bool among_party;
    bool records_read, item;
    const RollRow *rolled;
    uint8_t roll_buttons;
    bool gathered;
    Anchors anchors;
    bool placed;
    AreaRule rule;
    AreaAsked asked;
    uint16_t rules;
    uint8_t rule_count;
} Aiming;

static void set_among_party(Aiming *a, bool party) { a->among_party = party; }

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

/* The action's aim: an enemy tech's, an item's, a party tech's. An item has
 * no area rules; its number is the action's detail less the item flag the
 * original shifts out of a byte. */
enum { ITEM_NUMBER_BITS = 0x7F };

static ActionAim action_aim(const GameState *g)
{
    uint8_t detail = battle_action_detail(g);
    if (!actor_in_party(g))
        return enemy_tech_aim(g, detail);
    if (member_uses_item(g))
        return (ActionAim){ item_aim_mode(g, (uint8_t)(detail & ITEM_NUMBER_BITS)), NO_AREA };
    return party_tech_aim(g, detail);
}

/* Step 1: whether the action can be made. Past the actor, the action's
 * records are looked at, and its area taken to be among the other side's. */
static bool can_be_made(const GameState *g, Aiming *a)
{
    uint8_t actor = battle_actor(g);
    if (!actor_can_act(g, actor))
        return false;
    bool party = actor < BATTLE_PARTY_SIZE;
    a->records_read = true;
    a->item = party && member_uses_item(g);
    set_among_party(a, !party);
    if (!party)
        return enemy_brought_in_can_act(g);
    return a->item || partners_can_act(g);
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
    set_among_party(a, party);
    return own_side == party ? take_everyone(g, 0, party_end) : take_everyone(g, BATTLE_FIRST_ENEMY, BATTLE_COMBATANTS);
}

// $C1:C82D
/* The chosen foe, the first candidate the caller named: where it has left the
 * fight, another drawn at random from those in it (in a demo battle, the
 * first enemy in it, so that the demo plays the same), which becomes the
 * chosen one. False where the actor's foes are all out. */
static bool take_foe(GameState *g, Aiming *a)
{
    bool party = actor_in_party(g);
    uint8_t first = party ? BATTLE_FIRST_ENEMY : 0, end = party ? BATTLE_COMBATANTS : BATTLE_PARTY_SIZE;
    uint8_t slot = first;
    while (slot < end && !in_fight(g, slot))
        slot++;
    if (slot == end)
        return false;
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
    set_among_party(a, !party);
    return true;
}

/* A target taken by a roll, which isn't random: the original draws a number
 * and then takes as the roll the number of the action's tech members'
 * record, which its lookup at the start of the aim left where the draw puts
 * its cell back as it found it, so each item that aims this way rolls the
 * same every time. By the roll: a foe, the actor's side, a foe, the other
 * side, or nothing. The actor's side taken this way ends the party's places
 * at a byte of the buttons the player holds (the repeating ones' low byte,
 * or their high). The original also leaves the row's number and a target
 * byte in its cells (leave_aim_cells). */
enum { ROLL_LIMIT = 100, REPEATING_BUTTONS = 2 };

struct RollRow {
    uint8_t below;
    bool foe, own_side, high_buttons;
    uint8_t left_target, left_row;
};

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

static uint8_t the_roll(const GameState *g) { return party_tech(g, battle_action_detail(g)).members; }

/* *done: the foe is the whole aim, with nothing after it. */
static bool take_by_roll(GameState *g, Aiming *a, bool *done)
{
    TIMED_AS(g, 0xC1AF22, (void)battle_random(g, 0, ROLL_LIMIT));
    uint8_t roll = the_roll(g);
    for (unsigned n = 0; n < sizeof ROLLS / sizeof ROLLS[0]; n++) {
        const RollRow *r = &ROLLS[n];
        if (roll >= r->below)
            continue;
        a->rolled = r;
        a->roll_buttons = buttons_byte(g, r->high_buttons);
        if (r->foe) {
            *done = true;
            return take_foe(g, a);
        }
        return take_side(g, a, r->own_side, a->roll_buttons);
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
    set_among_party(a, party);
    return true;
}

static bool take_one_standing(GameState *g, Aiming *a)
{
    set_among_party(a, actor_in_party(g));
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
        return take_foe(g, a);
    case TAKE_BY_ROLL:
        return take_by_roll(g, a, done);
    case TAKE_CHOSEN_AND_AREA:
        only_target(g, chosen_target(g));
        set_among_party(a, true);
        return true;
    case TAKE_NOBODY:
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

// $C1:C74C
/* The party places of a party tech's members, which the original looks up
 * again with the tech's members (its multiply entered with the carry clear)
 * and leaves for the effect as the further targets. An enemy's tech has none:
 * its rules are placed without them. */
static Anchors gather_anchors(GameState *g)
{
    Anchors a = { 0, 0, 0, battle_first_candidate(g), battle_actor(g) };
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
        a.third_member = places[2];
    }
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

/* ---- What the original leaves in its cells ------------------------------------------------------- */

/* The direct page's first cells, as the original leaves them. It works in
 * them as it goes, each holding several things in turn; what each holds at
 * the end follows from how far the aim went. Those a later reader takes as
 * game input (battle.h: the clock's word, the member's cell, the cells read
 * as stats) are written plainly; the others are scaffolding. */
enum {
    LEFT_MODE_OR_FIRST = 0x00,  /* the aim mode, or a party tech's first member's place once its area is gathered */
    LEFT_SECOND = 0x02,         /* a party tech's second member's place */
    LEFT_BOUND = 0x04,          /* a word: where the party's places end; its low byte then the roll's buttons,
                                   the third member's place, the rule's placing */
    LEFT_TARGET = 0x06,         /* the roll's target byte, then the chosen target */
    LEFT_SIZE = 0x08,           /* the roll row's number, then the area's size */
    LEFT_SIDE = 0x0A,           /* 1 where the area is among the party's places */
    LEFT_RULES_OR_ACTOR = 0x0C, /* the area rules' set (an item's is its caller's), then the actor */
    LEFT_FLAG = 0x0E,           /* the area's size byte's top bit */
    LEFT_ENEMIES_END = 0x10,    /* a word: where the enemies' places end; its low byte then where the next rule is */
    LEFT_RULE = 0x12,           /* the rule being placed */
    LEFT_RULES_LEFT = 0x14,     /* the rules still to place */
};

static void leave_aim_cells(GameState *g, const Aiming *a)
{
    if (!a->records_read)
        return;
    bool members = a->gathered && battle_actor(g) < BATTLE_PARTY_SIZE;
    uint8_t bound = a->placed   ? (uint8_t)(a->rule.placing | (a->rule.searches_for_target ? AREA_RULE_TOP_BIT : 0))
                    : members   ? a->anchors.third_member
                    : a->rolled ? a->roll_buttons
                                : BATTLE_PARTY_SIZE;
    ORIGINAL_EFFECT(battle_leave_word(g, LEFT_BOUND, bound));
    ORIGINAL_EFFECT(battle_leave_word(g, LEFT_ENEMIES_END, BATTLE_COMBATANTS));
    battle_keep_byte(g, LEFT_SIDE, a->among_party);
    if (a->gathered)
        ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_RULES_OR_ACTOR, a->anchors.actor));
    else if (!a->item)
        ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_RULES_OR_ACTOR, a->aim.area));
    battle_keep_byte(g, LEFT_MODE_OR_FIRST, members ? a->anchors.first_member : a->aim.mode);
    if (members)
        ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_SECOND, a->anchors.second_member));
    if (a->gathered)
        battle_keep_byte(g, LEFT_TARGET, a->anchors.chosen);
    else if (a->rolled)
        battle_keep_byte(g, LEFT_TARGET, a->rolled->left_target);
    if (a->placed) {
        battle_keep_byte(g, LEFT_SIZE, a->rule.size);
        battle_keep_byte(g, LEFT_FLAG, a->rule.flag);
    } else if (a->rolled) {
        battle_keep_byte(g, LEFT_SIZE, a->rolled->left_row);
    }
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

/* The set's one rule's area, placed for the area search, which keeps it in
 * its cells (as the area's side, the original's whole byte: a first member's
 * slot where the placing takes the side from it). */
static AreaAsked place_area(GameState *g, const Aiming *a, const Anchors *at, const AreaRule *r)
{
    set_battle_aim_rule_shape(g, r->shape);
    const AreaPlacing *p = &PLACINGS[r->placing];
    uint8_t side = p->side_is_first_member ? at->first_member : a->among_party;
    AreaAsked asked = { side != 0, anchor(at, p->centre, r->size), anchor(at, p->toward, r->size),
                        (uint8_t)(p->size ? r->size : 0), p->flag && r->flag };
    set_battle_area_search_among_party(g, side);
    set_battle_area_search_centre(g, asked.centre);
    set_battle_area_search_toward(g, asked.toward);
    set_battle_area_search_size(g, asked.size);
    set_battle_area_search_flag(g, p->flag ? r->flag : 0);
    return asked;
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
 * finds where the rule's finds start on the multiplier (the first rule's, at
 * the list's start), whose cells it leaves, and walks them in the product's
 * cell, which it leaves past the last place. */
static void take_finds(GameState *g)
{
    (void)effect_times(g, 0, AREA_FOUND_PLACES);
    uint8_t taken = 0;
    for (unsigned place = 0; place < AREA_FOUND_PLACES; place++) {
        uint8_t slot = battle_area_found(g, place);
        if (in_reserve(g, slot))
            continue;
        set_target(g, place, slot);
        set_battle_candidates(g, place, slot);
        if (slot != NO_TARGET)
            taken++;
    }
    battle_set_wide_math_quotient(g, AREA_FOUND_PLACES);
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

/* ---- The aim ------------------------------------------------------------------------------------ */

/* How far the aim gets before the original's area search: aimed, come to
 * nothing, or with an area to search. */
typedef enum { AIMED, CAME_TO_NOTHING, SEARCH_THE_AREA } AimProgress;

/* Step 3's start: the area's anchors gathered and its rule placed. Every set
 * the ROM has holds one rule (the entry's guard takes no more); a set of
 * none places nothing. */
static void place_the_area(GameState *g, Aiming *a)
{
    a->rules = area_rules_place(g, a->aim.area);
    a->rule_count = area_rule_count(g, a->rules);
    ORIGINAL_EFFECT(set_battle_aim_rules(g, a->rules));
    a->anchors = gather_anchors(g);
    a->gathered = true;
    if (a->rule_count == 0)
        return;
    a->rule = area_rule(g, a->rules, 0);
    a->placed = true;
    a->asked = place_area(g, a, &a->anchors, &a->rule);
}

/* Steps 1 and 2, and the area placed for step 3. The original starts the aim
 * by looking up the action's tech members, whose multiply takes its caller's
 * carry (`carry`), which it leaves in a cell. */
static AimProgress aim_up_to_the_area(GameState *g, Aiming *a, bool carry)
{
    set_battle_action_came_to_nothing(g, 0);
    effect_take_tech_members(g, battle_action_detail(g));
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, carry));
    a->aim = action_aim(g);
    a->row = aim_row(a->aim.mode);
    if (!can_be_made(g, a))
        return CAME_TO_NOTHING;
    /* a roll's way leaves its finish before it rolls; every other leaves
       none until it has taken its targets */
    ORIGINAL_EFFECT(set_battle_aim_finish(g, a->row.take == TAKE_BY_ROLL ? a->row.left : 0));
    bool done = false;
    if (!take_by_mode(g, a, &done))
        return CAME_TO_NOTHING;
    ORIGINAL_EFFECT(set_battle_aim_finish(g, a->row.left));
    if (done)
        return AIMED;
    if (a->row.finish == FINISH_FIRST_IS_CHOSEN)
        set_target(g, 0, chosen_target(g));
    ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_RULE, 0));
    if (a->aim.area == NO_AREA)
        return AIMED;
    place_the_area(g, a);
    return SEARCH_THE_AREA;
}

/* After the area search: the targets settled, or nothing found. A party
 * tech's area that finds nobody is shown as its "no target" showing; an
 * enemy's comes to nothing. */
typedef enum { SETTLED, NOTHING_SETTLED, SHOW_NO_TARGET } AreaOutcome;

static AreaOutcome settle_after_search(GameState *g, const Aiming *a)
{
    leave_rules_placed(g, a->rule_count);
    if (a->row.finish != FINISH_AREA_AND_CHOSEN && area_found_nobody(g))
        return actor_in_party(g) ? SHOW_NO_TARGET : NOTHING_SETTLED;
    return settle_targets(g, a->row.finish) ? SETTLED : NOTHING_SETTLED;
}

/* ---- The entry ---------------------------------------------------------------------------------- */

static void came_to_nothing(GameState *g) { set_battle_action_came_to_nothing(g, 1); }

enum { SHOWING = 0xC1AC57, SHOWING_CALL = 0xC1C6A3 };

// $C1:C741
/* The rule's area searched (area_search.c), the shape noted where the
 * original's step 7 takes it. */
static void search_area(GameState *g, uint8_t shape, const AreaAsked *asked)
{
    set_battle_area_shape(g, shape);
    battle_find_area(g, shape, asked);
}

/* The placed rule's area searched, and its finds taken. The carry the search
 * leaves goes into the original's multiply that finds where its finds
 * start. */
static void search_the_area(GameState *g, const Aiming *a)
{
    leave_rule_reached(g, a->rules, 0, 1);
    TIMED_AS(g, 0xC1C741, search_area(g, a->rule.shape, &a->asked));
    take_finds(g);
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, battle_area_search_carry(a->rule.shape, a->asked.among_party,
                                                                            a->asked.centre, a->asked.toward)));
}

// $C1:C1DD
/* Aim the action. A's high byte goes back clear, as the original's last loads
 * leave it, but after the "no target" showing, which hands back its own. */
void battle_aim_action(Snes *s)
{
    GameState *g = state_of(s);
    Aiming a = { 0 };
    AimProgress progress = aim_up_to_the_area(g, &a, m_flag(s, CPU_FLAG_C));
    leave_aim_cells(g, &a);
    if (progress == SEARCH_THE_AREA) {
        if (a.placed)
            search_the_area(g, &a);
        AreaOutcome outcome = settle_after_search(g, &a);
        if (outcome == SHOW_NO_TARGET) {
            s->cpu.a = record_no_target_showing(g);
            if (!battle_run(s, SHOWING_CALL, SHOWING, battle_start_action_showing))
                return;
            came_to_nothing(g);
            m_rts(s);
            return;
        }
        progress = outcome == SETTLED ? AIMED : CAME_TO_NOTHING;
    }
    if (progress == CAME_TO_NOTHING)
        came_to_nothing(g);
    s->cpu.a &= 0x00FF;
    m_rts(s);
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

/* The member's turn leaves an item's area rules for the original's targeting
 * in a cell: none (battle/member_turn.c). */
enum { ITEM_AREA_CELL = 0x0C };

static bool aim_fits(const GameState *g)
{
    uint8_t actor = battle_actor(g);
    if (actor >= BATTLE_COMBATANTS)
        return false;
    bool party = actor < BATTLE_PARTY_SIZE;
    if (party && battle_cell_word(g, BATTLE_MEMBER_CELL) != battle_member_record_place(g, actor))
        return false;
    bool item = party && member_uses_item(g);
    if (item && battle_cell_word(g, ITEM_AREA_CELL) != NO_AREA)
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

/* The area search for the shape and the area its caller hands it in cells
 * (battle_aim_rule_shape, battle_area_search). The carry the search leaves
 * goes back to the caller, whose multiply after it takes it. */
void battle_search_area(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t shape = battle_aim_rule_shape(g);
    AreaAsked asked = battle_area_asked(g);
    search_area(g, shape, &asked);
    m_set_flag(s, CPU_FLAG_C, battle_area_search_carry(shape, asked.among_party, asked.centre, asked.toward));
    m_rts(s);
}
