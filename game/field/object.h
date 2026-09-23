/* Field objects: the people, creatures and props of a map, which the event
 * scripts direct and the field moves, animates and draws every frame.
 *
 * An object is one of FIELD_OBJECTS slots. What it is doing is kept as a few
 * concepts, and the rest of the game changes it through the operations here
 * rather than through its bytes:
 *
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
 * - its place on the screen (field/object_update.c): whether it is in view,
 *   where on the screen it is drawn, and its place in the rows the sprite
 *   table is built from, bottom row first.
 *
 * The storage is still the original's work RAM, reached through game/vars.h,
 * so each operation still writes the bytes the original writes; those are its
 * business, and nothing outside this module should need them. */
#ifndef CT_GAME_FIELD_OBJECT_H
#define CT_GAME_FIELD_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

typedef uint8_t FieldObject;
enum { FIELD_OBJECTS = 64 };

/* The original names an object by its slot doubled: the event scripts' object
 * operands, the queues' and rows' links. A link with bit 7 set is none. */
static inline FieldObject field_object_of_link(uint8_t link) { return (FieldObject)(link >> 1); }
static inline uint8_t field_object_link(FieldObject obj) { return (uint8_t)(obj << 1); }
static inline bool field_link_is_object(uint8_t link) { return !(link & 0x80); }
enum { FIELD_NO_LINK = 0x80 };

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
void object_play_animation(GameState *g, FieldObject obj, uint8_t animation, uint8_t plays);
bool object_playing_animation(const GameState *g, FieldObject obj, uint8_t animation);
bool object_animation_played(const GameState *g, FieldObject obj);
void object_finish_animation(GameState *g, FieldObject obj);
void object_show_frame(GameState *g, FieldObject obj, uint8_t frame);

/* The movement animations: the one its speed calls for, or standing, unless
 * a script has taken the animation over. */
typedef enum { MOVEMENT_KEPT_BY_SCRIPT, MOVEMENT_STAND, MOVEMENT_WALK, MOVEMENT_RUN } MovementAnimation;
MovementAnimation object_animate_movement(GameState *g, FieldObject obj);
void object_stand(GameState *g, FieldObject obj);

/* Every frame, for an object in view: count down to its next frame, and when
 * it is due, queue the object for the frame builder and move to the frame. */
void object_animation_tick(GameState *g, FieldObject obj);

/* The frame the object shows now, for the frame builder, and whether there
 * is one (a one-off animation that has played out has none; its plays left
 * are in `frame`). An animation at its end starts over at its first frame. */
typedef struct {
    uint8_t frame;
    bool has_frame;
    bool started_over;
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
    int8_t probe_step_y;      /* the vertical step it last looked ahead with */
} StepStart;
StepStart object_start_step(GameState *g, FieldObject obj, Direction direction);

/* Whether another solid object in view stands within 256 sixteenths of a
 * pixel ahead of the object, the way it faces (only for an object in view
 * that minds other objects): the object in the way, and what the check last
 * measured, the distance ahead or across. */
typedef struct {
    bool looked, blocked, measured;
    FieldObject last;           /* the last object measured against: the one in the way where blocked */
    uint16_t last_distance;
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
typedef struct {
    StepKind kind;
    bool held_at_edge_y;   /* within its tile, the vertical step would have left it */
} Step;
Step object_take_step(GameState *g, FieldObject obj);
Step party_member_take_step(GameState *g, FieldObject obj);

#endif
