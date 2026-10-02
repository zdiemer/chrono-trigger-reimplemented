/* Scenes: the scaled picture gathered into sprite tiles.
 *
 * The bitmap scaler (scene/scaler.c) leaves a picture a row at a time: each
 * row is $20 bytes, eight groups of four bitplane bytes, one group for every
 * eight pixels across. Sprites want it as 8x8 tiles, each the eight rows of
 * its first two bitplanes and then the eight rows of the other two. The jet
 * bike race scales its two racers this way every frame they change size, and
 * the Beyond Time ending its picture, and both regroup the result into the
 * tilemap buffer, which the vblank sends to VRAM as the racers' tiles.
 *
 * The original does the regrouping with a routine it writes for the purpose at
 * setup, straight-line code with no loop in it: 1024 pairs of a load from the
 * picture and a store to the buffer, the direct page moved on a page every 128
 * pairs. The generator ($C3:09E9) writes that routine at $7E:B574 through the
 * work RAM port, and the port below writes the same bytes. The routine itself
 * ($7E:B574) is the regrouping as a loop. The pairs come in eight strips of
 * eight rows, the strips in the order a table of the generator's names them
 * ($C3:09D9), so the tiles land in the order the sprites ask for them.
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

/* ---- The regrouping --------------------------------------------------------------------------- */

/* Where the scaler leaves the picture, in the bank the routine is entered
 * with: the generator is always given this base. */
enum { PICTURE = 0x0200 };

/* The routine the generator writes, guarded by a hash of all of it: the bytes
 * are the generator's output for its one table and base, so any other code
 * there is not this. It reads the picture through the data bank, which every
 * caller points at the second bank of work RAM. */
bool scene_matrix_code_present(Snes *s)
{
    return s->cpu.db == SCENE_UPPER_WORK_RAM && m_code_matches(s, 0x7EB574, 5153, 0x087A234E99D12FFFull);
}

// $7E:B574
/* Regroup the scaled picture into tiles in the tilemap buffer, a word at a
 * time: strip by strip, the tiles of a strip from left to right, in each tile
 * the rows of its first two bitplanes and then of its other two. */
static void gather_picture_into_tiles(GameState *g)
{
    uint16_t word = 0;
    for (uint16_t strip = 0; strip < TABLE_ENTRIES; strip++) {
        uint16_t strip_start = (uint16_t)(PICTURE + scene_matrix_offset(g, strip));
        for (uint8_t tile = 0; tile < ROWS; tile++) {
            for (uint8_t planes = 0; planes < PAIRS; planes++) {
                for (uint8_t row = 0; row < COLUMNS; row++) {
                    uint16_t from = (uint16_t)(strip_start + tile * ROW_STEP + planes * PAIR_STEP + row * COLUMN_STEP);
                    uint16_t pixels = scene_race_picture(g, (uint16_t)(from - PICTURE) / 2);
                    set_scene_tilemap_buffer_byte(g, word * 2u, (uint8_t)pixels);
                    set_scene_tilemap_buffer_byte(g, word * 2u + 1, (uint8_t)(pixels >> 8));
                    word++;
                }
            }
        }
    }
}

/* Entered with 16-bit A. The routine moves the direct page as it goes, which
 * its callers put back; nothing it leaves in the registers is read. */
void scene_gather_picture(Snes *s)
{
    gather_picture_into_tiles(state_of(s));
    m_rts(s);
}
