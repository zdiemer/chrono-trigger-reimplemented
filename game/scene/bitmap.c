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
 * them. The source and the output are wherever the data bank says ($7F while
 * the race draws), so their bank is a value the scaler is handed, and the
 * clear moves it to $7F as its block move does. */
#include "scene/scene.h"

#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

enum { ROW_BYTES = 0x20, PLANES = 4, SAMPLES = 8 };
enum { ROW_POSITION_MASK = 0xFFC0 };   /* a source row is $40 bytes */
enum { OUTPUT_BANK = 0x7F };

/* The loop head, where the port stops after each row. */
enum { ROW_LOOP = 0xC30309 };

/* The registers and flags the original leaves that its callers read, and the
 * bank the rows are read from and written to. */
typedef struct {
    bool carry, overflow;
    uint16_t x, y;
    uint8_t bank;
} Scaler;

static uint16_t adc16(Scaler *r, uint16_t a, uint16_t b)
{
    uint32_t sum = (uint32_t)a + b + (r->carry ? 1 : 0);
    r->carry = sum > 0xFFFF;
    r->overflow = (~(a ^ b) & (a ^ sum) & 0x8000) != 0;
    return (uint16_t)sum;
}

static uint16_t sbc16(Scaler *r, uint16_t a, uint16_t b) { return adc16(r, a, (uint16_t)~b); }

/* An address in the scaler's bank, `offset` past `at` (which can run on into
 * the next bank, as indexed addressing does). */
static uint32_t in_bank(const Scaler *r, uint16_t at, uint16_t offset)
{
    return ((uint32_t)r->bank << 16 | at) + offset;
}

/* Count bytes within bank $7F from `from` to `to`, a byte at a time and
 * counting up, so an overlapping copy repeats what it just wrote. X and Y end
 * past the bytes, and the bank is $7F from then on. */
static void move_7f(GameState *g, Scaler *r, uint16_t from, uint16_t to, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        wram_write8(g, (uint32_t)OUTPUT_BANK << 16 | (uint16_t)(to + i),
                    wram_read8(g, (uint32_t)OUTPUT_BANK << 16 | (uint16_t)(from + i)));
    r->x = (uint16_t)(from + count);
    r->y = (uint16_t)(to + count);
    r->bank = OUTPUT_BANK;
}

/* A sample stored as a word at scratch byte i: the eight overlap, so each
 * one's high byte is the next one's low byte until that is stored. */
static void set_sample(GameState *g, unsigned i, uint16_t sample)
{
    set_scene_bitplane_scratch_byte(g, i, (uint8_t)sample);
    set_scene_bitplane_scratch_byte(g, i + 1, (uint8_t)(sample >> 8));
}

/* Eight samples along the source row (the 8.8 position in `*position`), packed
 * into four bitplane bytes at the plane pointer, which moves on by 4. Each
 * plane shifts its bit out of the samples, as the original does. */
static void convert_8_pixels(GameState *g, Scaler *r, uint16_t *position)
{
    for (uint8_t i = 0; i < SAMPLES; i++) {
        *position = adc16(r, *position, scene_scaler_column_step(g));
        uint16_t row = scene_scaler_source_row(g);
        set_sample(g, i, wram_read16(g, in_bank(r, row, (uint16_t)(*position >> 8))));
    }
    uint16_t out = scene_scaler_plane_output(g);
    for (uint8_t plane = 0; plane < PLANES; plane++) {
        uint8_t bits = 0;
        for (uint8_t i = 0; i < SAMPLES; i++) {
            uint8_t sample = scene_bitplane_scratch_byte(g, i);
            set_scene_bitplane_scratch_byte(g, i, sample >> 1);
            bits = (uint8_t)(bits << 1 | (sample & 1));
        }
        wram_write8(g, in_bank(r, plane, out), bits);
    }
    r->y = out;
    r->carry = false;
    set_scene_scaler_plane_output(g, adc16(r, out, PLANES));
    r->x = *position;
}

// $C3:0309
/* One output row, or the end of the bitmap. True when the bitmap has ended
 * (the original returns). */
static bool scale_row(GameState *g, Scaler *r)
{
    uint16_t rows_left = (uint16_t)(scene_scaler_rows_left(g) - 1);
    set_scene_scaler_rows_left(g, rows_left);
    uint16_t source_row = 0;
    bool ended = rows_left == 0;
    if (!ended) {
        uint16_t accumulator = adc16(r, scene_scaler_row_position(g), scene_scaler_row_step(g));
        set_scene_scaler_row_position(g, accumulator);
        source_row = (accumulator >> 1) & ROW_POSITION_MASK;
        uint16_t limit = scene_scaler_source_limit(g);
        r->carry = source_row >= limit;
        ended = !((uint16_t)(source_row - limit) & 0x8000);
    }
    uint16_t output = scene_scaler_output(g);
    if (ended) {
        /* clear what is left of the previous bitmap's output */
        uint16_t left = sbc16(r, scene_scaler_output_end(g), output);
        if (!(left & 0x8000) && left != 0) {
            wram_write16(g, in_bank(r, 0, output), 0);
            move_7f(g, r, output, (uint16_t)(output + 1), (uint32_t)left + 1);
        }
        set_scene_scaler_output_end(g, output);
        return true;
    }

    r->carry = false;
    uint16_t pointer = adc16(r, source_row, scene_scaler_source_base(g));
    uint16_t previous = scene_scaler_source_row(g);
    r->carry = pointer >= previous;
    if (pointer == previous) {
        /* the same source row as the last: copy the last output row */
        move_7f(g, r, (uint16_t)(output - ROW_BYTES), output, ROW_BYTES);
    } else {
        set_scene_scaler_source_row(g, pointer);
        uint16_t position = 0;
        set_scene_scaler_pair_counter(g, scene_scaler_pairs(g));
        uint16_t pairs;
        do {
            convert_8_pixels(g, r, &position);
            convert_8_pixels(g, r, &position);
            pairs = (uint16_t)(scene_scaler_pair_counter(g) - 1);
            set_scene_scaler_pair_counter(g, pairs);
        } while (pairs != 0);
    }
    r->carry = false;
    uint16_t next = adc16(r, scene_scaler_output(g), ROW_BYTES);
    set_scene_scaler_output(g, next);
    set_scene_scaler_plane_output(g, next);
    return false;
}

/* ---- Entries: 16-bit A and index registers, on the scene's page -------------------------- */

/* One row per call (see the top of the file). The registers come back at the
 * row loop's head (the port's stop) or at the return. */
void scene_scale_bitmap_row(Snes *s)
{
    Cpu *c = &s->cpu;
    Scaler r = { m_flag(s, CPU_FLAG_C), m_flag(s, CPU_FLAG_V), c->x, c->y, c->db };
    bool ended = scale_row(state_of(s), &r);
    m_set_flag(s, CPU_FLAG_C, r.carry);
    m_set_flag(s, CPU_FLAG_V, r.overflow);
    c->x = r.x;
    c->y = r.y;
    c->db = r.bank;
    if (ended) {
        m_rtl(s);
        return;
    }
    m_stop_at(s, ROW_LOOP, DC_V);
}

// $C3:0307
/* A bitmap starts with the row accumulator at 0; its rows are the loop's. */
void scene_scale_bitmap(Snes *s)
{
    set_scene_scaler_row_position(state_of(s), 0);
    m_stop_at(s, ROW_LOOP, 0);
}
