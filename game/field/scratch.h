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

#include <stdint.h>

#include "state.h"
#include "vars.h"

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
