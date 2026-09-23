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
#include "field/layers.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

enum { QUARTER_TURN = 0x40, HALF_TURN = 0x80 };

/* field_object_blocking */
enum { MINDS_THE_MAP = 0x01, MINDS_OBJECTS = 0x02 };
/* field_object_solid */
enum { SOLID = 0x01 };
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
 * words, the map's place masked as the map's size says. */
static MapPlace tile_ahead(const GameState *g, FieldObject obj, uint16_t across, uint16_t down)
{
    uint16_t x = (uint16_t)(position_x(g, obj) + across);
    uint16_t y = (uint16_t)(position_y(g, obj) + down);
    return map_place_at((uint16_t)(((y & 0xFF00) | x >> 8) & field_map_index_mask(g)));
}

/* The priority of the object's sprites' two halves: from the map's tile, or
 * its own. */
static void set_sprite_priority(GameState *g, FieldObject obj, TileAttributes ahead)
{
    uint8_t mode = field_object_priority_mode(g, obj);
    uint8_t upper, lower;
    if (mode & PRIORITY_FROM_MAP) {
        upper = (uint8_t)(((ahead.upper & MAP_PRIORITY_BIT) >> 2) | BASE_PRIORITY);
        lower = (uint8_t)(((ahead.lower & MAP_PRIORITY_BIT) >> 2) | BASE_PRIORITY);
    } else {
        upper = (uint8_t)((mode << 4) & PRIORITY_BITS);
        lower = (uint8_t)(mode & PRIORITY_BITS);
    }
    set_field_object_sprite_priority(g, obj, (uint16_t)(lower << 8 | upper));
}

static bool moves(Velocity v) { return v.x != 0 || v.y != 0; }

/* How far across a look ahead goes: the try's horizontal step as a word. The
 * original keeps each step's sign in a byte beside it, and sets the
 * horizontal one only where the step isn't 0 or on the first try; so a retry
 * with no horizontal step looks with the first try's sign, and where the
 * first try went left, looks a tile to the left of where the object stands.
 * The port looks where the original does. */
static uint16_t retry_look_across(Velocity first, Velocity retry)
{
    if (retry.x != 0)
        return sign_extended(retry.x);
    return first.x < 0 ? 0xFF00 : 0x0000;
}

/* What looking ahead finds: where it looked across, and that tile's
 * attributes. */
typedef struct {
    uint16_t x;
    TileAttributes tile;
} Look;

static Look look_ahead(const GameState *g, FieldObject obj, uint16_t across, int8_t down)
{
    return (Look){ (uint16_t)(position_x(g, obj) + across),
                   map_tile_attributes(g, tile_ahead(g, obj, across, sign_extended(down))) };
}

static uint8_t sign_byte(int8_t v) { return v < 0 ? 0xFF : 0x00; }

/* For the original's code and shadow mode (field/scratch.h): a start leaves
 * the steps it set ($DB, $DD), the sign bytes it kept beside them ($DE the
 * horizontal one, as retry_look_across has it; $DC the vertical one of the
 * last try that moved), where it last looked across ($DF) and that tile's
 * attributes ($E1, $E5), and the object's priority mode ($D9) where the
 * object keeps its own. */
static void leave_start(GameState *g, FieldObject obj, Velocity steps, Velocity first, const Velocity *retry, const Look *look,
                        bool own_priority)
{
    original_cell8(g, 0xDB, (uint8_t)steps.y);
    original_cell8(g, 0xDD, (uint8_t)steps.x);
    original_cell8(g, 0xDE, sign_byte(retry && retry->x != 0 ? retry->x : first.x));
    if (retry && moves(*retry))
        original_cell8(g, 0xDC, sign_byte(retry->y));
    else if (moves(first))
        original_cell8(g, 0xDC, sign_byte(first.y));
    if (look) {
        original_cell16(g, 0xDF, look->x);
        original_cell8(g, 0xE1, look->tile.upper);
        original_cell8(g, 0xE5, look->tile.lower);
    }
    if (own_priority)
        original_cell8(g, 0xD9, field_object_priority_mode(g, obj));
}

// $C0:ACFD
StepStart object_start_step(GameState *g, FieldObject obj, Direction direction)
{
    StepStart r = { START_STILL, direction };
    stand_still(g, obj);
    Velocity first = velocity_for(g, obj, direction), retry = { 0, 0 }, steps = first;
    bool retried = false, looked = false;
    Look look = { 0, { 0, 0, 0 } };
    if (moves(first)) {
        r.outcome = START_MOVING;
        look = look_ahead(g, obj, sign_extended(first.x), first.y);
        looked = true;
        if ((field_object_blocking(g, obj) & MINDS_THE_MAP) && (look.tile.lower & TILE_BLOCKED)) {
            uint8_t index = (uint8_t)(field_random_index(g) + 1);
            set_field_random_index(g, index);
            r.direction = field_random_direction(g, index);
            set_field_direction(g, (uint16_t)((field_direction(g) & 0xFF00) | r.direction));
            retried = true;
            retry = steps = velocity_for(g, obj, r.direction);
            r.outcome = START_TURNED_STILL;
            if (moves(retry)) {
                r.outcome = START_TURNED_MOVING;
                look = look_ahead(g, obj, retry_look_across(first, retry), retry.y);
                if (look.tile.lower & TILE_BLOCKED) {
                    r.outcome = START_BLOCKED;
                    steps = (Velocity){ 0, 0 };
                }
            }
        }
    }
    bool moving = r.outcome == START_MOVING || r.outcome == START_TURNED_MOVING;
    if (moving)
        set_sprite_priority(g, obj, look.tile);
    set_step_x(g, obj, steps.x);
    set_step_y(g, obj, steps.y);
    leave_multiplier(g, obj, r.direction);
    leave_start(g, obj, steps, first, retried ? &retry : NULL, looked ? &look : NULL,
                moving && !(field_object_priority_mode(g, obj) & PRIORITY_FROM_MAP));
    return r;
}

/* ---- Objects in the way ------------------------------------------------------------------- */

enum { REACH = 0x0100 };   /* sixteenths of a pixel */

/* How far along the way it looks the other object is from this one. The
 * original measures an object in a slot before this one's a sixteenth of a
 * pixel nearer (it subtracts with the carry its last comparison left), and a
 * distance behind as the one's complement, a sixteenth short. So two objects
 * exactly a tile apart block each other one way and not the other, by which
 * of them came first. False if the other object isn't on that side. */
static bool distance_ahead(uint16_t other, uint16_t own, bool earlier_slot_measured_nearer, bool looking_back,
                           uint16_t *distance)
{
    uint16_t d = (uint16_t)(other - own - earlier_slot_measured_nearer);
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
    return object_marked_in_view(g, other) && !object_disabled(g, other) && other != obj &&
           (field_object_solid(g, other) & SOLID);
}

/* The search starts at the slot after the last object, which with 64 objects
 * is past the tables, and with a count past 64 (the original takes it modulo
 * 128) more of them. There the original reads on past each table into the one
 * after it in its memory, at the same place: the in-view flags into the
 * objects' priority modes, the roles into the scripts' positions, and the
 * solidity into the accelerations. No map has 64 objects; the port reads the
 * same bytes, by their own names, so that shadow mode and fuzzing see what
 * the original sees. A build without the original's memory has no slots
 * there (docs/TODO.md). */
static bool stray_slot_can_block(const GameState *g, unsigned slot)
{
    unsigned past = slot - FIELD_OBJECTS;
    bool in_view = field_object_priority_mode(g, past) != 0;
    bool disabled = (event_object_script_position(g, past) & ROLE_DISABLED_BIT) != 0;
    return in_view && !disabled && (field_object_acceleration(g, past) & SOLID);
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

/* For the entry's wrapper (field/scratch.h, "What a wrapper can't work out"):
 * the last distance the check measured, ahead or across, whose high byte the
 * original leaves in A. Written only by record_measured, read only by
 * field_object_in_the_way. */
static struct {
    bool measured;
    uint16_t last_distance;
} in_the_way_leaves;

static void record_measured(uint16_t distance)
{
    in_the_way_leaves.measured = true;
    in_the_way_leaves.last_distance = distance;
}

/* For the original's code and shadow mode: the check stores the object it
 * finds as a byte into the word field_target_object, whose high byte it
 * clears first, whether or not it looks. */
static void leave_target_as_byte(GameState *g)
{
    set_field_target_object(g, (uint16_t)(field_target_object(g) & 0x00FF));
}

static bool looks_for_objects(const GameState *g, FieldObject obj)
{
    return object_marked_in_view(g, obj) && (field_object_blocking(g, obj) & MINDS_OBJECTS);
}

// $C0:5B95
/* The objects are looked at from the one past the last down to the second
 * (the first is the party's leader), until one is found in the way. The
 * object found (or the last looked at) is kept in field_target_object, where
 * the scripts' movement reads it. */
ObjectsAhead object_in_the_way(GameState *g, FieldObject obj)
{
    ObjectsAhead r = { false, 0 };
    leave_target_as_byte(g);
    if (!looks_for_objects(g, obj))
        return r;
    uint8_t faces = field_object_facing(g, obj);
    Facing facing = (Facing)(faces & 0x03);
    FieldPosition at = object_position(g, obj);
    leave_blocking_check(g, faces, at);
    bool vertical = facing == FACING_UP || facing == FACING_DOWN;
    bool looking_back = facing == FACING_UP || facing == FACING_LEFT;
    unsigned slot = event_object_count(g) % SLOTS_IN_A_BYTE;
    do {
        bool blocks = slot < FIELD_OBJECTS ? can_block(g, (FieldObject)slot, obj) : stray_slot_can_block(g, slot);
        if (blocks) {
            FieldObject other = (FieldObject)slot;
            set_field_target_object(g, field_object_link(other));
            FieldPosition there = object_position(g, other);
            uint16_t along;
            bool ahead = distance_ahead(vertical ? there.y : there.x, vertical ? at.y : at.x, other < obj,
                                        looking_back, &along);
            record_measured(along);
            if (ahead && along < REACH) {
                uint16_t across = distance_across(vertical ? there.x : there.y, vertical ? at.x : at.y);
                record_measured(across);
                if (across < REACH)
                    return (ObjectsAhead){ true, other };
            }
        }
        slot = (slot - 1) & (SLOTS_IN_A_BYTE - 1);
    } while (slot != 0);
    return r;
}

/* ---- Lining up ---------------------------------------------------------------------------- */

enum { ON_TILE = 0xFFF0 };   /* a position rounded to the pixel it is on */
enum { TILE_MIDDLE_X = 0x80, TILE_BOTTOM_Y = 0xF0, FINE_PIXEL = 0xF0 };
enum { ONE_PIXEL = 0x10 };   /* a pixel, as a step */

/* A step toward something on one axis: a pixel forward, or back. */
static int8_t step_toward(bool forward) { return forward ? ONE_PIXEL : -ONE_PIXEL; }

/* Whether a move's steps may take the object out of its tile: never, once
 * (the move's first crossing ends it), or always. The original keeps it in a
 * byte: 0, a positive number, or a negative one. */
typedef enum { STAY_IN_TILE, CROSS_ONCE, CROSS_ALWAYS } TileCrossing;
enum { STORED_STAY = 0, STORED_CROSS_ONCE = 1, STORED_CROSS_ALWAYS = 0x80 };

static TileCrossing tile_crossing(const GameState *g, FieldObject obj)
{
    uint8_t stored = field_object_move_state_tile_mode(g, obj);
    if (stored == STORED_STAY)
        return STAY_IN_TILE;
    return (int8_t)stored < 0 ? CROSS_ALWAYS : CROSS_ONCE;
}

static void set_tile_crossing(GameState *g, FieldObject obj, TileCrossing crossing)
{
    static const uint8_t stored[] = { [STAY_IN_TILE] = STORED_STAY, [CROSS_ONCE] = STORED_CROSS_ONCE,
                                      [CROSS_ALWAYS] = STORED_CROSS_ALWAYS };
    set_field_object_move_state_tile_mode(g, obj, stored[crossing]);
}

/* A move of one step, which may cross into the next tile (or not). */
static void one_step(GameState *g, FieldObject obj, bool crossing)
{
    set_field_object_steps_left(g, obj, 1);
    if (crossing)
        set_tile_crossing(g, obj, CROSS_ONCE);
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
 * step goes no further and the move ends. */
static void step_axis(GameState *g, FieldObject obj, bool vertical, int8_t step, bool stay_in_tile)
{
    uint8_t fine = vertical ? field_object_y_fine(g, obj) : field_object_x_fine(g, obj);
    int16_t moved = (int16_t)(fine + step);
    bool leaves = moved < 0 || moved > 0xFF;
    if (leaves && stay_in_tile) {
        set_field_object_steps_left(g, obj, 0);
        return;
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

// $C0:AA07
StepKind object_take_step(GameState *g, FieldObject obj)
{
    if (!count_step(g, obj))
        return STEP_NONE;
    if (field_object_accelerating(g, obj) != 0) {
        fall(g, obj);
        return STEP_FALLING;
    }
    TileCrossing crossing = tile_crossing(g, obj);
    bool stay = crossing == STAY_IN_TILE;
    if (crossing == CROSS_ONCE)
        set_tile_crossing(g, obj, STAY_IN_TILE);
    int8_t x = step_x(g, obj), y = step_y(g, obj);
    step_axis(g, obj, false, x, stay);
    step_axis(g, obj, true, y, stay);
    leave_backward_step(g, backward(backward(0, x), y));
    return STEP_TAKEN;
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
    StepStart r = object_start_step(g, obj, (Direction)field_direction(g));
    s->cpu.y = r.outcome != START_STILL ? field_current_object(g) : (uint16_t)(s->cpu.y & 0x00FF);
    bool no_step = r.outcome == START_STILL || r.outcome == START_TURNED_STILL;
    /* The vertical step it last looked ahead with: the one it set, or where
     * it stayed blocked, its last direction's. */
    int8_t looked_ahead_y = r.outcome == START_BLOCKED ? velocity_for(g, obj, r.direction).y : step_y(g, obj);
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
    bool looked = looks_for_objects(g, obj);
    in_the_way_leaves.measured = false;
    ObjectsAhead r = object_in_the_way(g, obj);
    if (looked)
        original_cell16(g, 0xC7, s->cpu.y);
    if (in_the_way_leaves.measured)
        s->cpu.a = (uint16_t)((in_the_way_leaves.last_distance & 0xFF00) | (s->cpu.a & 0x00FF));
    else if (looked)
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
 * vertical step on the fine position it had before the step (which a step
 * held at the tile's edge leaves as it was). */
static bool overflow_after_step(const GameState *g, FieldObject obj, StepKind kind, uint8_t fine_y_before, bool free,
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
    return y < 0 ? field_subtract_overflows8(fine_y_before, (uint8_t)-y) : field_add_overflows8((uint8_t)y, fine_y_before);
}

/* The current object (8-bit X) takes its step: a party member's freely. */
void field_move_current_object(Snes *s, bool party_member)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    uint8_t fine_y_before = field_object_y_fine(g, obj);
    StepKind kind = party_member ? party_member_take_step(g, obj) : object_take_step(g, obj);
    m_set_flag(s, CPU_FLAG_V, overflow_after_step(g, obj, kind, fine_y_before, party_member, m_flag(s, CPU_FLAG_V)));
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
