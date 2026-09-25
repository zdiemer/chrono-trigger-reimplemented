/* The leader's walk (field/walk.h): every frame the party is in the player's
 * hands, the pad walks the leader; the map and the other objects stop it or
 * turn it aside; what it steps onto -- the map's level there, an exit, a step
 * event, a moving floor, a climb -- takes effect; and the screen follows it.
 *
 * What the walk works out is the leader's step for the frame, 1/16 pixel on
 * each axis: a walk or a run the pad asks for, on top of what a moving floor
 * pulls. The party's own step (field/party.h) then moves the leader by it and
 * the members along the leader's trail, and the screen scrolls by it where the
 * screen follows (field/layers.c).
 *
 * The leader stands in a box against the map: its feet at its position, half
 * a tile less a pixel to either side, and a tile less a pixel tall. Walking
 * one way, the box's leading side is looked at on the map at both its
 * corners; where one corner meets another level, the leader is nudged around
 * it, and where it meets a wall, it stops. Walking aslant, the leading corner
 * and the two sides are looked at, and a blocked walk goes on along the one
 * axis the map allows. Before the map, the other objects: one standing close
 * to where the leader is going stops it if it is solid, and is what the
 * leader is up against either way (field/field.h, leader_contact).
 *
 * The map has two levels, and each tile says which parts of it are on which
 * (a slope, a half, a quarter): the leader walks on its level, onto stairs
 * that join the two, and over or under a crossing without changing level.
 *
 * The screen follows the leader across the map while the leader is past the
 * screen's middle the way it walks, and not past the map's edges; while a
 * script has the screen scroll back onto the 16-pixel grid (before a map
 * change), the party holds still and the screen moves a pixel a frame.
 *
 * The walk works in cells of the layers' page, which only its own code reads
 * back; they are left as the original leaves them, from a record of the last
 * values, as scaffolding (field/scratch.h). */
#include "field/walk.h"

#include "charge.h"
#include "core/input.h"
#include "cpu_io.h"
#include "field/bridge.h"
#include "field/field.h"
#include "field/field_data.h"
#include "field/layers.h"
#include "field/parts.h"
#include "field/party.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "vars.h"

/* ---- What the original leaves (field/scratch.h) --------------------------------------- *
 *
 * The walk keeps its working values in the layers' page's cells $52-$73: the
 * last point it looked at on the map ($66, $68), the leader's place ($62,
 * $64), what the map says there ($58-$5C), the level worked out ($56, $60,
 * $6A-$6F), the slope's sum ($54), the screen's checks and the floor's speed
 * ($52, $54), and where the leader will stand ($70-$73). Nothing reads them
 * but the walk; each cell's last value is recorded here on the way and left
 * once at the end. */
enum { WALK_CELLS = 0x52, WALK_CELLS_END = 0x74 };

static struct {
    uint8_t value[WALK_CELLS_END - WALK_CELLS];
    bool kept[WALK_CELLS_END - WALK_CELLS];
} walk_cells;

static void forget_walk_cells(void)
{
    for (unsigned i = 0; i < WALK_CELLS_END - WALK_CELLS; i++)
        walk_cells.kept[i] = false;
}

static void record_cell(uint8_t cell, uint8_t v)
{
    walk_cells.value[cell - WALK_CELLS] = v;
    walk_cells.kept[cell - WALK_CELLS] = true;
}

static void record_cell_word(uint8_t cell, uint16_t v)
{
    record_cell(cell, (uint8_t)v);
    record_cell((uint8_t)(cell + 1), (uint8_t)(v >> 8));
}

enum { AHEAD_CELLS = 0x70 };

static void leave_walk_cells(GameState *g)
{
    for (uint8_t cell = WALK_CELLS; cell < WALK_CELLS_END; cell++) {
        if (!walk_cells.kept[cell - WALK_CELLS])
            continue;
        uint8_t v = walk_cells.value[cell - WALK_CELLS];
        if (cell < AHEAD_CELLS)
            original_layer_cell8(g, cell, v);
        else if (cell < AHEAD_CELLS + 2)
            set_field_walk_ahead_x(g, (uint16_t)(cell == AHEAD_CELLS ? (field_walk_ahead_x(g) & 0xFF00) | v
                                                                       : (field_walk_ahead_x(g) & 0x00FF) | v << 8));
        else
            set_field_walk_ahead_y(g, (uint16_t)(cell == AHEAD_CELLS + 2 ? (field_walk_ahead_y(g) & 0xFF00) | v
                                                                           : (field_walk_ahead_y(g) & 0x00FF) | v << 8));
    }
}

/* The cells, by what the walk keeps in them. */
enum {
    CELL_SCREEN_CHECK = 0x52,   /* also a floor's speed, an exit's how */
    CELL_SCREEN_CHECK_BACK = 0x54,   /* also the slope's sum */
    CELL_HEIGHT_IN_TILE = 0x56,
    CELL_PROPERTIES = 0x58, CELL_UPPER_ATTRIBUTES = 0x5A, CELL_LOWER_ATTRIBUTES = 0x5C,
    CELL_SHAPE = 0x60,
    CELL_AT_X = 0x62, CELL_AT_Y = 0x64, CELL_LOOK_X = 0x66, CELL_LOOK_Y = 0x68,
    CELL_LEVEL = 0x6A, CELL_CROSSING = 0x6C, CELL_PRIORITY_LOWER = 0x6E, CELL_PRIORITY_UPPER = 0x6F,
    CELL_AHEAD_X = 0x70, CELL_AHEAD_Y = 0x72,
};

/* ---- The map under a point -------------------------------------------------------------- */

/* A point on the map, 1/16 pixel, the tile in the high byte. */
typedef FieldPosition MapPoint;

static MapPlace place_of(const GameState *g, MapPoint p)
{
    uint16_t index = (uint16_t)((p.y & 0xFF00) | (p.x >> 8)) & field_map_index_mask(g);
    return map_place_at(index);
}

/* The map's levels, as a tile's part gives one. */
typedef enum { NO_LEVEL = 0, STAIRS = 3 } MapLevel;

/* What the map says of a point: the level of the tile's part it is on,
 * whether that part is a crossing, whether the tile is a climb, and the
 * tile's attributes the step onto it takes on. */
typedef struct {
    MapLevel level;
    bool crossing;
    bool climb;
    TileAttributes tile;
} Ground;

/* Where in its tile a point is: its fine position on each axis, a half tile
 * at $80. */
enum { HALF_A_TILE = 0x80 };

static bool in_left_half(uint8_t fine_x) { return fine_x < HALF_A_TILE; }
static bool in_top_half(uint8_t fine_y) { return fine_y < HALF_A_TILE; }

FieldSlopeSum field_slope_sum(const GameState *g, uint8_t slope, uint8_t fine_x, uint8_t fine_y)
{
    FieldSlope s = field_slope(g, slope);
    /* the rise along the line: the place across shifted down, from the
     * tile's left or (mirrored) its right; a shift of 0 is the original's
     * 256, which leaves nothing */
    uint8_t along = s.shift == 0 ? 0 : (uint8_t)(fine_x >> (s.shift < 8 ? s.shift : 8));
    if (s.mirrored)
        along = (uint8_t)~along;
    uint8_t height = (uint8_t)(fine_y >> 2);
    uint8_t sum = (uint8_t)(s.start + along - height);
    /* the point is on the upper part where the sum's sign isn't the upper
     * side's */
    return (FieldSlopeSum){ along, height, sum, ((int8_t)s.upper_side < 0) != ((int8_t)sum < 0) };
}

/* Whether a point of a tile of the shape is on the tile's upper part. */
static bool on_upper_part(const GameState *g, TileShape shape, uint8_t fine_x, uint8_t fine_y)
{
    switch (shape) {
    case SHAPE_LOWER: return false;
    case SHAPE_UPPER: return true;
    case SHAPE_LEFT_HALF: return in_left_half(fine_x);
    case SHAPE_TOP_HALF: return in_top_half(fine_y);
    case SHAPE_BOTTOM_LEFT_QUARTER: return in_left_half(fine_x) && !in_top_half(fine_y);
    case SHAPE_BOTTOM_RIGHT_QUARTER: return !in_left_half(fine_x) && !in_top_half(fine_y);
    case SHAPE_TOP_RIGHT_QUARTER: return !in_left_half(fine_x) && in_top_half(fine_y);
    case SHAPE_TOP_LEFT_QUARTER: return in_left_half(fine_x) && in_top_half(fine_y);
    default:
        if (tile_shape_is_a_slope(shape)) {
            FieldSlopeSum s = field_slope_sum(g, tile_shape_slope(shape), fine_x, fine_y);
            ORIGINAL_EFFECT(record_cell(CELL_SCREEN_CHECK_BACK, s.sum));
            return s.upper;
        }
        return false;
    }
}

/* The byte the original keeps a tile part's crossing as: the crossing bit
 * where the part's level sits. */
static uint8_t crossing_byte(uint8_t lower, uint8_t shift) { return (uint8_t)(lower & TILE_CROSSING_BIT << shift); }

static void record_ground(Ground ground, TileShape shape, uint8_t fine_y, uint8_t shift)
{
    record_cell(CELL_PROPERTIES, ground.tile.properties);
    record_cell(CELL_UPPER_ATTRIBUTES, ground.tile.upper);
    record_cell(CELL_LOWER_ATTRIBUTES, ground.tile.lower);
    record_cell(CELL_HEIGHT_IN_TILE, (uint8_t)(fine_y >> 2));
    record_cell(CELL_SHAPE, shape >= SHAPE_FIRST_SLOPE ? tile_shape_slope(shape) : 0);
    record_cell(CELL_LEVEL, ground.level);
    record_cell(CELL_CROSSING, crossing_byte(ground.tile.lower, shift));
    record_cell(CELL_PRIORITY_LOWER, (uint8_t)(tile_priority(ground.tile.upper) << 4));
    record_cell(CELL_PRIORITY_UPPER, (uint8_t)(tile_priority(ground.tile.lower) << 4));
}

/* The byte of the layers' page the original notes a climb in is the one the
 * layers' edges keep their wrap in (field/layers.c), which reads it there
 * after the walk: the walk leaves it as the original does. */
enum { CLIMB_NOTED = 0x04 };

static void note_climb(GameState *g, bool climb)
{
    set_field_layer_edge_wrap(g, (uint16_t)((field_layer_edge_wrap(g) & 0xFF00) | (climb ? CLIMB_NOTED : 0)));
}

// $C0:9AB2 (the attributes), $C0:9AD3 (the level)
/* What the map says at a point of a tile. */
static Ground ground_at(GameState *g, MapPlace place, uint8_t fine_x, uint8_t fine_y)
{
    Ground ground = { 0 };
    ground.tile = map_tile_attributes(g, place);
    TileShape shape = tile_shape(ground.tile);
    ground.climb = shape == SHAPE_CLIMB;
    note_climb(g, ground.climb);
    uint8_t shift = on_upper_part(g, shape, fine_x, fine_y) ? TILE_UPPER_PART_SHIFT : TILE_LOWER_PART;
    ground.level = (MapLevel)(ground.tile.lower >> shift & TILE_LEVEL_BITS);
    ground.crossing = crossing_byte(ground.tile.lower, shift) != 0;
    ORIGINAL_EFFECT(record_ground(ground, shape, fine_y, shift));
    return ground;
}

// $C0:9C37
/* Whether the leader may stand on the ground from its level: its own level,
 * stairs, or anywhere with a level from stairs; a crossing unless it is on
 * stairs. */
static bool reachable(const GameState *g, Ground ground)
{
    uint8_t on = field_leader_level(g);
    if (ground.crossing)
        return on != STAIRS;
    if (ground.level == NO_LEVEL)
        return false;
    return ground.level == STAIRS || ground.level == on || on == STAIRS;
}

/* On a climb the leader goes up and down at a walk. */
static int16_t slowed_to_a_climb(int16_t vertical)
{
    if (vertical == 0)
        return 0;
    return vertical < 0 ? -0x10 : 0x10;
}

/* ---- Looking ahead ------------------------------------------------------------------------ */

/* The leader's step as the walk works it out, 1/16 pixel on each axis. */
typedef struct {
    int16_t x, y;
} Step;

/* The walk as it is worked out: the leader, where it stands, its step so far,
 * and the last point it looked at. */
typedef struct {
    FieldObject leader;
    FieldPosition at;
    Step step;
    MapPoint look;
} Walk;

typedef enum { WAY_OPEN, WAY_WALL, WAY_OTHER_LEVEL } Way;

enum { WALKABLE = 0x80 };

/* The original works a point out at length -- the tile's shape from its
 * tables, the slope's sum, the levels' rules spelled out for each -- where
 * the port reads a few bytes: what it charges is that difference, a look at
 * the point and more for its ground where it is no wall, at the walk's own rate
 * (charge.h). */
enum { THE_WALK = 0xC0881E, ACCESSES_A_LOOK = 12, ACCESSES_THE_GROUND = 24 };

// $C0:8A9E, and $C0:9AA1
/* What stops the leader at a point: a wall, a tile's part on a level it
 * can't step onto, or nothing. A climb there slows its step up or down. */
static Way look(GameState *g, Walk *w, uint16_t x, uint16_t y)
{
    w->look = (MapPoint){ x, y };
    ORIGINAL_EFFECT(record_cell_word(CELL_LOOK_X, x));
    ORIGINAL_EFFECT(record_cell_word(CELL_LOOK_Y, y));
    MapPlace place = place_of(g, w->look);
    ORIGINAL_EFFECT(charge_as(g, THE_WALK, ACCESSES_A_LOOK));
    if (!(field_tile_walkable(g, place.row, place.column) & WALKABLE))
        return WAY_WALL;
    ORIGINAL_EFFECT(charge_as(g, THE_WALK, ACCESSES_THE_GROUND));
    Ground ground = ground_at(g, place, (uint8_t)x, (uint8_t)y);
    if (ground.climb)
        w->step.y = slowed_to_a_climb(w->step.y);
    return reachable(g, ground) ? WAY_OPEN : WAY_OTHER_LEVEL;
}

static bool is_open(Way way) { return way == WAY_OPEN; }

/* ---- Other objects in the way -------------------------------------------------------------- */

/* Whether a slot is one of the party's places, by the byte the party keeps
 * for it (an empty place's marker is a byte too, which a slot past the
 * tables can match). */
static bool in_the_party(const GameState *g, uint8_t offset)
{
    for (PartyPlace place = PARTY_LEADER; place < PARTY_PLACES; place++)
        if (field_link_byte(party_member_link(g, place)) == offset)
            return true;
    return false;
}

/* How close an object may stand to a point before the leader meets it, on
 * each axis: 14 pixels. */
enum { REACH = 0xE0 };

static uint16_t magnitude(uint16_t d) { return (d & 0x8000) ? (uint16_t)-d : d; }

// $C0:9965
/* Whether something at `at` stands close to the point. The original
 * measures a sixteenth short across, and down a sixteenth short too unless
 * the slot is past the second member's (its slots' comparisons' borrow). */
static bool stands_close(const GameState *g, uint8_t offset, FieldPosition at, MapPoint p)
{
    uint8_t second = field_link_byte(party_member_link(g, PARTY_SECOND_MEMBER));
    uint16_t short_down = offset > second ? 0 : 1;
    uint16_t down = (uint16_t)(at.y - p.y - short_down), across = (uint16_t)(at.x - p.x - 1);
    return magnitude(down) < REACH && magnitude(across) < REACH;
}

/* What the search met: nothing, or the slot, and whether it stops the
 * leader. */
typedef struct {
    bool met;
    uint8_t offset;
    bool solid;
} Met;

/* For shadow mode: what the original meets past the object tables. A map's
 * object count of 64 to 127 (the original's search starts from it doubled
 * in a byte) starts the search on the slots past the tables, where the
 * original reads the next tables' bytes as a slot's (field/object.h,
 * object_past_the_tables): whether it is in view from the bytes after the
 * in-view flags, whether it is disabled from the bytes after the roles, and
 * where it stands from the objects' vertical positions (read as its
 * horizontal one) and velocities (as its vertical one). Where one of those
 * reads as in view, enabled, no party place's and close, the original stops
 * there: the leader is up against no object (the slot's byte has the top bit
 * of a link to none), and stopped if the byte after the solidity flags says
 * solid.
 *
 * Whether the game sees it: over the movie library the count is 112 or 123
 * for a frame or two in six movies (20 calls), while a map changes, and no
 * slot past the tables reads as close on any of them, so the leader walks as
 * it would have. A native build looks at the map's objects alone. */
static void what_the_original_meets_past_the_tables(const GameState *g, FieldObject start, MapPoint p, Met *m)
{
    for (FieldObject slot = start; slot >= FIELD_OBJECTS; slot--) {
        uint8_t offset = field_object_offset(slot);
        ObjectPastTheTables past = object_past_the_tables(g, slot);
        if (!past.looked_at || past.disabled || in_the_party(g, offset))
            continue;
        if (stands_close(g, offset, past.at, p)) {
            *m = (Met){ true, offset, past.solid };
            return;
        }
    }
}

// $C0:9923
/* Another object where the leader is going: the first, from the last slot
 * of the map's down, that is in view, not disabled and not one of the party,
 * and stands close. The leader is up against it; it stops the leader if it
 * is solid. The original looks from one slot past the map's objects, and
 * never at slot 0; from past the tables, for a count past them, where it
 * reads what isn't an object (above). */
static bool object_stands_in_the_way(GameState *g, MapPoint p)
{
    ORIGINAL_EFFECT(record_cell_word(CELL_LOOK_X, p.x));
    ORIGINAL_EFFECT(record_cell_word(CELL_LOOK_Y, p.y));
    FieldObject start = object_search_start(g);
    Met m = { false, 0, false };
    if (start >= FIELD_OBJECTS)
        ORIGINAL_EFFECT(what_the_original_meets_past_the_tables(g, start, p, &m));
    FieldObject first = start < FIELD_OBJECTS ? start : FIELD_OBJECTS - 1;
    for (FieldObject obj = first; obj >= 1 && !m.met; obj--) {
        uint8_t offset = field_object_offset(obj);
        if (object_view(g, obj) == OUT_OF_VIEW || object_disabled(g, obj) || in_the_party(g, offset))
            continue;
        if (stands_close(g, offset, object_position(g, obj), p))
            m = (Met){ true, offset, object_solid(g, obj) };
    }
    set_leader_contact(g, m.met ? field_link_stored(m.offset) : field_no_link());
    return m.met && m.solid;
}

/* ---- The leader's box ------------------------------------------------------------------------ */

enum { HALF_WIDTH = 0x70, HEIGHT = 0x70, OBJECT_HEIGHT = 0x40, NUDGE = 0x10 };

static uint16_t right_side(uint16_t x) { return (uint16_t)(x + HALF_WIDTH); }
static uint16_t left_side(uint16_t x) { return (uint16_t)(x - HALF_WIDTH); }
static uint16_t top_side(uint16_t y) { return (uint16_t)(y - HEIGHT); }

/* Two of the aslant looks take the left side a sixteenth further right, but
 * in the map's first tile (they add the step in with the borrow of working
 * the side out). */
static uint16_t left_side_as_aslant(uint16_t x) { return (uint16_t)(left_side(x) + (x >= HALF_WIDTH)); }

/* And the nudge left along a row takes the right side a sixteenth further
 * right, but within the map's first pixel (the same, with the nudge). */
static uint16_t right_side_nudged_left(uint16_t x) { return (uint16_t)(right_side(x) - NUDGE + (x >= NUDGE)); }

static void record_at(const Walk *w)
{
    record_cell_word(CELL_AT_X, w->at.x);
    record_cell_word(CELL_AT_Y, w->at.y);
}

// $C0:8AB5 (right), $C0:8BC4 (left)
/* Walking across: the box's leading side at its feet and its head. Where the
 * head meets something, the leader steps down around it; where the feet meet
 * another level, up around it; a wall at its feet stops it. A nudge that
 * can't go on across goes on alone. */
static bool walk_across(GameState *g, Walk *w)
{
    ORIGINAL_EFFECT(record_at(w));
    bool right = w->step.x > 0;
    uint16_t front = (uint16_t)((right ? right_side(w->at.x) : left_side(w->at.x)) + w->step.x);
    if (object_stands_in_the_way(g, (MapPoint){ front, (uint16_t)(w->at.y - OBJECT_HEIGHT) })) {
        w->step.x = 0;
        return false;
    }
    Way feet = look(g, w, front, w->at.y);
    if (is_open(feet)) {
        if (is_open(look(g, w, front, top_side(w->at.y))))
            return true;
        w->step.y = NUDGE;
        if (!is_open(look(g, w, front, (uint16_t)(w->at.y + w->step.y)))) {
            w->step = (Step){ 0, 0 };
            return false;
        }
        if (is_open(look(g, w, front, (uint16_t)(top_side(w->at.y) + w->step.y))))
            return true;
        front = (uint16_t)(front - w->step.x);
        w->step.x = 0;
        if (is_open(look(g, w, front, (uint16_t)(w->at.y + w->step.y))))
            return true;
        w->step.y = 0;
        return false;
    }
    if (feet == WAY_WALL || !is_open(look(g, w, front, top_side(w->at.y)))) {
        w->step.x = 0;
        return false;
    }
    w->step.y = -NUDGE;
    if (is_open(look(g, w, front, (uint16_t)(w->at.y + w->step.y)))) {
        if (is_open(look(g, w, front, (uint16_t)(top_side(w->at.y) + w->step.y))))
            return true;
        w->step = (Step){ 0, 0 };
        return false;
    }
    front = (uint16_t)(front - w->step.x);
    w->step.x = 0;
    if (is_open(look(g, w, front, (uint16_t)(top_side(w->at.y) + w->step.y))))
        return true;
    w->step.y = 0;
    return false;
}

// $C0:8BF9 (up), $C0:8D11 (down)
/* Walking up or down: the box's leading row at its two sides. Where the
 * right side meets another level, the leader steps left around it; where
 * the left side does, right; a wall stops it. */
static bool walk_along(GameState *g, Walk *w)
{
    ORIGINAL_EFFECT(record_at(w));
    bool up = w->step.y < 0;
    uint16_t row = (uint16_t)((up ? top_side(w->at.y) : w->at.y) + w->step.y);
    if (object_stands_in_the_way(g, (MapPoint){ w->at.x, row })) {
        w->step.y = 0;
        return false;
    }
    Way left = look(g, w, left_side(w->at.x), row);
    if (is_open(left)) {
        Way right = look(g, w, right_side(w->at.x), row);
        if (is_open(right))
            return true;
        if (right == WAY_WALL) {
            w->step.y = 0;
            return false;
        }
        w->step.x = -NUDGE;
        if (is_open(look(g, w, right_side_nudged_left(w->at.x), row))) {
            if (is_open(look(g, w, (uint16_t)(left_side(w->at.x) + w->step.x), row)))
                return true;
            w->step = (Step){ 0, 0 };
            return false;
        }
        row = (uint16_t)(row - w->step.y);
        w->step.y = 0;
        if (is_open(look(g, w, (uint16_t)(left_side(w->at.x) + w->step.x), row)))
            return true;
        w->step.x = 0;
        return false;
    }
    if (left == WAY_WALL || !is_open(look(g, w, right_side(w->at.x), row))) {
        w->step.y = 0;
        return false;
    }
    w->step.x = NUDGE;
    if (is_open(look(g, w, (uint16_t)(left_side(w->at.x) + w->step.x), row))) {
        if (is_open(look(g, w, (uint16_t)(right_side(w->at.x) + w->step.x), row)))
            return true;
        w->step = (Step){ 0, 0 };
        return false;
    }
    row = (uint16_t)(row - w->step.y);
    w->step.y = 0;
    if (is_open(look(g, w, (uint16_t)(right_side(w->at.x) + w->step.x), row)))
        return true;
    w->step.x = 0;
    return false;
}

/* The sides an aslant walk looks at, as each works them out. */
typedef enum { RIGHT_SIDE, LEFT_SIDE, LEFT_SIDE_AS_ASLANT } Side;

static uint16_t side_x(const Walk *w, Side side)
{
    uint16_t x = side == RIGHT_SIDE ? right_side(w->at.x)
                 : side == LEFT_SIDE ? left_side(w->at.x)
                                     : left_side_as_aslant(w->at.x);
    return (uint16_t)(x + w->step.x);
}

typedef struct {
    Side leading, across, side, side_alone;
} Aslant;

// $C0:8E21 (up right), $C0:8EF1 (up left), $C0:8FC1 (down left), $C0:909C (down right)
/* Walking aslant: the box's leading corner, the other end of its leading
 * row, and its leading side's other end. Where the row is open but the side
 * isn't, the leader walks on up or down alone; where the row is blocked but
 * the side is open, it walks on across alone; blocked at the corner but open
 * on both ends, it stops, as it does where both are blocked. */
static bool walk_aslant(GameState *g, Walk *w, Aslant a)
{
    ORIGINAL_EFFECT(record_at(w));
    bool up = w->step.y < 0;
    uint16_t row = (uint16_t)((up ? top_side(w->at.y) : w->at.y) + w->step.y);
    uint16_t corner = side_x(w, a.leading);
    if (object_stands_in_the_way(g, (MapPoint){ corner, row })) {
        w->step = (Step){ 0, 0 };
        return false;
    }
    bool at_the_corner = is_open(look(g, w, corner, row));
    bool along_the_row = is_open(look(g, w, side_x(w, a.across), row));
    if (along_the_row) {
        uint16_t side_row = (uint16_t)(up ? w->at.y + w->step.y : top_side((uint16_t)(w->at.y + w->step.y)));
        if (is_open(look(g, w, side_x(w, a.side), side_row))) {
            if (at_the_corner)
                return true;
            w->step = (Step){ 0, 0 };
            return false;
        }
        w->step.x = 0;
        return walk_along(g, w);
    }
    uint16_t side_row = (uint16_t)(up ? w->at.y + w->step.y : top_side((uint16_t)(w->at.y + w->step.y)));
    if (!is_open(look(g, w, side_x(w, a.side_alone), side_row))) {
        w->step = (Step){ 0, 0 };
        return false;
    }
    w->step.y = 0;
    return walk_across(g, w);
}

static const Aslant UP_RIGHT = { RIGHT_SIDE, LEFT_SIDE, RIGHT_SIDE, RIGHT_SIDE };
static const Aslant UP_LEFT = { LEFT_SIDE, RIGHT_SIDE, LEFT_SIDE_AS_ASLANT, LEFT_SIDE };
static const Aslant DOWN_LEFT = { LEFT_SIDE, RIGHT_SIDE, LEFT_SIDE_AS_ASLANT, LEFT_SIDE };
static const Aslant DOWN_RIGHT = { RIGHT_SIDE, LEFT_SIDE_AS_ASLANT, RIGHT_SIDE, RIGHT_SIDE };

/* ---- Stepping onto the map ------------------------------------------------------------------ */

enum { NO_EXIT = 0x80, ONE_TILE = 0x100, NEAR_THE_EXIT = 0x3F };

// $C0:9CA1
/* An exit on the tile the leader steps onto takes the party to its location:
 * where the exit's column is the tile's, anywhere near its row, or where its
 * row is. */
static void take_an_exit(GameState *g, MapPlace place, MapPoint to)
{
    uint8_t exit = field_tile_exits(g, place.row, place.column);
    if (exit & NO_EXIT)
        return;
    ORIGINAL_EFFECT(hw_set_multiplicand(g, exit));
    ORIGINAL_EFFECT(hw_multiply_by(g, 7));
    FieldExit e = field_exit(g, field_location_exits(g), exit);
    uint8_t column = (uint8_t)(to.x >> 8), row = (uint8_t)(to.y >> 8);
    int8_t rows_off = (int8_t)(e.y - row);
    uint8_t distance = rows_off < 0 ? (uint8_t)-rows_off : (uint8_t)rows_off;
    bool on = e.x == column ? distance < NEAR_THE_EXIT : e.y == row;
    if (!on)
        return;
    ORIGINAL_EFFECT(record_cell(CELL_SCREEN_CHECK, (uint8_t)(e.map_and_how >> 8)));
    field_set_next_location(g, location_from(e.map_and_how, e.to_x, e.to_y));
    field_request(g, FIELD_REQUEST_LOCATION_CHANGE);
}

// $C0:9D35
/* A step event on the tile beyond: the main loop runs it. With a change of
 * place already asked for, the original looks for it on the map's first row
 * at the column, and notes only the column. */
static void set_off_a_step_event(GameState *g, MapPoint ahead, bool changing_place)
{
    MapPlace place = place_of(g, ahead);
    if (changing_place)
        place.row = 0;
    if (!(field_tile_upper_attributes(g, place.row, place.column) & TILE_STEP_EVENT))
        return;
    uint16_t tile = (uint16_t)(place.row << 8 | place.column);
    set_field_step_tile(g, changing_place ? (uint16_t)((field_step_tile(g) & 0xFF00) | place.column) : tile);
    field_request(g, FIELD_REQUEST_STEP_EVENT);
}

/* A moving floor's pull: its speed, and which way. */
static const uint8_t floor_speeds[] = { 0, 0x08, 0x10, 0x20 };
typedef enum { FLOOR_UP, FLOOR_DOWN, FLOOR_LEFT, FLOOR_RIGHT } FloorWay;

static Velocity floor_pull(uint8_t upper_attributes)
{
    uint8_t speed = floor_speeds[(upper_attributes & TILE_FLOOR_SPEED) >> 2];
    ORIGINAL_EFFECT(record_cell(CELL_SCREEN_CHECK, speed));
    switch ((FloorWay)(upper_attributes & TILE_FLOOR_WAY)) {
    case FLOOR_UP: return (Velocity){ 0, (int8_t)-speed };
    case FLOOR_DOWN: return (Velocity){ 0, (int8_t)speed };
    case FLOOR_LEFT: return (Velocity){ (int8_t)-speed, 0 };
    default: return (Velocity){ (int8_t)speed, 0 };
    }
}

// $C0:9C5C
/* The leader steps where the walk takes it, if it may stand there: it takes
 * an exit there, sets off a step event beyond, walks on the level there
 * (layered as the map is), and the ground under it -- a climb, a moving
 * floor -- is what the party's step and the next frame go by. */
static void step_onto(GameState *g, Walk *w)
{
    MapPoint to = { (uint16_t)(w->at.x + w->step.x), (uint16_t)(w->at.y + w->step.y) };
    MapPoint ahead = { to.x, (uint16_t)((int8_t)w->step.y < 0 ? to.y - ONE_TILE : to.y + ONE_TILE) };
    w->look = to;
    ORIGINAL_EFFECT(record_cell_word(CELL_LOOK_X, to.x));
    ORIGINAL_EFFECT(record_cell_word(CELL_LOOK_Y, to.y));
    ORIGINAL_EFFECT(record_cell_word(CELL_AHEAD_X, ahead.x));
    ORIGINAL_EFFECT(record_cell_word(CELL_AHEAD_Y, ahead.y));
    MapPlace place = place_of(g, to);
    Ground ground = ground_at(g, place, (uint8_t)to.x, (uint8_t)to.y);
    if (ground.climb)
        w->step.y = slowed_to_a_climb(w->step.y);
    if (!reachable(g, ground)) {
        w->step = (Step){ 0, 0 };
        return;
    }
    bool changing_place = field_requested(g, FIELD_REQUEST_LOCATION_CHANGE);
    if (!changing_place)
        take_an_exit(g, place, to);
    set_off_a_step_event(g, ahead, changing_place);
    if (!ground.crossing) {
        set_field_leader_level(g, ground.level);
        /* the other way round from a script's layering, as the step's start
         * takes a tile's priorities (field/motion.c) */
        object_set_sprite_priorities(g, w->leader, tile_priority(ground.tile.lower), tile_priority(ground.tile.upper));
    }
    bool moving_floor = (ground.tile.upper & TILE_FLOOR_SPEED) != 0;
    set_leader_ground(g, leader_ground_of(ground.climb, moving_floor));
    Velocity pull = moving_floor ? floor_pull(ground.tile.upper) : (Velocity){ 0, 0 };
    set_field_floor_pull_x(g, (uint8_t)pull.x);
    set_field_floor_pull_y(g, (uint8_t)pull.y);
}

// $C0:8A6D
/* The map and the other objects on the leader's way: its step as they allow
 * it, and the step onto the map where it still goes. */
static void walk_against_the_map(GameState *g, Walk *w)
{
    bool moves;
    if (w->step.y == 0 && w->step.x == 0)
        return;
    if (w->step.y == 0)
        moves = walk_across(g, w);
    else if (w->step.x == 0)
        moves = walk_along(g, w);
    else if (w->step.y < 0)
        moves = walk_aslant(g, w, w->step.x > 0 ? UP_RIGHT : UP_LEFT);
    else
        moves = walk_aslant(g, w, w->step.x > 0 ? DOWN_RIGHT : DOWN_LEFT);
    if (moves)
        step_onto(g, w);
}

/* ---- The pad ------------------------------------------------------------------------------ */

/* The direction pad's buttons. */
enum { DIRECTIONS = BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT };
enum { WALKING = 0x10, RUNNING = 0x20 };

// $C0:88EE, and the directions' $C0:8925-$C0:8A6C
/* How the pad walks the leader: a direction or a diagonal, at a walk, or at
 * a run while the dash is held; nothing with opposite directions held. */
static Velocity pad_walk(const GameState *g)
{
    uint16_t held = input_mapped(g, ASSIGNED_HELD);
    int8_t pace = (held & ASSIGNED_DASH) ? RUNNING : WALKING;
    switch (held & DIRECTIONS) {
    case BUTTON_RIGHT: return (Velocity){ pace, 0 };
    case BUTTON_LEFT: return (Velocity){ (int8_t)-pace, 0 };
    case BUTTON_DOWN: return (Velocity){ 0, pace };
    case BUTTON_UP: return (Velocity){ 0, (int8_t)-pace };
    case BUTTON_DOWN | BUTTON_RIGHT: return (Velocity){ pace, pace };
    case BUTTON_DOWN | BUTTON_LEFT: return (Velocity){ (int8_t)-pace, pace };
    case BUTTON_UP | BUTTON_RIGHT: return (Velocity){ pace, (int8_t)-pace };
    case BUTTON_UP | BUTTON_LEFT: return (Velocity){ (int8_t)-pace, (int8_t)-pace };
    default: return (Velocity){ 0, 0 };
    }
}

/* ---- The map's edges and the screen -------------------------------------------------------------- */

// $C0:9175
/* The leader walks no further than the map's edges: its step on each axis,
 * or none where its tile is at the edge it walks toward. */
static Velocity keep_to_the_map(GameState *g, FieldObject leader, Velocity step)
{
    Velocity moved = step;
    if (step.x == 0 && step.y == 0)
        return moved;
    MapTile t = object_tile(g, leader);
    if (step.x < 0 && field_map_edges_left(g) >= t.x)
        moved.x = 0;
    if (step.x > 0) {
        ORIGINAL_EFFECT(original_cell8(g, 0xD9, field_map_edges_right(g)));
        if (t.x >= field_map_edges_right(g))
            moved.x = 0;
    }
    if (step.y < 0 && (uint8_t)(field_map_edges_top(g) + 1) >= t.y)
        moved.y = 0;
    if (step.y > 0) {
        uint8_t last = (uint8_t)(field_map_edges_bottom(g) + 1);
        ORIGINAL_EFFECT(original_cell8(g, 0xD9, last));
        if (t.y >= last)
            moved.y = 0;
    }
    return moved;
}

/* Where the leader is on the screen, as the original reads it: the low byte
 * of its nine-bit place, so that a leader off the screen's left or top reads
 * as far along. The screen's middle, for following: across, and down, where
 * the leader stands a little below it. */
enum { SCREEN_MIDDLE_X = 0x80, SCREEN_MIDDLE_Y = 0x88, TILE_PIXELS = 16 };

static uint8_t leader_on_screen(const GameState *g, FieldObject leader, Axis axis)
{
    return (uint8_t)(axis == AXIS_X ? field_object_screen_x(g, leader) : field_object_screen_y(g, leader));
}

/* The screen's edge on an axis, in 16-pixel tiles, and the map's. */
static uint8_t screen_leading_edge(const GameState *g, Axis axis, bool forward)
{
    uint8_t edge = axis == AXIS_X ? (forward ? field_screen_right(g) : field_screen_left(g))
                                  : (forward ? field_screen_bottom(g) : field_screen_top(g));
    return (uint8_t)(edge >> 1);
}

static uint8_t map_edge(const GameState *g, Axis axis, bool forward)
{
    return axis == AXIS_X ? (forward ? field_map_edges_right(g) : field_map_edges_left(g))
                          : (forward ? field_map_edges_bottom(g) : field_map_edges_top(g));
}

static uint8_t fine_scroll(const GameState *g, Axis axis)
{
    return axis == AXIS_X ? field_layer_fine_x(g, FIELD_BG1) : field_layer_fine_y(g, FIELD_BG1);
}

// $C0:9A1F, $C0:9A3D, $C0:9A60, $C0:9A7E
/* Whether the screen follows the leader forward (right, down) or back by
 * `pixels`: short of the map's edge, while the leader is past the screen's
 * middle that way; at the edge, only as far as the screen's last tile. */
static bool screen_follows(const GameState *g, FieldObject leader, Axis axis, bool forward, uint8_t pixels)
{
    uint8_t screen = screen_leading_edge(g, axis, forward), edge = map_edge(g, axis, forward);
    uint8_t middle = axis == AXIS_X ? SCREEN_MIDDLE_X : SCREEN_MIDDLE_Y;
    if (forward) {
        if (screen < edge)
            return leader_on_screen(g, leader, axis) > middle;
        if (screen > edge)
            return false;
        return (uint8_t)(fine_scroll(g, axis) + pixels) < TILE_PIXELS;
    }
    if (edge < screen)
        return leader_on_screen(g, leader, axis) < middle;
    if (edge > screen)
        return false;
    return (int8_t)(fine_scroll(g, axis) - pixels) >= 0;
}

/* For the original's code and shadow mode: what each check leaves in the
 * walk's cells -- forward, how far; back, how far and the screen's edge that
 * way. The original, turning down the screen's step down, goes on into its
 * check up with what the check down ended on (the screen's middle short of
 * the map's edge, the screen's edge past it, and at it the fine scroll with
 * the step), negated and made pixels, which only these cells show. */
static void leave_screen_check(const GameState *g, Axis axis, bool forward, uint8_t pixels)
{
    if (forward) {
        record_cell(CELL_SCREEN_CHECK, pixels);
        return;
    }
    record_cell(CELL_SCREEN_CHECK_BACK, pixels);
    record_cell(CELL_SCREEN_CHECK, screen_leading_edge(g, axis, false));
}

static void leave_check_down_turned_back(const GameState *g, uint8_t pixels)
{
    uint8_t screen = screen_leading_edge(g, AXIS_Y, true), edge = map_edge(g, AXIS_Y, true);
    uint8_t ended_on = screen < edge ? SCREEN_MIDDLE_Y : screen > edge ? screen : (uint8_t)(fine_scroll(g, AXIS_Y) + pixels);
    leave_screen_check(g, AXIS_Y, false, (uint8_t)((uint8_t)-ended_on >> 4));
}

static uint8_t pixels_of(int8_t step) { return (uint8_t)((step < 0 ? -step : step) >> 4); }

/* Whether the screen follows the leader one way on an axis. */
static bool screen_follows_step(const GameState *g, FieldObject leader, Axis axis, int8_t step)
{
    bool forward = step > 0;
    ORIGINAL_EFFECT(leave_screen_check(g, axis, forward, pixels_of(step)));
    return screen_follows(g, leader, axis, forward, pixels_of(step));
}

// $C0:99DE
/* The screen follows the leader's step on each axis where it may. */
static Velocity follow_with_the_screen(GameState *g, FieldObject leader, Velocity step)
{
    Velocity screen = step;
    if (step.x != 0 && !screen_follows_step(g, leader, AXIS_X, step.x))
        screen.x = 0;
    if (step.y != 0 && !screen_follows_step(g, leader, AXIS_Y, step.y)) {
        screen.y = 0;
        if (step.y > 0)
            ORIGINAL_EFFECT(leave_check_down_turned_back(g, pixels_of(step.y)));
    }
    return screen;
}

/* ---- The walk ----------------------------------------------------------------------------------- */

/* The sign the walk makes the high byte of the screen's step on each axis,
 * as it works the step out as a word; nothing reads it. */
static void leave_step_signs(GameState *g, Step step)
{
    set_field_screen_scroll_step_high(g, AXIS_X, (uint8_t)((uint16_t)step.x >> 8));
    set_field_screen_scroll_step_high(g, AXIS_Y, (uint8_t)((uint16_t)step.y >> 8));
}

/* Where the screen's recentring onto the grid is (field_screen_recentring):
 * none under way, starting, or under way. */
typedef enum { RECENTRED = 0, RECENTRE_STARTS = 1, RECENTRING = 2 } Recentring;

/* How the leader would step this frame before the map has its say: a moving
 * floor's pull, and the pad's walk where the party is the player's. */
static Velocity pad_and_pull(const GameState *g, Velocity *walk)
{
    *walk = (Velocity){ 0, 0 };
    if (dialog_choice_active(g) == DIALOG_NO_CHOICES && party_is_the_players(g) &&
        field_screen_recentring(g) == RECENTRED)
        *walk = pad_walk(g);
    return (Velocity){ (int8_t)(field_floor_pull_x(g) + walk->x), (int8_t)(field_floor_pull_y(g) + walk->y) };
}

// $C0:881E
/* The leader's walk this frame: the leader forgets what it was up against;
 * the pad (where the party is the player's) and a moving floor ask for a
 * step; where the leader collides with the map, the map and the other
 * objects stop it or turn it aside, and what it steps onto takes effect;
 * it stays within the map's edges; and the screen follows it where it may.
 * The step, the leader's movement and the screen's step are what the party's
 * step and the layers' scroll take on from here. With nobody leading the
 * party (a scene before anyone joins) and no step, there is nobody to move;
 * the guard leaves a step then to the original. */
void leader_walks(GameState *g)
{
    ORIGINAL_EFFECT(forget_walk_cells());
    FieldObject leader = field_linked_object(party_member_link(g, PARTY_LEADER));
    leader_loses_contact(g);
    Velocity walk;
    Velocity first = pad_and_pull(g, &walk);
    set_field_leader_walk_x(g, (uint8_t)walk.x);
    set_field_leader_walk_y(g, (uint8_t)walk.y);
    Walk w = { leader, { 0, 0 }, { first.x, first.y }, { 0, 0 } };
    bool against_the_map = field_leader_collides(g) != 0 && (w.step.x != 0 || w.step.y != 0);
    if (against_the_map) {
        w.at = object_position(g, leader);
        walk_against_the_map(g, &w);
    }
    Velocity step = { (int8_t)w.step.x, (int8_t)w.step.y };
    Velocity moved = keep_to_the_map(g, leader, step);
    set_field_leader_movement_x(g, (uint8_t)moved.x);
    set_field_leader_movement_y(g, (uint8_t)moved.y);
    Velocity screen = follow_with_the_screen(g, leader, step);
    set_field_screen_scroll_step(g, AXIS_X, (uint8_t)screen.x);
    set_field_screen_scroll_step(g, AXIS_Y, (uint8_t)screen.y);
    if (against_the_map)
        ORIGINAL_EFFECT(leave_step_signs(g, w.step));
    ORIGINAL_EFFECT(leave_walk_cells(g));
}

/* ---- The screen back onto the grid --------------------------------------------------------------- */


/* The screen's step back onto the grid: a pixel, toward the leader. */
enum { A_PIXEL_FORWARD = 0x10, A_PIXEL_BACK = 0xF0 };

static uint8_t pixel_toward(uint8_t leader_on_screen, uint8_t middle)
{
    return leader_on_screen >= middle ? A_PIXEL_FORWARD : A_PIXEL_BACK;
}

// $C0:885A
/* While a script has the screen scroll back onto the 16-pixel grid (before a
 * tile copy), the party holds still and the screen moves a pixel a frame on
 * each axis where it is off the grid, toward the leader, until it is on it.
 * True where the screen moves this frame. */
bool screen_recentres(GameState *g)
{
    FieldObject leader = field_linked_object(party_member_link(g, PARTY_LEADER));
    uint8_t off_x = fine_scroll(g, AXIS_X), off_y = fine_scroll(g, AXIS_Y);
    switch ((Recentring)field_screen_recentring(g)) {
    case RECENTRED:
        return false;
    case RECENTRE_STARTS:
        set_field_leader_movement_x(g, 0);
        set_field_leader_movement_y(g, 0);
        set_field_screen_scroll_step(g, AXIS_X,
                                     off_x == 0 ? 0 : pixel_toward(leader_on_screen(g, leader, AXIS_X), SCREEN_MIDDLE_X));
        set_field_screen_scroll_step(g, AXIS_Y, 0);
        if (off_x == 0 && off_y == 0) {
            set_field_screen_recentring(g, RECENTRED);
            return false;
        }
        if (off_y != 0)
            set_field_screen_scroll_step(g, AXIS_Y, pixel_toward(leader_on_screen(g, leader, AXIS_Y), SCREEN_MIDDLE_Y));
        return true;
    case RECENTRING:
        set_field_leader_movement_x(g, 0);
        set_field_leader_movement_y(g, 0);
        if (off_x == 0)
            set_field_screen_scroll_step(g, AXIS_X, 0);
        if (off_y == 0) {
            set_field_screen_scroll_step(g, AXIS_Y, 0);
            if (off_x == 0) {
                set_field_screen_recentring(g, RECENTRED);
                return false;
            }
        }
        return true;
    default:
        set_field_screen_recentring(g, RECENTRED);
        return false;
    }
}

/* After the screen's first step back, the recentring is under way. */
void screen_recentring_moved(GameState *g)
{
    if (field_screen_recentring(g) == RECENTRE_STARTS)
        set_field_screen_recentring(g, RECENTRING);
}

/* ---- A map's start ------------------------------------------------------------------------------- */

enum { FIRST_LEVEL = 1 };

MapPlace field_start_place(const GameState *g)
{
    return map_place_at((uint16_t)((location_y(g) << 8 | location_x(g)) & field_map_index_mask(g)));
}

// $C0:9DC3
/* A map's start, after the objects' openings: the party's trail starts
 * afresh, and the leader takes the level and the ground of the tile it
 * starts on (the first level where the tile has none), and the whole party
 * the tile's layering. The screen's step up or down slows to a walk on a
 * climb, as a walk's does. */
void party_start_on_the_map(GameState *g)
{
    party_trail_start(g);
    ORIGINAL_EFFECT(forget_walk_cells());
    ORIGINAL_EFFECT(record_cell(CELL_LOOK_X, FIELD_START_FINE_X));
    ORIGINAL_EFFECT(record_cell(CELL_LOOK_Y, FIELD_START_FINE_Y));
    Ground ground = ground_at(g, field_start_place(g), FIELD_START_FINE_X, FIELD_START_FINE_Y);
    if (ground.climb) {
        int16_t vertical = (int8_t)field_screen_scroll_step(g, AXIS_Y);
        set_field_screen_scroll_step(g, AXIS_Y, (uint8_t)slowed_to_a_climb(vertical));
    }
    set_field_leader_level(g, ground.level != NO_LEVEL ? ground.level : FIRST_LEVEL);
    /* the other way round from a script's layering, as the walk takes them;
     * the members take the tile's lower half's for both of theirs (the
     * original lays them out with what it laid the leader's upper half
     * with) */
    uint8_t lower_half = tile_priority(ground.tile.upper), upper_half = tile_priority(ground.tile.lower);
    party_place_layered(g, PARTY_LEADER, upper_half, lower_half);
    party_place_layered(g, PARTY_FIRST_MEMBER, upper_half, upper_half);
    party_place_layered(g, PARTY_SECOND_MEMBER, upper_half, upper_half);
    set_leader_ground(g, leader_ground_of(ground.climb, false));
    ORIGINAL_EFFECT(leave_walk_cells(g));
}

/* ---- The entries -------------------------------------------------------------------------------- */

/* The walk and the screen's recentring run the layers' scroll and the
 * screen's edges after them, the original's ($C0:91AC, $C0:93E1), on the
 * layers' page with the field's pushed. */
enum { NOTHING_READ = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static bool scroll_the_layers(Snes *s, uint32_t scroll_site, uint32_t edges_site)
{
    BridgeMachine m = { false, FIELD_LAYERS_PAGE, s->cpu.db };
    return bridge_call(s, scroll_site, 0xC091AC, NOTHING_READ, m) && bridge_call(s, edges_site, 0xC093E1, NOTHING_READ, m);
}

static void on_the_layers_page(Snes *s) { m_push16(s, s->cpu.d), s->cpu.d = FIELD_LAYERS_PAGE; }
static void back_on_the_field_page(Snes *s) { s->cpu.d = m_pull16(s); }

void field_walk_the_leader(Snes *s)
{
    leader_walks(state_of(s));
    on_the_layers_page(s);
    if (!scroll_the_layers(s, 0xC08852, 0xC08855))
        return;
    back_on_the_field_page(s);
    m_rts(s);
}

void field_recentre_the_screen(Snes *s)
{
    on_the_layers_page(s);
    bool starts = (Recentring)field_screen_recentring(state_of(s)) == RECENTRE_STARTS;
    bool moves = screen_recentres(state_of(s));
    bool came_back = true;
    if (moves && starts)
        came_back = scroll_the_layers(s, 0xC088B9, 0xC088BC);
    else if (moves)
        came_back = scroll_the_layers(s, 0xC088DD, 0xC088E0);
    if (!came_back)
        return;
    if (moves)
        screen_recentring_moved(state_of(s));
    back_on_the_field_page(s);
    m_rts(s);
}

/* The guards. The walk reads the ROM's tables and the requests through the
 * data bank, on the field's page; the leader is an object the tables hold,
 * as the game keeps its place (a word whose high byte is 0); the search for
 * objects starts somewhere (a count of 0, or 128, would start it on object
 * 0, which no map has); the map's tables are indexed
 * within their 64 by 64; and the leader stands clear of the 16-bit range's
 * end, where the original's sums carry into the step. */
enum { WITHIN_THE_MAPS = 0x3F3F, CLEAR_OF_THE_END = 0xFF00 };

static bool walk_fits(const GameState *g)
{
    FieldLink leader = party_member_link(g, PARTY_LEADER);
    if (field_party_objects_high(g, PARTY_LEADER) != 0)
        return false;
    if (!field_link_names_object(leader)) {
        Velocity walk;
        Velocity first = pad_and_pull(g, &walk);
        return first.x == 0 && first.y == 0;
    }
    FieldPosition at = object_position(g, field_linked_object(leader));
    return object_search_start(g) != 0 && (field_map_index_mask(g) & ~WITHIN_THE_MAPS) == 0 &&
           at.x < CLEAR_OF_THE_END && at.y < CLEAR_OF_THE_END;
}

bool field_walk_can_run(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    bool ok = walk_fits(state_of(s));
    vars_peek(false);
    return ok;
}

bool field_recentring_can_run(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = field_link_names_object(party_member_link(g, PARTY_LEADER));
    vars_peek(false);
    return ok;
}

/* The map's start's part: the party's places are objects the tables hold,
 * or the marker of an empty place (which the original layers as the slot
 * past the tables: party_place_layered), their high bytes 0, and the map's
 * tables indexed within their 64 by 64. */
bool party_start_fits(const GameState *g)
{
    for (PartyPlace place = PARTY_LEADER; place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        bool empty_marker = field_link_byte(link) == field_link_byte(field_no_link());
        if ((!field_link_names_object(link) && !empty_marker) || field_party_objects_high(g, place) != 0)
            return false;
    }
    return (field_map_index_mask(g) & ~WITHIN_THE_MAPS) == 0;
}
