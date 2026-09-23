/* What the field's routines leave behind for the original's code and shadow
 * mode, and nothing else reads.
 *
 * Two kinds, both scaffolding: they stand while the original's code runs
 * beside the ports and shadow mode compares every byte and register, and go
 * when the gate retires (docs/PLAN.md, "Retiring the gate, deliberately").
 *
 * - Cells the original writes on the way. The field's routines keep values in
 *   scratch cells of their direct page as they work: the field's page, $C1-$C8
 *   (field_work) and $D9-$E6 (the cells the event commands use too,
 *   `event_scratch`); the layers' page, $1D52-$1D5C and $1D62-$1D6F
 *   (field_layer_work, field_layer_edge_work); and the window shape
 *   rasterizer's page, $0353-$035A and $03F0-$03FD (field_window_shape_edge,
 *   _steps). A port keeps its values in C, so nothing in game/ reads these
 *   back. Each file gathers its writes in small helpers named for what the
 *   original had in the cells (`leave_...`), which write the final values
 *   once. `cell` is the offset on the page, as the original's direct-page
 *   operand spells it: these are addresses on purpose, since they are the
 *   original's.
 *
 * - What a wrapper can't work out. An entry's wrapper gives the original's
 *   callers the registers they read, derived from the game's state after the
 *   call. Where one follows from a value the routine had on the way and
 *   nowhere else (the last distance a search measured, whether an animation
 *   started over), the routine records it in a file-static record named
 *   `..._leaves`, written only by a `record_...` helper and read only by the
 *   entry's wrapper, which clears it before the call. The logic's signatures
 *   never carry it. */
#ifndef CT_GAME_FIELD_SCRATCH_H
#define CT_GAME_FIELD_SCRATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "field/object.h"
#include "original_effects.h"
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

/* What the motion's routines leave the original's code and shadow mode, for a
 * caller that runs them as C where the original called them (event's moves,
 * whose commands hand on a Y and a V; event/leaves.h): the cells the
 * direction routine works from and leaves (field_leave_aim,
 * field_leave_direction) and its overflow; the check for objects in the way
 * keeping its caller's Y; the Y and the overflow a step's start leaves; and
 * the overflow animating with the movement leaves where it did. Each is what
 * that routine's entry leaves too (field/motion.c, field/animation.c). */
void field_leave_aim(GameState *g, Aim aim);
void field_leave_direction(GameState *g, Aim aim, Direction d);
bool field_direction_overflow(Aim aim, Direction d);
void field_leave_in_the_way_check(GameState *g, FieldObject obj, uint16_t callers_y);
uint16_t field_step_start_y(const GameState *g, StepStart r, uint16_t callers_y);
bool field_step_start_overflow(const GameState *g, FieldObject obj, StepStart r);
bool field_walk_overflow(const GameState *g, FieldObject obj);
bool field_jump_overflow(const GameState *g, FieldObject obj, uint8_t steps, uint8_t height);

enum { FIELD_WORK_CELLS = 0xC1, FIELD_SCRATCH_CELLS = 0xD9, FIELD_LAYER_WORK_CELLS = 0x50, FIELD_LAYER_EDGE_CELLS = 0x60 };
enum { SHAPE_EDGE_CELLS = 0x53, SHAPE_STEP_CELLS = 0xF0 };

static inline void original_cell8(GameState *g, uint8_t cell, uint8_t v)
{
    if (cell >= FIELD_SCRATCH_CELLS)
        set_event_scratch_byte(g, (unsigned)(cell - FIELD_SCRATCH_CELLS), v);
    else
        set_field_work_byte(g, (unsigned)(cell - FIELD_WORK_CELLS), v);
}

static inline void original_cell16(GameState *g, uint8_t cell, uint16_t v)
{
    original_cell8(g, cell, (uint8_t)v);
    original_cell8(g, (uint8_t)(cell + 1), (uint8_t)(v >> 8));
}

static inline void original_layer_cell8(GameState *g, uint8_t cell, uint8_t v)
{
    if (cell >= FIELD_LAYER_EDGE_CELLS)
        set_field_layer_edge_work_byte(g, (unsigned)(cell - FIELD_LAYER_EDGE_CELLS), v);
    else
        set_field_layer_work_byte(g, (unsigned)(cell - FIELD_LAYER_WORK_CELLS), v);
}

static inline void original_layer_cell16(GameState *g, uint8_t cell, uint16_t v)
{
    original_layer_cell8(g, cell, (uint8_t)v);
    original_layer_cell8(g, (uint8_t)(cell + 1), (uint8_t)(v >> 8));
}

static inline void original_shape_cell8(GameState *g, uint8_t cell, uint8_t v)
{
    if (cell >= SHAPE_STEP_CELLS)
        set_field_window_shape_steps_byte(g, (unsigned)(cell - SHAPE_STEP_CELLS), v);
    else
        set_field_window_shape_edge_byte(g, (unsigned)(cell - SHAPE_EDGE_CELLS), v);
}

static inline void original_shape_cell16(GameState *g, uint8_t cell, uint16_t v)
{
    original_shape_cell8(g, cell, (uint8_t)v);
    original_shape_cell8(g, (uint8_t)(cell + 1), (uint8_t)(v >> 8));
}

#endif
