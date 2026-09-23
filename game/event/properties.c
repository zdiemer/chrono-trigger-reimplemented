/* Event commands: the running object's properties -- speed, solidity, step
 * length, where it stands, the priority its sprites are drawn with, and
 * whether it has a sprite at all. */
#include "event/bridge.h"

enum { PRIORITY_FROM_MAP = 0x80 };

/* The map's two attribute tables are interleaved a row at a time, the second
 * $40 bytes after the first (field_tile_attributes). */
enum { SECOND_ATTRIBUTE_TABLE = 0x40 };

// $C0:4B49
/* $89: set this object's speed to operand 1. */
static EventStep set_speed(GameState *g, const EventCommand *c)
{
    set_field_object_speed(g, current_object(g), operand(g, c, 1));
    return next_command(c, 2, 1);
}

// $C0:4B58
/* $8A: set this object's speed from variable (operand 1). */
static EventStep set_speed_from_variable(GameState *g, const EventCommand *c)
{
    set_field_object_speed(g, current_object(g), var8(g, variable_operand(g, c, 1)));
    return next_command(c, 2, 1);
}

// $C0:4867
/* $84: set whether this object blocks other objects' moves (operand 1). */
static EventStep set_solidity(GameState *g, const EventCommand *c)
{
    set_field_object_solid(g, current_object(g), operand(g, c, 1));
    return next_command(c, 2, 2);
}

// $C0:4876
/* $87: set the steps this object's moves take to operand 1 + 1, keeping the
 * flag that makes calls skip it (bit 7). */
static EventStep set_step_count(GameState *g, const EventCommand *c)
{
    unsigned object = current_object(g);
    uint8_t steps = (uint8_t)(operand(g, c, 1) + 1);
    keep8(g, SCRATCH_D9, steps);
    steps |= field_object_step_length(g, object) & 0x80;
    set_field_object_step_length(g, object, steps);
    set_field_object_step_count(g, object, steps);
    return next_command(c, 2, 1);
}

/* The sprite priority an attribute byte of the map gives: its bit 6 picks
 * priority 3 over 2 (bits 4-5). */
static uint8_t priority_of(uint8_t attributes) { return (uint8_t)((attributes & 0x40) >> 2 | 0x20); }

// $C0:4BA3
/* Take the object's two sprite priorities from the map's attributes at the
 * tile it stands on (row << 8 | column): the second from the first attribute
 * table, the first from the second. */
static void priority_from_map(GameState *g, unsigned object, uint16_t tile)
{
    uint8_t second = priority_of(field_tile_attributes_byte(g, tile));
    keep8(g, SCRATCH_D9, second);
    uint8_t first = priority_of(field_tile_attributes_byte(g, (unsigned)tile + SECOND_ATTRIBUTE_TABLE));
    set_field_object_sprite_priority(g, object, (uint16_t)(first | second << 8));
}

static uint16_t tile_of(const GameState *g, unsigned object)
{
    return (uint16_t)(field_object_y_tile(g, object) << 8 | field_object_x_tile(g, object));
}

/* Stand the object on a tile: at the tile's top left, a sixteenth of a pixel
 * short of it vertically, taking the map's priorities there if its priority
 * follows the map. The tile is kept in the scratch cells. */
static void place_on_tile(GameState *g, unsigned object, uint8_t x, uint8_t (*y_of)(const GameState *, const EventCommand *),
                          const EventCommand *c)
{
    set_field_object_x_tile(g, object, x);
    keep8(g, SCRATCH_D9, x);
    uint8_t y = y_of(g, c);
    set_field_object_y_tile(g, object, y);
    keep8(g, SCRATCH_DA, y);
    set_field_object_y_fine(g, object, 0xFF);
    if (field_object_priority_mode(g, object) & PRIORITY_FROM_MAP)
        priority_from_map(g, object, scratch16(g, SCRATCH_D9));
}

// $C0:4B74
/* $8B: stand this object on the tile at operands (X, Y). */
static uint8_t second_operand(const GameState *g, const EventCommand *c) { return operand(g, c, 2); }

static EventStep set_tile_position(GameState *g, const EventCommand *c)
{
    unsigned object = current_object(g);
    uint8_t x = operand(g, c, 1);
    set_field_object_x_tile(g, object, x);
    set_field_object_x_fine(g, object, 0x80);
    place_on_tile(g, object, x, second_operand, c);
    return next_command(c, 3, 2);
}

// $C0:4BC3
/* $8C: as $8B with the tile from variables (operands 1 and 2), leaving the
 * object's fine X as it is. */
static uint8_t second_variable(const GameState *g, const EventCommand *c)
{
    return var8(g, variable_operand(g, c, 2));
}

static EventStep set_tile_position_from_variables(GameState *g, const EventCommand *c)
{
    place_on_tile(g, current_object(g), var8(g, variable_operand(g, c, 1)), second_variable, c);
    return next_command(c, 3, 2);
}

// $C0:4C27
/* $8D: set this object's position (operands 1-2 and 3-4, 1/16 pixel, the
 * tile in the high byte), taking the map's priorities there if its priority
 * follows the map. */
static EventStep set_position(GameState *g, const EventCommand *c)
{
    unsigned object = current_object(g);
    uint16_t x = operand_word(g, c, 1);
    set_field_object_x_fine(g, object, (uint8_t)x);
    set_field_object_x_tile(g, object, (uint8_t)(x >> 8));
    uint16_t y = operand_word(g, c, 3);
    set_field_object_y_fine(g, object, (uint8_t)y);
    set_field_object_y_tile(g, object, (uint8_t)(y >> 8));
    if (field_object_priority_mode(g, object) & PRIORITY_FROM_MAP)
        priority_from_map(g, object, tile_of(g, object));
    return next_command(c, 5, 3);
}

// $C0:4C74
/* $8E: set how this object's sprites are prioritized (operand 1): with bit 7
 * from the map where it stands, otherwise the upper half at bits 4-5's
 * priority and the lower at bits 0-1's. */
static EventStep set_sprite_priority(GameState *g, const EventCommand *c)
{
    unsigned object = current_object(g);
    uint8_t mode = operand(g, c, 1);
    set_field_object_priority_mode(g, object, mode);
    if (mode & PRIORITY_FROM_MAP) {
        priority_from_map(g, object, tile_of(g, object));
    } else {
        keep8(g, SCRATCH_D9, mode);
        set_field_object_sprite_priority(g, object, (uint16_t)(((mode << 4) & 0x30) | ((mode & 0x30) << 8)));
    }
    set_field_object_battle_info(g, object, 0xFF);
    set_field_object_frame_delay(g, object, 0);
    return next_command(c, 2, 1);
}

/* Whether an object has a sprite: 1 it has, 0 or $80 it hasn't. Ends the turn. */
enum { SPRITE_SHOWN = 0x01, SPRITE_HIDDEN = 0x00, SPRITE_NONE = 0x80 };

// $C0:4CD9
static void set_sprite(GameState *g, unsigned object, uint8_t flag) { set_field_object_sprite_flag(g, object, flag); }

// $C0:4CD5
/* $90: give this object a sprite. */
static EventStep show_sprite(GameState *g, const EventCommand *c)
{
    set_sprite(g, current_object(g), SPRITE_SHOWN);
    return end_turn(c, 1, 0);
}

// $C0:4CE0
/* $91: take this object's sprite away. */
static EventStep hide_sprite(GameState *g, const EventCommand *c)
{
    set_sprite(g, current_object(g), SPRITE_HIDDEN);
    return end_turn(c, 1, 0);
}

// $C0:4CE6
/* $7E: this object has no sprite ($80). */
static EventStep set_no_sprite(GameState *g, const EventCommand *c)
{
    set_sprite(g, current_object(g), SPRITE_NONE);
    return end_turn(c, 1, 0);
}

// $C0:4CEC
/* $7C: give the object in operand 1 a sprite. */
static EventStep show_object_sprite(GameState *g, const EventCommand *c)
{
    set_sprite(g, object_operand(g, c, 1), SPRITE_SHOWN);
    return end_turn(c, 2, 1);
}

// $C0:4CF9
/* $7D: take the sprite of the object in operand 1 away. */
static EventStep hide_object_sprite(GameState *g, const EventCommand *c)
{
    set_sprite(g, object_operand(g, c, 1), SPRITE_HIDDEN);
    return end_turn(c, 2, 1);
}

EVENT_COMMAND(event_cmd_set_speed, set_speed)
EVENT_COMMAND(event_cmd_set_speed_from_variable, set_speed_from_variable)
EVENT_COMMAND(event_cmd_set_solidity, set_solidity)
EVENT_COMMAND(event_cmd_set_step_count, set_step_count)
EVENT_COMMAND(event_cmd_set_tile_position, set_tile_position)
EVENT_COMMAND(event_cmd_set_tile_position_from_variables, set_tile_position_from_variables)
EVENT_COMMAND(event_cmd_set_position, set_position)
EVENT_COMMAND(event_cmd_set_sprite_priority, set_sprite_priority)
EVENT_COMMAND(event_cmd_show_sprite, show_sprite)
EVENT_COMMAND(event_cmd_hide_sprite, hide_sprite)
EVENT_COMMAND(event_cmd_set_no_sprite, set_no_sprite)
EVENT_COMMAND(event_cmd_show_object_sprite, show_object_sprite)
EVENT_COMMAND(event_cmd_hide_object_sprite, hide_object_sprite)
