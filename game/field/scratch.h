/* Cells the original writes on the way, for the original's code and shadow
 * mode only.
 *
 * The field's routines keep values in scratch cells of their direct page as
 * they work: the field's page, $C1-$C8 (field_work) and $D9-$E6 (the cells the
 * event commands use too, `event_scratch`), and the layers' page, $1D52-$1D5C
 * and $1D62-$1D6F (field_layer_work, field_layer_edge_work). A port keeps its values in C, so nothing in game/ reads
 * these back; the writes stay only because the original's code still runs
 * around the ports and shadow mode compares memory byte for byte, and no
 * reader that means something by them is known. They are kept out of the
 * logic: each file gathers its writes in one helper, named for what the
 * original had in the cells, and the helpers go when the gate retires
 * (docs/PLAN.md, "Retiring the gate, deliberately").
 *
 * `cell` is the offset on the page, as the original's direct-page operand
 * spells it: these are addresses on purpose, since they are the original's. */
#ifndef CT_GAME_FIELD_SCRATCH_H
#define CT_GAME_FIELD_SCRATCH_H

#include <stdint.h>

#include "state.h"
#include "vars.h"

enum { FIELD_WORK_CELLS = 0xC1, FIELD_SCRATCH_CELLS = 0xD9, FIELD_LAYER_WORK_CELLS = 0x50, FIELD_LAYER_EDGE_CELLS = 0x60 };

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

/* What a cell holds, for an entry wrapper that gives back a register the
 * original reloads from one. */
static inline uint8_t original_cell_value8(const GameState *g, uint8_t cell)
{
    if (cell >= FIELD_SCRATCH_CELLS)
        return event_scratch_byte(g, (unsigned)(cell - FIELD_SCRATCH_CELLS));
    return field_work_byte(g, (unsigned)(cell - FIELD_WORK_CELLS));
}

static inline uint16_t original_cell_value16(const GameState *g, uint8_t cell)
{
    return (uint16_t)(original_cell_value8(g, cell) | original_cell_value8(g, (uint8_t)(cell + 1)) << 8);
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

#endif
