/* The parts of the field's motion and animation (field/motion.c,
 * field/animation.c) that what their routines leave the original's code is
 * worked out from (field/leaves.c): the direction routine's quarter and
 * scale, a direction's steps at the object's speed, whether the check for
 * objects in the way looks at all, and where the frame read reads. Nothing
 * else of the game calls these; object.h is the objects' vocabulary. */
#ifndef CT_GAME_FIELD_PARTS_H
#define CT_GAME_FIELD_PARTS_H

#include <stdbool.h>
#include <stdint.h>

#include "field/object.h"
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

#endif
