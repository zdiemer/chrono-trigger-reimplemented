/* Scripts animating their own object: a looping animation (walking on the
 * spot, a flickering torch), a one-off animation played for a while (a nod,
 * a jump of surprise) after which the object goes back to what it looped, and
 * a single frame held still (a pose in a cutscene).
 *
 * The animation itself runs in the field's object update; a script only
 * chooses what it runs. */
#include "event/leaves.h"
#include "event/script.h"

/* ---- The object's animation state ----------------------------------------------------
 *
 * The field's (game/field/), which will own these once its object module
 * lands; until then they are the few words event needs, over the field's
 * accessors. */

typedef enum { NOT_ANIMATING = 0, LOOPING = 1, PLAYING_ONCE = 2, HOLDING_A_FRAME = 3 } AnimationMode;

/* The field keeps "nothing to go back to after a one-off" as this animation
 * number, in the looping animation's place. */
enum { NOTHING_TO_LOOP = 0xFF };

static AnimationMode animation_mode(const GameState *g, EventObject object)
{
    return (AnimationMode)field_object_animation_mode_mode(g, object);
}

/* Start the object's animation from its first frame, in `mode`. */
static void restart_animation(GameState *g, EventObject object, AnimationMode mode)
{
    set_field_object_animation_mode_mode(g, object, mode);
    set_field_object_frame_delay(g, object, 0);
    set_field_object_move_state_animation_count(g, object, 0);
    set_field_object_animation_frame(g, object, 0);
}

static void set_looping_animation(GameState *g, EventObject object, uint8_t animation)
{
    set_field_object_animation_number(g, object, animation);
}

/* Frames left of a one-off animation: 0 when none is playing, 1 on its last. */
static uint8_t one_off_frames_left(const GameState *g, EventObject object)
{
    return field_object_move_state_animation_count(g, object);
}

static uint8_t one_off_animation(const GameState *g, EventObject object)
{
    return field_object_animation_mode_timed_animation(g, object);
}

/* Break off the loop for a one-off or a still frame, noting whether there was
 * a loop to go back to. */
static void interrupt_loop(GameState *g, EventObject object, AnimationMode mode)
{
    set_field_object_animation_frame(g, object, 0);
    set_field_object_frame_delay(g, object, 0);
    if (animation_mode(g, object) == NOT_ANIMATING)
        set_looping_animation(g, object, NOTHING_TO_LOOP);
    set_field_object_animation_mode_mode(g, object, mode);
}

/* A one-off is over: back to the loop it broke off, or to standing still. */
static void back_to_loop(GameState *g, EventObject object)
{
    set_field_object_move_state_animation_count(g, object, 0);
    set_field_object_frame_delay(g, object, 0);
    set_field_object_animation_frame(g, object, 0);
    AnimationMode mode = LOOPING;
    if (field_object_animation_number(g, object) == NOTHING_TO_LOOP) {
        set_looping_animation(g, object, 0);
        mode = NOT_ANIMATING;
    }
    set_field_object_animation_mode_mode(g, object, mode);
}

/* ---- The commands ---------------------------------------------------------------------- */

// $C0:2E67
/* The object loops an animation from its start: its idle, a walk on the spot. */
static EventFlow loop_animation(GameState *g, const EventCommand *c)
{
    set_looping_animation(g, c->self, c->animation);
    restart_animation(g, c->self, LOOPING);
    return next_command();
}

// $C0:2E8F
/* The object stops animating, on its first frame of animation 0. */
static EventFlow stop_animating(GameState *g, const EventCommand *c)
{
    set_looping_animation(g, c->self, 0);
    restart_animation(g, c->self, NOT_ANIMATING);
    return next_command();
}

// $C0:2E9A
/* The object plays a one-off animation for a number of frames -- a gesture, a
 * reaction -- and its script waits for it to finish; the object then goes
 * back to what it was looping. Met again while that animation plays, the
 * command just waits on. */
static EventFlow play_animation(GameState *g, const EventCommand *c)
{
    uint8_t left = one_off_frames_left(g, c->self);
    if (left == 1) {
        back_to_loop(g, c->self);
        return next_command();
    }
    if (left != 0 && c->animation == one_off_animation(g, c->self))
        return stay();
    set_field_object_animation_mode_timed_animation(g, c->self, c->animation);
    interrupt_loop(g, c->self, PLAYING_ONCE);
    /* the field counts down to 1, so the count starts one past the frames */
    set_field_object_move_state_animation_count(g, c->self, (uint8_t)(c->frames + 1));
    return stay();
}

// $C0:2F71
/* The object holds one frame of its sprite -- a pose for a cutscene -- until
 * a script animates it again. */
static EventFlow hold_frame(GameState *g, const EventCommand *c)
{
    set_field_object_static_frame(g, c->self, c->frame);
    interrupt_loop(g, c->self, HOLDING_A_FRAME);
    return end_turn();
}

EVENT_OPCODES(event_animation_opcodes,
    { 0xAA, loop_animation, .animation = OPERAND(1), .length = 2, .routine = 0xC02E67 },
    { 0xB3, loop_animation, .animation = CONSTANT(0), .length = 1, .routine = 0xC02E87 },
    { 0xB4, loop_animation, .animation = CONSTANT(1), .length = 1, .routine = 0xC02E8B },
    { 0xAE, stop_animating, .length = 1, .routine = 0xC02E8F },
    { 0xAB, play_animation, .animation = OPERAND(1), .frames = CONSTANT(1), .length = 2, .routine = 0xC02E9A },
    { 0xB7, play_animation, .animation = OPERAND(1), .frames = OPERAND(2), .length = 3, .routine = 0xC02F01 },
    { 0xAC, hold_frame, .frame = OPERAND(1), .length = 2, .routine = 0xC02F71 });
