/* The parts of the field's motion, animation and party (field/motion.c,
 * field/animation.c, field/party_trail.c) that what their routines leave the
 * original's code is worked out from (field/leaves.c): the direction
 * routine's quarter and scale, a direction's steps at the object's speed,
 * whether the check for objects in the way looks at all, where the frame
 * read reads, and the party's footprints and the pad's walk. Nothing else of
 * the game calls these but the derivations of what the original leaves
 * (field/leaves.c, and event/leaves.c for the party's step); object.h and
 * party.h are the vocabulary. */
#ifndef CT_GAME_FIELD_PARTS_H
#define CT_GAME_FIELD_PARTS_H

#include <stdbool.h>
#include <stdint.h>

#include "field/layers.h"
#include "field/object.h"
#include "field/party.h"
#include "field/switches.h"
#include "state.h"

enum { FIELD_QUARTER_TURN = 0x40, FIELD_DISTANCE_BITS = 0x1F };

/* The quarter of the turn a move from one tile toward another is in, and the
 * two distances turned into it: across it, and along it. */
typedef struct {
    Direction quarter;
    uint8_t across, along;
} Quarter;
Quarter field_quarter_between(MapTile from, MapTile to);

/* The distances scaled down to five bits for the angle table, keeping their
 * ratio: by the first power of two that brings both under 32, as far as 4
 * (a distance of 128 or more is taken as its low seven bits). */
unsigned field_angle_scale(Quarter q);

/* The two axes' steps a direction makes at the object's speed. */
Velocity field_velocity_for(const GameState *g, FieldObject obj, Direction direction);

/* Whether the check for objects in the way looks at all: the object is in
 * view and minds other objects. */
bool field_looks_for_objects(const GameState *g, FieldObject obj);

/* The start of the object's frame table for its facing, where its current
 * animation's first frame is in it, whether the animation was put a frame
 * before its first (starting over), and whether a frame number is that. */
uint16_t field_facing_frame_table(const GameState *g, FieldObject obj);
uint16_t field_first_frame_place(const GameState *g, FieldObject obj);
bool field_animation_rewound(const GameState *g, FieldObject obj);
bool field_frame_before_the_first(uint8_t frame);

/* The frame builder (field/frames.c): what drawing came to -- the object not
 * drawn (hidden, out of view, or of the 24-sprite layout, whose frames are
 * its records), no next frame to draw, the frame already drawn, no areas
 * free for it, a 12-sprite object holding a frame (never drawn), a part drawn
 * with more to come, or the frame whole -- and what it had before; where the
 * last tile of a part of the object's frame went in bank $7F, and whether it
 * was mirrored; and what a whole frame sends to VRAM: where the second
 * stretch starts in the areas, and whether the sprites' tiles skip a row of
 * VRAM's, from which tile and by how much. */
typedef enum {
    FRAME_NOT_DUE,
    FRAME_NO_NEXT,
    FRAME_ALREADY_DRAWN,
    FRAME_NO_ROOM,
    FRAME_WAITS,
    FRAME_PART_DRAWN,
    FRAME_DRAWN,
} FrameBuild;
typedef struct {
    bool overflow;
    uint8_t animation_frame;
} FrameBuildBefore;
typedef struct {
    uint16_t at;
    bool mirrored;
} FieldDrawnTile;
FieldDrawnTile field_frame_part_last_tile(const GameState *g, FieldObject obj, unsigned part);
typedef struct {
    uint16_t second_stretch;
    bool skips_a_row;
    uint8_t tile_before_the_skip, row;
} FieldFrameSent;
FieldFrameSent field_frame_sent(const GameState *g, FieldObject obj);
/* The overflow the builder's entry hands on, worked out from those
 * (field/leaves.c). */
bool field_frame_build_overflow(const GameState *g, FieldObject obj, FrameBuild outcome, FrameBuildBefore before);

/* The main loop's switches (field/switches.c): the overflow of the last
 * entry's sum with BG1's base that pressing one leaves, and A's high byte at
 * its sound (field/leaves.c). */
bool field_switch_redraw_overflow(const GameState *g);
uint8_t field_switch_sound_high_a(const GameState *g, SwitchPress p, uint16_t step);

/* A footprint of the leader's trail (field/party_trail.c): how far it moved
 * that frame, how the pad walked it, its sprite's priorities and the ground
 * under it. */
typedef struct {
    Velocity movement;
    Velocity walk;
    SpritePriorities priorities;
    LeaderGround ground;
} Footprint;
Footprint field_footprint(const GameState *g, uint8_t at);

/* What the party's steps leave the original's callers (field/leaves.c): the
 * overflow of the leader's step, and of a member's, which follows from
 * whether it moved on. */
bool field_leader_step_overflow(const GameState *g, bool before);
bool field_member_step_overflow(const GameState *g, PartyPlace place, bool moved_on);

/* A tile copy (field/tile_copy.c): the map's tables it can take whole, in
 * the order it takes them; what the copy's description names, a table's bit
 * or one of the properties' bits alone; and the rectangle it describes, its
 * top left tile, its size in tiles, and the top left tile it goes to. */
typedef enum {
    MAP_BG1_TILES, MAP_BG2_TILES, MAP_BG3_TILES, MAP_PROPERTIES, MAP_UPPER_ATTRIBUTES, MAP_LOWER_ATTRIBUTES, MAP_TABLES
} MapTable;
enum { COPY_BG1_TILE_SET_HALF = 0x40, COPY_BG2_TILE_SET_HALF = 0x80 };

typedef struct {
    MapPlace from;
    uint8_t width, height;
    MapPlace to;
} TileRectangle;
TileRectangle field_described_tile_copy(const GameState *g);

/* A slope's line across its tile (field/walk.c): where a point is along it
 * (its place across shifted down, or from the other side), the point's
 * height in the tile (a quarter of its fine position down), their sum with
 * the line's start, and whether that puts the point on the upper part. */
typedef struct {
    uint8_t along, height, sum;
    bool upper;
} FieldSlopeSum;
FieldSlopeSum field_slope_sum(const GameState *g, uint8_t slope, uint8_t fine_x, uint8_t fine_y);

/* The first of the tiles' shapes that are slopes (field/walk.c, one shape a
 * slope); and where on its tile the party is taken to stand at a map's
 * start, and that tile. */
enum { FIELD_START_FINE_X = 0x80, FIELD_START_FINE_Y = 0xFF };
MapPlace field_start_place(const GameState *g);

/* What the party's start on the map leaves the openings' caller
 * (field/leaves.c): whether it looked at a slope, and the overflow of the
 * slope's sum there. */
bool field_start_on_a_slope(const GameState *g);
bool field_start_overflow(const GameState *g, bool before);

/* What each part of the copy leaves in the original's cells (field/leaves.c). */
void field_leave_table_copied(GameState *g, TileRectangle r, MapTable table);
void field_leave_bit_copied(GameState *g, TileRectangle r, uint8_t bit);

/* How the pad walks the leader this frame, and a moving floor's pull in a
 * footprint's movement, rounded away from nothing to whole walking steps. */
Velocity field_pad_walk(const GameState *g);
int8_t field_floor_pull(int8_t movement);

#endif
