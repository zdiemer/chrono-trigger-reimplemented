/* The parts of the field's motion, animation, frames and party
 * (field/motion.c, field/animation.c, field/frames.c, field/party_trail.c,
 * field/walk.c, field/tile_copy.c) that their routines are built from: the
 * direction routine's quarter and scale, a direction's steps at the object's
 * speed, whether the check for objects in the way looks at all, what drawing
 * a frame came to, the party's footprints and the pad's walk, a tile copy's
 * tables and a slope's line. object.h and party.h are the vocabulary the rest
 * of the game calls. */
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

/* The frame builder (field/frames.c): what drawing came to -- the object not
 * drawn (hidden, out of view, or of the 24-sprite layout, whose frames are
 * its records), no next frame to draw, the frame already drawn, no areas
 * free for it, a 12-sprite object holding a frame (never drawn), a part drawn
 * with more to come, or the frame whole. */
typedef enum {
    FRAME_NOT_DUE,
    FRAME_NO_NEXT,
    FRAME_ALREADY_DRAWN,
    FRAME_NO_ROOM,
    FRAME_WAITS,
    FRAME_PART_DRAWN,
    FRAME_DRAWN,
} FrameBuild;

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

/* How the pad walks the leader this frame, and a moving floor's pull in a
 * footprint's movement, rounded away from nothing to whole walking steps. */
Velocity field_pad_walk(const GameState *g);
int8_t field_floor_pull(int8_t movement);

#endif
