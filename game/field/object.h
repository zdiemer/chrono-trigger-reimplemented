/* Field objects: the people, creatures and props of a map, which the event
 * scripts direct and the field moves, animates and draws every frame.
 *
 * An object is one of FIELD_OBJECTS slots. What it is doing is kept as a few
 * concepts, each with the operations that change it:
 *
 * - its role (below): the party's leader, a member following, another
 *   object, one that only comes and goes with the view, or one left alone;
 *   and whether it is disabled;
 * - its animation (field/animation.c): it animates with its movement
 *   (standing, walking, running), loops an animation a script chose, plays a
 *   one-off animation a number of times and then goes back to what it did
 *   before, or holds one frame. Each frame the object update counts down to
 *   its next frame and queues it for the frame builder;
 * - its motion (field/motion.c): a position in 1/16 pixel with the tile in
 *   the high byte, a direction, a speed and the velocity they make, and a
 *   number of steps left in the current move, which it takes a step a frame,
 *   staying within its tile unless the move lets it cross; the map's blocked
 *   tiles and other solid objects can stop it;
 * - its place on the screen (field/object_view.c, field/object_update.c):
 *   whether it is in view, where on the screen it is drawn, and its place in
 *   the rows the sprite table is built from, bottom row first.
 *
 * The storage is still the original's work RAM, reached through game/vars.h,
 * so each operation still writes the bytes the original writes, and the
 * original's encodings (flag bits, markers, nine-bit positions) are decoded
 * in the storage functions beside each concept. The field's own files use
 * these operations. The script-facing ones (the animation a script plays or
 * loops, the moves it starts) have no callers yet: game/event/ still keeps
 * its own copy of the same state machine and calls the motion as the
 * original's code, until it moves onto this module (docs/TODO.md). */
#ifndef CT_GAME_FIELD_OBJECT_H
#define CT_GAME_FIELD_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"
#include "vars.h"

typedef uint8_t FieldObject;
enum { FIELD_OBJECTS = 64 };

/* How the rows and the frame builder's queue name an object: its slot
 * doubled, as the event scripts' object operands do too. A link with its top
 * bit set is none. */
typedef uint8_t FieldLink;
enum { FIELD_NO_LINK = 0x80 };
static inline FieldObject field_object_of_link(FieldLink link) { return (FieldObject)(link >> 1); }
static inline FieldLink field_object_link(FieldObject obj) { return (FieldLink)(obj << 1); }
static inline bool field_link_is_object(FieldLink link) { return !(link & FIELD_NO_LINK); }

/* The object the field is working on (field_current_object, a word whose low
 * byte is the object's link), which the original's routines take it from. */
static inline void field_make_link_current(GameState *g, FieldLink link)
{
    set_field_current_object(g, (uint16_t)((field_current_object(g) & 0xFF00) | link));
}
static inline void object_make_current(GameState *g, FieldObject obj) { field_make_link_current(g, field_object_link(obj)); }

/* ---- Role ------------------------------------------------------------------------------ */

/* What the object is to the field, which decides how the object update treats
 * it: the party's leader and the two members following it, who move freely;
 * other objects, which walk (the party may run); objects that only come and
 * go with the view; and objects left alone. A disabled object is out of the
 * view, of the checks for what is in the way, and of the animation. */
enum {
    ROLE_LEADER = 0,
    ROLE_PARTY_1 = 1,
    ROLE_PARTY_2 = 2,
    ROLE_PARTY_END = 3,
    ROLE_WALKS_ONLY = 4,     /* from here on an object only ever walks */
    ROLE_WINDOW_ONLY = 7,
    ROLE_LEFT_ALONE = 8,
};

typedef struct {
    bool disabled;
    uint8_t kind;   /* one of the ROLE_ values, or another object's */
} ObjectRole;

/* The original keeps the role in a byte with the disabled flag as its top
 * bit. */
enum { ROLE_DISABLED_BIT = 0x80 };

static inline ObjectRole object_role(const GameState *g, FieldObject obj)
{
    uint8_t stored = field_object_state_state(g, obj);
    return (ObjectRole){ (stored & ROLE_DISABLED_BIT) != 0, (uint8_t)(stored & ~ROLE_DISABLED_BIT) };
}
static inline bool object_disabled(const GameState *g, FieldObject obj) { return object_role(g, obj).disabled; }
static inline bool object_is_party_member(const GameState *g, FieldObject obj)
{
    ObjectRole r = object_role(g, obj);
    return !r.disabled && r.kind < ROLE_PARTY_END;
}
static inline bool object_may_run(const GameState *g, FieldObject obj)
{
    ObjectRole r = object_role(g, obj);
    return !r.disabled && r.kind < ROLE_WALKS_ONLY;
}

/* ---- In view -------------------------------------------------------------------------- */

/* Whether the object is inside the window around the screen, where the field
 * updates, animates and draws it (field/object_view.c). The original keeps a
 * flag byte, $80 in view and 0 out of it; the object update and the view
 * test its top bit, while the party's re-sort and the check for what is in
 * the way take any bit (object_marked_in_view). The game only ever stores
 * the two values, where the two readings agree. */
enum { IN_VIEW_BYTE = 0x80, OUT_OF_VIEW_BYTE = 0x00 };

static inline bool object_in_view(const GameState *g, FieldObject obj)
{
    return (field_object_in_view(g, obj) & IN_VIEW_BYTE) != 0;
}
static inline bool object_marked_in_view(const GameState *g, FieldObject obj)
{
    return field_object_in_view(g, obj) != OUT_OF_VIEW_BYTE;
}
static inline void object_set_in_view(GameState *g, FieldObject obj, bool in_view)
{
    set_field_object_in_view(g, obj, in_view ? IN_VIEW_BYTE : OUT_OF_VIEW_BYTE);
}

/* ---- Animation (field/animation.c) --------------------------------------------------- */

typedef enum {
    ANIMATE_WITH_MOVEMENT,   /* stands, walks or runs as it moves */
    ANIMATE_LOOP,            /* loops an animation a script chose */
    ANIMATE_PLAY,            /* plays a one-off animation, then goes back to its loop or its movement */
    ANIMATE_SHOW_FRAME,      /* holds one frame a script chose */
} ObjectAnimationMode;

/* The animations the field picks for an object animating with its movement. */
enum { ANIMATION_STAND = 0, ANIMATION_WALK = 1, ANIMATION_RUN = 6 };

ObjectAnimationMode object_animation_mode(const GameState *g, FieldObject obj);

/* What a script does with an object's animation. */
void object_loop_animation(GameState *g, FieldObject obj, uint8_t animation);
void object_stop_animating(GameState *g, FieldObject obj);
void object_play_animation(GameState *g, FieldObject obj, uint8_t animation, uint8_t passes);
bool object_playing_animation(const GameState *g, FieldObject obj, uint8_t animation);
bool object_animation_played(const GameState *g, FieldObject obj);
void object_finish_animation(GameState *g, FieldObject obj);
void object_show_frame(GameState *g, FieldObject obj, uint8_t frame);
void object_hurry_animation(GameState *g, FieldObject obj);

/* The object's animation starts again from its first frame, with the
 * frame's whole time (an object coming into view). */
void object_restart_animation(GameState *g, FieldObject obj);

/* The movement animations: the one its speed calls for, or standing, unless
 * a script has taken the animation over. */
typedef enum { MOVEMENT_KEPT_BY_SCRIPT, MOVEMENT_STAND, MOVEMENT_WALK, MOVEMENT_RUN } MovementAnimation;
MovementAnimation object_animate_movement(GameState *g, FieldObject obj);
void object_stand(GameState *g, FieldObject obj);

/* Every frame, for an object in view: count down to its next frame, and when
 * it is due, queue the object for the frame builder and move to the frame. */
void object_animation_tick(GameState *g, FieldObject obj);

/* The frame the object shows now, for the frame builder, and whether there
 * is one: a one-off animation at the end of its frames counts half a pass and
 * has none. Any other animation at its end starts over at its first frame. */
typedef struct {
    bool has_frame;
    uint8_t frame;
} ObjectFrame;
ObjectFrame object_current_frame(GameState *g, FieldObject obj);

/* ---- Motion (field/motion.c) ------------------------------------------------------------ */

/* Which way an object faces. */
typedef enum { FACING_UP, FACING_DOWN, FACING_LEFT, FACING_RIGHT } Facing;

/* A direction of motion, a byte to a turn: down, left, up and right start the
 * quarters. */
typedef uint8_t Direction;
enum { DIRECTION_DOWN = 0x00, DIRECTION_LEFT = 0x40, DIRECTION_UP = 0x80, DIRECTION_RIGHT = 0xC0 };

/* A tile of the map. */
typedef struct {
    uint8_t x, y;
} MapTile;

/* An object's position: 1/16 pixel, the tile in the high byte. */
typedef struct {
    uint16_t x, y;
} FieldPosition;

FieldPosition object_position(const GameState *g, FieldObject obj);

/* The direction from one tile toward another. */
Direction direction_between(const GameState *g, MapTile from, MapTile to);

/* Set the object heading in a direction at its speed, whatever is in the way. */
void object_head(GameState *g, FieldObject obj, Direction direction);

/* Start the object's step in a direction: it heads that way at its speed, and
 * where it minds the map, a blocked tile ahead turns it in a random direction
 * once, and stops it if that is blocked too. Its sprites' priority follows
 * the tile ahead where it takes it from the map. */
typedef enum {
    START_STILL,              /* the direction gives it no step at its speed */
    START_MOVING,             /* the way ahead was open (or it doesn't mind the map) */
    START_TURNED_STILL,       /* blocked; the new direction gives no step */
    START_TURNED_MOVING,      /* blocked; the new direction is open */
    START_BLOCKED,            /* blocked both ways: it stays */
} StartOutcome;
typedef struct {
    StartOutcome outcome;
    Direction direction;      /* the direction it heads in the end */
} StepStart;
StepStart object_start_step(GameState *g, FieldObject obj, Direction direction);

/* Whether another solid object in view stands within 256 sixteenths of a
 * pixel ahead of the object, the way it faces (only for an object in view
 * that minds other objects), and which. */
typedef struct {
    bool blocked;
    FieldObject blocker;
} ObjectsAhead;
ObjectsAhead object_in_the_way(GameState *g, FieldObject obj);

/* A step to line the object up with its tile's middle, or with another
 * object's tile: true while it still has to move. */
bool object_line_up_with_tile(GameState *g, FieldObject obj);
bool object_line_up_with(GameState *g, FieldObject obj, FieldObject target);

/* Every frame: the object's step of its current move, if it has steps left.
 * An object that accelerates falls; otherwise it steps at its velocity,
 * within its tile unless the move lets it cross. The party's members step
 * freely. */
typedef enum { STEP_NONE, STEP_FALLING, STEP_TAKEN } StepKind;
StepKind object_take_step(GameState *g, FieldObject obj);
StepKind party_member_take_step(GameState *g, FieldObject obj);

/* ---- On the screen (field/object_update.c) ------------------------------------------------ */

/* Where on the screen an object is drawn, in pixels from the screen's top
 * left corner; negative off its left or top. */
typedef struct {
    int16_t x, y;
} ScreenPoint;

/* The original keeps each coordinate in nine bits, the ninth set where it is
 * negative; the game's screen, 256 pixels, reads it as a signed number of
 * nine bits. */
enum { SCREEN_NINE_BITS = 0x1FF, SCREEN_NEGATIVE = 0x100 };

static inline int16_t screen_coordinate_of(uint16_t stored)
{
    uint16_t nine = stored & SCREEN_NINE_BITS;
    return (int16_t)((nine & SCREEN_NEGATIVE) ? (int16_t)nine - 2 * SCREEN_NEGATIVE : (int16_t)nine);
}
static inline uint16_t stored_screen_coordinate(int16_t v)
{
    return (uint16_t)(((uint16_t)v & SCREEN_NINE_BITS) | (v < 0 ? SCREEN_NEGATIVE : 0));
}

static inline ScreenPoint object_screen_position(const GameState *g, FieldObject obj)
{
    return (ScreenPoint){ screen_coordinate_of(field_object_screen_x(g, obj)),
                          screen_coordinate_of(field_object_screen_y(g, obj)) };
}

/* Where on the screen the object is drawn, from its position and the
 * screen's. */
void object_update_screen_position(GameState *g, FieldObject obj);

/* The rows: an object in view is in the row for its screen position (four
 * pixels a row), a list linked both ways whose first object the row keeps,
 * which the sprite table walks from the bottom of the screen up. */
enum { FIELD_ROWS = 64 };
void object_leave_row(GameState *g, FieldObject obj);
void object_join_row(GameState *g, FieldObject obj);
/* An object coming into view is in no row yet. */
void object_out_of_rows(GameState *g, FieldObject obj);
/* The object after this one in its row, and a row's first. */
FieldLink object_next_in_row(const GameState *g, FieldObject obj);
FieldLink row_first_object(const GameState *g, unsigned row);

#endif
