/* Which way objects face: turning an object to a direction, to face another
 * object or a party member, and asking which way one faces.
 *
 * A facing is one of four directions (0-3), the way the object's sprite
 * looks; turning restarts its animation's frame timing, and ends the turn so
 * the new facing shows before the script goes on. */
#include "event/bridge.h"
#include "event/event_data.h"
#include "event/variables.h"

// $C0:554D
/* The object turns: its sprite shows the new direction from the next frame. */
static void turn(GameState *g, EventObject object, Facing facing)
{
    set_field_object_facing(g, object, facing);
    set_field_object_frame_delay(g, object, 0);
}

// $C0:5535
/* Turn an object -- this one, or another -- to a direction. */
static EventFlow face(GameState *g, const EventCommand *c)
{
    turn(g, c->object, c->facing);
    return end_turn();
}

// $C0:6600
/* A variable learns which way an object faces. */
static EventFlow note_facing(GameState *g, const EventCommand *c)
{
    Facing facing = field_object_facing(g, c->object);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, facing));
    set_variable_byte(g, c->variable, facing);
    return next_command();
}

/* ---- Facing another object, through the field's aim --------------------------------------
 *
 * Which way one object sees another is the field's to work out
 * (field_direction_to_target, $C0:ABA2, still the original's code), from the
 * tiles the two stand on, or, on the same tile, their places within it. So
 * facing an object is a bridge: set the tiles, call out, turn. */

/* Set up the tiles field_direction_to_target works from. The target is kept
 * in the scratch cell. */
static void aim_at(GameState *g, EventObject self, EventObject target)
{
    uint8_t target_x = field_object_x_tile(g, target), target_y = field_object_y_tile(g, target);
    uint8_t own_x = field_object_x_tile(g, self), own_y = field_object_y_tile(g, self);
    ORIGINAL_EFFECT(keep_word(g, KEPT_FIRST, (uint16_t)(target * 2)));
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

enum { FACE_TOWARD_LENGTH = 2, DISABLED = 0x80 };

// $C0:55A9
/* Turn to face an object, unless it is disabled, which skips the command.
 * The direction's high byte is clear as the callee leaves it, so the facing
 * is the table's for its low byte. The command's offset after the call is the
 * one the callee leaves in Y, as the original's is. */
static void face_toward(Snes *s, EventObject target)
{
    GameState *g = state_of(s);
    if (field_object_state_state(g, target) & DISABLED) {
        event_return(s, (EventStep){ (uint16_t)(event_command_offset(s) + FACE_TOWARD_LENGTH), true });
        return;
    }
    aim_at(g, running_object(g), target);
    m_jsr_dc(s, 0xC055E6, 0xC0ABA2, DC_A | DC_X | DC_N | DC_Z | DC_C);
    if (!back(s, 0xC055E9, false))
        return;
    turn(g, running_object(g), facing_for_direction(g, (uint8_t)field_direction(g)));
    event_return(s, (EventStep){ (uint16_t)(event_command_offset(s) + FACE_TOWARD_LENGTH), false });
}

// $C0:55A0
/* Face the object the command names. */
void event_cmd_face_toward_object(Snes *s) { face_toward(s, event_command_of(s, 0xA8).target); }

// $C0:5605
/* Face the party member the command names. */
void event_cmd_face_toward_member(Snes *s) { face_toward(s, event_command_of(s, 0xA9).target); }

EVENT_OPCODES(event_facing_opcodes,
    { 0x0F, face, .object = SELF, .facing = CONSTANT(0), .length = 1, .routine = 0xC05535 },
    { 0x17, face, .object = SELF, .facing = CONSTANT(1), .length = 1, .routine = 0xC05539 },
    { 0x1B, face, .object = SELF, .facing = CONSTANT(2), .length = 1, .routine = 0xC0553D },
    { 0x1D, face, .object = SELF, .facing = CONSTANT(3), .length = 1, .routine = 0xC05541 },
    { 0xA6, face, .object = SELF, .facing = OPERAND(1), .length = 2, .routine = 0xC05545 },
    { 0xA7, face, .object = SELF, .facing = VARIABLE_BYTE(1), .length = 2, .routine = 0xC05557 },
    { 0x1E, face, .object = OBJECT(1), .facing = CONSTANT(0), .length = 2, .routine = 0xC0556C },
    { 0x1F, face, .object = OBJECT(1), .facing = CONSTANT(1), .length = 2, .routine = 0xC05579 },
    { 0x25, face, .object = OBJECT(1), .facing = CONSTANT(2), .length = 2, .routine = 0xC05586 },
    { 0x26, face, .object = OBJECT(1), .facing = CONSTANT(3), .length = 2, .routine = 0xC05593 },
    { 0x23, note_facing, .variable = VARIABLE(2), .object = OBJECT(1), .length = 3, .routine = 0xC065F8 },
    { 0x24, note_facing, .variable = VARIABLE(2), .object = MEMBER_OBJECT(1), .length = 3, .routine = 0xC0661F },
    { 0xA8, .bridge = event_cmd_face_toward_object, .target = OBJECT(1), .length = 2, .routine = 0xC055A0 },
    { 0xA9, .bridge = event_cmd_face_toward_member, .target = MEMBER_OBJECT(1), .length = 2, .routine = 0xC05605 });
