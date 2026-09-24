/* Which way objects face: turning an object to a direction, to face another
 * object or a party member, and asking which way one faces.
 *
 * A facing is one of four directions, the way the object's sprite looks
 * (field/object.h). A turn shows from the next frame, and ends the object's
 * turn so the new facing shows before the script goes on. */
#include "event/leaves.h"
#include "event/script.h"
#include "event/variables.h"
#include "field/scratch.h"

// $C0:5535
/* Turn an object -- this one, or another -- to a direction. */
static EventFlow face(GameState *g, FieldObject object, Facing facing)
{
    object_turn(g, object, facing);
    return end_turn();
}
EVENT_OPERATION(face, object, facing)

// $C0:6600
/* A variable learns which way an object faces. */
static EventFlow note_facing(GameState *g, FieldObject object, EventVariable variable)
{
    Facing facing = object_facing(g, object);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, (uint8_t)facing));
    set_variable_byte(g, variable, (uint8_t)facing);
    return next_command();
}
EVENT_OPERATION(note_facing, object, variable)

/* The guard's part: the object faces one of the four ways (field/object.h;
 * the original would note whatever byte it keeps). */
static bool faces_a_way(const GameState *g, FieldObject object) { return object_facing_is_one(g, object); }
EVENT_CHECK(faces_a_way, object)

/* For the original's code and shadow mode: the original keeps the object it
 * turns toward in its first scratch cell, and works the direction out with
 * the field's routine, which leaves its cells (field/scratch.h; its overflow
 * is event/leaves.c's). */
static void leave_aim_toward(GameState *g, FieldObject target, Aim aim, Direction d)
{
    keep_word(g, KEPT_FIRST, field_object_offset(target));
    field_leave_aim(g, aim);
    field_leave_direction(g, aim, d);
}

// $C0:55A9
/* Turn to face an object, or a party member: toward the tile it stands on,
 * or on the same tile, where on it. A disabled object is passed over. */
static EventFlow face_toward(GameState *g, FieldObject self, FieldObject target)
{
    if (object_disabled(g, target))
        return next_command();
    Aim aim = aim_toward_object(g, self, target);
    Direction d = direction_between(g, aim.from, aim.to);
    ORIGINAL_EFFECT(leave_aim_toward(g, target, aim, d));
    object_turn(g, self, facing_of_direction(g, d));
    return end_turn();
}
EVENT_OPERATION(face_toward, self, target)

EVENT_OPCODES(event_facing_opcodes,
    { 0x0F, OPERATION(face), .object = SELF, .facing = CONSTANT(FACING_UP), .length = 1 },
    { 0x17, OPERATION(face), .object = SELF, .facing = CONSTANT(FACING_DOWN), .length = 1 },
    { 0x1B, OPERATION(face), .object = SELF, .facing = CONSTANT(FACING_LEFT), .length = 1 },
    { 0x1D, OPERATION(face), .object = SELF, .facing = CONSTANT(FACING_RIGHT), .length = 1 },
    { 0xA6, OPERATION(face), .object = SELF, .facing = OPERAND(1), .length = 2 },
    { 0xA7, OPERATION(face), .object = SELF, .facing = VARIABLE_BYTE(1), .length = 2 },
    { 0x1E, OPERATION(face), .object = OBJECT(1), .facing = CONSTANT(FACING_UP), .length = 2 },
    { 0x1F, OPERATION(face), .object = OBJECT(1), .facing = CONSTANT(FACING_DOWN), .length = 2 },
    { 0x25, OPERATION(face), .object = OBJECT(1), .facing = CONSTANT(FACING_LEFT), .length = 2 },
    { 0x26, OPERATION(face), .object = OBJECT(1), .facing = CONSTANT(FACING_RIGHT), .length = 2 },
    { 0x23, OPERATION(note_facing), .variable = VARIABLE(2), .object = OBJECT(1), .length = 3, CHECK(faces_a_way) },
    { 0x24, OPERATION(note_facing), .variable = VARIABLE(2), .object = MEMBER_OBJECT(1), .length = 3,
      CHECK(faces_a_way) },
    { 0xA8, OPERATION(face_toward), .target = OBJECT(1), .length = 2 },
    { 0xA9, OPERATION(face_toward), .target = MEMBER_OBJECT(1), .length = 2 });
