/* Scenes: the jet bike race's generated matrix routine.
 *
 * $C3:09E9 writes a routine into work RAM through the work RAM port, which its
 * callers point at $7E:B574: 1024 pairs of LDA absolute and STA direct page,
 * with the direct page moved on by $100 (LDA #$xx00 : TCD) every 128 pairs,
 * and an RTS. Pair n loads from the base in Y plus four offsets built from
 * n's digits (a table word by the outermost, then 4, 2 and $20 steps) and
 * stores to direct page byte 2n, so the routine gathers a 128x8 block of the
 * race's data into the pages from X on. The port writes the same bytes; the
 * routine itself runs as the program's own code (RECOMP.md, "The race's
 * generated code").
 *
 * The generator keeps its offsets in the scene's math scratch cells, and leaves
 * them as they end, the destination's page moved on. */
#include "scene/scene.h"

#include "cpu_io.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "vars.h"

/* The instructions it writes. */
enum { OP_LDA_IMMEDIATE = 0xA9, OP_TCD = 0x5B, OP_LDA_ABSOLUTE = 0xAD, OP_STA_DIRECT = 0x85, OP_RTS = 0x60 };

enum { TABLE_ENTRIES = 8, STEPS_4 = 8, STEPS_2 = 2, STEPS_20 = 8 };

/* Where it keeps them in the scene's math scratch cells: the table word, the
 * three steps (bytes, as words with their high halves cleared), the source
 * base and the destination. */
enum { TABLE_OFFSET = 0x0, STEP_4_OFFSET = 0x2, STEP_2_OFFSET = 0x4, STEP_20_OFFSET = 0x6, SOURCE_BASE = 0x8,
       DESTINATION = 0xA };

/* An 8-bit addition's overflow, the last of which the callers see. */
static bool add8_overflows(uint8_t a, uint8_t b)
{
    uint8_t sum = (uint8_t)(a + b);
    return (~(a ^ b) & (a ^ sum) & 0x80) != 0;
}

static void emit_word(GameState *g, uint16_t v)
{
    hw_port_write(g, (uint8_t)v);
    hw_port_write(g, (uint8_t)(v >> 8));
}

/* The generator's offsets. */
typedef struct {
    uint16_t table;
    uint8_t step4, step2, step20;
} Offsets;

// $C3:09E9
/* Write the routine: pairs store from the page in `destination` on, and load
 * from `source` on. Returns the overflow of its last addition. */
static bool write_matrix_code(GameState *g, uint16_t destination, uint16_t source)
{
    Offsets o = { 0, 0, 0, 0 };
    uint16_t pair = destination;
    uint8_t page = (uint8_t)(destination >> 8);
    bool overflow = false;
    for (uint16_t t = 0; t < TABLE_ENTRIES; t++) {
        o.table = scene_matrix_offset(g, t);
        o.step4 = 0;
        for (uint8_t i = 0; i < STEPS_4; i++) {
            o.step2 = 0;
            for (uint8_t j = 0; j < STEPS_2; j++) {
                o.step20 = 0;
                for (uint8_t k = 0; k < STEPS_20; k++) {
                    if ((uint8_t)pair == 0) {
                        /* LDA #$pp00 : TCD, and the next page */
                        hw_port_write(g, OP_LDA_IMMEDIATE);
                        emit_word(g, (uint16_t)(page << 8));
                        hw_port_write(g, OP_TCD);
                        page++;
                    }
                    hw_port_write(g, OP_LDA_ABSOLUTE);
                    emit_word(g, (uint16_t)(o.table + o.step4 + o.step2 + o.step20 + source));
                    hw_port_write(g, OP_STA_DIRECT);
                    hw_port_write(g, (uint8_t)pair);
                    pair = (uint16_t)(pair + 2);
                    overflow = add8_overflows(o.step20, 0x20);
                    o.step20 = (uint8_t)(o.step20 + 0x20);
                }
                o.step2 = (uint8_t)(o.step2 + 2);
            }
            overflow = add8_overflows(o.step4, 4);
            o.step4 = (uint8_t)(o.step4 + 4);
        }
    }
    hw_port_write(g, OP_RTS);

    set_scene_scratch16(g, SCENE_OWN_PAGE, TABLE_OFFSET, o.table);
    set_scene_scratch16(g, SCENE_OWN_PAGE, STEP_4_OFFSET, o.step4);
    set_scene_scratch16(g, SCENE_OWN_PAGE, STEP_2_OFFSET, o.step2);
    set_scene_scratch16(g, SCENE_OWN_PAGE, STEP_20_OFFSET, o.step20);
    set_scene_scratch16(g, SCENE_OWN_PAGE, SOURCE_BASE, source);
    set_scene_scratch16(g, SCENE_OWN_PAGE, DESTINATION, (uint16_t)((destination & 0x00FF) | page << 8));
    return overflow;
}

/* Entered with 16-bit index registers: X the first destination page's
 * address, Y the source base. The routine sets the direct page and the data
 * bank to 0 itself; Y ends at the table's end (16), which callers read. */
void scene_write_matrix_code(Snes *s)
{
    bool overflow = write_matrix_code(state_of(s), s->cpu.x, s->cpu.y);
    s->cpu.d = 0x0000;
    s->cpu.db = 0x00;
    s->cpu.y = TABLE_ENTRIES * 2;
    m_set_flag(s, CPU_FLAG_V, overflow);
    s->cpu.p |= CPU_FLAG_M;
    m_rtl(s);
}
