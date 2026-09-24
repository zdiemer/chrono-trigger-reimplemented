/* Scripts setting up their objects: how fast one walks, whether others can
 * walk through it, what blocks it, how far its moves go, where it stands, how
 * its sprite is layered against the map, and whether it has a sprite at all;
 * and asking where an object stands. Each is the field's object's
 * (field/object.h). */
#include "event/leaves.h"
#include "event/script.h"
#include "event/variables.h"
#include "field/layers.h"

// $C0:4B49
/* How fast the object walks. */
static EventFlow set_speed(GameState *g, FieldObject self, uint8_t speed)
{
    object_set_speed(g, self, speed);
    return next_command();
}
EVENT_OPERATION(set_speed, self, speed)

// $C0:4867
/* Whether other objects bump into this one or pass through it. */
static EventFlow set_solidity(GameState *g, FieldObject self, bool solid)
{
    object_set_solidity(g, self, solid);
    return next_command();
}
EVENT_OPERATION(set_solidity, self, solid)

// $C0:6295
/* What stops the object's moves: the map's blocked tiles, other objects,
 * both or neither. */
static EventFlow set_blocked_by(GameState *g, FieldObject self, ObjectBlocking blocked_by)
{
    object_set_blocked_by(g, self, blocked_by);
    return next_command();
}
EVENT_OPERATION(set_blocked_by, self, blocked_by)

// $C0:62A4
/* What the object does on arriving at a walk's end: line itself up with the
 * tile, or with the object it walked up to. */
static EventFlow set_arrival(GameState *g, FieldObject self, ObjectArrival arrival)
{
    object_set_arrival(g, self, arrival);
    return next_command();
}
EVENT_OPERATION(set_arrival, self, arrival)

// $C0:4876
/* How many steps the object's walks take at a time: the count given plus
 * one. */
static EventFlow set_move_length(GameState *g, FieldObject self, uint8_t move_length)
{
    uint8_t steps = (uint8_t)(move_length + 1);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, steps));
    object_set_move_length(g, self, steps);
    return next_command();
}
EVENT_OPERATION(set_move_length, self, move_length)

/* For the original's code and shadow mode: layering an object as the map is
 * where it stands, the original keeps its upper half's priority, as the
 * hardware's bits, in its first scratch cell. */
static void keep_upper_priority(GameState *g, MapTile tile)
{
    uint8_t upper = tile_priority(map_tile_attributes(g, (MapPlace){ tile.y, tile.x }).upper);
    keep_byte(g, KEPT_FIRST, (uint8_t)(upper << 4));
}

// $C0:4B74
/* Put the object on a tile, at its bottom edge -- centred across it, or where
 * it was across -- layered as the map is there if it follows the map. (The
 * original keeps the tile in its scratch cell, before the map's priority
 * takes the low byte's place.) */
static EventFlow stand_on_tile(GameState *g, FieldObject self, uint8_t tile_x, uint8_t tile_y, bool centred)
{
    MapTile tile = { tile_x, tile_y };
    object_put_on_tile(g, self, tile, centred);
    if (object_follows_the_map(g, self)) {
        object_layer_as_the_map(g, self, tile);
        ORIGINAL_EFFECT(keep_upper_priority(g, tile));
    } else {
        ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, tile_x));
    }
    ORIGINAL_EFFECT(keep_high_byte(g, KEPT_FIRST, tile_y));
    return next_command();
}
EVENT_OPERATION(stand_on_tile, self, tile_x, tile_y, centred)

// $C0:4C27
/* Put the object at a position (in sixteenths of a pixel, the tile in the
 * high byte); layered as the map is there if it follows the map. */
static EventFlow place(GameState *g, FieldObject self, uint16_t x, uint16_t y)
{
    object_place(g, self, (FieldPosition){ x, y });
    if (object_follows_the_map(g, self)) {
        MapTile tile = object_tile(g, self);
        object_layer_as_the_map(g, self, tile);
        ORIGINAL_EFFECT(keep_upper_priority(g, tile));
    }
    return next_command();
}
EVENT_OPERATION(place, self, x, y)

// $C0:4C74
/* Layer the object's sprite against the map: following the map where it
 * stands, or with the priorities the layering gives. The script gives the
 * layering as the byte the object keeps it in (field/object.h). */
static EventFlow set_layering(GameState *g, FieldObject self, uint8_t layering)
{
    ObjectLayering l = layering_of_byte(layering);
    object_set_layering(g, self, l);
    if (l.follows_the_map)
        ORIGINAL_EFFECT(keep_upper_priority(g, object_tile(g, self)));
    else
        ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, layering));
    return next_command();
}
EVENT_OPERATION(set_layering, self, layering)

/* The guard's part: the byte is a layering (field/object.h). */
static bool a_layering(const GameState *g, uint8_t layering)
{
    (void)g;
    return layering_byte_is_one(layering);
}
EVENT_CHECK(a_layering, layering)

// $C0:4CD9
/* Show an object's sprite, hide it, or take it away. Ends the turn. */
static EventFlow set_sprite(GameState *g, FieldObject object, ObjectSprite sprite)
{
    object_set_sprite(g, object, sprite);
    return end_turn();
}
EVENT_OPERATION(set_sprite, object, sprite)

// $C0:65B2
/* Variables learn the tile an object stands on. */
static EventFlow note_tile(GameState *g, FieldObject object, EventVariable column, EventVariable row)
{
    MapTile tile = object_tile(g, object);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, tile.x));
    ORIGINAL_EFFECT(keep_byte(g, KEPT_SECOND, tile.y));
    set_variable_byte(g, column, tile.x);
    set_variable_byte(g, row, tile.y);
    return next_command();
}
EVENT_OPERATION(note_tile, object, column, row)

EVENT_OPCODES(event_properties_opcodes,
    { 0x89, OPERATION(set_speed), .speed = OPERAND(1), .length = 2 },
    { 0x8A, OPERATION(set_speed), .speed = VARIABLE_BYTE(1), .length = 2 },
    { 0x84, OPERATION(set_solidity), .solid = FLAG(1), .length = 2 },
    { 0x0D, OPERATION(set_blocked_by), .blocked_by = OPERAND(1), .length = 2 },
    { 0x0E, OPERATION(set_arrival), .arrival = OPERAND(1), .length = 2 },
    { 0x87, OPERATION(set_move_length), .move_length = OPERAND(1), .length = 2 },
    { 0x8B, OPERATION(stand_on_tile), .tile_x = OPERAND(1), .tile_y = OPERAND(2), .centred = CONSTANT(true),
      .length = 3 },
    { 0x8C, OPERATION(stand_on_tile), .tile_x = VARIABLE_BYTE(1), .tile_y = VARIABLE_BYTE(2), .length = 3 },
    { 0x8D, OPERATION(place), .x = OPERAND_WORD(1), .y = OPERAND_WORD(3), .length = 5 },
    { 0x8E, OPERATION(set_layering), .layering = OPERAND(1), .length = 2, CHECK(a_layering) },
    { 0x90, OPERATION(set_sprite), .object = SELF, .sprite = CONSTANT(SPRITE_SHOWN), .length = 1 },
    { 0x91, OPERATION(set_sprite), .object = SELF, .sprite = CONSTANT(SPRITE_HIDDEN), .length = 1 },
    { 0x7E, OPERATION(set_sprite), .object = SELF, .sprite = CONSTANT(SPRITE_NONE), .length = 1 },
    { 0x7C, OPERATION(set_sprite), .object = OBJECT(1), .sprite = CONSTANT(SPRITE_SHOWN), .length = 2 },
    { 0x7D, OPERATION(set_sprite), .object = OBJECT(1), .sprite = CONSTANT(SPRITE_HIDDEN), .length = 2 },
    { 0x21, OPERATION(note_tile), .column = VARIABLE(2), .row = VARIABLE(3), .object = OBJECT(1), .length = 4 },
    { 0x22, OPERATION(note_tile), .column = VARIABLE(2), .row = VARIABLE(3), .object = MEMBER_OBJECT(1),
      .length = 4 });
