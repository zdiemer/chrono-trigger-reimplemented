/* Field objects: how they move.
 *
 * An object moves in moves of so many steps: a script (or the party's
 * following) sets it heading in a direction at its speed, and the object
 * update takes a step a frame until the steps run out. A direction is a byte
 * to a turn; the velocity on each axis is the speed times the direction's
 * factor on that axis, from a table in the ROM. A step keeps the object inside
 * its tile unless the move lets it cross (field_object_move_state_tile_mode:
 * never, once, or always); the party's members step freely. An object that
 * falls accelerates downward instead.
 *
 * Where an object minds the map, starting a step looks at the tile ahead: a
 * blocked tile turns it in a random direction once, and stops it if that is
 * blocked too; and an object that takes its sprite priority from the map
 * (behind a pillar, in front of a railing) takes it from that tile. An object
 * that minds other objects can ask whether one is in the way.
 *
 * An object faces up, down, left or right (Facing); a move's direction is
 * separate from where it faces. */
#include "field/field.h"
#include "field/object.h"

#include "cpu_io.h"
#include "field/bridge.h"
#include "field/field_data.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

enum { QUARTER_TURN = 0x40, HALF_TURN = 0x80 };

/* field_object_blocking */
enum { MINDS_THE_MAP = 0x01, MINDS_OBJECTS = 0x02 };
/* field_object_solid */
enum { SOLID = 0x01 };
/* A tile's second attribute: blocked */
enum { TILE_BLOCKED = 0x80 };
/* field_object_priority_mode: bit 7 takes the priority from the map, else
 * its bits give the two halves' priority */
enum { PRIORITY_FROM_MAP = 0x80, PRIORITY_BITS = 0x30, MAP_PRIORITY_BIT = 0x40, BASE_PRIORITY = 0x20 };

/* ---- Positions --------------------------------------------------------------------------- */

static uint16_t position_x(const GameState *g, FieldObject obj)
{
    uint8_t fine = field_object_x_fine(g, obj);
    return (uint16_t)(field_object_x_tile(g, obj) << 8 | fine);
}

static uint16_t position_y(const GameState *g, FieldObject obj)
{
    uint8_t fine = field_object_y_fine(g, obj);
    return (uint16_t)(field_object_y_tile(g, obj) << 8 | fine);
}

static void set_position_x(GameState *g, FieldObject obj, uint16_t x)
{
    set_field_object_x_fine(g, obj, (uint8_t)x);
    set_field_object_x_tile(g, obj, (uint8_t)(x >> 8));
}

static void set_position_y(GameState *g, FieldObject obj, uint16_t y)
{
    set_field_object_y_fine(g, obj, (uint8_t)y);
    set_field_object_y_tile(g, obj, (uint8_t)(y >> 8));
}

FieldPosition object_position(const GameState *g, FieldObject obj)
{
    return (FieldPosition){ position_x(g, obj), position_y(g, obj) };
}

/* A step on an axis, as the velocity keeps it: the step in the low byte, and
 * where it was set with one, its sign in the high byte. */
static int8_t step_x(const GameState *g, FieldObject obj) { return (int8_t)field_object_velocity_x(g, obj); }
static int8_t step_y(const GameState *g, FieldObject obj) { return (int8_t)field_object_velocity_y(g, obj); }

static void set_step_x(GameState *g, FieldObject obj, int8_t v)
{
    set_field_object_velocity_x(g, obj, (uint16_t)((field_object_velocity_x(g, obj) & 0xFF00) | (uint8_t)v));
}

static void set_step_y(GameState *g, FieldObject obj, int8_t v)
{
    set_field_object_velocity_y(g, obj, (uint16_t)((field_object_velocity_y(g, obj) & 0xFF00) | (uint8_t)v));
}

static uint16_t sign_extended(int8_t v) { return (uint16_t)(int16_t)v; }

static void stand_still(GameState *g, FieldObject obj)
{
    set_step_x(g, obj, 0);
    set_step_y(g, obj, 0);
}

/* ---- Directions -------------------------------------------------------------------------- */

/* The quarter of the turn a move from one tile toward another is in, and the
 * two distances turned into it: across it, and along it. */
typedef struct {
    Direction quarter;
    uint8_t across, along;
} Quarter;

static Quarter quarter_between(MapTile from, MapTile to)
{
    bool down = to.y >= from.y, right = to.x >= from.x;
    uint8_t dy = down ? (uint8_t)(to.y - from.y) : (uint8_t)(from.y - to.y);
    uint8_t dx = right ? (uint8_t)(to.x - from.x) : (uint8_t)(from.x - to.x);
    if (down)
        return right ? (Quarter){ DIRECTION_DOWN, dx, dy } : (Quarter){ DIRECTION_LEFT, dy, dx };
    return right ? (Quarter){ DIRECTION_RIGHT, dy, dx } : (Quarter){ DIRECTION_UP, dx, dy };
}

/* The distances scaled down to five bits for the angle table, keeping their
 * ratio: by the first power of two that brings both under 32, as far as 4
 * (a distance of 128 or more is taken as its low seven bits). */
static unsigned angle_scale(Quarter q)
{
    if ((q.across | q.along) & 0x40)
        return 2;
    return ((q.across | q.along) & 0x20) ? 1 : 0;
}

enum { DISTANCE_BITS = 0x1F };

// $C0:ABA2
/* The direction from one tile toward another: the quarter it is in, and the
 * angle within it the ROM's table gives for the two distances. */
Direction direction_between(const GameState *g, MapTile from, MapTile to)
{
    Quarter q = quarter_between(from, to);
    unsigned scale = angle_scale(q);
    uint16_t index = (uint16_t)(((q.along >> scale) & DISTANCE_BITS) << 5 | ((q.across >> scale) & DISTANCE_BITS));
    return (Direction)(field_quadrant_angle(g, index) + q.quarter);
}

/* ---- Velocity ----------------------------------------------------------------------------- */

/* The magnitude of a direction's factor on one axis (the ROM's table holds
 * it negated in the half of the turn where the axis's step is negative). */
static uint8_t factor_magnitude(const GameState *g, Direction direction)
{
    uint8_t factor = field_direction_factor(g, direction);
    return direction >= HALF_TURN ? (uint8_t)-factor : factor;
}

/* A direction's step on one axis at a speed: the speed times the factor, in
 * 1/256, signed by the half of the turn it is in. */
static int8_t axis_step(const GameState *g, uint8_t speed, Direction direction)
{
    uint8_t step = (uint8_t)((speed * factor_magnitude(g, direction)) >> 8);
    return (int8_t)(direction >= HALF_TURN ? -step : step);
}

/* The two axes' steps: down from the direction, across from a quarter turn
 * on. */
typedef struct {
    int8_t x, y;
} Velocity;

static Velocity velocity_for(const GameState *g, FieldObject obj, Direction direction)
{
    uint8_t speed = field_object_speed(g, obj);
    return (Velocity){ axis_step(g, speed, (Direction)(direction + QUARTER_TURN)), axis_step(g, speed, direction) };
}

/* For the original's code and shadow mode: the original multiplies on the
 * hardware multiplier, whose registers keep its last product, the speed by
 * the across factor of the last direction it worked out. */
static void leave_multiplier(GameState *g, FieldObject obj, Direction last)
{
    hw_set_multiplicand(g, field_object_speed(g, obj));
    hw_multiply_by(g, factor_magnitude(g, (Direction)(last + QUARTER_TURN)));
}

/* For the original's code and shadow mode (field/scratch.h): setting a
 * velocity keeps its vertical step at $DB. */
static void leave_step_y(GameState *g, int8_t y) { original_cell8(g, 0xDB, (uint8_t)y); }

// $C0:AC69
void object_head(GameState *g, FieldObject obj, Direction direction)
{
    stand_still(g, obj);
    Velocity v = velocity_for(g, obj, direction);
    leave_multiplier(g, obj, direction);
    leave_step_y(g, v.y);
    set_field_object_velocity_y(g, obj, sign_extended(v.y));
    set_field_object_velocity_x(g, obj, sign_extended(v.x));
}

/* ---- Starting a step --------------------------------------------------------------------- */

/* The tile a step would take the object onto: the position plus the steps as
 * words. */
static uint16_t tile_ahead(const GameState *g, FieldObject obj, uint16_t across, uint16_t down)
{
    uint16_t x = (uint16_t)(position_x(g, obj) + across);
    uint16_t y = (uint16_t)(position_y(g, obj) + down);
    return (uint16_t)((y & 0xFF00) | x >> 8) & field_map_index_mask(g);
}

/* A tile's two attribute bytes: the first's bit 6 the priority of an object's
 * upper half there, the second's its lower half's and bit 7 blocked. */
typedef struct {
    uint8_t first, second;
} TileAttributes;

static TileAttributes tile_attributes(const GameState *g, uint16_t tile)
{
    return (TileAttributes){ field_tile_attributes_byte(g, tile), field_tile_attributes_byte(g, tile + 0x40) };
}

/* The priority of the object's sprites' two halves: from the map's tile, or
 * its own. */
static void set_sprite_priority(GameState *g, FieldObject obj, TileAttributes ahead)
{
    uint8_t mode = field_object_priority_mode(g, obj);
    uint8_t upper, lower;
    if (mode & PRIORITY_FROM_MAP) {
        upper = (uint8_t)(((ahead.first & MAP_PRIORITY_BIT) >> 2) | BASE_PRIORITY);
        lower = (uint8_t)(((ahead.second & MAP_PRIORITY_BIT) >> 2) | BASE_PRIORITY);
    } else {
        upper = (uint8_t)((mode << 4) & PRIORITY_BITS);
        lower = (uint8_t)(mode & PRIORITY_BITS);
    }
    set_field_object_sprite_priority(g, obj, (uint16_t)(lower << 8 | upper));
}

/* A start's steps as the original keeps them, which the look ahead adds as
 * words: each step with a sign byte, the horizontal one's set only where
 * that step isn't 0 or on the first try. So a second try with no horizontal
 * step keeps the first try's sign, and where that went left, looks a tile to
 * the left of the object: the port looks where the original does. */
typedef struct {
    Velocity v;
    uint8_t sign_x, sign_y;
    bool sign_x_set, sign_y_set;
} KeptSteps;

/* Note a start's steps; false if it doesn't move. */
static bool keep_steps(KeptSteps *k, Velocity v, bool first_try)
{
    k->v = v;
    if (v.x != 0 || first_try) {
        k->sign_x = v.x < 0 ? 0xFF : 0;
        k->sign_x_set = true;
    }
    if (v.x == 0 && v.y == 0)
        return false;
    k->sign_y = v.y < 0 ? 0xFF : 0;
    k->sign_y_set = true;
    return true;
}

static uint16_t kept_across(const KeptSteps *k) { return (uint16_t)(k->sign_x << 8 | (uint8_t)k->v.x); }
static uint16_t kept_down(const KeptSteps *k) { return (uint16_t)(k->sign_y << 8 | (uint8_t)k->v.y); }

/* What starting a step leaves for the original's code and shadow mode
 * (field/scratch.h): the last steps it worked out with their signs
 * ($DB-$DE), the position ahead it last looked at ($DF) with that tile's
 * attributes ($E1, $E5), and the object's priority mode where it set the
 * priority from it ($D9). */
typedef struct {
    bool looked_ahead, own_priority;
    int8_t looked_ahead_y;
    uint16_t x_ahead;
    TileAttributes ahead;
    uint8_t priority_mode;
} StartLeftovers;

static void leave_start(GameState *g, const KeptSteps *k, const StartLeftovers *l)
{
    original_cell8(g, 0xDB, (uint8_t)k->v.y);
    original_cell8(g, 0xDD, (uint8_t)k->v.x);
    if (k->sign_x_set)
        original_cell8(g, 0xDE, k->sign_x);
    if (k->sign_y_set)
        original_cell8(g, 0xDC, k->sign_y);
    if (l->looked_ahead) {
        original_cell16(g, 0xDF, l->x_ahead);
        original_cell8(g, 0xE1, l->ahead.first);
        original_cell8(g, 0xE5, l->ahead.second);
    }
    if (l->own_priority)
        original_cell8(g, 0xD9, l->priority_mode);
}

/* Look at the tile the kept steps lead onto. */
static TileAttributes look_ahead(const GameState *g, FieldObject obj, const KeptSteps *k, StartLeftovers *l)
{
    l->looked_ahead = true;
    l->looked_ahead_y = k->v.y;
    l->x_ahead = (uint16_t)(position_x(g, obj) + kept_across(k));
    l->ahead = tile_attributes(g, tile_ahead(g, obj, kept_across(k), kept_down(k)));
    return l->ahead;
}

/* The start, and what it leaves for the entry's wrapper: the vertical step it
 * last looked ahead with, whose sum the original's callers see. */
static StepStart start_step(GameState *g, FieldObject obj, Direction direction, int8_t *looked_ahead_y)
{
    KeptSteps k = { 0 };
    StartLeftovers l = { 0 };
    StepStart r = { START_STILL, direction };
    stand_still(g, obj);
    if (keep_steps(&k, velocity_for(g, obj, direction), true)) {
        r.outcome = START_MOVING;
        TileAttributes ahead = look_ahead(g, obj, &k, &l);
        if ((field_object_blocking(g, obj) & MINDS_THE_MAP) && (ahead.second & TILE_BLOCKED)) {
            uint8_t index = (uint8_t)(field_random_index(g) + 1);
            set_field_random_index(g, index);
            r.direction = field_random_direction(g, index);
            set_field_direction(g, (uint16_t)((field_direction(g) & 0xFF00) | r.direction));
            r.outcome = START_TURNED_STILL;
            if (keep_steps(&k, velocity_for(g, obj, r.direction), false)) {
                r.outcome = START_TURNED_MOVING;
                if (look_ahead(g, obj, &k, &l).second & TILE_BLOCKED) {
                    r.outcome = START_BLOCKED;
                    k.v = (Velocity){ 0, 0 };
                }
            }
        }
        if (r.outcome == START_MOVING || r.outcome == START_TURNED_MOVING) {
            set_sprite_priority(g, obj, l.ahead);
            l.priority_mode = field_object_priority_mode(g, obj);
            l.own_priority = !(l.priority_mode & PRIORITY_FROM_MAP);
        }
    }
    set_step_x(g, obj, k.v.x);
    set_step_y(g, obj, k.v.y);
    leave_multiplier(g, obj, r.direction);
    leave_start(g, &k, &l);
    *looked_ahead_y = l.looked_ahead_y;
    return r;
}

// $C0:ACFD
StepStart object_start_step(GameState *g, FieldObject obj, Direction direction)
{
    int8_t looked_ahead_y;
    return start_step(g, obj, direction, &looked_ahead_y);
}

/* ---- Objects in the way ------------------------------------------------------------------- */

enum { REACH = 0x0100 };   /* sixteenths of a pixel */

/* How far along the way it looks the other object is from this one. The
 * original subtracts with the carry its last comparison left, so an object in
 * a slot before this one's is measured a sixteenth nearer; and it takes a
 * distance behind as the one's complement, a sixteenth short. The port
 * measures as it does. False if the other object isn't on that side. */
static bool distance_ahead(uint16_t other, uint16_t own, bool borrow, bool looking_back, uint16_t *distance)
{
    uint16_t d = (uint16_t)(other - own - borrow);
    bool behind = (int16_t)d < 0;
    *distance = behind && looking_back ? (uint16_t)~d : d;
    return behind == looking_back;
}

/* How far across the other object is, either way, measured as the original
 * does: a sixteenth nearer, and one's complement behind. */
static uint16_t distance_across(uint16_t other, uint16_t own)
{
    uint16_t d = (uint16_t)(other - own - 1);
    return (int16_t)d < 0 ? (uint16_t)~d : d;
}

static bool can_block(const GameState *g, FieldObject other, FieldObject obj)
{
    return field_object_in_view(g, other) != OBJECT_OUT_OF_VIEW && !object_disabled(g, other) && other != obj &&
           (field_object_solid(g, other) & SOLID);
}

/* For the original's code and shadow mode (field/scratch.h): the blocking
 * check keeps the facing doubled ($D9) and the object's position ($DB, $DD);
 * the entry keeps the caller's Y ($C7). */
static void leave_blocking_check(GameState *g, uint8_t facing, FieldPosition at)
{
    original_cell8(g, 0xD9, (uint8_t)(facing * 2));
    original_cell16(g, 0xDB, at.x);
    original_cell16(g, 0xDD, at.y);
}

enum { SLOTS_IN_A_BYTE = 0x80 };   /* an object count doubled wraps round a byte */

/* What the check leaves for the entry's wrapper: whether it looked, and the
 * last distance it measured (ahead, or across where the other object was
 * near enough ahead), whose high byte the original's callers see. */
typedef struct {
    bool looked, measured;
    uint16_t last_distance;
} AheadTrace;

/* The objects are looked at from the one past the last down to the second
 * (the first is the party's leader), until one is found in the way. The
 * object found (or the last looked at) is kept in field_target_object, where
 * the scripts' movement reads it. */
static ObjectsAhead objects_in_the_way(GameState *g, FieldObject obj, AheadTrace *t)
{
    ObjectsAhead r = { false, 0 };
    *t = (AheadTrace){ false, false, 0 };
    /* The object found is a link, a byte; the word's high byte is cleared
     * whether or not it looks. */
    uint16_t target = field_target_object(g);
    set_field_target_object(g, (uint16_t)(target & 0x00FF));
    if (field_object_in_view(g, obj) == OBJECT_OUT_OF_VIEW || !(field_object_blocking(g, obj) & MINDS_OBJECTS))
        return r;
    t->looked = true;
    uint8_t faces = field_object_facing(g, obj);
    Facing facing = (Facing)(faces & 0x03);
    FieldPosition at = object_position(g, obj);
    leave_blocking_check(g, faces, at);
    bool vertical = facing == FACING_UP || facing == FACING_DOWN;
    bool looking_back = facing == FACING_UP || facing == FACING_LEFT;
    FieldObject other = (FieldObject)(event_object_count(g) % SLOTS_IN_A_BYTE);
    do {
        if (can_block(g, other, obj)) {
            set_field_target_object(g, field_object_link(other));
            FieldPosition there = object_position(g, other);
            uint16_t along;
            t->measured = true;
            bool ahead = distance_ahead(vertical ? there.y : there.x, vertical ? at.y : at.x, other < obj,
                                        looking_back, &along);
            t->last_distance = along;
            if (ahead && along < REACH) {
                t->last_distance = distance_across(vertical ? there.x : there.y, vertical ? at.x : at.y);
                if (t->last_distance < REACH)
                    return (ObjectsAhead){ true, other };
            }
        }
        other = (FieldObject)((other - 1) & (SLOTS_IN_A_BYTE - 1));
    } while (other != 0);
    return r;
}

// $C0:5B95
ObjectsAhead object_in_the_way(GameState *g, FieldObject obj)
{
    AheadTrace t;
    return objects_in_the_way(g, obj, &t);
}

/* ---- Lining up ---------------------------------------------------------------------------- */

enum { ON_TILE = 0xFFF0 };   /* a position rounded to the pixel it is on */
enum { TILE_MIDDLE_X = 0x80, TILE_BOTTOM_Y = 0xF0, FINE_PIXEL = 0xF0 };
enum { ONE_PIXEL = 0x10 };   /* a pixel, as a step */

/* A step toward something on one axis: a pixel forward, or back. */
static int8_t step_toward(bool forward) { return forward ? ONE_PIXEL : -ONE_PIXEL; }

/* A move of one step, which may cross into the next tile (or not). */
static void one_step(GameState *g, FieldObject obj, bool crossing)
{
    set_field_object_steps_left(g, obj, 1);
    if (crossing)
        set_field_object_move_state_tile_mode(g, obj, 1);
}

// $C0:305D
/* Toward the middle of its tile across, and its bottom down. */
bool object_line_up_with_tile(GameState *g, FieldObject obj)
{
    stand_still(g, obj);
    uint8_t x = (uint8_t)(field_object_x_fine(g, obj) & FINE_PIXEL);
    bool x_lined_up = x == TILE_MIDDLE_X;
    if (!x_lined_up)
        set_step_x(g, obj, step_toward(x < TILE_MIDDLE_X));
    uint8_t y = (uint8_t)(field_object_y_fine(g, obj) & FINE_PIXEL);
    if (y == TILE_BOTTOM_Y && x_lined_up)
        return false;
    if (y != TILE_BOTTOM_Y)
        set_step_y(g, obj, step_toward(y < TILE_BOTTOM_Y));
    one_step(g, obj, false);
    return true;
}

/* For the original's code and shadow mode (field/scratch.h): lining up with
 * an object keeps its own position, last the vertical one, at $D9. */
static void leave_own_position(GameState *g, uint16_t y) { original_cell16(g, 0xD9, y); }

// $C0:30B3
/* Toward the other object's pixel, a step on each axis where they differ. */
bool object_line_up_with(GameState *g, FieldObject obj, FieldObject target)
{
    stand_still(g, obj);
    uint16_t own_x = position_x(g, obj) & ON_TILE, other_x = position_x(g, target) & ON_TILE;
    uint16_t own_y = position_y(g, obj) & ON_TILE, other_y = position_y(g, target) & ON_TILE;
    leave_own_position(g, own_y);
    if (other_x == own_x && other_y == own_y)
        return false;
    if (other_x != own_x)
        set_step_x(g, obj, step_toward(other_x > own_x));
    if (other_y != own_y)
        set_step_y(g, obj, step_toward(other_y > own_y));
    one_step(g, obj, true);
    return true;
}

/* ---- Taking steps ------------------------------------------------------------------------- */

/* field_object_move_state_tile_mode: 0 stays in the tile, a negative mode
 * always crosses, any other crosses once. */
enum { STAY_IN_TILE = 0 };
static bool crosses_always(uint8_t mode) { return (int8_t)mode < 0; }

/* For the original's code and shadow mode (field/scratch.h): a step backward
 * on an axis keeps its size at $D9, the last one's. */
static void leave_backward_step(GameState *g, int8_t last_backward)
{
    if (last_backward < 0)
        original_cell8(g, 0xD9, (uint8_t)-last_backward);
}

static int8_t backward(int8_t before, int8_t step) { return step < 0 ? step : before; }

/* One axis of a step: the fine position moves by the step, and where it
 * leaves the tile, the tile moves too; or where it must stay in its tile, the
 * step goes no further and the move ends. True if it stayed. */
static bool step_axis(GameState *g, FieldObject obj, bool vertical, int8_t step, bool stay_in_tile)
{
    uint8_t fine = vertical ? field_object_y_fine(g, obj) : field_object_x_fine(g, obj);
    int16_t moved = (int16_t)(fine + step);
    bool leaves = moved < 0 || moved > 0xFF;
    if (leaves && stay_in_tile) {
        set_field_object_steps_left(g, obj, 0);
        return true;
    }
    int8_t tiles = !leaves ? 0 : step < 0 ? -1 : 1;
    if (vertical) {
        set_field_object_y_fine(g, obj, (uint8_t)moved);
        if (leaves)
            set_field_object_y_tile(g, obj, (uint8_t)(field_object_y_tile(g, obj) + tiles));
    } else {
        set_field_object_x_fine(g, obj, (uint8_t)moved);
        if (leaves)
            set_field_object_x_tile(g, obj, (uint8_t)(field_object_x_tile(g, obj) + tiles));
    }
    return false;
}

/* A step off its count: false if there were none left. */
static bool count_step(GameState *g, FieldObject obj)
{
    uint8_t steps = field_object_steps_left(g, obj);
    if (steps == 0)
        return false;
    set_field_object_steps_left(g, obj, (uint8_t)(steps - 1));
    return true;
}

// $C0:AAC7
/* A falling object: its horizontal velocity as a word, and its vertical one,
 * which grows by its acceleration each step. */
static void fall(GameState *g, FieldObject obj)
{
    set_position_x(g, obj, (uint16_t)(position_x(g, obj) + field_object_velocity_x(g, obj)));
    set_field_object_velocity_y(g, obj, (uint16_t)(field_object_velocity_y(g, obj) + field_object_acceleration(g, obj)));
    set_position_y(g, obj, (uint16_t)(position_y(g, obj) + field_object_velocity_y(g, obj)));
}

/* The step, and what it leaves for the entry's wrapper: whether the vertical
 * step was held at the tile's edge, which decides the sum the original's
 * callers see. */
static StepKind take_step(GameState *g, FieldObject obj, bool *held_at_edge_y)
{
    *held_at_edge_y = false;
    if (!count_step(g, obj))
        return STEP_NONE;
    if (field_object_accelerating(g, obj) != 0) {
        fall(g, obj);
        return STEP_FALLING;
    }
    uint8_t mode = field_object_move_state_tile_mode(g, obj);
    bool stay = mode == STAY_IN_TILE;
    if (!stay && !crosses_always(mode))
        set_field_object_move_state_tile_mode(g, obj, STAY_IN_TILE);
    int8_t x = step_x(g, obj), y = step_y(g, obj);
    step_axis(g, obj, false, x, stay);
    *held_at_edge_y = step_axis(g, obj, true, y, stay);
    leave_backward_step(g, backward(backward(0, x), y));
    return STEP_TAKEN;
}

// $C0:AA07
StepKind object_take_step(GameState *g, FieldObject obj)
{
    bool held_at_edge_y;
    return take_step(g, obj, &held_at_edge_y);
}

// $C0:AAFD
StepKind party_member_take_step(GameState *g, FieldObject obj)
{
    if (!count_step(g, obj))
        return STEP_NONE;
    if (field_object_accelerating(g, obj) != 0) {
        fall(g, obj);
        return STEP_FALLING;
    }
    set_position_x(g, obj, (uint16_t)(position_x(g, obj) + sign_extended(step_x(g, obj))));
    set_position_y(g, obj, (uint16_t)(position_y(g, obj) + sign_extended(step_y(g, obj))));
    return STEP_TAKEN;
}

/* ---- Entries ------------------------------------------------------------------------------ */

/* For the original's code and shadow mode (field/scratch.h): the direction's
 * working cells, the angle table index's low part and the two distances. */
static void leave_direction_work(GameState *g, Quarter q)
{
    unsigned scale = angle_scale(q);
    uint16_t across = (uint16_t)((q.across >> scale) & DISTANCE_BITS);
    set_field_direction_work(g, (uint32_t)across | (uint32_t)q.across << 16 | (uint32_t)q.along << 24);
}

/* The direction from field_own_tile to field_target_tile, into
 * field_direction. The overflow of the angle's sum with its quarter is read
 * after. */
void field_direction_to_target(Snes *s)
{
    GameState *g = state_of(s);
    MapTile from = { field_own_tile_x(g), field_own_tile_y(g) };
    MapTile to = { field_target_tile_x(g), field_target_tile_y(g) };
    Quarter q = quarter_between(from, to);
    leave_direction_work(g, q);
    Direction d = direction_between(g, from, to);
    set_field_direction(g, d);
    m_set_flag(s, CPU_FLAG_V, field_add_overflows8((uint8_t)(d - q.quarter), q.quarter));
    m_rts(s);
}

/* The current object heads in field_direction; X is left the object, and
 * Y's high byte cleared (the original runs with 8-bit index registers). */
void field_set_velocity(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    object_head(g, obj, (Direction)field_direction(g));
    s->cpu.x = field_object_link(obj);
    s->cpu.y &= 0x00FF;
    m_rts(s);
}

/* The current object starts a step in field_direction. Y is left the object
 * where it looked ahead, and its high byte cleared where not; the overflow is that of the last sum: the vertical
 * position it last looked ahead at, or where its last direction gave it no
 * step, the quarter turn to that direction's horizontal axis. */
void field_start_step(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    int8_t looked_ahead_y;
    StepStart r = start_step(g, obj, (Direction)field_direction(g), &looked_ahead_y);
    s->cpu.y = r.outcome != START_STILL ? field_current_object(g) : (uint16_t)(s->cpu.y & 0x00FF);
    bool no_step = r.outcome == START_STILL || r.outcome == START_TURNED_STILL;
    m_set_flag(s, CPU_FLAG_V, no_step ? field_add_overflows8(r.direction, QUARTER_TURN)
                                      : field_add_overflows16(position_y(g, obj), sign_extended(looked_ahead_y)));
    m_rts(s);
}

/* Is another object in the way of the current one? The carry says, and the
 * object is left in field_target_object. The original keeps the caller's Y
 * while it looks, and leaves in A's high byte what it measured last (the
 * object's own vertical position where it measured nothing). */
void field_object_in_the_way(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    AheadTrace t;
    ObjectsAhead r = objects_in_the_way(g, obj, &t);
    if (t.looked)
        original_cell16(g, 0xC7, s->cpu.y);
    if (t.measured)
        s->cpu.a = (uint16_t)((t.last_distance & 0xFF00) | (s->cpu.a & 0x00FF));
    else if (t.looked)
        s->cpu.a = (uint16_t)((position_y(g, obj) & 0xFF00) | (s->cpu.a & 0x00FF));
    m_set_flag(s, CPU_FLAG_C, r.blocked);
    m_rts(s);
}

/* The carry says whether the current object still has a step to take. */
void field_line_up_with_object(Snes *s)
{
    GameState *g = state_of(s);
    bool moving = object_line_up_with(g, (FieldObject)field_current_object_slot(s),
                                      field_object_of_link((uint8_t)field_target_object(g)));
    m_set_flag(s, CPU_FLAG_C, moving);
    m_rts(s);
}

/* The same; X is left the object. */
void field_line_up_with_tile(Snes *s)
{
    GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_C, object_line_up_with_tile(g, (FieldObject)field_current_object_slot(s)));
    s->cpu.x = field_current_object(g);
    m_rts(s);
}

/* The overflow of a step's last sum, which the object update's callers read:
 * the vertical position's word where it fell or stepped freely, else the
 * vertical step on the fine position. */
static bool overflow_after_step(const GameState *g, FieldObject obj, StepKind kind, bool held_at_edge_y, bool free,
                                bool entry_overflow)
{
    if (kind == STEP_NONE)
        return entry_overflow;
    int8_t y = step_y(g, obj);
    if (kind == STEP_FALLING) {
        uint16_t v = field_object_velocity_y(g, obj);
        return field_add_overflows16((uint16_t)(position_y(g, obj) - v), v);
    }
    if (free)
        return field_add_overflows16((uint16_t)(position_y(g, obj) - sign_extended(y)), sign_extended(y));
    uint8_t fine = field_object_y_fine(g, obj);
    uint8_t before = held_at_edge_y ? fine : (uint8_t)(fine - y);
    return y < 0 ? field_subtract_overflows8(before, (uint8_t)-y) : field_add_overflows8((uint8_t)y, before);
}

/* The current object (8-bit X) takes its step: a party member's freely. */
void field_move_current_object(Snes *s, bool party_member)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    bool held_at_edge_y = false;
    StepKind kind = party_member ? party_member_take_step(g, obj) : take_step(g, obj, &held_at_edge_y);
    m_set_flag(s, CPU_FLAG_V, overflow_after_step(g, obj, kind, held_at_edge_y, party_member, m_flag(s, CPU_FLAG_V)));
}

void field_move_object(Snes *s)
{
    field_move_current_object(s, false);
    m_rts(s);
}

void field_move_object_unbounded(Snes *s)
{
    field_move_current_object(s, true);
    m_rts(s);
}

/* ---- Guards ------------------------------------------------------------------------------- */

/* Setting a velocity and starting a step work on the object in X until they
 * have its speed, and on the current object after; the game always enters
 * them with the two the same. The current object is read as a word. */
bool field_heads_current_object(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    uint16_t object = field_current_object(state_of(s));
    vars_peek(false);
    return field_names_object(object) && (uint8_t)s->cpu.x == (uint8_t)object;
}

/* The blocking check: the current object faces one of its four ways (the
 * original looks the check up in a table of four). */
bool field_object_checks_its_way(Snes *s)
{
    if (!field_current_object_low_bank(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool faces = (uint8_t)(field_object_facing(g, (FieldObject)field_current_object_slot(s)) << 1) < 8;
    vars_peek(false);
    return faces;
}

/* Lining up with another object: that object is one. */
bool field_lines_up_with_an_object(Snes *s)
{
    if (!field_current_object_low_bank(s))
        return false;
    vars_peek(true);
    uint16_t target = field_target_object(state_of(s));
    vars_peek(false);
    return field_names_object(target);
}
