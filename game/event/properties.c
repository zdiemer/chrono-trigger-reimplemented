/* Scripts setting up their objects: how fast one walks, whether others can
 * walk through it, what blocks it, how far its moves go, where it stands, how
 * its sprite is layered against the map, and whether it has a sprite at all;
 * and asking where an object stands.
 *
 * These are the field's objects (game/field/): the helpers at the top are
 * the few words of the field's object state event needs, until the field's
 * object module lands. */
#include "event/leaves.h"
#include "field/layers.h"
#include "event/script.h"
#include "event/variables.h"

/* ---- The field's objects ----------------------------------------------------------------- */

/* Where a sprite is drawn against the map's layers: a priority, 0 (behind
 * everything) to 3 (in front of everything), for the upper and the lower half
 * of the object's sprite. An object can follow the map instead, taking the
 * priorities the map's tiles give where it stands. The field keeps the two
 * as the hardware's sprite attribute bits (4-5), the lower half's first. */
typedef uint8_t SpritePriority;

enum { AMONG_THE_MAP = 2, IN_FRONT_OF_THE_MAP = 3 };
enum { FOLLOWS_THE_MAP = 0x80 };   /* in the object's layering */

static void set_priorities(GameState *g, EventObject object, SpritePriority upper, SpritePriority lower)
{
    set_field_object_sprite_priority(g, object, (uint16_t)((lower << 4) | (upper << 4) << 8));
}

/* A tile raises a sprite over it in front of the map. */
enum { RAISES_SPRITES = TILE_RAISES_SPRITES };

static SpritePriority priority_from(uint8_t attributes)
{
    return attributes & RAISES_SPRITES ? IN_FRONT_OF_THE_MAP : AMONG_THE_MAP;
}

typedef uint16_t Tile;   /* row << 8 | column */

static Tile tile_of(const GameState *g, EventObject object)
{
    return (Tile)(field_object_y_tile(g, object) << 8 | field_object_x_tile(g, object));
}

// $C0:4BA3
/* The object takes its priorities from the map where it stands: its upper
 * half's from the first attribute table, its lower half's from the second.
 * (The original keeps the upper one in its scratch cell, as the field does.) */
static void layer_as_the_map(GameState *g, EventObject object, Tile tile)
{
    TileAttributes there = map_tile_attributes(g, map_place_at(tile));
    SpritePriority upper = priority_from(there.upper);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, (uint8_t)(upper << 4)));
    SpritePriority lower = priority_from(there.lower);
    set_priorities(g, object, upper, lower);
}

static bool follows_the_map(const GameState *g, EventObject object)
{
    return field_object_priority_mode(g, object) & FOLLOWS_THE_MAP;
}

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

/* The object's layering, as a script gives it: following the map (bit 7),
 * or its lower half's priority in bits 0-1 and its upper half's in bits 4-5. */
static void relayer(GameState *g, EventObject object, uint8_t how)
{
    set_field_object_priority_mode(g, object, how);
    if (!(how & FOLLOWS_THE_MAP))
        set_priorities(g, object, (SpritePriority)((how >> 4) & 3), (SpritePriority)(how & 3));
}

static void after_relayering(GameState *g, EventObject object)
{
    set_field_object_battle_info(g, object, BATTLE_PLACES_IT);
    set_field_object_frame_delay(g, object, 0);
}

/* ---- The commands ------------------------------------------------------------------------ */

// $C0:4B49
/* How fast the object walks. */
static EventFlow set_speed(GameState *g, const EventCommand *c)
{
    set_field_object_speed(g, c->self, c->speed);
    return next_command();
}

/* What an object's solidity says: whether it stands in other objects' way. */
enum { IN_OTHERS_WAY = 0x01 };

/* What stops an object's moves. */
enum { STOPPED_BY_WALLS = 0x01, STOPPED_BY_OBJECTS = 0x02 };

// $C0:4867
/* Whether other objects bump into this one (IN_OTHERS_WAY) or pass through it. */
static EventFlow set_solid(GameState *g, const EventCommand *c)
{
    set_field_object_solid(g, c->self, c->solid);
    return next_command();
}

// $C0:6295
/* What stops the object's moves: the map's walls (STOPPED_BY_WALLS), other
 * objects (STOPPED_BY_OBJECTS), both or neither. */
static EventFlow set_blocked_by(GameState *g, const EventCommand *c)
{
    set_field_object_blocking(g, c->self, c->blocked_by);
    return next_command();
}

// $C0:62A4
/* What the object does on arriving at a move's end: line itself up with the
 * tile, or with the object it went to. */
static EventFlow set_arrival(GameState *g, const EventCommand *c)
{
    set_field_object_arrival_flags(g, c->self, c->arrival);
    return next_command();
}

// $C0:4876
/* How many steps the object's moves take: the count given plus one. Whether
 * calls skip the object (bit 7) is kept. */
static EventFlow set_move_length(GameState *g, const EventCommand *c)
{
    uint8_t steps = (uint8_t)(c->move_length + 1);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, steps));
    set_steps_per_move(g, c->self, steps);
    return next_command();
}

// $C0:4B74
/* Put the object on a tile: at its left edge, or its middle, and a
 * sixteenth of a pixel above its bottom; layered as the map is there if it
 * follows the map. */
static EventFlow stand_on_tile(GameState *g, const EventCommand *c)
{
    put_on_tile(g, c->self, c->tile_x, c->tile_y, c->centred);
    /* the original keeps the tile in its scratch cell, before the map's
     * priority takes the low byte's place */
    if (follows_the_map(g, c->self))
        layer_as_the_map(g, c->self, (Tile)(c->tile_y << 8 | c->tile_x));
    else
        ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, c->tile_x));
    ORIGINAL_EFFECT(keep_high_byte(g, KEPT_FIRST, c->tile_y));
    return next_command();
}

// $C0:4C27
/* Put the object at a position (in sixteenths of a pixel, the tile in the
 * high byte); layered as the map is there if it follows the map. */
static EventFlow place(GameState *g, const EventCommand *c)
{
    put_at(g, c->self, c->x, c->y);
    if (follows_the_map(g, c->self))
        layer_as_the_map(g, c->self, tile_of(g, c->self));
    return next_command();
}

// $C0:4C74
/* Layer the object's sprite against the map: following the map where it
 * stands (bit 7), or with its upper half at bits 4-5's priority and its lower
 * at bits 0-1's. */
static EventFlow set_layering(GameState *g, const EventCommand *c)
{
    relayer(g, c->self, c->layering);
    if (c->layering & FOLLOWS_THE_MAP)
        layer_as_the_map(g, c->self, tile_of(g, c->self));
    else
        ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, c->layering));
    after_relayering(g, c->self);
    return next_command();
}

// $C0:4CD9
/* Show an object's sprite, hide it, or take it away. Ends the turn. */
static EventFlow set_sprite(GameState *g, const EventCommand *c)
{
    set_field_object_sprite_flag(g, c->object, c->sprite);
    return end_turn();
}

// $C0:65B2
/* Variables learn the tile an object stands on. */
static EventFlow note_tile(GameState *g, const EventCommand *c)
{
    uint8_t x = field_object_x_tile(g, c->object), y = field_object_y_tile(g, c->object);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, x));
    ORIGINAL_EFFECT(keep_byte(g, KEPT_SECOND, y));
    set_variable_byte(g, c->column, x);
    set_variable_byte(g, c->row, y);
    return next_command();
}

EVENT_OPCODES(event_properties_opcodes,
    { 0x89, set_speed, .speed = OPERAND(1), .length = 2, .routine = 0xC04B49 },
    { 0x8A, set_speed, .speed = VARIABLE_BYTE(1), .length = 2, .routine = 0xC04B58 },
    { 0x84, set_solid, .solid = OPERAND(1), .length = 2, .routine = 0xC04867 },
    { 0x0D, set_blocked_by, .blocked_by = OPERAND(1), .length = 2, .routine = 0xC06295 },
    { 0x0E, set_arrival, .arrival = OPERAND(1), .length = 2, .routine = 0xC062A4 },
    { 0x87, set_move_length, .move_length = OPERAND(1), .length = 2, .routine = 0xC04876 },
    { 0x8B, stand_on_tile, .tile_x = OPERAND(1), .tile_y = OPERAND(2), .centred = CONSTANT(true), .length = 3,
      .routine = 0xC04B74 },
    { 0x8C, stand_on_tile, .tile_x = VARIABLE_BYTE(1), .tile_y = VARIABLE_BYTE(2), .length = 3, .routine = 0xC04BC3 },
    { 0x8D, place, .x = OPERAND_WORD(1), .y = OPERAND_WORD(3), .length = 5, .routine = 0xC04C27 },
    { 0x8E, set_layering, .layering = OPERAND(1), .length = 2, .routine = 0xC04C74 },
    { 0x90, set_sprite, .object = SELF, .sprite = CONSTANT(SPRITE_SHOWN), .length = 1, .routine = 0xC04CD5 },
    { 0x91, set_sprite, .object = SELF, .sprite = CONSTANT(SPRITE_HIDDEN), .length = 1, .routine = 0xC04CE0 },
    { 0x7E, set_sprite, .object = SELF, .sprite = CONSTANT(NO_SPRITE), .length = 1, .routine = 0xC04CE6 },
    { 0x7C, set_sprite, .object = OBJECT(1), .sprite = CONSTANT(SPRITE_SHOWN), .length = 2, .routine = 0xC04CEC },
    { 0x7D, set_sprite, .object = OBJECT(1), .sprite = CONSTANT(SPRITE_HIDDEN), .length = 2, .routine = 0xC04CF9 },
    { 0x21, note_tile, .column = VARIABLE(2), .row = VARIABLE(3), .object = OBJECT(1), .length = 4,
      .routine = 0xC065AA },
    { 0x22, note_tile, .column = VARIABLE(2), .row = VARIABLE(3), .object = MEMBER_OBJECT(1), .length = 4,
      .routine = 0xC065EB });
