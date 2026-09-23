/* Event commands: the running object's animation.
 *
 * An object animates in one of four modes: 0 not at all, 1 looping its
 * animation, 2 playing an animation for a while (a timed play), 3 showing one
 * static frame. A timed play keeps the animation it was started with, and
 * counts down on the object's animation countdown, which the field's object
 * update steps; when it is over the object goes back to looping what it
 * looped before, or to nothing if it wasn't looping (animation $FF marks that). */
#include "event/bridge.h"

enum { MODE_STILL = 0, MODE_LOOP = 1, MODE_TIMED = 2, MODE_STATIC = 3 };
enum { NO_LOOP = 0xFF };

/* Start the object animating in `mode` from the animation's first frame. */
static void start_mode(GameState *g, unsigned object, uint8_t mode)
{
    set_field_object_animation_mode_mode(g, object, mode);
    set_field_object_frame_delay(g, object, 0);
    set_field_object_move_state_animation_count(g, object, 0);
    set_field_object_animation_frame(g, object, 0);
}

// $C0:2E6D
static void loop(GameState *g, uint8_t animation)
{
    unsigned object = current_object(g);
    set_field_object_animation_number(g, object, animation);
    start_mode(g, object, MODE_LOOP);
}

// $C0:2E67
/* $AA: loop animation (operand 1). */
static EventStep loop_animation(GameState *g, const EventCommand *c)
{
    loop(g, operand(g, c, 1));
    return next_command(c, 2, 1);
}

// $C0:2E87
/* $B3: loop animation 0. */
static EventStep loop_animation_0(GameState *g, const EventCommand *c)
{
    loop(g, 0);
    return next_command(c, 1, 0);
}

// $C0:2E8B
/* $B4: loop animation 1. */
static EventStep loop_animation_1(GameState *g, const EventCommand *c)
{
    loop(g, 1);
    return next_command(c, 1, 0);
}

// $C0:2E8F
/* $AE: stop animating (animation 0, mode 0). */
static EventStep reset_animation(GameState *g, const EventCommand *c)
{
    unsigned object = current_object(g);
    set_field_object_animation_number(g, object, 0);
    start_mode(g, object, MODE_STILL);
    return next_command(c, 1, 0);
}

/* Leave the loop for a timed play or a static frame, remembering whether
 * there was one to go back to. */
static void leave_loop(GameState *g, unsigned object, uint8_t mode)
{
    set_field_object_animation_frame(g, object, 0);
    set_field_object_frame_delay(g, object, 0);
    if (field_object_animation_mode_mode(g, object) == MODE_STILL)
        set_field_object_animation_number(g, object, NO_LOOP);
    set_field_object_animation_mode_mode(g, object, mode);
}

/* A timed play is over: back to the loop, or to no animation. */
static void end_timed_play(GameState *g, unsigned object)
{
    set_field_object_move_state_animation_count(g, object, 0);
    set_field_object_frame_delay(g, object, 0);
    set_field_object_animation_frame(g, object, 0);
    uint8_t mode = MODE_LOOP;
    if (field_object_animation_number(g, object) == NO_LOOP) {
        set_field_object_animation_number(g, object, 0);
        mode = MODE_STILL;
    }
    set_field_object_animation_mode_mode(g, object, mode);
}

/* $AB and $B7: play the animation in operand 1 for two frames, or for
 * operand 2 + 1 (a countdown the field steps; 1 is the end), staying on the command while it plays and going
 * on after its `length` bytes once it is over. The same command reached again
 * while its animation plays just waits. */
static EventStep play_timed(GameState *g, const EventCommand *c, bool for_frames, uint16_t length)
{
    unsigned object = current_object(g);
    uint8_t count = field_object_move_state_animation_count(g, object);
    uint8_t animation = operand(g, c, 1);
    if (count == 1) {
        end_timed_play(g, object);
        return next_command(c, length, 0);
    }
    if (count != 0 && animation == field_object_animation_mode_timed_animation(g, object))
        return wait_here(c, 0);
    set_field_object_animation_mode_timed_animation(g, object, animation);
    leave_loop(g, object, MODE_TIMED);
    uint8_t frames = for_frames ? (uint8_t)(operand(g, c, 2) + 1) : 2;
    set_field_object_move_state_animation_count(g, object, frames);
    return wait_here(c, 0);
}

// $C0:2E9A
/* $AB: play animation (operand 1) once through a countdown of 2. */
static EventStep play_animation(GameState *g, const EventCommand *c) { return play_timed(g, c, false, 2); }

// $C0:2F01
/* $B7: play animation (operand 1) for operand 2 frames. */
static EventStep play_animation_for(GameState *g, const EventCommand *c)
{
    return play_timed(g, c, true, 3);
}

// $C0:2F71
/* $AC: show static frame (operand 1). Ends the turn. */
static EventStep static_frame(GameState *g, const EventCommand *c)
{
    unsigned object = current_object(g);
    set_field_object_static_frame(g, object, operand(g, c, 1));
    leave_loop(g, object, MODE_STATIC);
    return end_turn(c, 2, 0);
}

EVENT_COMMAND(event_cmd_loop_animation, loop_animation)
EVENT_COMMAND(event_cmd_loop_animation_0, loop_animation_0)
EVENT_COMMAND(event_cmd_loop_animation_1, loop_animation_1)
EVENT_COMMAND(event_cmd_reset_animation, reset_animation)
EVENT_COMMAND(event_cmd_play_animation, play_animation)
EVENT_COMMAND(event_cmd_play_animation_for, play_animation_for)
EVENT_COMMAND(event_cmd_static_frame, static_frame)
