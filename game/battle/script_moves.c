/* Battle: the animation scripts' moves -- the commands that send a combatant
 * or an effect's anchor along a path: a straight line, a chase, a jump, a
 * spiral out, an orbit, a throw's rise and fall, a flicker; putting one
 * somewhere at once; and a point some way along the line to a place.
 *
 * A command acts on what its channel moves (battle/script_language.h): its
 * combatant, each of the other targets, or its effect's anchor. Starting a
 * move sets up the mover's path (battle/effect_paths.c runs it, a step each
 * time its timer comes round) and marks the mover moving; the command then
 * stays on itself until the path has ended, when it stops the mover and the
 * channel goes on. Where the move is aimed is the command's coordinates, the
 * script's point or a place (battle/script_places.c).
 *
 * A mover's speed, set by the pose commands (battle/script_poses.c), is how
 * far its line goes a step, how fast its chase turns and goes, or how much
 * of a turn its orbit takes a step.
 *
 * The other targets' forms of the commands are the original's, quirks and
 * all: some start every target and go on at once, some wait for the targets
 * in ways that differ from command to command, and the jumps and throws do
 * nothing on that channel. Each is said where it is.
 *
 * The original works in the battle's first work cells as it goes; what it
 * leaves there is scaffolding, written by the leaves helpers. */
#include "battle/script_language.h"

#include "battle/animation.h"
#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/effects.h"
#include "battle/positions.h"
#include "battle/sprite_data.h"
#include "original_effects.h"
#include "vars.h"

/* ---- A mover's tables ---------------------------------------------------------------------- */

static Mover channel_mover(const GameState *g, ScriptChannel ch)
{
    return ch.kind == SCRIPT_EFFECT ? anchor_mover(script_effect(g)) : combatant_mover(script_combatant(g, ch));
}

static bool moving(const GameState *g, Mover m) { return battle_effect_active(g, mover_entry(m)) != 0; }
static void set_moving(GameState *g, Mover m, bool on) { set_battle_effect_active(g, mover_entry(m), on); }

static uint8_t speed_of(const GameState *g, Mover m) { return battle_effect_steps(g, mover_entry(m)); }
static uint8_t heading_of(const GameState *g, Mover m) { return battle_effect_angle_used(g, mover_entry(m)); }

static void set_base(GameState *g, Mover m, BattlePoint p)
{
    set_battle_effect_base_x(g, mover_entry(m), p.x);
    set_battle_effect_base_y(g, mover_entry(m), p.y);
}

static void set_target(GameState *g, Mover m, BattlePoint p)
{
    set_battle_effect_target_x(g, mover_entry(m), p.x);
    set_battle_effect_target_y(g, mover_entry(m), p.y);
}

/* The mover faces along an angle: a combatant's sprite, or the anchor's
 * effect. */
static void face_along(GameState *g, Mover m, uint8_t angle)
{
    uint8_t facing = battle_facing_for_angle(g, angle);
    if (mover_is_anchor(m))
        set_battle_anchor_facing(g, m.index, facing);
    else
        set_battle_object_facing(g, m.index, facing);
}

/* A new path: its first step comes next frame, and it hasn't ended. */
static void begin_path(GameState *g, Mover m, PathKind kind)
{
    set_battle_effect_delay(g, mover_entry(m), 1);
    set_battle_effect_type(g, mover_entry(m), (uint8_t)kind);
    set_mover_done_count(g, m, 0);
}

static void at_the_start_of_the_line(GameState *g, Mover m, uint16_t vx, uint16_t vy)
{
    unsigned e = mover_entry(m);
    set_battle_effect_velocity_x(g, e, vx);
    set_battle_effect_velocity_y(g, e, vy);
    set_battle_effect_position_x(g, e, 0);
    set_battle_effect_position_y(g, e, 0);
}

/* ---- The line's velocity -------------------------------------------------------------------- */

/* A line's velocity: a whole pixel a step (8.8) along its longer axis, and
 * the share of that along the shorter one. Every line's set-up leaves its
 * length, in pixels along the longer axis, where a throw's rise takes it
 * from (battle_throw_wait_after's low byte). */
typedef struct {
    uint16_t vx, vy;
    uint8_t across, down, length;
    bool tall;
} LineVelocity;

enum { WHOLE_PIXEL = 0x100 };

static uint8_t apart(uint8_t a, uint8_t b) { return a < b ? (uint8_t)(b - a) : (uint8_t)(a - b); }

/* A line runs between two points, which the battle keeps where a direction
 * is asked of them (battle/positions.c): the next direction can be asked of
 * the same points. */
enum { DIFFERENCES = 4 };

static void keep_the_points(GameState *g, BattlePoint from, BattlePoint to)
{
    set_battle_direction_cells(g, 0, from.x);
    set_battle_direction_cells(g, 1, from.y);
    set_battle_direction_cells(g, 2, to.x);
    set_battle_direction_cells(g, 3, to.y);
}

/* What working out a line leaves: the differences and their signs, the
 * sizes; which ways it goes, half its length and which axis is the
 * longer. */
enum { LEFT_LEFTWARD = 0x02, LEFT_UPWARD = 0x03, LEFT_HALF_LENGTH = 0x04, LEFT_TALL = 0x0C };

enum { ACROSS_SIGN = 5, DOWN_DIFFERENCE = 6, DOWN_SIGN = 7, ACROSS = 11, DOWN = 13 };

static void leave_line_cells(GameState *g, BattlePoint from, BattlePoint to, uint8_t across, uint8_t down)
{
    set_battle_direction_cells(g, DIFFERENCES, (uint8_t)(to.x - from.x));
    set_battle_direction_cells(g, ACROSS_SIGN, to.x < from.x ? 0xFF : 0x00);
    set_battle_direction_cells(g, DOWN_DIFFERENCE, (uint8_t)(to.y - from.y));
    set_battle_direction_cells(g, DOWN_SIGN, to.y < from.y ? 0xFF : 0x00);
    set_battle_direction_cells(g, ACROSS, across);
    set_battle_direction_cells(g, ACROSS + 1, 0);
    set_battle_direction_cells(g, DOWN, down);
    set_battle_direction_cells(g, DOWN + 1, 0);
}

static void leave_line(GameState *g, BattlePoint from, BattlePoint to, LineVelocity v)
{
    leave_line_cells(g, from, to, v.across, v.down);
    script_leave(g, LEFT_LEFTWARD, to.x < from.x);
    script_leave(g, LEFT_UPWARD, to.y < from.y);
    script_leave_word(g, LEFT_HALF_LENGTH, (uint8_t)(v.length >> 1));
    script_leave_word(g, LEFT_TALL, v.tall);
}

// $C1:65BA
static LineVelocity line_velocity(GameState *g, BattlePoint from, BattlePoint to)
{
    LineVelocity v = { 0, 0, apart(from.x, to.x), apart(from.y, to.y), 0, false };
    keep_the_points(g, from, to);
    v.tall = v.across < v.down;
    v.length = v.tall ? v.down : v.across;
    uint8_t shorter = v.tall ? v.across : v.down;
    set_battle_throw_wait_after(g, (uint16_t)((battle_throw_wait_after(g) & 0xFF00) | v.length));
    uint16_t share = battle_divide_by(g, (uint16_t)(shorter << 8), v.length).quotient;
    v.vx = v.tall ? share : (uint16_t)WHOLE_PIXEL;
    v.vy = v.tall ? (uint16_t)WHOLE_PIXEL : share;
    if (to.x < from.x)
        v.vx = (uint16_t)-v.vx;
    if (to.y < from.y)
        v.vy = (uint16_t)-v.vy;
    ORIGINAL_EFFECT(leave_line(g, from, to, v));
    return v;
}

/* A velocity times the mover's speed: the steps it takes each step. */
static uint16_t at_speed(GameState *g, uint16_t v, uint8_t speed) { return (uint16_t)battle_multiply_long(g, speed, v); }

/* The original works the velocity out in its own cells, and multiplies it
 * by the speed there. */
static void leave_velocity(GameState *g, uint16_t vx, uint16_t vy)
{
    set_battle_line_velocity_x(g, vx);
    set_battle_line_velocity_y(g, vy);
}

/* ---- Lines ----------------------------------------------------------------------------------- */

/* The original keeps the mover it starts in a work cell. */
enum { LEFT_LINE_MOVER = 0x06 };

// $C1:5135, $C1:5042
/* Set the mover off in a straight line to a point, from where it is, at its
 * speed, facing the way it goes. (An anchor's line keeps its angle for a
 * spiral or a chase after.) */
static void start_line(GameState *g, Mover m, BattlePoint to)
{
    unsigned e = mover_entry(m);
    BattlePoint from = mover_position(g, m);
    set_base(g, m, from);
    uint8_t angle = battle_direction(g, from, to);
    LineVelocity v = line_velocity(g, from, to);
    uint8_t speed = speed_of(g, m);
    set_battle_effect_line_frames(g, e, (uint8_t)battle_divide_by(g, v.length, speed).quotient);
    uint16_t vx = at_speed(g, v.vx, speed), vy = at_speed(g, v.vy, speed);
    if (mover_is_anchor(m)) {
        set_battle_effect_angle(g, e, angle);
        set_battle_effect_angle_used(g, e, angle);
        set_battle_effect_radius(g, e, 0);
    } else {
        ORIGINAL_EFFECT(script_leave_word(g, LEFT_LINE_MOVER, m.index));
    }
    face_along(g, m, angle);
    set_target(g, m, to);
    begin_path(g, m, PATH_LINE);
    at_the_start_of_the_line(g, m, vx, vy);
    ORIGINAL_EFFECT(leave_velocity(g, vx, vy));
}

/* A channel's move: the length of the command that started it, which the
 * channel goes past once the move ends, whichever command it is at then. */
static void keep_move_length(GameState *g, ScriptChannel ch, uint8_t length) { set_battle_script_move_lengths(g, ch.channel, length); }
static ScriptFlow past_the_move(GameState *g, ScriptChannel ch)
{
    script_go_past(g, battle_script_move_lengths(g, ch.channel));
    return SCRIPT_MOVED_NEXT_FRAME;
}

/* One mover's move: started the first time, then waited for; once its path
 * has ended the mover stops. */
typedef enum { MOVE_STARTS, MOVE_GOES_ON, MOVE_ENDED } MoveStage;

/* Whether the mover's path has ended, stopping it if it has. */
static bool stopped_at_the_end(GameState *g, Mover m)
{
    if (!mover_path_done(g, m))
        return false;
    set_moving(g, m, false);
    return true;
}

static MoveStage stage_of(GameState *g, Mover m)
{
    if (!moving(g, m))
        return MOVE_STARTS;
    return stopped_at_the_end(g, m) ? MOVE_ENDED : MOVE_GOES_ON;
}

/* The original keeps the command's length in a work cell, and on the other
 * targets' channel the place in their list it has come to. */
enum { LEFT_LENGTH = 0x0E, LEFT_LISTED = 0x00 };

// $C1:4FBF, $C1:4FD1, $C1:4FE3
/* What the channel moves goes in a straight line to where the command aims.
 * The other targets all set off and the channel goes on at once. */
static ScriptFlow line(GameState *g, ScriptChannel ch, uint8_t length, ScriptAim aim, uint8_t x, uint8_t y, uint8_t place)
{
    BattlePoint to = script_aim(g, aim, x, y, place);
    ORIGINAL_EFFECT(script_leave(g, LEFT_LENGTH, length));
    if (ch.kind == SCRIPT_OTHER_TARGETS) {
        uint8_t slot;
        unsigned k = 0;
        for (; script_each_combatant(g, ch, k, &slot); k++) {
            start_line(g, combatant_mover(slot), to);
            set_moving(g, combatant_mover(slot), true);
        }
        ORIGINAL_EFFECT(script_leave_word(g, LEFT_LISTED, (uint8_t)k));
        keep_move_length(g, ch, length);
        return past_the_move(g, ch);
    }
    Mover m = channel_mover(g, ch);
    switch (stage_of(g, m)) {
    case MOVE_STARTS:
        start_line(g, m, to);
        set_moving(g, m, true);
        keep_move_length(g, ch, length);
        return SCRIPT_STAY;
    case MOVE_ENDED: return past_the_move(g, ch);
    default: return SCRIPT_STAY;
    }
}
SCRIPT_OPERATION(line, channel, length, aim, x, y, place)

/* ---- Chases ---------------------------------------------------------------------------------- */

/* Which way a chase turns its heading toward the target's angle: down
 * (round the turn backward) where the target lies less than half a turn
 * below. The original works it out by the quarters of the turn the two are
 * in, and leaves them in work cells. */
enum { HALF_TURN = 0x80, QUARTER_TURN = 0x40, QUARTER_SHIFT = 6, LEFT_TARGET_QUARTER = 0x06, LEFT_HEADING_QUARTER = 0x07 };

// $C1:53B0
static bool turns_down(GameState *g, uint8_t heading, uint8_t target)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_TARGET_QUARTER, (uint8_t)(target >> QUARTER_SHIFT)));
    ORIGINAL_EFFECT(script_leave(g, LEFT_HEADING_QUARTER, (uint8_t)(heading >> QUARTER_SHIFT)));
    return (uint8_t)(heading - target) < HALF_TURN;
}

/* A heading of none: the mover's own (the angle it last went out at). */
enum { OWN_HEADING = 0xFF, LEFT_CHASE_HEADING = 0x02 };

// $C1:523C, $C1:5333
/* Set the mover chasing a point, setting out along a heading and turning
 * toward the point as it goes. An anchor's place is put back on the
 * battlefield for it (its high bytes cleared). Returns the heading it set
 * out on. */
static uint8_t start_chase(GameState *g, Mover m, BattlePoint to, uint8_t heading)
{
    unsigned e = mover_entry(m);
    if (mover_is_anchor(m))
        place_anchor(g, m.index, (uint8_t)anchor_x(g, m.index), (uint8_t)anchor_y(g, m.index));
    uint8_t angle = battle_direction(g, mover_position(g, m), to);
    if (heading == OWN_HEADING)
        heading = heading_of(g, m);
    ORIGINAL_EFFECT(script_leave(g, LEFT_CHASE_HEADING, heading));
    set_battle_effect_chase_angle(g, e, heading);
    set_battle_effect_chase_turn(g, e, turns_down(g, heading, angle));
    set_battle_effect_radius(g, e, 0);
    set_battle_effect_chase_aligned(g, e, 0);
    begin_path(g, m, PATH_CHASE);
    set_target(g, m, to);
    set_moving(g, m, true);
    return heading;
}

/* The original counts, on the other targets' channel, the chasers still
 * going in a work cell. */
enum { LEFT_STILL_GOING = 0x04 };

// $C1:51EE, $C1:5205, $C1:521C
/* What the channel moves chases where the command aims, setting out on a
 * heading. The other targets set off one by one until one is already
 * chasing; from there on the list, they are waited for, those whose chase
 * has ended stopping, and the channel goes on once none is still going. A
 * heading of their own is the first's for those after it. */
static ScriptFlow chase(GameState *g, ScriptChannel ch, uint8_t length, ScriptAim aim, uint8_t x, uint8_t y,
                        uint8_t place, uint8_t heading)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_CHASE_HEADING, heading));
    BattlePoint to = script_aim(g, aim, x, y, place);
    ORIGINAL_EFFECT(script_leave(g, LEFT_LENGTH, length));
    if (ch.kind == SCRIPT_OTHER_TARGETS) {
        uint8_t slot;
        unsigned k = 0;
        for (; script_each_combatant(g, ch, k, &slot) && !moving(g, combatant_mover(slot)); k++)
            heading = start_chase(g, combatant_mover(slot), to, heading);
        bool waited = script_each_combatant(g, ch, k, &slot);
        uint8_t still_going = 0;
        for (; script_each_combatant(g, ch, k, &slot); k++)
            if (!stopped_at_the_end(g, combatant_mover(slot)))
                still_going++;
        ORIGINAL_EFFECT(script_leave_word(g, LEFT_LISTED, (uint8_t)k));
        if (waited)
            ORIGINAL_EFFECT(script_leave(g, LEFT_STILL_GOING, still_going));
        return !waited || still_going != 0 ? SCRIPT_STAY : SCRIPT_NEXT_FRAME;
    }
    Mover m = channel_mover(g, ch);
    switch (stage_of(g, m)) {
    case MOVE_STARTS: start_chase(g, m, to, heading); return SCRIPT_STAY;
    case MOVE_ENDED: return SCRIPT_NEXT_FRAME;
    default: return SCRIPT_STAY;
    }
}
SCRIPT_OPERATION(chase, channel, length, aim, x, y, place, heading)

/* ---- Putting a mover somewhere --------------------------------------------------------------- */

/* The original keeps the point in work cells. */
enum { LEFT_PUT_AT = 0x00 };

// $C1:5611, $C1:566C, $C1:567D
/* What the channel moves is put where the command aims, at once: its
 * combatant, each of the other targets, or its effect's anchor (on the
 * battlefield). */
static ScriptFlow put(GameState *g, ScriptChannel ch, uint8_t length, ScriptAim aim, uint8_t x, uint8_t y, uint8_t place)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_LENGTH, length));
    BattlePoint at = aim == AIM_AT_A_PLACE ? script_find_place(g, place) : aim == AIM_AT_THE_POINT ? script_point(g) : (BattlePoint){ x, y };
    ORIGINAL_EFFECT(script_leave(g, LEFT_PUT_AT, at.x));
    ORIGINAL_EFFECT(script_leave(g, LEFT_PUT_AT + 1, at.y));
    if (ch.kind == SCRIPT_EFFECT) {
        place_anchor(g, script_effect(g), at.x, at.y);
        return SCRIPT_NEXT_FRAME;
    }
    uint8_t slot;
    for (unsigned k = 0; script_each_combatant(g, ch, k, &slot); k++) {
        place_combatant_x(g, slot, at.x);
        place_combatant_y(g, slot, at.y);
    }
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(put, channel, length, aim, x, y, place)

/* ---- Jumps ----------------------------------------------------------------------------------- */

/* How a jump lifts: by its height twice over (a combatant's sprite) or once,
 * and how fast it bounces back up off a fall: full, or at half. A jump to the
 * script's point has no lift of its own, and lifts once at full bounce
 * (POINT_JUMP_LIFT, battle/script_language.h). The original leaves a lift
 * the command gives where it takes that one from. */
enum { LIFTED_TWICE = 0, BOUNCES_AT_HALF = 2, FULL_BOUNCE = 0xFFFF, HALF_BOUNCE = 0x7FFF, LEFT_LIFT = 0x10 };

static uint16_t bounce_of(uint8_t lift) { return lift == BOUNCES_AT_HALF ? HALF_BOUNCE : FULL_BOUNCE; }

/* A short hop goes a quarter of the way to its point, in a quarter of the
 * steps a jump there would take each its own (four times the frames). */
enum { HOP_SHARE_SHIFT = 2 };

// $C1:6549
static BattlePoint a_quarter_of_the_way(BattlePoint from, BattlePoint to)
{
    uint8_t dx = (uint8_t)(apart(from.x, to.x) >> HOP_SHARE_SHIFT), dy = (uint8_t)(apart(from.y, to.y) >> HOP_SHARE_SHIFT);
    return (BattlePoint){ to.x < from.x ? (uint8_t)(from.x - dx) : (uint8_t)(from.x + dx),
                          to.y < from.y ? (uint8_t)(from.y - dy) : (uint8_t)(from.y + dy) };
}

/* A jump at rest: its speed and bounce, its height 0, rising. */
static void ready_the_jump(GameState *g, Mover m, uint16_t gravity, uint16_t bounce)
{
    unsigned e = mover_entry(m);
    set_battle_effect_jump_gravity(g, e, gravity);
    set_battle_effect_jump_speed(g, e, bounce);
    set_battle_effect_jump_bounce(g, e, bounce);
    set_battle_effect_jump_height(g, e, 0);
    set_battle_effect_jump_falling(g, e, 0);
}

/* The original keeps the jumper, whether it goes down and its lift in work
 * cells. */
enum { LEFT_JUMPER = 0x00, LEFT_DOWNWARD = 0x0F };

// $C1:62B4, $C1:63FC
/* Set the mover jumping to a point: along the line there, its height rising
 * and falling under gravity (enough to come down as it gets there), at its
 * speed, facing the way it goes; or a short hop a quarter of the way. */
static void start_jump(GameState *g, Mover m, BattlePoint to, uint8_t lift, bool downward, bool short_hop)
{
    unsigned e = mover_entry(m);
    BattlePoint from = mover_position(g, m);
    set_base(g, m, from);
    BattlePoint aim = short_hop ? a_quarter_of_the_way(from, to) : to;
    uint8_t angle = battle_direction(g, from, aim);
    LineVelocity v = line_velocity(g, from, aim);
    uint8_t speed = speed_of(g, m);
    uint8_t frames = (uint8_t)battle_divide_by(g, v.length, speed).quotient;
    set_battle_effect_jump_frames(g, e, short_hop ? (uint8_t)(frames << HOP_SHARE_SHIFT) : frames);
    set_battle_effect_jump_downward(g, e, downward);
    uint16_t vx = at_speed(g, v.vx, speed), vy = at_speed(g, v.vy, speed);
    set_battle_effect_jump_single(g, e, lift);
    uint16_t bounce = bounce_of(lift);
    uint16_t gravity = battle_divide_by(g, bounce, (uint8_t)(v.length >> 1)).quotient;
    if (mover_is_anchor(m))
        set_battle_effect_angle(g, e, angle);
    else
        ORIGINAL_EFFECT(script_leave_word(g, LEFT_JUMPER, m.index));
    face_along(g, m, angle);
    set_target(g, m, to);
    begin_path(g, m, PATH_JUMP);
    at_the_start_of_the_line(g, m, vx, vy);
    ready_the_jump(g, m, gravity, bounce);
    ORIGINAL_EFFECT(leave_velocity(g, vx, vy));
}

// $C1:6299, $C1:672E, $C1:6743, $C1:674A
/* What the channel moves jumps to where the command aims, up or down the
 * screen, or hops a short way toward it. On the other targets' channel
 * nothing jumps, and the channel goes past the last move it started. */
static ScriptFlow jump(GameState *g, ScriptChannel ch, uint8_t length, ScriptAim aim, uint8_t place, uint8_t lift,
                      bool downward, bool short_hop)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_DOWNWARD, downward));
    if (aim == AIM_AT_A_PLACE) /* the jumps that give their lift */
        ORIGINAL_EFFECT(script_leave(g, LEFT_LIFT, lift));
    BattlePoint to = script_aim(g, aim, 0, 0, place);
    ORIGINAL_EFFECT(script_leave(g, LEFT_LENGTH, length));
    if (ch.kind == SCRIPT_OTHER_TARGETS)
        return past_the_move(g, ch);
    Mover m = channel_mover(g, ch);
    switch (stage_of(g, m)) {
    case MOVE_STARTS:
        start_jump(g, m, to, lift, downward, short_hop);
        keep_move_length(g, ch, length);
        set_moving(g, m, true);
        return SCRIPT_STAY;
    case MOVE_ENDED: return past_the_move(g, ch);
    default: return SCRIPT_STAY;
    }
}
SCRIPT_OPERATION(jump, channel, length, aim, place, lift, downward, short_hop)

/* ---- Spirals --------------------------------------------------------------------------------- */

/* A spiral goes out from where the mover is along its heading, until it
 * comes to the screen's edge, its frames run out, or (for the commands that
 * look) the battlefield's grid stops it: 16 pixels ahead of the mover a
 * cell the grid marks as a wall or a combatant's. A spiral with no frames of
 * its own is given the most. */
enum { MOST_FRAMES = 0xFF, LOOK_AHEAD = 16, GRID_COLUMN_SHIFT = 4, GRID_ROW = 0xF0, GRID_STOPS = 0xC0 };

// $C1:676C, $C1:688E
static void start_spiral(GameState *g, Mover m, uint8_t frames)
{
    unsigned e = mover_entry(m);
    uint8_t angle = heading_of(g, m);
    set_battle_effect_angle(g, e, angle);
    face_along(g, m, angle);
    set_base(g, m, mover_position(g, m));
    begin_path(g, m, PATH_SPIRAL);
    set_battle_effect_radius(g, e, 0);
    set_battle_effect_spiral_frames(g, e, frames);
}

/* What looking ahead leaves: the cell's row and the step across, and the
 * radius the sine's multiply takes. */
enum { LEFT_AHEAD_ROW = 0x02, LEFT_AHEAD_ACROSS = 0x03 };

static void leave_looked_ahead(GameState *g, uint8_t row, int8_t dx)
{
    script_leave(g, LEFT_AHEAD_ROW, row);
    script_leave(g, LEFT_AHEAD_ACROSS, (uint8_t)dx);
    set_battle_multiply_factors(g, 1, LOOK_AHEAD);
}

// $C1:679F
static bool blocked_ahead(GameState *g, Mover m)
{
    uint8_t angle = battle_effect_angle(g, mover_entry(m));
    BattlePoint at = mover_position(g, m);
    int8_t dy = battle_along(g, angle, LOOK_AHEAD), dx = battle_along(g, (uint8_t)(angle + QUARTER_TURN), LOOK_AHEAD);
    uint8_t row = (uint8_t)((at.y + dy) & GRID_ROW);
    uint8_t cell = (uint8_t)(row | (uint8_t)(at.x + dx) >> GRID_COLUMN_SHIFT);
    ORIGINAL_EFFECT(leave_looked_ahead(g, row, dx));
    return (battle_base_grid(g, cell) & GRID_STOPS) != 0;
}

/* The original keeps, on the other targets' channel, the place in their list
 * and how many of them it waits on in work cells. */
enum { LEFT_WAITED_ON = 0x04 };

// $C1:6751
/* What the channel moves spirals out along its heading until the grid stops
 * it or its path ends. The other targets set off one by one until one is
 * already spiralling (setting off doesn't mark them moving, so on a first
 * run every one sets off); from there on the list, those whose way is clear
 * go on and the rest stop, and the channel goes on once none goes on. */
static ScriptFlow spiral_out(GameState *g, ScriptChannel ch)
{
    if (ch.kind == SCRIPT_OTHER_TARGETS) {
        uint8_t slot;
        unsigned k = 0;
        for (; script_each_combatant(g, ch, k, &slot) && !moving(g, combatant_mover(slot)); k++)
            start_spiral(g, combatant_mover(slot), MOST_FRAMES);
        uint8_t going = 0;
        for (; script_each_combatant(g, ch, k, &slot); k++) {
            Mover m = combatant_mover(slot);
            bool goes = !mover_path_done(g, m) && !blocked_ahead(g, m);
            set_moving(g, m, goes);
            going = (uint8_t)(going + goes);
        }
        ORIGINAL_EFFECT(script_leave_word(g, LEFT_LISTED, (uint8_t)k));
        ORIGINAL_EFFECT(script_leave(g, LEFT_WAITED_ON, going));
        return going ? SCRIPT_STAY : SCRIPT_NEXT_FRAME;
    }
    Mover m = channel_mover(g, ch);
    if (!moving(g, m))
        start_spiral(g, m, MOST_FRAMES);
    bool goes = !mover_path_done(g, m) && !blocked_ahead(g, m);
    set_moving(g, m, goes);
    return goes ? SCRIPT_STAY : SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(spiral_out, channel)

/* A combatant spiralling with others about stops where its box meets another
 * combatant's. The original's overlap test leaves its answer in the first
 * work cell. */
enum { MEETS_A_MEMBER = 0x80, MEETS_AN_ENEMY = 0x81 };

static bool meets_another(GameState *g, uint8_t slot)
{
    set_battle_bounds_x(g, slot, combatant_x(g, slot));
    set_battle_bounds_y(g, slot, combatant_y(g, slot));
    battle_update_bounds(g, slot);
    Overlap o = battle_box_overlaps(g, slot);
    ORIGINAL_EFFECT(script_leave_word(g, LEFT_LISTED, o == OVERLAPS_NOBODY ? 0 : o == OVERLAPS_MEMBER ? MEETS_A_MEMBER : MEETS_AN_ENEMY));
    return o != OVERLAPS_NOBODY;
}

/* The command counts the spiral's frames down as well as its path does. */
static bool counted_out(GameState *g, Mover m)
{
    unsigned e = mover_entry(m);
    uint8_t left = (uint8_t)(battle_effect_spiral_frames(g, e) - 1);
    set_battle_effect_spiral_frames(g, e, left);
    return left == 0;
}

// $C1:69C3, $C1:6AF0
/* What the channel moves spirals out for a number of frames; or, stopping
 * too where the grid or (for a combatant) another combatant stops it, with
 * the frames counted down here as well. The other targets set off one by one
 * until one is already spiralling; from the first of the list those whose
 * path has ended stop, and the channel goes on once every one has. (Stopping
 * at others is for one mover only: a row's needs see to it.) */
static ScriptFlow spiral_for(GameState *g, ScriptChannel ch, uint8_t frames, bool stops_at_others)
{
    set_battle_script_spiral_frames(g, frames);
    if (ch.kind == SCRIPT_OTHER_TARGETS) {
        uint8_t slot;
        unsigned k = 0;
        for (; script_each_combatant(g, ch, k, &slot) && !moving(g, combatant_mover(slot)); k++) {
            start_spiral(g, combatant_mover(slot), frames);
            set_moving(g, combatant_mover(slot), true);
        }
        ORIGINAL_EFFECT(script_leave_word(g, LEFT_LISTED, (uint8_t)k));
        if (!script_each_combatant(g, ch, k, &slot))
            return SCRIPT_STAY;
        uint8_t going = 0;
        for (unsigned j = 0; script_each_combatant(g, ch, j, &slot); j++)
            if (!stopped_at_the_end(g, combatant_mover(slot)))
                going++;
        ORIGINAL_EFFECT(script_leave_word(g, LEFT_WAITED_ON, going));
        return going ? SCRIPT_STAY : SCRIPT_NEXT_FRAME;
    }
    Mover m = channel_mover(g, ch);
    if (!moving(g, m)) {
        start_spiral(g, m, frames);
        set_moving(g, m, true);
        if (!stops_at_others)
            return SCRIPT_STAY;
    }
    if (!stops_at_others)
        return stopped_at_the_end(g, m) ? SCRIPT_NEXT_FRAME : SCRIPT_STAY;
    bool stops = mover_path_done(g, m) || blocked_ahead(g, m) || (!mover_is_anchor(m) && meets_another(g, m.index))
                 || counted_out(g, m);
    if (stops)
        set_moving(g, m, false);
    return stops ? SCRIPT_NEXT_FRAME : SCRIPT_STAY;
}
SCRIPT_OPERATION(spiral_for, channel, frames, stops_at_others)

/* ---- Orbits --------------------------------------------------------------------------------- */

/* An orbit goes round a point at a radius, setting out at a heading and
 * turning by the mover's speed a step, forward or backward (the circle's top
 * bit), for a number of quarter turns. A combatant is put at the center and
 * its sprite turns about it; an anchor turns round the center itself. A flat
 * orbit is half as tall as it is wide. */
enum { CIRCLE_BACKWARD = 0x80, CIRCLE_RADIUS = 0x7F, QUARTER_TURN_STEPS = 0x40 };

/* The original keeps the circle, the heading, the quarter turns, the place
 * and the kind of orbit in work cells. */
enum { LEFT_CIRCLE = 0x00, LEFT_START = 0x02, LEFT_QUARTERS = 0x04, LEFT_CENTER = 0x06, LEFT_KIND = 0x0F };

// $C1:6D32, $C1:6E9A
static uint8_t start_orbit(GameState *g, Mover m, BattlePoint center, uint8_t circle, uint8_t start, uint8_t quarter_turns,
                           bool flat)
{
    unsigned e = mover_entry(m);
    if (start == OWN_HEADING)
        start = heading_of(g, m);
    if (mover_is_anchor(m)) {
        set_battle_anchor_orbit_center_x(g, m.index, center.x);
        set_battle_anchor_orbit_center_y(g, m.index, center.y);
    } else {
        set_battle_combatant_orbit_center_x(g, m.index, center.x);
        set_battle_combatant_orbit_center_y(g, m.index, center.y);
        place_combatant_x(g, m.index, center.x);
        place_combatant_y(g, m.index, center.y);
    }
    uint8_t step = speed_of(g, m);
    bool backward = (circle & CIRCLE_BACKWARD) != 0;
    set_battle_effect_orbit_angle(g, e, backward ? (uint8_t)(start + step) : (uint8_t)(start - step));
    set_battle_effect_orbit_step(g, e, backward ? (uint8_t)-step : step);
    set_battle_effect_orbit_radius(g, e, circle & CIRCLE_RADIUS);
    begin_path(g, m, flat ? PATH_ORBIT_FLAT : PATH_ORBIT);
    uint16_t sweep = battle_multiply(g, QUARTER_TURN_STEPS, quarter_turns);
    set_battle_effect_orbit_frames(g, e, battle_divide_by(g, sweep, step).quotient);
    set_moving(g, m, true);
    return start;
}

// $C1:6CF3, $C1:6F31, $C1:6F53, $C1:6F75
/* What the channel moves orbits round where the command aims. The other
 * targets set off one by one until one is already orbiting (a heading of
 * their own is the first's for those after it); from there the whole list
 * is looked at again, those whose orbit has ended stopping, and the channel
 * goes on if none of them has ended. */
static ScriptFlow orbit(GameState *g, ScriptChannel ch, uint8_t length, ScriptAim aim, uint8_t place, uint8_t circle,
                       uint8_t start, uint8_t quarter_turns, bool flat)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_CIRCLE, circle));
    ORIGINAL_EFFECT(script_leave(g, LEFT_START, start));
    ORIGINAL_EFFECT(script_leave(g, LEFT_QUARTERS, quarter_turns));
    if (aim == AIM_AT_A_PLACE)
        ORIGINAL_EFFECT(script_leave(g, LEFT_CENTER, place));
    BattlePoint center = script_aim(g, aim, 0, 0, place);
    ORIGINAL_EFFECT(script_leave(g, LEFT_LENGTH, length));
    ORIGINAL_EFFECT(script_leave(g, LEFT_KIND, flat ? PATH_ORBIT_FLAT : PATH_ORBIT));
    if (ch.kind == SCRIPT_OTHER_TARGETS) {
        uint8_t slot;
        unsigned k = 0;
        for (; script_each_combatant(g, ch, k, &slot) && !moving(g, combatant_mover(slot)); k++) {
            start = start_orbit(g, combatant_mover(slot), center, circle, start, quarter_turns, flat);
            ORIGINAL_EFFECT(script_leave(g, LEFT_START, start));
        }
        if (!script_each_combatant(g, ch, k, &slot)) {
            ORIGINAL_EFFECT(script_leave_word(g, LEFT_CENTER, (uint8_t)k));
            return SCRIPT_STAY;
        }
        uint8_t ended = 0;
        unsigned j = 0;
        for (; script_each_combatant(g, ch, j, &slot); j++)
            if (stopped_at_the_end(g, combatant_mover(slot)))
                ended++;
        ORIGINAL_EFFECT(script_leave_word(g, LEFT_CENTER, (uint8_t)j));
        ORIGINAL_EFFECT(script_leave(g, LEFT_QUARTERS, ended));
        return ended ? SCRIPT_STAY : SCRIPT_NEXT_FRAME;
    }
    Mover m = channel_mover(g, ch);
    switch (stage_of(g, m)) {
    case MOVE_STARTS:
        start = start_orbit(g, m, center, circle, start, quarter_turns, flat);
        ORIGINAL_EFFECT(script_leave(g, LEFT_START, start));
        return SCRIPT_STAY;
    case MOVE_ENDED: return SCRIPT_NEXT_FRAME;
    default: return SCRIPT_STAY;
    }
}
SCRIPT_OPERATION(orbit, channel, length, aim, place, circle, start, quarter_turns, flat)

/* ---- Throws ---------------------------------------------------------------------------------- */

/* A throw: the mover is aimed to land on a place, as a jump onto it, and an
 * arc's top is worked out, halfway there and 64 pixels up; then it rises to
 * the top in a line, waits, and falls onto the place. A throw takes 36
 * frames before its fall; its rise's frames are taken off the wait before it
 * (or, where the rise takes longer, the rise's frames past 36 are a wait
 * after it). Nothing is thrown on the other targets' channel. */
enum { THROW_FRAMES = 36, ARC_HEIGHT = 0x40, THROW_SPEED_SHIFT = 2, THROW_GRAVITY_DIVISOR = 0x80 };

/* What aiming a throw leaves: the half distances on the axes it goes back
 * along (0 on the others), and its line cells. */
static void leave_aimed(GameState *g, BattlePoint from, BattlePoint to, uint8_t across, uint8_t down)
{
    leave_line_cells(g, from, to, across, down);
    script_leave(g, LEFT_LEFTWARD, to.x < from.x ? (uint8_t)(across >> 1) : 0);
    script_leave(g, LEFT_UPWARD, to.y < from.y ? (uint8_t)(down >> 1) : 0);
    script_leave_word(g, LEFT_HALF_LENGTH, 0);
}

// $C1:6671
/* The throw's velocity, four times the distance a step (the jump's line
 * runs at its speed times that), and the arc's top. */
static LineVelocity aim_the_throw(GameState *g, BattlePoint from, BattlePoint to)
{
    LineVelocity v = { 0, 0, apart(from.x, to.x), apart(from.y, to.y), 0, false };
    keep_the_points(g, from, to);
    v.vx = (uint16_t)(v.across << THROW_SPEED_SHIFT);
    v.vy = (uint16_t)(v.down << THROW_SPEED_SHIFT);
    uint8_t half_across = (uint8_t)(v.across >> 1), half_down = (uint8_t)(v.down >> 1);
    uint8_t top_x = to.x < from.x ? (uint8_t)(from.x - half_across) : (uint8_t)(from.x + half_across);
    uint8_t top_y = to.y < from.y ? (uint8_t)(from.y - half_down) : (uint8_t)(from.y + half_down);
    if (to.x < from.x)
        v.vx = (uint16_t)-v.vx;
    if (to.y < from.y)
        v.vy = (uint16_t)-v.vy;
    set_battle_throw_apex_x(g, top_x);
    set_battle_throw_apex_y(g, top_y >= ARC_HEIGHT ? (uint8_t)(top_y - ARC_HEIGHT) : 0);
    set_battle_throw_wait_before(g, THROW_FRAMES);
    ORIGINAL_EFFECT(leave_aimed(g, from, to, v.across, v.down));
    return v;
}

// $C1:705A
/* What the channel moves is aimed to land on a place: its path is a jump
 * onto it, which waits until the throw falls. */
static ScriptFlow aim_throw(GameState *g, ScriptChannel ch, uint8_t place)
{
    BattlePoint to = script_find_place(g, place);
    if (ch.kind == SCRIPT_OTHER_TARGETS)
        return SCRIPT_NEXT_FRAME;
    Mover m = channel_mover(g, ch);
    unsigned e = mover_entry(m);
    BattlePoint from = mover_position(g, m);
    set_base(g, m, from);
    uint8_t angle = battle_direction(g, from, to);
    LineVelocity v = aim_the_throw(g, from, to);
    uint8_t speed = speed_of(g, m);
    uint16_t vx = at_speed(g, v.vx, speed), vy = at_speed(g, v.vy, speed);
    set_battle_effect_jump_single(g, e, LIFTED_TWICE);
    uint16_t gravity = battle_divide_by(g, HALF_BOUNCE, THROW_GRAVITY_DIVISOR).quotient;
    if (mover_is_anchor(m)) {
        set_battle_effect_angle(g, e, angle);
        face_along(g, m, angle);
    } else {
        ORIGINAL_EFFECT(script_leave_word(g, LEFT_JUMPER, m.index));
    }
    set_target(g, m, to);
    begin_path(g, m, PATH_JUMP_TO_TARGET);
    set_battle_effect_home_stage(g, e, 0);
    at_the_start_of_the_line(g, m, vx, vy);
    ready_the_jump(g, m, gravity, HALF_BOUNCE);
    ORIGINAL_EFFECT(leave_velocity(g, vx, vy));
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(aim_throw, channel, place)

// $C1:7313
/* Share the throw's 36 frames between the wait before its rise and the wait
 * after it, by the frames the rise takes at the mover's speed. */
static void time_the_rise(GameState *g, uint8_t speed)
{
    uint16_t rise = battle_divide_by(g, (uint8_t)battle_throw_wait_after(g), speed).quotient;
    uint16_t before = battle_throw_wait_before(g);
    set_battle_throw_wait_before(g, before >= rise ? 0 : (uint16_t)(rise - before));
    set_battle_throw_wait_after(g, before >= rise ? (uint16_t)(before - rise) : 0);
}

/* The original's rise for an anchor reads the speed at twice the anchor's
 * number past the combatants' entries: past the movers' table for the last
 * four anchors (re/ram.toml). */
static uint8_t anchor_rise_speed(const GameState *g, uint8_t anchor)
{
    unsigned entry = FIRST_ANCHOR_MOVER + 2u * anchor;
    return entry < BATTLE_EFFECT_STEPS_COUNT ? battle_effect_steps(g, entry) : battle_effect_steps_unchecked(g, entry);
}

// $C1:720E
/* What the channel moves is set to rise to the throw's top, in a line it
 * doesn't start yet: a combatant is stopped; an anchor is left moving or
 * not as it was. */
static ScriptFlow throw_rise(GameState *g, ScriptChannel ch)
{
    BattlePoint top = script_aim(g, AIM_AT_COORDINATES, battle_throw_apex_x(g), battle_throw_apex_y(g), 0);
    if (ch.kind == SCRIPT_OTHER_TARGETS)
        return SCRIPT_NEXT_FRAME;
    Mover m = channel_mover(g, ch);
    start_line(g, m, top);
    if (!mover_is_anchor(m))
        set_moving(g, m, false);
    time_the_rise(g, mover_is_anchor(m) ? anchor_rise_speed(g, m.index) : speed_of(g, m));
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(throw_rise, channel)

/* Count a frame off the throw's wait, if it has one left. */
static bool waits(GameState *g, ThrowWait wait)
{
    if (wait == WAIT_BEFORE && battle_throw_wait_before(g) != 0) {
        set_battle_throw_wait_before(g, (uint16_t)(battle_throw_wait_before(g) - 1));
        return true;
    }
    if (wait == WAIT_AFTER && battle_throw_wait_after(g) != 0) {
        set_battle_throw_wait_after(g, (uint16_t)(battle_throw_wait_after(g) - 1));
        return true;
    }
    return false;
}

// $C1:7349, $C1:73AD, $C1:7364
/* The throw goes on: after its wait, the mover moves until its path has
 * ended, when it stops; the path's end is cleared for the next, but after
 * the wait after the rise. */
static ScriptFlow throw_moves(GameState *g, ScriptChannel ch, ThrowWait wait)
{
    if (waits(g, wait))
        return SCRIPT_STAY;
    if (ch.kind == SCRIPT_OTHER_TARGETS)
        return SCRIPT_NEXT_FRAME;
    Mover m = channel_mover(g, ch);
    set_moving(g, m, true);
    if (!stopped_at_the_end(g, m))
        return SCRIPT_STAY;
    if (wait != WAIT_AFTER)
        set_mover_done_count(g, m, 0);
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(throw_moves, channel, wait)

/* ---- A point along the line to a place --------------------------------------------------------- */

/* The original keeps the line's sums in work cells, and the steps. */
enum { LEFT_ALONG_X = 0x00, LEFT_ALONG_Y = 0x02, LEFT_STEPS = 0x0E, STEPS_OF_NONE = 0x100 };

// $C1:740B, $C1:7468
/* The script's point is put a number of steps along the line from what the
 * channel moves toward a place (a step a pixel along the line's longer
 * axis); at the screen's corner where the steps would reach the place or go
 * past it. Nothing is put on the other targets' channel. */
static ScriptFlow point_along(GameState *g, ScriptChannel ch, uint8_t steps, uint8_t place)
{
    BattlePoint to = script_find_place(g, place);
    ORIGINAL_EFFECT(script_leave(g, LEFT_STEPS, steps));
    if (ch.kind == SCRIPT_OTHER_TARGETS)
        return SCRIPT_NEXT_FRAME;
    BattlePoint from = mover_position(g, channel_mover(g, ch));
    LineVelocity v = line_velocity(g, from, to);
    ORIGINAL_EFFECT(leave_velocity(g, v.vx, v.vy));
    if (steps >= v.length) {
        set_battle_script_point_x(g, 0);
        set_battle_script_point_y(g, 0);
        return SCRIPT_NEXT_FRAME;
    }
    unsigned n = steps ? steps : STEPS_OF_NONE;
    uint16_t sx = (uint16_t)(v.vx * n), sy = (uint16_t)(v.vy * n);
    ORIGINAL_EFFECT(script_leave_word(g, LEFT_ALONG_X, sx));
    ORIGINAL_EFFECT(script_leave_word(g, LEFT_ALONG_Y, sy));
    ORIGINAL_EFFECT(script_leave(g, LEFT_STEPS, 0));
    set_battle_script_point_x(g, (uint8_t)(from.x + (sx >> 8)));
    set_battle_script_point_y(g, (uint8_t)(from.y + (sy >> 8)));
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(point_along, channel, steps, place)

/* ---- The flicker ------------------------------------------------------------------------------ */

/* The original keeps the radius, the period and the frames in work cells. */
enum { LEFT_FLICKER_RADIUS = 0x00, LEFT_FLICKER_PERIOD = 0x02, LEFT_FLICKER_FRAMES = 0x04 };

/* Set a combatant's sprite flickering: shown off by the radius first, and
 * moved every frame, for `lasting` frames. */
static void start_flicker(GameState *g, uint8_t slot, uint8_t radius, uint8_t period, uint8_t lasting)
{
    set_battle_effect_flicker_radius(g, slot, radius);
    set_battle_effect_flicker_period(g, slot, period);
    set_battle_effect_flicker_frames(g, slot, lasting);
    set_battle_effect_flicker_count(g, slot, 1);
    set_battle_effect_flicker_hidden(g, slot, 1);
    set_battle_effect_period(g, slot, 1);
    set_battle_effect_delay(g, slot, 1);
    set_battle_effect_type(g, slot, PATH_FLICKER);
    set_moving(g, combatant_mover(slot), true);
}

// $C1:74B4
/* What the channel moves flickers: its combatant's sprite (each of the other
 * targets') jumps between where it stands and the given distance off, every
 * so many frames, for a number of frames (the path runs every frame). An
 * effect's anchor doesn't. */
static ScriptFlow flicker(GameState *g, ScriptChannel ch, uint8_t radius, uint8_t period, uint8_t frames)
{
    uint8_t lasting = (uint8_t)(frames + 1);
    ORIGINAL_EFFECT(script_leave(g, LEFT_FLICKER_RADIUS, radius));
    ORIGINAL_EFFECT(script_leave(g, LEFT_FLICKER_PERIOD, period));
    ORIGINAL_EFFECT(script_leave(g, LEFT_FLICKER_FRAMES, lasting));
    uint8_t slot;
    for (unsigned k = 0; script_each_combatant(g, ch, k, &slot); k++)
        start_flicker(g, slot, radius, period, lasting);
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(flicker, channel, radius, period, frames)

/* ---- The table ------------------------------------------------------------------------- */

#define AT_COORDINATES .aim = CONSTANT(AIM_AT_COORDINATES)
#define AT_THE_POINT .aim = CONSTANT(AIM_AT_THE_POINT)
#define AT_A_PLACE .aim = CONSTANT(AIM_AT_A_PLACE)
#define POINT_LIFT .lift = CONSTANT(POINT_JUMP_LIFT)

SCRIPT_OPCODES(script_move_opcodes,
    { 0x10, OPERATION(line), AT_COORDINATES, .x = OPERAND(1), .y = OPERAND(2), .length = 3, .needs = NEEDS_SUBJECTS },
    { 0x11, OPERATION(line), AT_THE_POINT, .length = 1, .needs = NEEDS_SUBJECTS },
    { 0x12, OPERATION(line), AT_A_PLACE, .place = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0x13, OPERATION(chase), .heading = OPERAND(1), AT_COORDINATES, .x = OPERAND(2), .y = OPERAND(3), .length = 4, .needs = NEEDS_SUBJECTS },
    { 0x14, OPERATION(chase), .heading = OPERAND(1), AT_THE_POINT, .length = 2, .needs = NEEDS_SUBJECTS },
    { 0x15, OPERATION(chase), .heading = OPERAND(1), AT_A_PLACE, .place = OPERAND(2), .length = 3, .needs = NEEDS_SUBJECTS_AND_A_QUIET_PLACE },
    { 0x19, OPERATION(put), AT_COORDINATES, .x = OPERAND(1), .y = OPERAND(2), .length = 3, .needs = NEEDS_SUBJECTS },
    { 0x1A, OPERATION(put), AT_THE_POINT, .length = 1, .needs = NEEDS_SUBJECTS },
    { 0x1B, OPERATION(put), AT_A_PLACE, .place = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0x98, OPERATION(jump), AT_A_PLACE, .lift = OPERAND(1), .place = OPERAND(2), .length = 3, .needs = NEEDS_SUBJECTS },
    { 0x99, OPERATION(jump), AT_THE_POINT, POINT_LIFT, .length = 2, .needs = NEEDS_SUBJECTS_AND_THE_USUAL_LIFT },
    { 0x9A, OPERATION(jump), AT_A_PLACE, .lift = OPERAND(1), .place = OPERAND(2), .downward = CONSTANT(true), .length = 3, .needs = NEEDS_SUBJECTS },
    { 0x9B, OPERATION(jump), AT_THE_POINT, POINT_LIFT, .downward = CONSTANT(true), .length = 2, .needs = NEEDS_SUBJECTS_AND_THE_USUAL_LIFT },
    { 0x9C, OPERATION(jump), AT_A_PLACE, .lift = OPERAND(1), .place = OPERAND(2), .short_hop = CONSTANT(true), .length = 3, .needs = NEEDS_SUBJECTS },
    { 0x9D, OPERATION(jump), AT_THE_POINT, POINT_LIFT, .short_hop = CONSTANT(true), .length = 2, .needs = NEEDS_SUBJECTS_AND_THE_USUAL_LIFT },
    { 0xA2, OPERATION(spiral_out), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0xA8, OPERATION(spiral_for), .frames = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0xA9, OPERATION(spiral_for), .frames = OPERAND(1), .stops_at_others = CONSTANT(true), .length = 2, .needs = NEEDS_ONE_MOVER },
    { 0xC0, OPERATION(orbit), AT_A_PLACE, .place = OPERAND(1), .circle = OPERAND(2), .start = OPERAND(3), .quarter_turns = OPERAND(4), .length = 5, .needs = NEEDS_SUBJECTS_AND_A_QUIET_PLACE },
    { 0xC1, OPERATION(orbit), AT_THE_POINT, .circle = OPERAND(1), .start = OPERAND(2), .quarter_turns = OPERAND(3), .length = 4, .needs = NEEDS_SUBJECTS },
    { 0xC2, OPERATION(orbit), AT_A_PLACE, .place = OPERAND(1), .circle = OPERAND(2), .start = OPERAND(3), .quarter_turns = OPERAND(4), .flat = CONSTANT(true), .length = 5, .needs = NEEDS_SUBJECTS_AND_A_QUIET_PLACE },
    { 0xC3, OPERATION(orbit), AT_THE_POINT, .circle = OPERAND(1), .start = OPERAND(2), .quarter_turns = OPERAND(3), .flat = CONSTANT(true), .length = 4, .needs = NEEDS_SUBJECTS },
    { 0xD2, OPERATION(aim_throw), .place = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0xD3, OPERATION(throw_rise), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0xD4, OPERATION(throw_moves), .wait = CONSTANT(WAIT_BEFORE), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0xD5, OPERATION(throw_moves), .wait = CONSTANT(WAIT_AFTER), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0xD6, OPERATION(throw_moves), .wait = CONSTANT(NO_WAIT), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0xD7, OPERATION(point_along), .steps = OPERAND(1), .place = OPERAND(2), .length = 3, .needs = NEEDS_SUBJECTS },
    { 0xD8, OPERATION(flicker), .radius = OPERAND(1), .period = OPERAND(2), .frames = OPERAND(3), .length = 4, .needs = NEEDS_SUBJECTS });
