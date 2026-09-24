/* What the field's routines leave the original's code and shadow mode, worked
 * out beside the wrappers that hand it on (field/scratch.h): the entries of
 * field/motion.c and field/animation.c, and event's moves, which run those
 * routines as C where the original called them and hand on the Y and V the
 * routines left (event/leaves.h). Nothing here is the game's: each function
 * derives a register, a flag or a cell from what the routine was given and
 * the state it left, and the logic files call none of them but through
 * ORIGINAL_EFFECT. */
#include "field/field.h"
#include "field/parts.h"
#include "field/object.h"
#include "field/scratch.h"
#include "vars.h"

/* ---- The direction routine ($C0:ABA2) ------------------------------------------------------ */

/* It works from two tiles its caller leaves in cells, and leaves the
 * direction in another (a word, its high byte cleared), beside its working
 * cells: the angle table index's low part and the two distances. */
void field_leave_aim(GameState *g, Aim aim)
{
    set_field_own_tile_x(g, aim.from.x);
    set_field_own_tile_y(g, aim.from.y);
    set_field_target_tile_x(g, aim.to.x);
    set_field_target_tile_y(g, aim.to.y);
}

void field_leave_direction(GameState *g, Aim aim, Direction d)
{
    Quarter q = field_quarter_between(aim.from, aim.to);
    unsigned scale = field_angle_scale(q);
    uint16_t across = (uint16_t)((q.across >> scale) & FIELD_DISTANCE_BITS);
    set_field_direction_work(g, (uint32_t)across | (uint32_t)q.across << 16 | (uint32_t)q.along << 24);
    set_field_direction(g, d);
}

/* Its overflow: that of the angle's sum with its quarter. */
bool field_direction_overflow(Aim aim, Direction d)
{
    Quarter q = field_quarter_between(aim.from, aim.to);
    return field_add_overflows8((uint8_t)(d - q.quarter), q.quarter);
}

/* ---- The check for objects in the way ($C0:5B95) --------------------------------------------- */

/* It keeps its caller's Y while it looks. */
void field_leave_in_the_way_check(GameState *g, FieldObject obj, uint16_t callers_y)
{
    if (field_looks_for_objects(g, obj))
        original_cell16(g, 0xC7, callers_y);
}

/* The object a move approaches, or the check found (or last looked at), as
 * the original keeps it: its offset, a word. */
void field_leave_target_object(GameState *g, FieldObject obj) { set_field_target_object(g, field_object_offset(obj)); }

/* ---- Starting a step ($C0:ACFD) ------------------------------------------------------------- */

/* It leaves Y the object where it looked ahead, and its caller's Y's low byte
 * where not; and the overflow of its last sum: the vertical position it last
 * looked ahead at, or where its last direction gave it no step, the quarter
 * turn to that direction's horizontal axis. */
uint16_t field_step_start_y(const GameState *g, StepStart r, uint16_t callers_y)
{
    return r.outcome != START_STILL ? field_current_object(g) : (uint16_t)(callers_y & 0x00FF);
}

bool field_step_start_overflow(const GameState *g, FieldObject obj, StepStart r)
{
    bool no_step = r.outcome == START_STILL || r.outcome == START_TURNED_STILL;
    if (no_step)
        return field_add_overflows8(r.direction, FIELD_QUARTER_TURN);
    /* The vertical step it last looked ahead with: the one it set, or where
     * it stayed blocked, its last direction's. */
    int8_t looked_ahead_y = r.outcome == START_BLOCKED ? field_velocity_for(g, obj, r.direction).y
                                                       : object_velocity(g, obj).y;
    return field_add_overflows16(object_position(g, obj).y, (uint16_t)(int16_t)looked_ahead_y);
}

/* ---- Jumps and animation ------------------------------------------------------------------- */

/* A jump ($C0:4D06, event's) leaves the overflow of lowering the vertical
 * velocity by its rise, its steps times its height halved: from the steps
 * and the height the jump left the object. */
bool field_jump_overflow(const GameState *g, FieldObject obj)
{
    uint16_t lift = (uint16_t)(0 - ((field_object_steps_left(g, obj) * field_object_acceleration(g, obj)) >> 1));
    uint16_t velocity = field_object_velocity_y(g, obj);
    return field_add_overflows16((uint16_t)(velocity - lift), lift);
}

/* Animating with its movement ($C0:5614) leaves the overflow of the speed's
 * sum, where it looked at the speed. */
static uint8_t magnitude(int8_t step) { return step < 0 ? (uint8_t)-step : (uint8_t)step; }

bool field_walk_overflow(const GameState *g, FieldObject obj)
{
    Velocity v = object_velocity(g, obj);
    return field_add_overflows8(magnitude(v.y), magnitude(v.x));
}

/* ---- The frame read ($C0:CB3A) --------------------------------------------------------------- */

/* Its overflow is that of the entry's last sum: the frame taken back off,
 * where the animation started over; the table and the place in it
 * otherwise. It started over where it took a frame and left the animation a
 * frame before its first; where it was there already, the entry tells:
 * starting over reads from the animation's start, and a frame found there
 * from a place past it. */
bool field_frame_read_overflow(const GameState *g, FieldObject obj, bool took_a_frame, uint8_t frame_before)
{
    uint16_t entry = field_frame_entry_address(g);
    bool started_over = took_a_frame && field_animation_rewound(g, obj) &&
                        (!field_frame_before_the_first(frame_before) || entry == field_first_frame_place(g, obj));
    if (started_over)
        return field_subtract_overflows16((uint16_t)(entry + frame_before), frame_before);
    uint16_t table = field_facing_frame_table(g, obj);
    return field_add_overflows16((uint16_t)(entry - table), table);
}

/* ---- The screen test (field/object.h, tile_well_on_screen) ------------------------------- */

/* Where the tile is well on screen, the overflow of its last subtraction:
 * the tile's row less the screen's top, in the objects' tiles. */
bool field_screen_test_overflow(const GameState *g, MapTile tile)
{
    return field_subtract_overflows8(tile.y, (uint8_t)(field_screen_top(g) >> 1));
}
