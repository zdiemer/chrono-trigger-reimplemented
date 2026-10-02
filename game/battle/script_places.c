/* Battle: the places and facings the animation scripts name, and the
 * commands that set the script's point, an animation slot's place or a
 * mover's heading from them.
 *
 * A script names a point on the battlefield by a place, one of 72: where a
 * channel's subject stands, or the point it is aimed at (its cursor point),
 * halfway between two of them, part of the way from one to another, the
 * middle of the battlefield, where an anchor draws, a point the script keeps
 * in its variables. The moves (battle/script_moves.c, script_walk.c) send
 * their movers to one, and the commands here set the script's point, an
 * animation slot's place and a mover's heading by them. The battle keeps the
 * point last found (one of the places is "the place last found"), and a
 * facing likewise: a facing code names which way something faces, the way a
 * subject faces, toward a subject, or a fixed way.
 *
 * A heading is the angle a mover's spiral, chase or orbit sets out at (256
 * steps a turn); the commands that set it act on what the channel moves
 * (battle/script_language.h): its combatant, the other targets, or its
 * effect's anchor.
 *
 * The original works out a place in the battle's first work cells; what it
 * leaves there is scaffolding, each kind's in its own leaves helper. */
#include "battle/script_language.h"

#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/effects.h"
#include "battle/positions.h"
#include "battle/script_data.h"
#include "battle/sprite_data.h"
#include "battle/window.h"
#include "original_effects.h"
#include "vars.h"

/* ---- Where a combatant stands, and where it is aimed at -------------------------------- */

static BattlePoint standing(const GameState *g, uint8_t slot) { return (BattlePoint){ combatant_x(g, slot), combatant_y(g, slot) }; }

/* The point a combatant is aimed at: its cursor point, kept on the screen
 * from top to bottom (across, it wraps as a byte). */
static uint8_t kept_on_screen(uint8_t y, int8_t offset)
{
    int v = y + offset;
    return (uint8_t)(v < 0 ? 0 : v > 0xFF ? 0xFF : v);
}

static BattlePoint aimed_at(const GameState *g, uint8_t slot)
{
    return (BattlePoint){ (uint8_t)(combatant_x(g, slot) + combatant_cursor_offset_x(g, slot)),
                          kept_on_screen(combatant_y(g, slot), (int8_t)combatant_cursor_offset_y(g, slot)) };
}

/* A channel's subject, as a place names it by the channel. */
static uint8_t subject(const GameState *g, unsigned channel)
{
    return channel < BATTLE_SCRIPT_SUBJECTS_COUNT ? battle_script_subjects(g, channel) : battle_script_subjects_unchecked(g, channel);
}

/* An anchor's place as a point: its coordinates' low bytes. */
static BattlePoint anchor_point(const GameState *g, unsigned anchor)
{
    return (BattlePoint){ (uint8_t)anchor_x(g, anchor), (uint8_t)anchor_y(g, anchor) };
}

/* ---- The place last found --------------------------------------------------------------- */

static void found_at(GameState *g, BattlePoint p)
{
    set_battle_position_found_x(g, p.x);
    set_battle_position_found_y(g, p.y);
}

/* An anchor's places are words: an anchor can stand off the battlefield. */
static void found_at_words(GameState *g, uint16_t x, uint16_t y)
{
    set_battle_position_found_x(g, (uint8_t)x);
    set_battle_position_found_x_high(g, (uint8_t)(x >> 8));
    set_battle_position_found_y(g, (uint8_t)y);
    set_battle_position_found_y_high(g, (uint8_t)(y >> 8));
}

static BattlePoint last_found(const GameState *g)
{
    return (BattlePoint){ battle_position_found_x(g), battle_position_found_y(g) };
}

/* ---- The kinds of place ------------------------------------------------------------------ */

/* The places, by their first number. */
enum {
    WHERE_A_SUBJECT_STANDS = 0x00,   /* 0-8: where channel n's subject stands */
    WHERE_A_SUBJECT_IS_AIMED = 0x09, /* 9-$11: where channel n's subject is aimed at */
    THE_PLACE_LAST_FOUND = 0x12,
    HALFWAY_FROM_THE_FIRST = 0x13,   /* $13-$15: halfway between subject 0 and subject 3, 1 or 2 */
    AMID_THE_FIRST_THREE = 0x16,
    OUT_ALONG_THE_HEADING = 0x17,    /* 24 pixels out from the running channel's mover, along its angle */
    THE_MIDDLE = 0x18,
    WHERE_THE_FIRST_THREE_BEGAN = 0x19, /* $19-$1B */
    WHERE_AN_ANCHOR_DRAWS = 0x1C,    /* $1C-$23 */
    PART_WAY_FROM_THE_FIRST = 0x24,  /* $24-$29: part way from subject 0 to subject 3 */
    A_POINT_IN_THE_VARIABLES = 0x2A, /* $2A-$31: the point in variables 2n and 2n+1 */
    PART_WAY_FROM_THE_SECOND = 0x32, /* $32-$38: part way from subject 1 to subject 3 */
    PART_WAY_BETWEEN_AIMS = 0x39,    /* $39-$3F: part way between subject 0's aim and subject 3's */
    HALFWAY_ON_FROM_THE_SECOND = 0x40, /* halfway between subjects 1 and 2 */
    PART_WAY_TO_THE_ANCHOR = 0x41,   /* $41-$47: part way from subject 0 to anchor 0 */
};

enum { THE_FOURTH = 3, THE_ANCHOR = 0, MIDDLE = 0x80, OUT_ALONG = 24, QUARTER_TURN = 0x40, EIGHTHS = 3 };

/* What working out a place leaves in the work cells: the two ends' X and Y
 * as words (a halfway point's own), the three ends of the center, the
 * along-the-heading's subject and steps, and the blend's weights and ends. */
enum { LEFT_FIRST = 0x00, LEFT_SECOND = 0x02, LEFT_THIRD = 0x04, LEFT_FOURTH = 0x06, LEFT_FIFTH = 0x08, LEFT_SIXTH = 0x0A };

static void leave_halfway(GameState *g, uint16_t x, uint16_t y, BattlePoint b)
{
    script_leave_word(g, LEFT_FIRST, x);
    script_leave_word(g, LEFT_SECOND, y);
    script_leave_word(g, LEFT_THIRD, b.x);
    script_leave_word(g, LEFT_FOURTH, b.y);
}

static void leave_center(GameState *g, uint16_t x, uint16_t y, BattlePoint b, BattlePoint c)
{
    leave_halfway(g, x, y, b);
    script_leave_word(g, LEFT_FIFTH, c.x);
    script_leave_word(g, LEFT_SIXTH, c.y);
}

static void leave_blend(GameState *g, PlaceWeights w, uint16_t y, uint8_t first_y, uint8_t second_y)
{
    script_leave(g, LEFT_FIRST, w.first);
    script_leave(g, LEFT_FIRST + 1, w.second);
    script_leave_word(g, LEFT_SECOND, y);
    script_leave(g, LEFT_THIRD, first_y);
    script_leave(g, LEFT_THIRD + 1, second_y);
}

/* The radius goes to the sine's multiply in its operand cell. */
static void leave_along(GameState *g, uint8_t mover, int8_t dy, int8_t dx, bool words)
{
    script_leave_word(g, LEFT_FIRST, mover);
    script_leave(g, LEFT_SECOND, (uint8_t)dy);
    if (words) {
        script_leave(g, LEFT_SECOND + 1, 0);
        script_leave_word(g, LEFT_THIRD, (uint8_t)dx);
    } else {
        script_leave(g, LEFT_SECOND + 1, (uint8_t)dx);
    }
    set_battle_multiply_factors(g, 1, OUT_ALONG);
}

/* Halfway between two points, in words (no carry lost). */
static BattlePoint halfway(GameState *g, BattlePoint a, BattlePoint b)
{
    uint16_t x = (uint16_t)((a.x + b.x) >> 1), y = (uint16_t)((a.y + b.y) >> 1);
    ORIGINAL_EFFECT(leave_halfway(g, x, y, b));
    return (BattlePoint){ (uint8_t)x, (uint8_t)y };
}

/* The middle of the first three subjects, by their sums over three. The
 * original divides the sum of their X twice, so the middle's Y is its X
 * too; the game sees that, so it is kept. */
static BattlePoint amid(GameState *g, BattlePoint a, BattlePoint b, BattlePoint c)
{
    uint16_t xs = (uint16_t)(a.x + b.x + c.x), ys = (uint16_t)(a.y + b.y + c.y);
    ORIGINAL_EFFECT(leave_center(g, xs, ys, b, c));
    uint8_t x = (uint8_t)battle_divide_by(g, xs, 3).quotient;
    uint8_t y = (uint8_t)battle_divide_by(g, xs, 3).quotient;
    return (BattlePoint){ x, y };
}

/* Part of the way from one point to another, in eighths. */
static uint16_t weighed(GameState *g, uint8_t a, uint8_t b, PlaceWeights w)
{
    uint16_t sum = battle_multiply(g, a, w.first);
    return (uint16_t)((uint16_t)(sum + battle_multiply(g, b, w.second)) >> EIGHTHS);
}

static BattlePoint part_way(GameState *g, BattlePoint a, BattlePoint b, uint8_t weights)
{
    PlaceWeights w = battle_place_weights(g, weights);
    uint16_t x = weighed(g, a.x, b.x, w), y = weighed(g, a.y, b.y, w);
    ORIGINAL_EFFECT(leave_blend(g, w, y, a.y, b.y));
    return (BattlePoint){ (uint8_t)x, (uint8_t)y };
}

/* 24 pixels out from the running channel's mover along its angle: a
 * combatant's (the channel's subject, the first other target for the other
 * targets' channel), or the channel's anchor's, whose place is a word, the
 * steps added to it as bytes. */
static void out_along_the_heading(GameState *g)
{
    uint8_t channel = (uint8_t)battle_script_channel(g);
    if (channel < FIRST_EFFECT_CHANNEL) {
        uint8_t slot = subject(g, channel);
        uint8_t angle = slot < BATTLE_EFFECT_ANGLE_COUNT ? battle_effect_angle(g, slot) : battle_effect_angle_unchecked(g, slot);
        int8_t dy = battle_along(g, angle, OUT_ALONG), dx = battle_along(g, (uint8_t)(angle + QUARTER_TURN), OUT_ALONG);
        ORIGINAL_EFFECT(leave_along(g, slot, dy, dx, false));
        found_at(g, (BattlePoint){ (uint8_t)(combatant_x(g, slot) + dx), (uint8_t)(combatant_y(g, slot) + dy) });
        return;
    }
    uint8_t anchor = (uint8_t)(channel - FIRST_EFFECT_CHANNEL);
    uint8_t angle = battle_effect_angle(g, FIRST_ANCHOR_MOVER + anchor);
    int8_t dy = battle_along(g, angle, OUT_ALONG), dx = battle_along(g, (uint8_t)(angle + QUARTER_TURN), OUT_ALONG);
    ORIGINAL_EFFECT(leave_along(g, anchor, dy, dx, true));
    found_at_words(g, (uint16_t)(anchor_x(g, anchor) + (uint8_t)dx), (uint16_t)(anchor_y(g, anchor) + (uint8_t)dy));
}

/* Where an anchor's effect is drawn: the anchor and its drawing offset. */
static void where_the_anchor_draws(GameState *g, uint8_t anchor)
{
    found_at_words(g, (uint16_t)(battle_anchor_offset_x(g, anchor) + battle_anchor_x(g, anchor)),
                   (uint16_t)(battle_anchor_offset_y(g, anchor) + battle_anchor_y(g, anchor)));
}

static BattlePoint a_point_in_the_variables(const GameState *g, uint8_t n)
{
    return (BattlePoint){ battle_script_variables(g, 2 * n), battle_script_variables(g, 2 * n + 1) };
}

// $C1:762E
BattlePoint script_find_place(GameState *g, uint8_t place)
{
    if (place < WHERE_A_SUBJECT_IS_AIMED)
        found_at(g, standing(g, subject(g, place)));
    else if (place < THE_PLACE_LAST_FOUND)
        found_at(g, aimed_at(g, subject(g, place - WHERE_A_SUBJECT_IS_AIMED)));
    else if (place == THE_PLACE_LAST_FOUND)
        ;
    else if (place < AMID_THE_FIRST_THREE) {
        static const uint8_t other_end[] = { 3, 1, 2 };
        found_at(g, halfway(g, standing(g, subject(g, 0)), standing(g, subject(g, other_end[place - HALFWAY_FROM_THE_FIRST]))));
    } else if (place == AMID_THE_FIRST_THREE)
        found_at(g, amid(g, standing(g, subject(g, 0)), standing(g, subject(g, 1)), standing(g, subject(g, 2))));
    else if (place == OUT_ALONG_THE_HEADING)
        out_along_the_heading(g);
    else if (place == THE_MIDDLE)
        found_at(g, (BattlePoint){ MIDDLE, MIDDLE });
    else if (place < WHERE_AN_ANCHOR_DRAWS) {
        uint8_t n = (uint8_t)(place - WHERE_THE_FIRST_THREE_BEGAN);
        found_at(g, (BattlePoint){ battle_script_start_x(g, n), battle_script_start_y(g, n) });
    } else if (place < PART_WAY_FROM_THE_FIRST)
        where_the_anchor_draws(g, (uint8_t)(place - WHERE_AN_ANCHOR_DRAWS));
    else if (place < A_POINT_IN_THE_VARIABLES)
        found_at(g, part_way(g, standing(g, subject(g, 0)), standing(g, subject(g, THE_FOURTH)), (uint8_t)(place - PART_WAY_FROM_THE_FIRST)));
    else if (place < PART_WAY_FROM_THE_SECOND)
        found_at(g, a_point_in_the_variables(g, (uint8_t)(place - A_POINT_IN_THE_VARIABLES)));
    else if (place < PART_WAY_BETWEEN_AIMS)
        found_at(g, part_way(g, standing(g, subject(g, 1)), standing(g, subject(g, THE_FOURTH)), (uint8_t)(place - PART_WAY_FROM_THE_SECOND)));
    else if (place < HALFWAY_ON_FROM_THE_SECOND)
        found_at(g, part_way(g, aimed_at(g, subject(g, 0)), aimed_at(g, subject(g, THE_FOURTH)), (uint8_t)(place - PART_WAY_BETWEEN_AIMS)));
    else if (place == HALFWAY_ON_FROM_THE_SECOND)
        found_at(g, halfway(g, standing(g, subject(g, 1)), standing(g, subject(g, 2))));
    else
        found_at(g, part_way(g, standing(g, subject(g, 0)), anchor_point(g, THE_ANCHOR), (uint8_t)(place - PART_WAY_TO_THE_ANCHOR)));
    return last_found(g);
}

bool script_place_known(const GameState *g, uint8_t place)
{
    return place <= LAST_PLACE && (place != OUT_ALONG_THE_HEADING || (uint8_t)battle_script_channel(g) < SCRIPT_CHANNELS);
}

bool script_place_works_in_cells(uint8_t place)
{
    return (place >= HALFWAY_FROM_THE_FIRST && place <= OUT_ALONG_THE_HEADING)
           || (place >= PART_WAY_FROM_THE_FIRST && place < A_POINT_IN_THE_VARIABLES) || place >= PART_WAY_FROM_THE_SECOND;
}

BattlePoint script_point(const GameState *g) { return (BattlePoint){ battle_script_point_x(g), battle_script_point_y(g) }; }

static void set_script_point(GameState *g, BattlePoint p)
{
    set_battle_script_point_x(g, p.x);
    set_battle_script_point_y(g, p.y);
}

BattlePoint script_aim(GameState *g, ScriptAim aim, uint8_t x, uint8_t y, uint8_t place)
{
    switch (aim) {
    case AIM_AT_COORDINATES: found_at(g, (BattlePoint){ x, y }); break;
    case AIM_AT_THE_POINT: found_at(g, script_point(g)); break;
    default: script_find_place(g, place); break;
    }
    return last_found(g);
}

/* ---- Facings ----------------------------------------------------------------------------- */

/* The facing codes, by their first number. */
enum {
    THE_WAY_A_SUBJECT_FACES = 0x00, /* 0-9: the way channel n's subject faces */
    TOWARD_A_SUBJECT = 0x0A,        /* $A-$13: toward channel n's subject */
    A_FIXED_WAY = 0x14,             /* $14-$17: facing 0-3 */
    TOWARD_THE_MIDDLE = 0x18,       /* toward the middle of the battlefield, a little up */
};

enum { MIDDLE_ROW = 0x70 };

static uint8_t subject_facing(const GameState *g, uint8_t slot)
{
    return slot < BATTLE_OBJECT_FACING_COUNT ? battle_object_facing(g, slot) : battle_object_facing_unchecked(g, slot);
}

static uint8_t facing_toward(GameState *g, BattlePoint from, BattlePoint to)
{
    return battle_facing_for_angle(g, battle_direction(g, from, to));
}

// $C1:75CC
uint8_t script_find_facing(GameState *g, uint8_t facing, BattlePoint from)
{
    uint8_t found;
    if (facing < TOWARD_A_SUBJECT)
        found = subject_facing(g, subject(g, facing));
    else if (facing < A_FIXED_WAY)
        found = facing_toward(g, from, standing(g, subject(g, facing - TOWARD_A_SUBJECT)));
    else if (facing < TOWARD_THE_MIDDLE)
        found = (uint8_t)(facing - A_FIXED_WAY);
    else
        found = facing_toward(g, from, (BattlePoint){ MIDDLE, MIDDLE_ROW });
    set_battle_facing_found(g, found);
    return found;
}

/* ---- Setting the point and the animation slots' places ------------------------------------ */

// $C1:591A
/* The script's point is put at a place. */
static ScriptFlow point_at_place(GameState *g, uint8_t place)
{
    set_script_point(g, script_find_place(g, place));
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(point_at_place, place)

// $C1:5A0C
/* The script's point is put a given way off a place. The original keeps the
 * place in the first work cell while it finds it. */
enum { LEFT_PLACE = 0x00 };

static ScriptFlow point_off_place(GameState *g, uint8_t place, uint8_t x, uint8_t y)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_PLACE, place));
    BattlePoint p = script_find_place(g, place);
    set_script_point(g, (BattlePoint){ (uint8_t)(p.x + x), (uint8_t)(p.y + y) });
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(point_off_place, place, x, y)

// $C1:5A3E
/* The script's point is put where what the channel moves stands: its
 * combatant (the first of the other targets, on theirs), or its effect's
 * anchor. */
enum { LEFT_SUBJECT = 0x02 };

static ScriptFlow point_at_the_mover(GameState *g, ScriptChannel channel)
{
    if (channel.kind == SCRIPT_EFFECT) {
        set_script_point(g, anchor_point(g, script_effect(g)));
    } else {
        uint8_t slot = subject(g, channel.channel);
        ORIGINAL_EFFECT(script_leave_word(g, LEFT_SUBJECT, slot));
        set_script_point(g, standing(g, slot));
    }
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(point_at_the_mover, channel)

// $C1:59B9
/* An animation slot's place is set, found now. */
static ScriptFlow slot_at_place(GameState *g, uint8_t slot, uint8_t place)
{
    BattlePoint p = script_find_place(g, place);
    set_battle_animation_positions(g, slot, (uint16_t)(p.x | p.y << 8));
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(slot_at_place, slot, place)

// $C1:623C
/* An animation slot follows a place from now on: its place is found again
 * each frame (battle/objects.c). */
static ScriptFlow slot_follows_place(GameState *g, uint8_t slot, uint8_t followed)
{
    set_battle_animation_position_modes(g, slot, (uint8_t)(followed + 1));
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(slot_follows_place, slot, followed)

/* ---- Facing ----------------------------------------------------------------------------- */

/* The original keeps where the turning combatant stands and which it is in
 * its work cells, and the facing code. */
enum { LEFT_STANDING = 0x00, LEFT_TURNING = 0x02, LEFT_FACING = 0x0E };

static void leave_turned(GameState *g, BattlePoint at, bool a_combatant, uint8_t slot, uint8_t facing)
{
    script_leave(g, LEFT_STANDING, at.x);
    script_leave(g, LEFT_STANDING + 1, at.y);
    if (a_combatant)
        script_leave_word(g, LEFT_TURNING, slot);
    script_leave(g, LEFT_FACING, facing);
}

// $C1:5FDE
/* What the channel moves turns the way a facing code names: its combatant,
 * each of the other targets, or its effect's anchor. (The channel has
 * someone to turn: a row's needs see to that.) */
static ScriptFlow face(GameState *g, ScriptChannel channel, uint8_t facing)
{
    if (channel.kind == SCRIPT_EFFECT) {
        uint8_t anchor = script_effect(g);
        BattlePoint at = anchor_point(g, anchor);
        set_battle_anchor_facing(g, anchor, script_find_facing(g, facing, at));
        ORIGINAL_EFFECT(leave_turned(g, at, false, anchor, facing));
        return SCRIPT_NEXT_FRAME;
    }
    BattlePoint at = { 0, 0 };
    uint8_t slot, last = 0;
    for (unsigned k = 0; script_each_combatant(g, channel, k, &slot); k++) {
        at = standing(g, slot);
        set_battle_object_facing(g, slot, script_find_facing(g, facing, at));
        last = slot;
    }
    ORIGINAL_EFFECT(leave_turned(g, at, true, last, facing));
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(face, channel, facing)

/* ---- Headings --------------------------------------------------------------------------- */

/* A mover's heading: the angle its spiral last went out at, which a chase,
 * a spiral or an orbit can set out at. The scripts name a combatant by its
 * channel, 0-9, and an anchor by its number after those; the original
 * takes the entry they reach unchecked. */
enum { FIRST_NAMED_ANCHOR = 10 };

static uint8_t heading_at(const GameState *g, unsigned entry)
{
    return entry < BATTLE_EFFECT_ANGLE_USED_COUNT ? battle_effect_angle_used(g, entry) : battle_effect_angle_used_unchecked(g, entry);
}

static uint8_t named_heading(const GameState *g, uint8_t named)
{
    if (named < FIRST_NAMED_ANCHOR)
        return heading_at(g, subject(g, named));
    return heading_at(g, FIRST_ANCHOR_MOVER + (unsigned)(named - FIRST_NAMED_ANCHOR));
}

/* The movers a heading command turns: the channel's combatant, its effect's
 * anchor, or the other targets listed from `first` (the commands that take a
 * heading from somewhere list them from the second subject on, as the
 * original does; setting one outright from the list's own start). */
enum { HEADINGS_LIST_FROM = 1 };

static void turn_movers(GameState *g, ScriptChannel channel, unsigned first, uint8_t heading, bool by)
{
    if (channel.kind == SCRIPT_EFFECT) {
        unsigned entry = FIRST_ANCHOR_MOVER + script_effect(g);
        set_battle_effect_angle_used(g, entry, by ? (uint8_t)(battle_effect_angle_used(g, entry) + heading) : heading);
        return;
    }
    uint8_t slot;
    for (unsigned k = 0; script_combatant_from(g, channel, first, k, &slot); k++)
        set_battle_effect_angle_used(g, slot, by ? (uint8_t)(battle_effect_angle_used(g, slot) + heading) : heading);
}

/* The original keeps the heading (or the turn) in the first work cell. */
enum { LEFT_HEADING = 0x00 };

// $C1:6093
/* The movers take another's heading. */
static ScriptFlow take_heading(GameState *g, ScriptChannel channel, uint8_t named)
{
    uint8_t heading = named_heading(g, named);
    ORIGINAL_EFFECT(script_leave(g, LEFT_HEADING, heading));
    turn_movers(g, channel, HEADINGS_LIST_FROM, heading, false);
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(take_heading, channel, named)

// $C1:60E6
/* The movers head from one place toward another (the place toward is found
 * first, and kept while the other is). */
static void keep_heading_ends(GameState *g, uint8_t place, BattlePoint to)
{
    set_battle_heading_from_place(g, place);
    set_battle_heading_to_x(g, to.x);
    set_battle_heading_to_y(g, to.y);
}

static ScriptFlow heading_between(GameState *g, ScriptChannel channel, uint8_t place, uint8_t toward)
{
    BattlePoint to = script_find_place(g, toward);
    ORIGINAL_EFFECT(keep_heading_ends(g, place, to));
    BattlePoint from = script_find_place(g, place);
    turn_movers(g, channel, HEADINGS_LIST_FROM, battle_direction(g, from, to), false);
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(heading_between, channel, place, toward)

// $C1:6154
/* The movers' headings turn by an amount. */
static ScriptFlow turn_heading(GameState *g, ScriptChannel channel, uint8_t amount)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_HEADING, amount));
    turn_movers(g, channel, HEADINGS_LIST_FROM, amount, true);
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(turn_heading, channel, amount)

// $C1:6258
/* The movers head a given way. */
static ScriptFlow set_heading(GameState *g, ScriptChannel channel, uint8_t heading)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_HEADING, heading));
    turn_movers(g, channel, OTHER_TARGETS_START, heading, false);
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(set_heading, channel, heading)

/* ---- The table ------------------------------------------------------------------------- */

#define SLOT_AT(b, k) { b, OPERATION(slot_at_place), .slot = CONSTANT(k), .place = OPERAND(1), .length = 2 }
#define SLOT_FOLLOWS(b, k) { b, OPERATION(slot_follows_place), .slot = CONSTANT(k), .followed = OPERAND(1), .length = 2 }

SCRIPT_OPCODES(script_place_opcodes,
    { 0x33, OPERATION(point_at_place), .place = OPERAND(1), .length = 2 },
    { 0x43, OPERATION(point_off_place), .place = OPERAND(1), .x = OPERAND(2), .y = OPERAND(3), .length = 4 },
    { 0x44, OPERATION(point_at_the_mover), .length = 1, .needs = NEEDS_SUBJECTS },
    SLOT_AT(0x3D, 0), SLOT_AT(0x3E, 1), SLOT_AT(0x3F, 2), SLOT_AT(0x40, 3), SLOT_AT(0x45, 4), SLOT_AT(0x46, 5),
    SLOT_FOLLOWS(0x81, 0), SLOT_FOLLOWS(0x82, 1), SLOT_FOLLOWS(0x83, 2), SLOT_FOLLOWS(0x84, 3),
    SLOT_FOLLOWS(0x47, 4), SLOT_FOLLOWS(0x48, 5),
    { 0x72, OPERATION(face), .facing = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0x75, OPERATION(take_heading), .named = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0x76, OPERATION(heading_between), .place = OPERAND(1), .toward = OPERAND(2), .length = 3, .needs = NEEDS_SUBJECTS },
    { 0x77, OPERATION(turn_heading), .amount = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0x85, OPERATION(set_heading), .heading = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS });
