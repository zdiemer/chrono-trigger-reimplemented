/* Arithmetic and register results required by the machine compatibility
 * path and still-gated callers. */
#ifndef CT_GAME_FIELD_SCRATCH_H
#define CT_GAME_FIELD_SCRATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "field/cast.h"
#include "field/field_data.h"
#include "field/object.h"
#include "state.h"
#include "vars.h"

/* The overflow flag the original's last addition or subtraction leaves, for
 * the entry wrappers and the commands whose callers read it: whether a + b (or
 * a - b), taken as signed 8- or 16-bit numbers, left the signed range. The
 * game's logic never looks at it; only the original's code after the call
 * does. */
static inline bool field_add_overflows8(uint8_t a, uint8_t b)
{
    uint8_t sum = (uint8_t)(a + b);
    return ((~(a ^ b) & (a ^ sum)) & 0x80) != 0;
}
static inline bool field_subtract_overflows8(uint8_t a, uint8_t b)
{
    uint8_t difference = (uint8_t)(a - b);
    return (((a ^ b) & (a ^ difference)) & 0x80) != 0;
}
static inline bool field_add_with_carry_overflows16(uint16_t a, uint16_t b, bool carry)
{
    uint16_t sum = (uint16_t)(a + b + carry);
    return ((~(a ^ b) & (a ^ sum)) & 0x8000) != 0;
}
static inline bool field_add_overflows16(uint16_t a, uint16_t b) { return field_add_with_carry_overflows16(a, b, false); }
static inline bool field_subtract_overflows16(uint16_t a, uint16_t b)
{
    uint16_t difference = (uint16_t)(a - b);
    return (((a ^ b) & (a ^ difference)) & 0x8000) != 0;
}

/* What casting an object leaves the event command that cast it (field/cast.c):
 * Y at the last of its sprite record's table entries read -- the animations'
 * where its graphics were shared, else the graphics set's bank -- and the
 * overflow of the last sum: the animations' table size where the graphics
 * were shared, the unpacked graphics' new end where a set was unpacked, none
 * otherwise. */
uint16_t field_cast_y(FieldSpriteRecord r, GraphicsSource source);

/* The frame read's overflow (field/animation.c's entry): from whether it
 * took a frame and the frame the object was on before. */
bool field_frame_read_overflow(const GameState *g, FieldObject obj, bool took_a_frame, uint8_t frame_before);

/* What the window shapes' drawing leaves in Y, for the scaffolding of the
 * routines that call it and leave it in turn (field/window_shape.c; the
 * shared endings' swept window, scene/endings.c). */
uint16_t field_window_shape_leaves_y(const GameState *g);

#endif
