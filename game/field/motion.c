/* Field objects: how they move.
 *
 * An object moves in moves of so many steps: a script (or the party's
 * following) sets it heading in a direction at its speed, and the object
 * update takes a step a frame until the steps run out. A direction is a byte
 * to a turn; the velocity on each axis is the speed times the direction's
 * factor on that axis, from a table in the ROM. A step keeps the object inside
 * its tile unless the move lets it cross (field_object_tile_crossing:
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

#include "charge.h"
#include "cpu_io.h"
#include "field/bridge.h"
#include "field/field_data.h"
#include "field/layers.h"
#include "field/parts.h"
#include "field/scratch.h"
#include "port/vars_ram.h"
#include "vars.h"

enum { HALF_TURN = 0x80 };

/* ---- Positions: the storage ------------------------------------------------------------- */

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

MapTile object_tile(const GameState *g, FieldObject obj)
{
    return (MapTile){ field_object_x_tile(g, obj), field_object_y_tile(g, obj) };
}

// $C0:4C27
void object_place(GameState *g, FieldObject obj, FieldPosition at)
{
    set_position_x(g, obj, at.x);
    set_position_y(g, obj, at.y);
}

enum { TILE_MIDDLE = 0x80, TILE_BOTTOM = 0xFF };

// $C0:4B74
void object_put_on_tile(GameState *g, FieldObject obj, MapTile tile, bool centred)
{
    set_field_object_x_tile(g, obj, tile.x);
    if (centred)
        set_field_object_x_fine(g, obj, TILE_MIDDLE);
    set_field_object_y_tile(g, obj, tile.y);
    set_field_object_y_fine(g, obj, TILE_BOTTOM);
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

Velocity object_velocity(const GameState *g, FieldObject obj) { return (Velocity){ step_x(g, obj), step_y(g, obj) }; }

void object_set_step(GameState *g, FieldObject obj, Velocity step)
{
    set_step_x(g, obj, step.x);
    set_step_y(g, obj, step.y);
}

void object_set_step_past_the_tables(GameState *g, FieldObject slot, Velocity step)
{
    set_field_object_velocity_x_unchecked(g, slot, (uint16_t)((field_object_velocity_x_unchecked(g, slot) & 0xFF00) | (uint8_t)step.x));
    set_field_object_velocity_y_unchecked(g, slot, (uint16_t)((field_object_velocity_y_unchecked(g, slot) & 0xFF00) | (uint8_t)step.y));
}

/* ---- Directions -------------------------------------------------------------------------- */

Quarter field_quarter_between(MapTile from, MapTile to)
{
    bool down = to.y >= from.y, right = to.x >= from.x;
    uint8_t dy = down ? (uint8_t)(to.y - from.y) : (uint8_t)(from.y - to.y);
    uint8_t dx = right ? (uint8_t)(to.x - from.x) : (uint8_t)(from.x - to.x);
    if (down)
        return right ? (Quarter){ DIRECTION_DOWN, dx, dy } : (Quarter){ DIRECTION_LEFT, dy, dx };
    return right ? (Quarter){ DIRECTION_RIGHT, dy, dx } : (Quarter){ DIRECTION_UP, dx, dy };
}

unsigned field_angle_scale(Quarter q)
{
    if ((q.across | q.along) & 0x40)
        return 2;
    return ((q.across | q.along) & 0x20) ? 1 : 0;
}

// $C0:ABA2
/* The direction from one tile toward another: the quarter it is in, and the
 * angle within it the ROM's table gives for the two distances. */
Direction direction_between(const GameState *g, MapTile from, MapTile to)
{
    Quarter q = field_quarter_between(from, to);
    unsigned scale = field_angle_scale(q);
    uint16_t along = (q.along >> scale) & FIELD_DISTANCE_BITS, across = (q.across >> scale) & FIELD_DISTANCE_BITS;
    uint16_t index = (uint16_t)(along << 5 | across);
    return (Direction)(field_quadrant_angle(g, index) + q.quarter);
}

// $C0:55A9
Aim aim_toward_object(const GameState *g, FieldObject obj, FieldObject target)
{
    MapTile from = object_tile(g, obj), to = object_tile(g, target);
    if (from.x == to.x && from.y == to.y) {
        from = (MapTile){ field_object_x_fine(g, obj), field_object_y_fine(g, obj) };
        to = (MapTile){ field_object_x_fine(g, target), field_object_y_fine(g, target) };
    }
    return (Aim){ from, to };
}

/* The field's random numbers: a fixed table of 256 bytes stepped through, a
 * step a number, which both a blocked object's turn and the scripts' dice
 * draw from. */
uint8_t field_draw_random(GameState *g)
{
    uint8_t step = (uint8_t)(field_random_index(g) + 1);
    set_field_random_index(g, step);
    return field_random_byte(g, step);
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
Velocity field_velocity_for(const GameState *g, FieldObject obj, Direction direction)
{
    uint8_t speed = field_object_speed(g, obj);
    return (Velocity){ axis_step(g, speed, (Direction)(direction + FIELD_QUARTER_TURN)), axis_step(g, speed, direction) };
}

// $C0:AC69
void object_head(GameState *g, FieldObject obj, Direction direction)
{
    Velocity v = field_velocity_for(g, obj, direction);
    charge_legacy_accesses(g, 2); /* speed read and vertical-step temporary */
    charge_legacy_rom_accesses(g, 1); /* direction factor read back */
    charge_legacy_io_accesses(g, 4); /* multiplier operand and result accesses */
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

/* The priority of the object's sprites' two halves as it starts a step: from
 * the tile ahead where it follows the map, or its own layering's. From the
 * tile, the step takes them the other way round from a script's layering
 * (object_layer_as_the_map): the tile's upper attributes give the lower
 * half's priority, and its lower attributes the upper half's. The port does
 * as the original does. */
static void take_priorities_for_the_step(GameState *g, FieldObject obj, TileAttributes ahead)
{
    charge_legacy_accesses(g, 1); /* the old result's repeated layering test */
    if (object_follows_the_map(g, obj))
        object_set_sprite_priorities(g, obj, tile_priority(ahead.lower), tile_priority(ahead.upper));
    else {
        charge_legacy_accesses(g, 2); /* own priority read and temporary */
        object_take_own_priorities(g, obj);
    }
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

// $C0:ACFD
StepStart object_start_step(GameState *g, FieldObject obj, Direction direction)
{
    StepStart r = { START_STILL, direction };
    Velocity first = field_velocity_for(g, obj, direction), retry = { 0, 0 }, steps = first;
    Look look = { 0, { 0, 0, 0 } };
    if (moves(first)) {
        r.outcome = START_MOVING;
        look = look_ahead(g, obj, sign_extended(first.x), first.y);
        if ((field_object_blocking(g, obj) & BLOCKED_BY_THE_MAP) && (look.tile.lower & TILE_BLOCKED)) {
            r.direction = field_draw_random(g);
            charge_legacy_accesses(g, 4); /* new direction's word read/write */
            retry = steps = field_velocity_for(g, obj, r.direction);
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
        take_priorities_for_the_step(g, obj, look.tile);
    set_step_x(g, obj, steps.x);
    set_step_y(g, obj, steps.y);
    /* The old result recorded the two steps and their signs, the last
     * ground probe, and the multiplier's operands and result. */
    charge_legacy_accesses(g, 4 + (moves(first) ? 5 : 0));
    charge_legacy_rom_accesses(g, 1);
    charge_legacy_io_accesses(g, 4);
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

static bool can_block_in_view(const GameState *g, FieldObject other, FieldObject obj)
{
    return !object_disabled(g, other) && other != obj && object_solid(g, other);
}

/* The search starts at the slot after the last object and runs down to the
 * second. The original doubles the count in a byte, so a count past 127
 * starts from what is left of it (the endings' maps count 163). A start past
 * the tables (a count of 64 to 127: the forest of Frog's memory counts 64)
 * starts on the slots past them, which hold nobody but read as slots all the
 * same: the leader's walk (field/walk.c, met_past_the_tables) and the
 * look for an object in the way (below) look at them before the map's
 * objects; the confirm button's guard still leaves such a start to the
 * original. A start of 0 would go round into those slots from slot 0
 * (it counts round 128), which no map makes. */
enum { SEARCH_COUNT_BITS = 0x7F };

FieldObject object_search_start(const GameState *g) { return (FieldObject)(event_object_count(g) & SEARCH_COUNT_BITS); }

bool object_search_starts(const GameState *g) { return object_search_start(g) != 0; }

bool object_search_fits(const GameState *g)
{
    FieldObject start = object_search_start(g);
    return start != 0 && start < FIELD_OBJECTS;
}

/* The last of the map's slots a search looks at first. */
static FieldObject first_slot_in_the_tables(FieldObject start)
{
    return start < FIELD_OBJECTS ? start : (FieldObject)(FIELD_OBJECTS - 1);
}

ObjectBearing object_bearing(FieldPosition at, FieldPosition there, Facing facing, bool measured_nearer)
{
    bool vertical = facing == FACING_UP || facing == FACING_DOWN;
    bool looking_back = facing == FACING_UP || facing == FACING_LEFT;
    ObjectBearing m;
    m.ahead = distance_ahead(vertical ? there.y : there.x, vertical ? at.y : at.x, measured_nearer, looking_back,
                             &m.along);
    m.across = distance_across(vertical ? there.x : there.y, vertical ? at.x : at.y);
    return m;
}

static bool close_ahead(ObjectBearing m) { return m.ahead && m.along < REACH; }

bool field_looks_for_objects(const GameState *g, FieldObject obj)
{
    return object_view(g, obj) != OUT_OF_VIEW && (field_object_blocking(g, obj) & BLOCKED_BY_OBJECTS);
}

/* How far a search went: the slots it passed, those in view it looked at
 * more closely, and those it measured. */
typedef struct {
    uint8_t passed, in_view, measured;
} SearchLength;

/* The original steps its index down the slots with the checks spelled out
 * for each, where the port reads a byte or two a slot: its time is a pass
 * for every slot, more for one in view, and more again for each it measures.
 * What the port charges is that difference, in accesses at the search's own
 * rate, so that one scalar fits a search of any length -- the moves that run
 * it inline included (docs/history/reimplementation-chunks.md, "The absent party member and the
 * object off the map"). */
enum { OBJECT_IN_THE_WAY = 0xC05B95, ACCESSES_A_SLOT = 1, ACCESSES_IN_VIEW = 2, ACCESSES_A_MEASURE = 5 };

static void charge_search(GameState *g, SearchLength n)
{
    charge_as(g, OBJECT_IN_THE_WAY,
              (uint32_t)n.passed * ACCESSES_A_SLOT + (uint32_t)n.in_view * ACCESSES_IN_VIEW +
                  (uint32_t)n.measured * ACCESSES_A_MEASURE);
}

/* The map's objects are looked at from the last down to the second (the
 * first is the party's leader), until one is found in the way; `r` is what
 * the search measured before them. */
static ObjectsAhead look_for_objects_ahead(const GameState *g, FieldObject obj, ObjectsAhead r, SearchLength *n)
{
    Facing facing = object_facing(g, obj);
    FieldPosition at = object_position(g, obj);
    for (FieldObject other = first_slot_in_the_tables(object_search_start(g)); other != 0; other--) {
        n->passed++;
        if (object_view(g, other) == OUT_OF_VIEW)
            continue;
        n->in_view++;
        if (can_block_in_view(g, other, obj)) {
            n->measured++;
            r = (ObjectsAhead){ false, true, other };
            ObjectBearing m = object_bearing(at, object_position(g, other), facing, other < obj);
            if (close_ahead(m) && m.across < REACH)
                return (ObjectsAhead){ true, true, other };
        }
    }
    return r;
}

/* A map counting 64 to 127 objects starts the search past the object tables
 * (field/object.h, object_past_the_tables), and the slots there are looked
 * at first, before the map's own. Nobody stands there, but the game reads
 * each as a slot all the same, from the next tables' bytes: in view, enabled
 * and solid as those bytes say, at the place they make. One close ahead
 * stops the search; one measured and none of the map's is the last measured.
 * The forest of Frog's memory ($077) counts 64: its slot past the tables
 * reads the leader's priority mode for its view, its script's place for its
 * role, its acceleration for its solidity, and its Y and velocity for its
 * place. */
static ObjectsAhead look_past_the_tables(const GameState *g, FieldObject obj, FieldObject start, SearchLength *n)
{
    Facing facing = object_facing(g, obj);
    FieldPosition at = object_position(g, obj);
    ObjectsAhead past = { false, false, 0 };
    for (FieldObject slot = start; slot >= FIELD_OBJECTS; slot--) {
        n->passed++;
        if (object_view_past_the_tables(g, slot) == OUT_OF_VIEW)
            continue;
        n->in_view++;
        ObjectPastTheTables there = object_past_the_tables(g, slot);
        if (there.disabled || !object_solid_past_the_tables(g, slot))
            continue;
        n->measured++;
        past = (ObjectsAhead){ false, true, slot };
        ObjectBearing m = object_bearing(at, there.at, facing, false);
        if (close_ahead(m) && m.across < REACH)
            return (ObjectsAhead){ true, true, slot };
    }
    return past;
}

// $C0:5B95
ObjectsAhead object_in_the_way(GameState *g, FieldObject obj)
{
    SearchLength n = { 0, 0, 0 };
    ObjectsAhead r = { false, false, 0 };
    if (field_looks_for_objects(g, obj)) {
        charge_legacy_accesses(g, 10); /* facing/position read back and selected */
        FieldObject start = object_search_start(g);
        if (start >= FIELD_OBJECTS)
            r = look_past_the_tables(g, obj, start, &n);
        if (!r.blocked)
            r = look_for_objects_ahead(g, obj, r, &n);
    }
    charge_legacy_accesses(g, r.measured ? 2 : 4); /* the selected target */
    charge_search(g, n);
    return r;
}

/* ---- Lining up ---------------------------------------------------------------------------- */

enum { ON_TILE = 0xFFF0 };   /* a position rounded to the pixel it is on */
enum { TILE_MIDDLE_X = 0x80, TILE_BOTTOM_Y = 0xF0, FINE_PIXEL = 0xF0 };
enum { ONE_PIXEL = 0x10 };   /* a pixel, as a step */

/* A step toward something on one axis: a pixel forward, or back. */
static int8_t step_toward(bool forward) { return forward ? ONE_PIXEL : -ONE_PIXEL; }

/* A move's tile crossing (object.h): the original keeps it in a byte, 0, a
 * positive number, or a negative one. */
enum { STORED_STAY = 0, STORED_CROSS_ONCE = 1, STORED_CROSS_ALWAYS = 0x80 };

static TileCrossing tile_crossing(const GameState *g, FieldObject obj)
{
    uint8_t stored = field_object_tile_crossing(g, obj);
    if (stored == STORED_STAY)
        return STAY_IN_TILE;
    return (int8_t)stored < 0 ? CROSS_ALWAYS : CROSS_ONCE;
}

static void set_tile_crossing(GameState *g, FieldObject obj, TileCrossing crossing)
{
    static const uint8_t stored[] = { [STAY_IN_TILE] = STORED_STAY, [CROSS_ONCE] = STORED_CROSS_ONCE,
                                      [CROSS_ALWAYS] = STORED_CROSS_ALWAYS };
    set_field_object_tile_crossing(g, obj, stored[crossing]);
}

/* A move of one step, which may cross into the next tile (or not: then the
 * crossing is the last move's). */
static void one_step(GameState *g, FieldObject obj, bool crossing)
{
    set_field_object_steps_left(g, obj, 1);
    if (crossing)
        set_tile_crossing(g, obj, CROSS_ONCE);
}

/* ---- Moves a script starts --------------------------------------------------------------- */

/* A move of one step, its crossing as the last move left it: a step's worth
 * for the field's own routines to move the object in (event/movement.c's
 * behaving as its state says). */
void object_start_one_step(GameState *g, FieldObject obj) { one_step(g, obj, false); }

void object_start_move(GameState *g, FieldObject obj, uint8_t steps, TileCrossing crossing)
{
    set_field_object_steps_left(g, obj, steps);
    set_tile_crossing(g, obj, crossing);
}

/* The original stores the walk's step count itself as its crossing. */
void object_start_walk(GameState *g, FieldObject obj, uint8_t steps)
{
    set_field_object_steps_left(g, obj, steps);
    set_field_object_tile_crossing(g, obj, steps);
}

bool object_moving(const GameState *g, FieldObject obj) { return field_object_steps_left(g, obj) != 0; }

void object_stop_moving_across(GameState *g, FieldObject obj) { set_field_object_velocity_x(g, obj, 0); }

// $C0:5FBE (a call interrupting the object)
void object_stop_moving(GameState *g, FieldObject obj)
{
    object_unmark_scripted_move(g, obj);
    set_field_object_steps_left(g, obj, 0);
}

/* Falling is kept as a flag byte, 1 while it falls. */
enum { FALLING = 1 };

static void mark_launched(GameState *g, FieldObject obj)
{
    object_mark_scripted_move(g, obj);
    set_field_object_accelerating(g, obj, FALLING);
}

// $C0:4E25
void object_launch(GameState *g, FieldObject obj, uint8_t velocity_x, uint8_t velocity_y, uint8_t acceleration,
                   uint8_t steps)
{
    set_step_x(g, obj, (int8_t)velocity_x);
    set_step_y(g, obj, (int8_t)velocity_y);
    set_field_object_acceleration(g, obj, acceleration);
    set_field_object_steps_left(g, obj, steps);
    mark_launched(g, obj);
}

// $C0:4D06
void object_jump(GameState *g, FieldObject obj, uint8_t steps, uint8_t height)
{
    set_field_object_steps_left(g, obj, steps);
    set_field_object_acceleration(g, obj, height);
    uint16_t rise = (uint16_t)((steps * height) >> 1);
    set_field_object_velocity_y(g, obj, (uint16_t)(field_object_velocity_y(g, obj) - rise));
    mark_launched(g, obj);
}

void object_land(GameState *g, FieldObject obj)
{
    object_unmark_scripted_move(g, obj);
    set_field_object_accelerating(g, obj, 0);
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

// $C0:30B3
/* Toward the other object's pixel, a step on each axis where they differ. */
bool object_line_up_with(GameState *g, FieldObject obj, FieldPosition other)
{
    stand_still(g, obj);
    uint16_t own_x = position_x(g, obj) & ON_TILE, other_x = other.x & ON_TILE;
    uint16_t own_y = position_y(g, obj) & ON_TILE, other_y = other.y & ON_TILE;
    charge_legacy_accesses(g, 2); /* the alignment's vertical origin */
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
    if (x < 0 || y < 0)
        charge_legacy_accesses(g, 1); /* the backward step's magnitude */
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
