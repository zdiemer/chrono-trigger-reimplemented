/* Event commands: which way objects face (0-3). Setting a facing restarts
 * the object's animation frame delay, and ends the turn. */
#include "event/bridge.h"
#include "event/event_data.h"

// $C0:554D
static void set_facing(GameState *g, unsigned object, uint8_t facing)
{
    set_field_object_facing(g, object, facing);
    set_field_object_frame_delay(g, object, 0);
}

/* The running object faces `facing`. */
static EventStep face(GameState *g, const EventCommand *c, uint8_t facing, uint16_t length)
{
    set_facing(g, current_object(g), facing);
    return end_turn(c, length, length - 1);
}

// $C0:5535
/* $0F: face direction 0. */
static EventStep face_0(GameState *g, const EventCommand *c) { return face(g, c, 0, 1); }

// $C0:5539
/* $17: face direction 1. */
static EventStep face_1(GameState *g, const EventCommand *c) { return face(g, c, 1, 1); }

// $C0:553D
/* $1B: face direction 2. */
static EventStep face_2(GameState *g, const EventCommand *c) { return face(g, c, 2, 1); }

// $C0:5541
/* $1D: face direction 3. */
static EventStep face_3(GameState *g, const EventCommand *c) { return face(g, c, 3, 1); }

// $C0:5545
/* $A6: face the direction in operand 1. */
static EventStep face_operand(GameState *g, const EventCommand *c) { return face(g, c, operand(g, c, 1), 2); }

// $C0:5557
/* $A7: face the direction in variable (operand 1). */
static EventStep face_variable(GameState *g, const EventCommand *c)
{
    return face(g, c, var8(g, variable_operand(g, c, 1)), 2);
}

/* The object in operand 1 faces `facing`. */
static EventStep object_face(GameState *g, const EventCommand *c, uint8_t facing)
{
    set_facing(g, object_operand(g, c, 1), facing);
    return end_turn(c, 2, 1);
}

// $C0:556C
/* $1E: make the object in operand 1 face direction 0. */
static EventStep object_face_0(GameState *g, const EventCommand *c) { return object_face(g, c, 0); }

// $C0:5579
/* $1F: make the object in operand 1 face direction 1. */
static EventStep object_face_1(GameState *g, const EventCommand *c) { return object_face(g, c, 1); }

// $C0:5586
/* $25: make the object in operand 1 face direction 2. */
static EventStep object_face_2(GameState *g, const EventCommand *c) { return object_face(g, c, 2); }

// $C0:5593
/* $26: make the object in operand 1 face direction 3. */
static EventStep object_face_3(GameState *g, const EventCommand *c) { return object_face(g, c, 3); }

/* Set up the tiles field_direction_to_target works from: the running
 * object's and the target's, or where both stand on the same tile, their
 * positions within it. The target is kept in the scratch cell. */
static void aim_at(GameState *g, unsigned target)
{
    unsigned self = current_object(g);
    uint8_t target_x = field_object_x_tile(g, target), target_y = field_object_y_tile(g, target);
    uint8_t own_x = field_object_x_tile(g, self), own_y = field_object_y_tile(g, self);
    keep16(g, SCRATCH_D9, (uint16_t)(target * 2));
    if (own_x == target_x && own_y == target_y) {
        own_x = field_object_x_fine(g, self), own_y = field_object_y_fine(g, self);
        target_x = field_object_x_fine(g, target), target_y = field_object_y_fine(g, target);
    }
    set_field_own_tile_x(g, own_x);
    set_field_own_tile_y(g, own_y);
    set_field_target_tile_x(g, target_x);
    set_field_target_tile_y(g, target_y);
}

/* Back from a call-out, which returns to `returns_to`: true if the callee
 * left the machine as the command runs (event_machine_back); otherwise the
 * port stops there, declaring it, and the original carries on. */
static bool back(Snes *s, uint32_t returns_to, bool x8)
{
    if (event_machine_back(s, x8))
        return true;
    m_stop_at_own(s, returns_to, 0);
    return false;
}

// $C0:55A9
/* Face the object `target`, unless it is disabled, which skips the command.
 * The direction is field_direction_to_target's ($C0:ABA2), which the port
 * calls out to; it leaves the direction's high byte clear, so the facing is
 * the table's for its low byte. The command's offset after the call is the
 * one the callee leaves in Y, as the original's is. */
static void face_toward(Snes *s, unsigned target)
{
    GameState *g = state_of(s);
    EventCommand c = event_command_at(s);
    if (field_object_state_state(g, target) & 0x80) {
        event_return(s, next_command(&c, 2, 0));
        return;
    }
    aim_at(g, target);
    m_jsr_dc(s, 0xC055E6, 0xC0ABA2, DC_A | DC_X | DC_N | DC_Z | DC_C);
    if (!back(s, 0xC055E9, false))
        return;
    c = event_command_at(s);
    set_facing(g, current_object(g), facing_for_direction(g, field_direction(g)));
    event_return(s, end_turn(&c, 2, 0));
}

// $C0:55A0
/* $A8: face the object in operand 1. */
void event_cmd_face_object(Snes *s)
{
    EventCommand c = event_command_at(s);
    face_toward(s, object_operand(state_of(s), &c, 1));
}

// $C0:5605
/* $A9: face party member (operand 1). */
void event_cmd_face_party_member(Snes *s)
{
    EventCommand c = event_command_at(s);
    face_toward(s, member_object(state_of(s), &c, 1));
}

EVENT_COMMAND(event_cmd_face_0, face_0)
EVENT_COMMAND(event_cmd_face_1, face_1)
EVENT_COMMAND(event_cmd_face_2, face_2)
EVENT_COMMAND(event_cmd_face_3, face_3)
EVENT_COMMAND(event_cmd_face_operand, face_operand)
EVENT_COMMAND(event_cmd_face_variable, face_variable)
EVENT_COMMAND(event_cmd_object_face_0, object_face_0)
EVENT_COMMAND(event_cmd_object_face_1, object_face_1)
EVENT_COMMAND(event_cmd_object_face_2, object_face_2)
EVENT_COMMAND(event_cmd_object_face_3, object_face_3)
