/* Scripts setting up their objects: how fast one walks, whether others can
 * walk through it, what blocks it, how far its moves go, where it stands, how
 * its sprite is layered against the map, and whether it has a sprite at all;
 * and asking where an object stands.
 *
 * These are the field's objects (game/field/): the helpers at the top are
 * the few words of the field's object state event needs, until the field's
 * object module lands. */
#include "event/leaves.h"
#include "event/script.h"
#include "event/variables.h"

/* ---- The field's objects ----------------------------------------------------------------- */

/* Where a sprite is drawn against the map's layers: behind or in front of
 * the parts of the map it overlaps, a priority for its upper and its lower
 * half. An object can follow the map instead, taking the priorities the map's
 * tiles give where it stands. */
enum { FOLLOWS_THE_MAP = 0x80 };

/* The map's two attribute tables are interleaved a row at a time, the second
 * $40 bytes after the first (field_tile_attributes). */
enum { SECOND_ATTRIBUTE_TABLE = 0x40 };

/* The sprite priority a tile's attributes give: bit 6 raises it. */
static uint8_t priority_from(uint8_t attributes) { return (uint8_t)((attributes & 0x40) >> 2 | 0x20); }

typedef uint16_t Tile;   /* row << 8 | column */

static Tile tile_of(const GameState *g, EventObject object)
{
    return (Tile)(field_object_y_tile(g, object) << 8 | field_object_x_tile(g, object));
}

// $C0:4BA3
/* The object takes its priorities from the map where it stands: its lower
 * half's from the first attribute table, its upper half's from the second.
 * (The original keeps the lower one in its scratch cell.) */
static void layer_as_the_map(GameState *g, EventObject object, Tile tile)
{
    uint8_t lower = priority_from(field_tile_attributes_byte(g, tile));
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, lower));
    uint8_t upper = priority_from(field_tile_attributes_byte(g, (unsigned)tile + SECOND_ATTRIBUTE_TABLE));
    set_field_object_sprite_priority(g, object, (uint16_t)(upper | lower << 8));
}

static bool follows_the_map(const GameState *g, EventObject object)
{
    return field_object_priority_mode(g, object) & FOLLOWS_THE_MAP;
}

/* Whether an object has a sprite on the map. */
typedef enum { SPRITE_HIDDEN = 0x00, SPRITE_SHOWN = 0x01, NO_SPRITE = 0x80 } SpriteState;

/* Where an object stands: a tile, and where on it in sixteenths of a pixel. */
enum { TILE_MIDDLE = 0x80, TILE_BOTTOM = 0xFF };

static void put_on_tile(GameState *g, EventObject object, uint8_t x, uint8_t y, bool centred)
{
    set_field_object_x_tile(g, object, x);
    if (centred)
        set_field_object_x_fine(g, object, TILE_MIDDLE);
    set_field_object_y_tile(g, object, y);
    set_field_object_y_fine(g, object, TILE_BOTTOM);
}

/* A position as the scripts give one: the tile in the high byte, the place
 * on it in the low. */
static void put_at(GameState *g, EventObject object, uint16_t x, uint16_t y)
{
    set_field_object_x_fine(g, object, (uint8_t)x);
    set_field_object_x_tile(g, object, (uint8_t)(x >> 8));
    set_field_object_y_fine(g, object, (uint8_t)y);
    set_field_object_y_tile(g, object, (uint8_t)(y >> 8));
}

/* How many steps the object's moves take; whether calls skip it (bit 7) is
 * kept with it. */
enum { CALLS_SKIP = 0x80 };

static void set_steps_per_move(GameState *g, EventObject object, uint8_t steps)
{
    steps |= field_object_step_length(g, object) & CALLS_SKIP;
    set_field_object_step_length(g, object, steps);
    set_field_object_step_count(g, object, steps);
}

/* The sprite's layering set by a script: its placement for a battle is left
 * to the battle ($FF), and its animation's frame timing starts again. */
enum { BATTLE_PLACES_IT = 0xFF };

static void relayer(GameState *g, EventObject object, uint8_t how)
{
    set_field_object_priority_mode(g, object, how);
    if (!(how & FOLLOWS_THE_MAP))
        set_field_object_sprite_priority(g, object, (uint16_t)(((how << 4) & 0x30) | ((how & 0x30) << 8)));
}

static void after_relayering(GameState *g, EventObject object)
{
    set_field_object_battle_info(g, object, BATTLE_PLACES_IT);
    set_field_object_frame_delay(g, object, 0);
}

/* ---- The commands ------------------------------------------------------------------------ */

// $C0:4B49
/* How fast the object walks. */
static EventFlow set_speed(GameState *g, EventObject self, uint8_t speed)
{
    set_field_object_speed(g, self, speed);
    return next_command();
}

// $C0:4867
/* Whether other objects bump into this one or pass through it. */
static EventFlow set_solid(GameState *g, EventObject self, uint8_t solid)
{
    set_field_object_solid(g, self, solid);
    return next_command();
}

// $C0:6295
/* What stops the object's moves: bit 0 the map's walls, bit 1 other objects. */
static EventFlow set_blocked_by(GameState *g, EventObject self, uint8_t what)
{
    set_field_object_blocking(g, self, what);
    return next_command();
}

// $C0:62A4
/* What the object does on arriving at a move's end: line itself up with the
 * tile, or with the object it went to. */
static EventFlow set_arrival(GameState *g, EventObject self, uint8_t flags)
{
    set_field_object_arrival_flags(g, self, flags);
    return next_command();
}

// $C0:4876
/* How many steps the object's moves take: the count given plus one. Whether
 * calls skip the object (bit 7) is kept. */
static EventFlow set_move_length(GameState *g, EventObject self, uint8_t count)
{
    uint8_t steps = (uint8_t)(count + 1);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, steps));
    set_steps_per_move(g, self, steps);
    return next_command();
}

// $C0:4B74
/* Put the object on a tile: at its left edge, or its middle, and a
 * sixteenth of a pixel above its bottom; layered as the map is there if it
 * follows the map. */
static EventFlow stand_on_tile(GameState *g, EventObject self, uint8_t x, uint8_t y, bool centred)
{
    put_on_tile(g, self, x, y, centred);
    /* the original keeps the tile in its scratch cell, before the map's
     * priority takes the low byte's place */
    if (follows_the_map(g, self))
        layer_as_the_map(g, self, (Tile)(y << 8 | x));
    else
        ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, x));
    ORIGINAL_EFFECT(keep_high_byte(g, KEPT_FIRST, y));
    return next_command();
}

// $C0:4C27
/* Put the object at a position (in sixteenths of a pixel, the tile in the
 * high byte); layered as the map is there if it follows the map. */
static EventFlow place(GameState *g, EventObject self, uint16_t x, uint16_t y)
{
    put_at(g, self, x, y);
    if (follows_the_map(g, self))
        layer_as_the_map(g, self, tile_of(g, self));
    return next_command();
}

// $C0:4C74
/* Layer the object's sprite against the map: following the map where it
 * stands (bit 7), or with its upper half at bits 4-5's priority and its lower
 * at bits 0-1's. */
static EventFlow set_layering(GameState *g, EventObject self, uint8_t how)
{
    relayer(g, self, how);
    if (how & FOLLOWS_THE_MAP)
        layer_as_the_map(g, self, tile_of(g, self));
    else
        ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, how));
    after_relayering(g, self);
    return next_command();
}

// $C0:4CD9
/* Show an object's sprite, hide it, or take it away. Ends the turn. */
static EventFlow set_sprite(GameState *g, EventObject object, SpriteState state)
{
    set_field_object_sprite_flag(g, object, state);
    return end_turn();
}

// $C0:65B2
/* Variables learn the tile an object stands on. */
static EventFlow note_tile(GameState *g, EventVariable column, EventVariable row, EventObject object)
{
    uint8_t x = field_object_x_tile(g, object), y = field_object_y_tile(g, object);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, x));
    ORIGINAL_EFFECT(keep_byte(g, KEPT_SECOND, y));
    set_variable_byte(g, column, x);
    set_variable_byte(g, row, y);
    return next_command();
}

static EventFlow run_set_speed(GameState *g, const EventArgs *a) { return set_speed(g, a->self, a->arg[0]); }
static EventFlow run_set_solid(GameState *g, const EventArgs *a) { return set_solid(g, a->self, a->arg[0]); }
static EventFlow run_set_blocked_by(GameState *g, const EventArgs *a) { return set_blocked_by(g, a->self, a->arg[0]); }
static EventFlow run_set_arrival(GameState *g, const EventArgs *a) { return set_arrival(g, a->self, a->arg[0]); }
static EventFlow run_set_move_length(GameState *g, const EventArgs *a) { return set_move_length(g, a->self, a->arg[0]); }
static EventFlow run_stand_on_tile(GameState *g, const EventArgs *a)
{
    return stand_on_tile(g, a->self, a->arg[0], a->arg[1], a->arg[2]);
}
static EventFlow run_place(GameState *g, const EventArgs *a) { return place(g, a->self, a->arg[0], a->arg[1]); }
static EventFlow run_set_layering(GameState *g, const EventArgs *a) { return set_layering(g, a->self, a->arg[0]); }
static EventFlow run_set_sprite(GameState *g, const EventArgs *a) { return set_sprite(g, a->arg[0], a->arg[1]); }
static EventFlow run_note_tile(GameState *g, const EventArgs *a) { return note_tile(g, a->arg[0], a->arg[1], a->arg[2]); }

EVENT_OPCODES(event_properties_opcodes,
    { 0x89, run_set_speed, { BYTE(1) }, 2, .routine = 0xC04B49, .parse = 1 },
    { 0x8A, run_set_speed, { VARIABLE_BYTE(1) }, 2, .routine = 0xC04B58, .parse = 1 },
    { 0x84, run_set_solid, { BYTE(1) }, 2, .routine = 0xC04867, .parse = 2 },
    { 0x0D, run_set_blocked_by, { BYTE(1) }, 2, .routine = 0xC06295, .parse = 1 },
    { 0x0E, run_set_arrival, { BYTE(1) }, 2, .routine = 0xC062A4, .parse = 1 },
    { 0x87, run_set_move_length, { BYTE(1) }, 2, .routine = 0xC04876, .parse = 1 },
    { 0x8B, run_stand_on_tile, { BYTE(1), BYTE(2), CONSTANT(true) }, 3, .routine = 0xC04B74, .parse = 2 },
    { 0x8C, run_stand_on_tile, { VARIABLE_BYTE(1), VARIABLE_BYTE(2), CONSTANT(false) }, 3, .routine = 0xC04BC3,
      .parse = 2 },
    { 0x8D, run_place, { WORD(1), WORD(3) }, 5, .routine = 0xC04C27, .parse = 3 },
    { 0x8E, run_set_layering, { BYTE(1) }, 2, .routine = 0xC04C74, .parse = 1 },
    { 0x90, run_set_sprite, { SELF, CONSTANT(SPRITE_SHOWN) }, 1, .routine = 0xC04CD5 },
    { 0x91, run_set_sprite, { SELF, CONSTANT(SPRITE_HIDDEN) }, 1, .routine = 0xC04CE0 },
    { 0x7E, run_set_sprite, { SELF, CONSTANT(NO_SPRITE) }, 1, .routine = 0xC04CE6 },
    { 0x7C, run_set_sprite, { OBJECT(1), CONSTANT(SPRITE_SHOWN) }, 2, .routine = 0xC04CEC, .parse = 1 },
    { 0x7D, run_set_sprite, { OBJECT(1), CONSTANT(SPRITE_HIDDEN) }, 2, .routine = 0xC04CF9, .parse = 1 },
    { 0x21, run_note_tile, { VARIABLE(2), VARIABLE(3), OBJECT(1) }, 4, .routine = 0xC065AA, .parse = 4 },
    { 0x22, run_note_tile, { VARIABLE(2), VARIABLE(3), MEMBER_OBJECT(1) }, 4, .routine = 0xC065EB, .parse = 4 });
