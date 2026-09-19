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
 * generated code"). */
#include "scene/scene.h"

#include "port/machine.h"
#include "ram.h"
#include "scene/scene_data.h"

/* The instructions it writes. */
enum { OP_LDA_IMMEDIATE = 0xA9, OP_TCD = 0x5B, OP_LDA_ABSOLUTE = 0xAD, OP_STA_DIRECT = 0x85, OP_RTS = 0x60 };

/* The work RAM port: memory access, not video (docs/PLATFORM.md). */
enum { WRAM_PORT = 0x2180 };

/* The generator's cells on the direct page it sets ($0000): the offsets it
 * adds up, and the destination page it moves on. */
enum {
    TABLE_OFFSET = RAM_SCENE_MATH_SCRATCH_DP,
    STEP_4_OFFSET = TABLE_OFFSET + 2,
    STEP_2_OFFSET = TABLE_OFFSET + 4,
    STEP_20_OFFSET = TABLE_OFFSET + 6,
    SOURCE_BASE = TABLE_OFFSET + 8,
    DESTINATION = TABLE_OFFSET + 10,
    DESTINATION_PAGE = DESTINATION + 1,
};

enum { TABLE_ENTRIES = 8, STEPS_4 = 8, STEPS_2 = 2, STEPS_20 = 8 };

static void emit(Snes *s, uint8_t v) { m_write8(s, m_abs(s, WRAM_PORT), v); }

static uint8_t cell8(Snes *s, uint8_t dp) { return m_read8(s, dp); }
static void set_cell8(Snes *s, uint8_t dp, uint8_t v) { m_write8(s, dp, v); }
static uint16_t cell16(Snes *s, uint8_t dp) { return m_read16(s, dp); }
static void set_cell16(Snes *s, uint8_t dp, uint16_t v) { m_write16(s, dp, v); }

/* An 8-bit addition's overflow, the last of which the callers see. */
static bool add8_overflows(uint8_t a, uint8_t b)
{
    uint8_t sum = (uint8_t)(a + b);
    return (~(a ^ b) & (a ^ sum) & 0x80) != 0;
}

// $C3:09E9
/* Write the routine: pairs store from the page in `destination` (X) on, and
 * load from `source` (Y) on. Returns the overflow of its last addition. The
 * direct page and data bank end at 0. */
static bool write_matrix_code(Snes *s, uint16_t destination, uint16_t source)
{
    Cpu *c = &s->cpu;
    c->d = 0x0000;
    set_cell16(s, DESTINATION, destination);
    set_cell16(s, SOURCE_BASE, source);
    set_cell16(s, TABLE_OFFSET, 0);
    set_cell16(s, STEP_4_OFFSET, 0);
    set_cell16(s, STEP_2_OFFSET, 0);
    set_cell16(s, STEP_20_OFFSET, 0);
    c->db = 0x00;
    uint16_t pair = destination;
    bool overflow = false;
    for (uint16_t t = 0; t < TABLE_ENTRIES; t++) {
        set_cell16(s, TABLE_OFFSET, scene_matrix_table_word(s, t));
        set_cell8(s, STEP_4_OFFSET, 0);
        for (uint8_t i = 0; i < STEPS_4; i++) {
            set_cell8(s, STEP_2_OFFSET, 0);
            for (uint8_t j = 0; j < STEPS_2; j++) {
                set_cell8(s, STEP_20_OFFSET, 0);
                for (uint8_t k = 0; k < STEPS_20; k++) {
                    if ((uint8_t)pair == 0) {
                        /* LDA #$pp00 : TCD, and the next page */
                        uint8_t page = cell8(s, DESTINATION_PAGE);
                        emit(s, OP_LDA_IMMEDIATE);
                        emit(s, 0x00);
                        emit(s, page);
                        set_cell8(s, DESTINATION_PAGE, (uint8_t)(page + 1));
                        emit(s, OP_TCD);
                    }
                    uint16_t from = (uint16_t)(cell16(s, TABLE_OFFSET) + cell16(s, STEP_4_OFFSET) +
                                               cell16(s, STEP_2_OFFSET) + cell16(s, STEP_20_OFFSET) +
                                               cell16(s, SOURCE_BASE));
                    emit(s, OP_LDA_ABSOLUTE);
                    emit(s, (uint8_t)from);
                    emit(s, (uint8_t)(from >> 8));
                    emit(s, OP_STA_DIRECT);
                    emit(s, (uint8_t)pair);
                    pair = (uint16_t)(pair + 2);
                    uint8_t step20 = cell8(s, STEP_20_OFFSET);
                    overflow = add8_overflows(step20, 0x20);
                    set_cell8(s, STEP_20_OFFSET, (uint8_t)(step20 + 0x20));
                }
                set_cell8(s, STEP_2_OFFSET, (uint8_t)(cell8(s, STEP_2_OFFSET) + 2));
            }
            uint8_t step4 = cell8(s, STEP_4_OFFSET);
            overflow = add8_overflows(step4, 4);
            set_cell8(s, STEP_4_OFFSET, (uint8_t)(step4 + 4));
        }
    }
    emit(s, OP_RTS);
    return overflow;
}

/* Entered with 16-bit index registers: X the first destination page's
 * address, Y the source base. Y ends at the table's end (16), which callers
 * read. */
void scene_write_matrix_code(Snes *s)
{
    bool overflow = write_matrix_code(s, s->cpu.x, s->cpu.y);
    s->cpu.y = TABLE_ENTRIES * 2;
    m_set_flag(s, CPU_FLAG_V, overflow);
    s->cpu.p |= CPU_FLAG_M;
    m_rtl(s);
}
