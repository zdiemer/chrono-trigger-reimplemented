/* Scripts animating their own object: a looping animation (walking on the
 * spot, a flickering torch), a one-off animation played for a while (a nod,
 * a jump of surprise) after which the object goes back to what it looped, and
 * a single frame held still (a pose in a cutscene).
 *
 * The animation itself is the field's object's (field/animation.c), which
 * the object update runs every frame; a script only chooses what it runs. */
#include "event/script.h"

// $C0:2E67
/* Make the object loop an animation from its start: its idle, a walk on the
 * spot. Scripts use one-byte forms for the two most common ones. */
static EventFlow loop_animation(GameState *g, FieldObject self, uint8_t animation)
{
    object_loop_animation(g, self, animation);
    return next_command();
}
EVENT_OPERATION(loop_animation, self, animation)

// $C0:2E8F
/* Stop whatever animation the script gave the object: it stands, and walks
 * and runs again as it moves. */
static EventFlow stop_animating(GameState *g, FieldObject self)
{
    object_stop_animating(g, self);
    return next_command();
}
EVENT_OPERATION(stop_animating, self)

// $C0:2E9A
/* Play a one-off animation on the object (a gesture, a reaction) and hold the
 * script until it has played; the object then goes back to what it was
 * looping. Met again while that animation still plays, the command waits on.
 * How long it plays is counted in half passes (field/object.h): the short
 * form plays it once through. */
static EventFlow play_animation(GameState *g, FieldObject self, uint8_t animation, uint8_t half_passes)
{
    if (object_animation_played(g, self)) {
        object_finish_animation(g, self);
        return next_command();
    }
    if (!object_playing_animation(g, self, animation))
        object_play_animation(g, self, animation, half_passes);
    return stay();
}
EVENT_OPERATION(play_animation, self, animation, half_passes)

// $C0:2F71
/* Hold one frame of the object's sprite -- a pose for a cutscene -- until a
 * script animates it again. */
static EventFlow hold_frame(GameState *g, FieldObject self, uint8_t frame)
{
    object_show_frame(g, self, frame);
    return end_turn();
}
EVENT_OPERATION(hold_frame, self, frame)

/* The long form gives how long the one-off plays as its half passes less
 * one. */
EVENT_OPCODES(event_animation_opcodes,
    { 0xAA, OPERATION(loop_animation), .animation = OPERAND(1), .length = 2 },
    { 0xB3, OPERATION(loop_animation), .animation = CONSTANT(0), .length = 1 },
    { 0xB4, OPERATION(loop_animation), .animation = CONSTANT(1), .length = 1 },
    { 0xAE, OPERATION(stop_animating), .length = 1 },
    { 0xAB, OPERATION(play_animation), .animation = OPERAND(1), .half_passes = CONSTANT(2), .length = 2 },
    { 0xB7, OPERATION(play_animation), .animation = OPERAND(1), .half_passes = OPERAND_PLUS_ONE(2), .length = 3 },
    { 0xAC, OPERATION(hold_frame), .frame = OPERAND(1), .length = 2 });
