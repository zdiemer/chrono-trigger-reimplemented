/* Scenes: a bitmap scaled into 4-bit planar tile graphics.
 *
 * The jet bike race (and an ending) draws scaled bitmaps a row at a time into
 * bank $7F: each output row is 32 bytes of tile graphics, packed from samples
 * of a source row (a byte per pixel; the low four bits are the color). A row
 * accumulator picks each output row's source row, and an 8.8 fixed-point step
 * the samples along it. When the rows run out, or the source reaches its
 * limit, the output from there up to where the previous bitmap ended is
 * cleared.
 *
 * The original runs every row in one call, which spans several NMIs, so the
 * port stops at the row loop's head ($C3:0309) after each row and the next row
 * is a call of its own ($C3:0309 is an entry): each is compared on its own.
 *
 * The additions chain their carry (ADC without CLC) from one to the next, from
 * the row accumulator's through the samples, and the carry is live where the
 * port stops and after it returns, as are Y and the overflow; `Scaler` carries
 * them. The cells are on the scene's direct page, and the output and the
 * source are reached through the data bank ($7F while the race draws). */
#include "scene/scene.h"

#include "port/machine.h"
#include "ram.h"

enum { ROW_BYTES = 0x20, PLANES = 4, SAMPLES = 8 };
enum { ROW_POSITION_MASK = 0xFFC0 };   /* a source row is $40 bytes */
enum { OUTPUT_BANK = 0x7F };

/* The loop head, where the port stops after each row. */
enum { ROW_LOOP = 0xC30309 };

static uint16_t cell(Snes *s, uint8_t dp) { return m_read16(s, m_dp(s, dp)); }
static void set_cell(Snes *s, uint8_t dp, uint16_t v) { m_write16(s, m_dp(s, dp), v); }

/* The registers and flags the original leaves that its callers read. */
typedef struct {
    bool carry, overflow;
    uint16_t x, y;
} Scaler;

static uint16_t adc16(Scaler *r, uint16_t a, uint16_t b)
{
    uint32_t sum = (uint32_t)a + b + (r->carry ? 1 : 0);
    r->carry = sum > 0xFFFF;
    r->overflow = (~(a ^ b) & (a ^ sum) & 0x8000) != 0;
    return (uint16_t)sum;
}

static uint16_t sbc16(Scaler *r, uint16_t a, uint16_t b) { return adc16(r, a, (uint16_t)~b); }

/* MVN within bank $7F: count bytes from `from` to `to`, a byte at a time and
 * counting up, so an overlapping copy repeats what it just wrote. X and Y end
 * past the bytes, and the data bank is $7F. */
static void move_7f(Scaler *r, Snes *s, uint16_t from, uint16_t to, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        m_write8(s, (uint32_t)OUTPUT_BANK << 16 | (uint16_t)(to + i), m_read8(s, (uint32_t)OUTPUT_BANK << 16 | (uint16_t)(from + i)));
    r->x = (uint16_t)(from + count);
    r->y = (uint16_t)(to + count);
    s->cpu.db = OUTPUT_BANK;
}

/* Eight samples along the source row (the 8.8 position in `*position`), packed
 * into four bitplane bytes at the plane pointer, which moves on by 4. The
 * samples are stored as words at $3B-$42, overlapping, and each plane shifts
 * its bit out of them, as the original does. */
static void convert_8_pixels(Snes *s, Scaler *r, uint16_t *position)
{
    for (uint8_t i = 0; i < SAMPLES; i++) {
        *position = adc16(r, *position, cell(s, RAM_SCENE_SCALER_COLUMN_STEP_DP));
        uint16_t row = cell(s, RAM_SCENE_SCALER_SOURCE_ROW_DP);
        uint16_t sample = m_read16(s, m_abs(s, row) + (*position >> 8));
        set_cell(s, (uint8_t)(RAM_SCENE_BITPLANE_SCRATCH_DP + i), sample);
    }
    uint16_t out = cell(s, RAM_SCENE_SCALER_PLANE_OUTPUT_DP);
    for (uint8_t plane = 0; plane < PLANES; plane++) {
        uint8_t bits = 0;
        for (uint8_t i = 0; i < SAMPLES; i++) {
            uint32_t at = m_dp(s, (uint8_t)(RAM_SCENE_BITPLANE_SCRATCH_DP + i));
            uint8_t sample = m_read8(s, at);
            m_write8(s, at, sample >> 1);
            bits = (uint8_t)(bits << 1 | (sample & 1));
        }
        m_write8(s, m_abs(s, plane) + out, bits);
    }
    r->y = out;
    r->carry = false;
    set_cell(s, RAM_SCENE_SCALER_PLANE_OUTPUT_DP, adc16(r, out, PLANES));
    r->x = *position;
}

// $C3:0309
/* One output row, or the end of the bitmap. True when the bitmap has ended
 * (the original returns). */
static bool scale_row(Snes *s, Scaler *r)
{
    uint16_t rows_left = (uint16_t)(cell(s, RAM_SCENE_SCALER_ROWS_LEFT_DP) - 1);
    set_cell(s, RAM_SCENE_SCALER_ROWS_LEFT_DP, rows_left);
    uint16_t source_row = 0;
    bool ended = rows_left == 0;
    if (!ended) {
        uint16_t accumulator = adc16(r, cell(s, RAM_SCENE_SCALER_ROW_POSITION_DP), cell(s, RAM_SCENE_SCALER_ROW_STEP_DP));
        set_cell(s, RAM_SCENE_SCALER_ROW_POSITION_DP, accumulator);
        source_row = (accumulator >> 1) & ROW_POSITION_MASK;
        uint16_t limit = cell(s, RAM_SCENE_SCALER_SOURCE_LIMIT_DP);
        r->carry = source_row >= limit;
        ended = !((uint16_t)(source_row - limit) & 0x8000);
    }
    uint16_t output = cell(s, RAM_SCENE_SCALER_OUTPUT_DP);
    if (ended) {
        /* clear what is left of the previous bitmap's output */
        uint16_t left = sbc16(r, cell(s, RAM_SCENE_SCALER_OUTPUT_END_DP), output);
        if (!(left & 0x8000) && left != 0) {
            m_write16(s, m_abs(s, 0) + output, 0);
            move_7f(r, s, output, (uint16_t)(output + 1), (uint32_t)left + 1);
        }
        set_cell(s, RAM_SCENE_SCALER_OUTPUT_END_DP, output);
        return true;
    }

    r->carry = false;
    uint16_t pointer = adc16(r, source_row, cell(s, RAM_SCENE_SCALER_SOURCE_BASE_DP));
    uint16_t previous = cell(s, RAM_SCENE_SCALER_SOURCE_ROW_DP);
    r->carry = pointer >= previous;
    if (pointer == previous) {
        /* the same source row as the last: copy the last output row */
        move_7f(r, s, (uint16_t)(output - ROW_BYTES), output, ROW_BYTES);
    } else {
        set_cell(s, RAM_SCENE_SCALER_SOURCE_ROW_DP, pointer);
        uint16_t position = 0;
        uint16_t pairs = cell(s, RAM_SCENE_SCALER_PAIRS_DP);
        set_cell(s, RAM_SCENE_SCALER_PAIR_COUNTER_DP, pairs);
        do {
            convert_8_pixels(s, r, &position);
            convert_8_pixels(s, r, &position);
            pairs = (uint16_t)(cell(s, RAM_SCENE_SCALER_PAIR_COUNTER_DP) - 1);
            set_cell(s, RAM_SCENE_SCALER_PAIR_COUNTER_DP, pairs);
        } while (pairs != 0);
    }
    r->carry = false;
    uint16_t next = adc16(r, cell(s, RAM_SCENE_SCALER_OUTPUT_DP), ROW_BYTES);
    set_cell(s, RAM_SCENE_SCALER_OUTPUT_DP, next);
    set_cell(s, RAM_SCENE_SCALER_PLANE_OUTPUT_DP, next);
    return false;
}

/* The registers at the row loop's head (the port's stop) or at the return. */
static void finish_row(Snes *s, Scaler *r, bool ended)
{
    Cpu *c = &s->cpu;
    m_set_flag(s, CPU_FLAG_C, r->carry);
    m_set_flag(s, CPU_FLAG_V, r->overflow);
    c->x = r->x;
    c->y = r->y;
    if (ended) {
        m_rtl(s);
        return;
    }
    m_stop_at(s, ROW_LOOP, DC_V);
}

static void run_row(Snes *s)
{
    Scaler r = { m_flag(s, CPU_FLAG_C), m_flag(s, CPU_FLAG_V), s->cpu.x, s->cpu.y };
    bool ended = scale_row(s, &r);
    finish_row(s, &r, ended);
}

/* One row per call (see the top of the file). Entered with 16-bit A and index
 * registers. */
void scene_scale_bitmap_row(Snes *s) { run_row(s); }

// $C3:0307
/* A bitmap starts with the row accumulator at 0; its rows are the loop's. */
void scene_scale_bitmap(Snes *s)
{
    set_cell(s, RAM_SCENE_SCALER_ROW_POSITION_DP, 0);
    m_stop_at(s, ROW_LOOP, 0);
}
