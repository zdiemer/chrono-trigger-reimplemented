/* Event commands: reading what the game knows into the script -- the running
 * object's result value, and an object's tile or facing into variables. */
#include "event/bridge.h"

// $C0:653B
/* $19: set this object's result value from 16-bit variable (operand 1). */
static EventStep set_result_from_variable(GameState *g, const EventCommand *c)
{
    set_event_object_result(g, current_object(g), var16(g, variable_operand(g, c, 1)));
    return next_command(c, 2, 1);
}

// $C0:654E
/* $1C: set this object's result value from the word of event memory at
 * operands 1-2. */
static EventStep set_result_from_memory(GameState *g, const EventCommand *c)
{
    set_event_object_result(g, current_object(g), memory16(g, operand_word(g, c, 1)));
    return next_command(c, 3, 2);
}

// $C0:658F
/* $20: variable (operand 1) = the character leading the party. */
static EventStep get_party_leader(GameState *g, const EventCommand *c)
{
    set_var8(g, variable_operand(g, c, 1), party_members(g, 0));
    return next_command(c, 2, 2);
}

// $C0:65B2
/* Variables (operands 2 and 3) = the tile the object stands on. */
static EventStep get_tile(GameState *g, const EventCommand *c, unsigned object)
{
    uint8_t x = field_object_x_tile(g, object), y = field_object_y_tile(g, object);
    keep8(g, SCRATCH_D9, x);
    keep8(g, SCRATCH_DB, y);
    set_var8(g, variable_operand(g, c, 2), x);
    set_var8(g, variable_operand(g, c, 3), y);
    return next_command(c, 4, 4);
}

// $C0:65AA
/* $21: the tile of the object in operand 1. */
static EventStep get_object_tile(GameState *g, const EventCommand *c)
{
    return get_tile(g, c, object_operand(g, c, 1));
}

// $C0:65EB
/* $22: the tile of party member (operand 1). */
static EventStep get_party_member_tile(GameState *g, const EventCommand *c)
{
    return get_tile(g, c, member_object(g, c, 1));
}

// $C0:6600
/* Variable (operand 2) = the way the object faces. */
static EventStep get_facing(GameState *g, const EventCommand *c, unsigned object)
{
    uint8_t facing = field_object_facing(g, object);
    keep8(g, SCRATCH_D9, facing);
    set_var8(g, variable_operand(g, c, 2), facing);
    return next_command(c, 3, 3);
}

// $C0:65F8
/* $23: the facing of the object in operand 1. */
static EventStep get_object_facing(GameState *g, const EventCommand *c)
{
    return get_facing(g, c, object_operand(g, c, 1));
}

// $C0:661F
/* $24: the facing of party member (operand 1). */
static EventStep get_party_member_facing(GameState *g, const EventCommand *c)
{
    return get_facing(g, c, member_object(g, c, 1));
}

EVENT_COMMAND(event_cmd_set_result_from_variable, set_result_from_variable)
EVENT_COMMAND(event_cmd_set_result_from_memory, set_result_from_memory)
EVENT_COMMAND(event_cmd_get_party_leader, get_party_leader)
EVENT_COMMAND(event_cmd_get_object_tile, get_object_tile)
EVENT_COMMAND(event_cmd_get_party_member_tile, get_party_member_tile)
EVENT_COMMAND(event_cmd_get_object_facing, get_object_facing)
EVENT_COMMAND(event_cmd_get_party_member_facing, get_party_member_facing)
