/* Scenes: the jet bike race's generated matrix routine.
 *
 * To gather the track's data into the order its per-line effects read it, the
 * race writes a routine of its own, straight-line code with no loop in it, and
 * runs it every frame. This writes that routine into work RAM through the work
 * RAM port, which its callers point at $7E:B574: 1024 pairs of a load from an
 * absolute address and a store to the direct page, the direct page moved to the
 * next page every 128 pairs, and a return. Pair n loads from the base in Y plus
 * four offsets built from n's digits (a table word by the outermost, then 4, 2
 * and $20 steps) and stores to direct page byte 2n, so the routine gathers a
 * 128x8 block of the race's data into the pages from X on. The port writes the
 * same bytes; the routine itself runs as the program's own code (RECOMP.md,
 * "The race's generated code").
 *
 * The generator keeps its offsets in the scene's cells (scene_call). */
#include "scene/scene.h"

#include "cpu_io.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "vars.h"

/* The instructions it writes. */
enum { OP_LDA_IMMEDIATE = 0xA9, OP_TCD = 0x5B, OP_LDA_ABSOLUTE = 0xAD, OP_STA_DIRECT = 0x85, OP_RTS = 0x60 };

enum { TABLE_ENTRIES = 8, ROWS = 8, PAIRS = 2, COLUMNS = 8 };
enum { ROW_STEP = 4, PAIR_STEP = 2, COLUMN_STEP = 0x20 };

static void emit_word(GameState *g, uint16_t v)
{
    hw_port_write(g, (uint8_t)v);
    hw_port_write(g, (uint8_t)(v >> 8));
}

/* The generator's offsets: the table word of the eighth being written, and the
 * row's, the pair's and the column's steps within it. */
typedef struct {
    uint16_t table;
    uint8_t row, pair, column;
} Offsets;

/* The offsets as the generator leaves them in the scene's cells, the
 * destination's page moved on past the last; nothing reads them again but the
 * column step (below). */
static void leave_offsets(GameState *g, const Offsets *o, uint16_t destination)
{
    SceneProgramPage scene = scene_program_own();
    set_scene_call_matrix_table_offset(g, scene, o->table);
    set_scene_call_matrix_row_step(g, scene, o->row);
    set_scene_call_matrix_pair_step(g, scene, o->pair);
    set_scene_call_matrix_destination(g, scene, destination);
}

// $C3:09E9
/* Write the routine: pairs store from the page in `destination` on, and load
 * from `source` on. The source base stays in the scene's cells, where the
 * program's next multiply finds it after its product. */
static void write_matrix_code(GameState *g, uint16_t destination, uint16_t source)
{
    Offsets o = { 0, 0, 0, 0 };
    uint16_t pair = destination;
    uint8_t page = (uint8_t)(destination >> 8);
    for (uint16_t t = 0; t < TABLE_ENTRIES; t++) {
        o.table = scene_matrix_offset(g, t);
        o.row = 0;
        for (uint8_t i = 0; i < ROWS; i++) {
            o.pair = 0;
            for (uint8_t j = 0; j < PAIRS; j++) {
                o.column = 0;
                for (uint8_t k = 0; k < COLUMNS; k++) {
                    if ((uint8_t)pair == 0) {
                        /* the direct page moved to the next page */
                        hw_port_write(g, OP_LDA_IMMEDIATE);
                        emit_word(g, (uint16_t)(page << 8));
                        hw_port_write(g, OP_TCD);
                        page++;
                    }
                    hw_port_write(g, OP_LDA_ABSOLUTE);
                    emit_word(g, (uint16_t)(o.table + o.row + o.pair + o.column + source));
                    hw_port_write(g, OP_STA_DIRECT);
                    hw_port_write(g, (uint8_t)pair);
                    pair = (uint16_t)(pair + 2);
                    o.column = (uint8_t)(o.column + COLUMN_STEP);
                }
                o.pair = (uint8_t)(o.pair + PAIR_STEP);
            }
            o.row = (uint8_t)(o.row + ROW_STEP);
        }
    }
    hw_port_write(g, OP_RTS);

    SceneProgramPage scene = scene_program_own();
    set_scene_call_matrix_source(g, scene, source);
    /* The column step's cell is also the third byte of the product cell, which
     * the program's next signed multiply adds into its product
     * (scene/math.c): the step as the loop leaves it is game state. */
    set_scene_call_matrix_column_step(g, scene, o.column);
    ORIGINAL_EFFECT(leave_offsets(g, &o, (uint16_t)((destination & 0x00FF) | page << 8)));
}

/* Entered with 16-bit index registers: X the first destination page's
 * address, Y the source base. The routine sets the direct page and the data
 * bank to 0 itself, and the overflow is its last sum's, the row step's 28 + 4,
 * which is clear. */
void scene_write_matrix_code(Snes *s)
{
    write_matrix_code(state_of(s), s->cpu.x, s->cpu.y);
    s->cpu.d = 0x0000;
    s->cpu.db = 0x00;
    m_set_flag(s, CPU_FLAG_V, false);
    s->cpu.p |= CPU_FLAG_M;
    m_rtl(s);
}
