/* Field objects: the people, creatures and props of a map, which the event
 * scripts direct and the field moves, animates and draws every frame.
 *
 * An object is one of FIELD_OBJECTS slots. What it is doing is kept as a few
 * concepts, each with the operations that change it:
 *
 * - its role (field/object.c): the party's leader, a member following, another
 *   object, one that only comes and goes with the view, or one left alone;
 *   whether it is disabled, and whether other objects' calls reach it; the
 *   character it is; and whether it has a sprite;
 * - its place and look (field/object.c): where it stands, which way it faces,
 *   and how its sprite is layered against the map's;
 * - its animation (field/animation.c): it animates with its movement
 *   (standing, walking, running), loops an animation a script chose, plays a
 *   one-off animation for a while and then goes back to what it did before, or
 *   holds one frame. Each frame the object update counts down to its next frame
 *   and queues it for the frame builder;
 * - its motion (field/motion.c, and field/object.c for what a script sets): a
 *   position in 1/16 pixel with the tile in the high byte, a direction, a speed
 *   and the velocity they make, and a number of steps left in the current move,
 *   which it takes a step a frame, staying within its tile unless the move lets
 *   it cross; the map's blocked tiles and other solid objects can stop it;
 * - its place on the screen (field/object_view.c, field/object_update.c):
 *   whether it is in view, where on the screen it is drawn, and its place in
 *   the rows the sprite table is built from, bottom row first.
 *
 * This header is the only way to an object's state: the field's files and the
 * event scripts (game/event/) call these operations and never the object
 * tables' accessors. The storage is still the original's work RAM, reached
 * through game/vars.h in the files that implement the operations, so each
 * operation writes the bytes the original writes, and the original's
 * encodings (flag bits, markers, doubled slots, nine-bit positions) are
 * decoded there, in each concept's storage functions. */
#ifndef CT_GAME_FIELD_OBJECT_H
#define CT_GAME_FIELD_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

typedef uint8_t FieldObject;
enum { FIELD_OBJECTS = 64 };

/* A link to an object, or to none: how the rows, the frame builder's queue
 * and the party name an object. A link is a value to pass on and compare.
 *
 * What one is made of is the storage's: the original keeps it as a byte, the
 * object's slot doubled (its offset in the object tables, as the original's
 * index registers hold it), or any byte with the top bit set for none. Only
 * the storage functions that read and write links, and the guards
 * (field/bridge.h), see the byte, through the last two functions here. */
typedef struct {
    uint8_t stored;
} FieldLink;

enum { LINK_NONE_BIT = 0x80 };
static inline FieldLink field_link_to(FieldObject obj) { return (FieldLink){ (uint8_t)(obj << 1) }; }
static inline FieldLink field_no_link(void) { return (FieldLink){ LINK_NONE_BIT }; }
static inline bool field_link_is_object(FieldLink link) { return !(link.stored & LINK_NONE_BIT); }
static inline FieldObject field_linked_object(FieldLink link) { return (FieldObject)(link.stored >> 1); }
static inline bool field_same_link(FieldLink a, FieldLink b) { return a.stored == b.stored; }

static inline FieldLink field_link_stored(uint8_t byte) { return (FieldLink){ byte }; }
static inline uint8_t field_link_byte(FieldLink link) { return link.stored; }

/* Whether an offset as the original's tables are indexed -- a stored link, a
 * script's operand naming an object, the current object -- names an object
 * the tables hold: even, and inside them. Past them the original's index
 * reads the tables' neighbours, which only the original runs (the guards
 * check this, and the scripts' operands). */
static inline bool field_names_object(uint16_t offset) { return !(offset & 1) && offset < 2 * FIELD_OBJECTS; }
static inline bool field_link_names_object(FieldLink link)
{
    return field_link_is_object(link) && field_names_object(link.stored);
}

/* The object as the original's index registers hold it (its link's byte), for
 * scaffolding that keeps it in a scratch cell and for the machine side. */
static inline uint8_t field_object_offset(FieldObject obj) { return field_link_to(obj).stored; }

/* ---- The current object (field/object.c) ---------------------------------------------- *
 *
 * The object whose turn it is: the event engine runs its script, and the
 * object update, the sprite table and the original's routines work on it.
 * The original keeps it as a word whose low byte is the object's link. */
FieldObject object_current(const GameState *g);
bool object_current_is_one(const GameState *g);
void object_make_current(GameState *g, FieldObject obj);

/* ---- Role (field/object.c) ------------------------------------------------------------ */

/* What the object is to the field, which decides how the object update treats
 * it: the party's leader and the two members following it, who move freely;
 * a character who has left the party, who may still run; other objects, which
 * walk; objects that only come and go with the view; and objects left alone.
 * The kind is any of these or another value a map gives an object, which the
 * field treats as walking. A disabled object is out of the view, of the
 * checks for what is in the way, and of the animation. */
typedef enum {
    ROLE_LEADER = 0,
    ROLE_PARTY_1 = 1,
    ROLE_PARTY_2 = 2,
    ROLE_LEFT_THE_PARTY = 3,
    ROLE_WALKS_ONLY = 4,     /* from here on an object only ever walks */
    ROLE_WINDOW_ONLY = 7,
    ROLE_LEFT_ALONE = 8,
} ObjectKind;

typedef struct {
    bool disabled;
    ObjectKind kind;
} ObjectRole;

ObjectRole object_role(const GameState *g, FieldObject obj);
bool object_disabled(const GameState *g, FieldObject obj);
bool object_is_party_member(const GameState *g, FieldObject obj);
bool object_may_run(const GameState *g, FieldObject obj);

/* A character leaves the party: its object stays on the map as an object of
 * its own, which may still run. */
void object_leave_the_party(GameState *g, FieldObject obj);

/* The character the object is, and whether it runs a script of its own. */
uint8_t object_character(const GameState *g, FieldObject obj);
bool object_runs_a_script(const GameState *g, FieldObject obj);

/* Whether another object's call reaches it (event/calls.c): a disabled
 * object's never does, and a script can make an object unreachable for a
 * while. */
bool object_unreachable(const GameState *g, FieldObject obj);
void object_set_unreachable(GameState *g, FieldObject obj, bool unreachable);

/* Whether the object has a sprite, and whether it shows. */
typedef enum { SPRITE_HIDDEN = 0x00, SPRITE_SHOWN = 0x01, SPRITE_NONE = 0x80 } ObjectSprite;
void object_set_sprite(GameState *g, FieldObject obj, ObjectSprite sprite);
bool object_can_be_seen(const GameState *g, FieldObject obj);

/* The object leaves the map: disabled, its sprite hidden. */
void object_take_off_the_map(GameState *g, FieldObject obj);

/* How its sprite is drawn (field/sprites.c): with how many sprites, from
 * which of the records the frame builder writes, and in which part of the
 * sprite table -- in front of the others, behind them, or in the middle,
 * which its layering says (below). */
typedef enum { LAYOUT_4, LAYOUT_8, LAYOUT_12, LAYOUT_24 } SpriteLayout;
typedef enum { RANGE_MIDDLE, RANGE_BACK, RANGE_FRONT } SpriteRange;
SpriteLayout object_sprite_layout(const GameState *g, FieldObject obj);
SpriteRange object_sprite_range(const GameState *g, FieldObject obj);
uint16_t object_sprite_records(const GameState *g, FieldObject obj);

/* Its sprites' high table bits (a sprite's X's ninth bit, and large): a
 * byte for each four sprites, from sprite n on; the sprite table's placing
 * sets the first four's. */
uint8_t object_sprite_high_bits(const GameState *g, FieldObject obj, unsigned n);
void object_set_first_sprites_high_bits(GameState *g, FieldObject obj, uint8_t bits);

/* Its sprite records' state: whether they have been built since the object
 * came into view, and how many of the frame's sprites the frame builder has
 * changed since. An object coming into view has none to build from yet. */
typedef struct {
    bool built;
    uint8_t changed;
} RecordsState;
RecordsState object_records_state(const GameState *g, FieldObject obj);
void object_mark_records_built(GameState *g, FieldObject obj);
void object_records_to_build(GameState *g, FieldObject obj);

/* Where a battle on the map puts the object is the battle's to choose: what
 * coming into view and a script's layering leave (the byte the field hands
 * the battle with the party's placement, re/ram.toml). */
void object_let_the_battle_place(GameState *g, FieldObject obj);

/* Before a map's objects first run their scripts: none solid, none falling. */
void object_start_on_the_map(GameState *g, FieldObject obj);

/* ---- In view (field/object_view.c) ----------------------------------------------------- */

/* Whether the object is inside the window around the screen, where the field
 * updates, animates and draws it. The object update and the view take only
 * the flag; the checks for what is in the way, the party's re-sort and the
 * scripts' condition count as in view as well an object whose flag a row's
 * link has overwritten (object_update.c, leave_stray_in_view_write), which the
 * game does to the first object. */
typedef enum { OUT_OF_VIEW, IN_VIEW, OVERWRITTEN_BY_A_LINK } ObjectView;
ObjectView object_view(const GameState *g, FieldObject obj);
static inline bool object_in_view(const GameState *g, FieldObject obj) { return object_view(g, obj) == IN_VIEW; }
void object_set_in_view(GameState *g, FieldObject obj, bool in_view);

/* A tile of the map. */
typedef struct {
    uint8_t x, y;
} MapTile;

/* Whether two tiles touch, sides or corners (or are the same). */
static inline bool tiles_next_to(MapTile a, MapTile b)
{
    uint8_t across = (uint8_t)(a.x - b.x + 1), down = (uint8_t)(a.y - b.y + 1);
    return across <= 2 && down <= 2;
}

/* Whether a tile is well on screen, clear of its edges by two tiles, where a
 * scene can play without the camera moving. */
bool tile_well_on_screen(const GameState *g, MapTile tile);

/* ---- Place and look (field/motion.c, field/object.c) ----------------------------------- */

/* An object's position: 1/16 pixel, the tile in the high byte. */
typedef struct {
    uint16_t x, y;
} FieldPosition;

FieldPosition object_position(const GameState *g, FieldObject obj);
MapTile object_tile(const GameState *g, FieldObject obj);
void object_place(GameState *g, FieldObject obj, FieldPosition at);

/* Put the object on a tile, at the tile's bottom: `centred` across it, or
 * where across its tile it was. */
void object_put_on_tile(GameState *g, FieldObject obj, MapTile tile, bool centred);

/* Which way an object faces. */
typedef enum { FACING_UP, FACING_DOWN, FACING_LEFT, FACING_RIGHT } Facing;

Facing object_facing(const GameState *g, FieldObject obj);
/* Whether what the object keeps as its facing is one of the four (the
 * guards': the game sets no other). */
bool object_facing_is_one(const GameState *g, FieldObject obj);
/* It faces a way as it moves (its walk shows the facing). */
void object_set_facing(GameState *g, FieldObject obj, Facing facing);
/* It turns on the spot: the new facing shows from the next frame. */
void object_turn(GameState *g, FieldObject obj, Facing facing);

/* How the object's sprite is layered against the map's layers: a priority, 0
 * (behind everything) to 3 (in front of everything), for its upper half and
 * its lower half; or following the map, where each tile says (field/layers.h,
 * TILE_RAISES_SPRITES); and the part of the sprite table it draws in. */
typedef struct {
    bool follows_the_map;
    uint8_t upper, lower;   /* the halves' priorities, where it doesn't follow the map */
    uint8_t range;          /* 0 the middle, 1 or 3 behind the other sprites, 2 in front */
} ObjectLayering;

/* A script gives the layering as the byte the object keeps it in: bit 7
 * follows the map, bits 4-5 the upper half's priority, bits 2-3 the range
 * and bits 0-1 the lower half's priority. Bit 6 is no part of it: a byte
 * with it set is left to the original (the guards'), and none the game
 * gives has it. */
ObjectLayering layering_of_byte(uint8_t byte);
bool layering_byte_is_one(uint8_t byte);

/* A script layers the object: it follows the map where it stands, or takes
 * the byte's priorities; its place in a battle is the battle's to choose, and
 * its frame shows again at once. */
void object_set_layering(GameState *g, FieldObject obj, ObjectLayering layering);
bool object_follows_the_map(const GameState *g, FieldObject obj);
/* Its halves take the map's priorities on a tile. */
void object_layer_as_the_map(GameState *g, FieldObject obj, MapTile tile);
/* Its halves' priorities, 0-3; or those its own layering gives. */
void object_set_sprite_priorities(GameState *g, FieldObject obj, uint8_t upper, uint8_t lower);
void object_take_own_priorities(GameState *g, FieldObject obj);

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

/* What a script does with an object's animation. A one-off plays for a
 * number of half passes: a pass is the animation's times run through once,
 * and a half pass its frame table's end, where an animation whose frame table
 * is shorter than its times reaches it first. */
void object_loop_animation(GameState *g, FieldObject obj, uint8_t animation);
void object_stop_animating(GameState *g, FieldObject obj);
void object_play_animation(GameState *g, FieldObject obj, uint8_t animation, uint8_t half_passes);
bool object_playing_animation(const GameState *g, FieldObject obj, uint8_t animation);
bool object_animation_played(const GameState *g, FieldObject obj);
void object_finish_animation(GameState *g, FieldObject obj);
void object_show_frame(GameState *g, FieldObject obj, uint8_t frame);
void object_hurry_animation(GameState *g, FieldObject obj);

/* The object's animation starts again from its first frame, with the
 * frame's whole time (an object coming into view). */
void object_restart_animation(GameState *g, FieldObject obj);
/* The object's frame's time is up: it moves on to its next frame on the next
 * tick, drawn afresh (so a turn shows at once). */
void object_end_frame_time(GameState *g, FieldObject obj);

/* The movement animations: the one its speed calls for, or standing, unless
 * a script has taken the animation over. */
typedef enum { MOVEMENT_KEPT_BY_SCRIPT, MOVEMENT_STAND, MOVEMENT_WALK, MOVEMENT_RUN } MovementAnimation;
MovementAnimation object_animate_movement(GameState *g, FieldObject obj);
void object_stand(GameState *g, FieldObject obj);

/* Every frame, for an object in view: count down to its next frame, and when
 * it is due, queue the object for the frame builder and move to the frame. */
void object_animation_tick(GameState *g, FieldObject obj);

/* The frame builder takes the object's next frame to draw, if there is one:
 * a one-off animation at the end of its frames counts half a pass and has
 * none; any other animation at its end starts over at its first frame. */
typedef struct {
    bool has_frame;
    uint8_t frame;
} ObjectFrame;
ObjectFrame object_take_next_frame(GameState *g, FieldObject obj);

/* ---- Motion (field/motion.c, field/object.c) ------------------------------------------- */

/* A direction of motion, a byte to a turn: down, left, up and right start the
 * quarters. */
typedef uint8_t Direction;
enum { DIRECTION_DOWN = 0x00, DIRECTION_LEFT = 0x40, DIRECTION_UP = 0x80, DIRECTION_RIGHT = 0xC0 };

/* The facing an object moving in a direction takes. */
Facing facing_of_direction(const GameState *g, Direction direction);

/* The direction from one tile toward another. */
Direction direction_between(const GameState *g, MapTile from, MapTile to);

/* What a direction from one object toward another is worked out from: the
 * tiles they stand on, or where they stand on the same tile, their places on
 * it (a sixteenth of a tile to a unit). */
typedef struct {
    MapTile from, to;
} Aim;
Aim aim_toward_object(const GameState *g, FieldObject obj, FieldObject target);

/* How fast the object moves: its speed, which the direction's factors make
 * the velocity. */
void object_set_speed(GameState *g, FieldObject obj, uint8_t speed);

/* What stops the object's moves: the map's blocked tiles, other objects in
 * the way, both or neither. And whether it stands in other objects' way. */
typedef uint8_t ObjectBlocking;
enum { BLOCKED_BY_THE_MAP = 0x01, BLOCKED_BY_OBJECTS = 0x02 };
void object_set_blocked_by(GameState *g, FieldObject obj, ObjectBlocking blocking);

/* Whether it stands in other objects' way: a solid object blocks their
 * moves. */
void object_set_solidity(GameState *g, FieldObject obj, bool solid);
bool object_solid(const GameState *g, FieldObject obj);

/* What the object does on arriving where a script walked it: line itself up
 * with the tile's middle, or with the object it walked up to. */
typedef uint8_t ObjectArrival;
enum { LINE_UP_ON_THE_TILE = 0x01, LINE_UP_ON_THE_OBJECT = 0x02 };
void object_set_arrival(GameState *g, FieldObject obj, ObjectArrival arrival);
bool object_lines_up(const GameState *g, FieldObject obj, ObjectArrival how);

/* How many steps a script's walks take at a time: the length it sets, and
 * what the walks start with, which carries the object's unreachable mark
 * (object_start_walk). */
void object_set_move_length(GameState *g, FieldObject obj, uint8_t steps);
uint8_t object_move_length(const GameState *g, FieldObject obj);

/* Whether a move's steps may take the object out of its tile: never, once
 * (the move's first step may cross), or always. */
typedef enum { STAY_IN_TILE, CROSS_ONCE, CROSS_ALWAYS } TileCrossing;

/* A step's worth of move, for the field's own routines to move the object in
 * (its crossing as the last move left it). */
void object_start_one_step(GameState *g, FieldObject obj);

/* Start a move of `steps` steps, at the velocity the object heads with. */
void object_start_move(GameState *g, FieldObject obj, uint8_t steps, TileCrossing crossing);
/* The same for a script's walk toward a tile or an object: its first step may
 * cross, and a walk of 128 steps or more (an unreachable object's move length)
 * crosses on every step. */
void object_start_walk(GameState *g, FieldObject obj, uint8_t steps);
bool object_moving(const GameState *g, FieldObject obj);
/* An interrupted object forgets the move it was making. */
void object_stop_moving(GameState *g, FieldObject obj);

/* A script marks the move it waits on, and takes the mark away when it is
 * over. */
void object_mark_scripted_move(GameState *g, FieldObject obj);
bool object_scripted_move_marked(const GameState *g, FieldObject obj);
void object_unmark_scripted_move(GameState *g, FieldObject obj);

/* A move with a velocity of its own: the object falls, is thrown or jumps,
 * its vertical velocity growing by its acceleration each step. A launch
 * sets the velocities' low bytes; a jump keeps them and lowers the vertical
 * one by half of steps times height, so it rises and comes down after
 * `steps` steps. Landing takes the marks away. */
void object_launch(GameState *g, FieldObject obj, uint8_t velocity_x, uint8_t velocity_y, uint8_t acceleration,
                   uint8_t steps);
void object_jump(GameState *g, FieldObject obj, uint8_t steps, uint8_t height);
void object_land(GameState *g, FieldObject obj);

/* The object's velocity on each axis, as the step keeps it. */
typedef struct {
    int8_t x, y;
} Velocity;
Velocity object_velocity(const GameState *g, FieldObject obj);

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
 * that minds other objects): the object in the way, or else the last one it
 * measured, if any. */
typedef struct {
    bool blocked;
    bool measured;
    FieldObject object;
} ObjectsAhead;
ObjectsAhead object_in_the_way(GameState *g, FieldObject obj);
/* Whether the search fits the tables: there are objects, fewer than the
 * slots. Its callers' guards make sure. */
bool object_search_fits(const GameState *g);

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

ScreenPoint object_screen_position(const GameState *g, FieldObject obj);

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

/* The party's members as the field keeps them: a link to each place's
 * object, or none for an empty place, which a member leaving the map
 * leaves. */
FieldLink party_member_link(const GameState *g, unsigned place);
void party_place_left_empty(GameState *g, unsigned place);

/* The object a dialog's window is placed by, which is waiting on it. */
void dialog_set_speaker(GameState *g, FieldObject speaker);

#endif
