/* Scenes: a bitmap scaled into tile graphics.
 *
 * The jet bike race draws its racers and the endings their zooming pictures as
 * bitmaps scaled on the fly: a source bitmap of a byte per pixel (the low four
 * bits the color), sixty-four pixels a row, is sampled into rows of 4-bit planar
 * tile graphics, 32 bytes an output row. Each output row takes the source row a
 * row accumulator picks, and samples it a pair of 8-pixel groups at a time with
 * an 8.8 fixed-point step. A source row the same as the last output row's is
 * copied rather than sampled again. When the rows run out, or the accumulator
 * reaches the source's limit, the bitmap ends, and what is left of the previous
 * bitmap's output beyond this one's is cleared. Both live in the second bank of
 * work RAM, where the scene programs put them.
 *
 * The original draws every row in one call that spans several frames, so the
 * port draws a row per call and stops at the row loop's head ($C3:0309, an
 * entry of its own): each row is compared on its own.
 *
 * The original never clears the carry between its sums, so a sum that wraps
 * past $FFFF adds 1 to the next; `round_up` carries that from sum to sum (from
 * row to row, it rides in the carry flag between the calls). In the picture it
 * nudges a sample 1/256 of a pixel on: the first of a row whose source row is
 * further on than the last one's, and every one after a position that wrapped
 * past the source row's 256th pixel. */
#include "scene/scene.h"

#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

enum { ROW_BYTES = 0x20, PLANES = 4, GROUP = 8 };
enum { SOURCE_ROW_POSITION = 0xFFC0 };   /* a source row is $40 bytes: the accumulator's row part */
enum { SCALER_BANK = 0x7F };             /* where the source and the output are */

/* The row loop's head, where the port stops after each row. */
enum { ROW_LOOP = 0xC30309 };

/* An address in the scaler's bank, `offset` past `at` (running on into the next
 * bank, as the original's indexed addressing does). */
static uint32_t in_scaler_bank(uint16_t at, uint16_t offset) { return ((uint32_t)SCALER_BANK << 16 | at) + offset; }

/* ---- What the original leaves in its registers, for the entry's wrapper -------------------- */

typedef struct {
    bool carry, overflow;  /* the flags of the last subtraction, where the bitmap ends */
} ScalerLeaves;

static ScalerLeaves *scaler_leaves;

/* The flags the subtraction that measures what is left to clear leaves. */
static void leave_flags_of_measuring(uint16_t end, uint16_t output, bool one_short)
{
    uint16_t left = (uint16_t)(end - output - (one_short ? 1 : 0));
    scaler_leaves->carry = (uint32_t)output + (one_short ? 1 : 0) <= end;
    scaler_leaves->overflow = ((end ^ output) & (end ^ left) & 0x8000) != 0;
}

/* The sample scratch as the original leaves it: the last group's samples,
 * shifted down a nibble as it packed their planes, and the high byte of the
 * last; and its pair count run down to 0. */
static void leave_samples(GameState *g, const uint16_t samples[GROUP])
{
    for (unsigned i = 0; i < GROUP; i++)
        set_scene_bitplane_scratch_byte(g, i, (uint8_t)((samples[i] & 0xFF) >> PLANES));
    set_scene_bitplane_scratch_byte(g, GROUP, (uint8_t)(samples[GROUP - 1] >> 8));
    set_scene_scaler_pair_counter(g, 0);
}

/* ---- Sampling ---------------------------------------------------------------------------------- */

/* A position along the source row, 8.8 fixed point, stepped as the original
 * steps it: the step plus the rounding the sum before it left. */
typedef struct {
    uint16_t position;
    bool round_up;
} Sampler;

static uint16_t next_sample(Sampler *s, uint16_t step)
{
    uint32_t next = (uint32_t)s->position + step + (s->round_up ? 1 : 0);
    s->round_up = next > 0xFFFF;
    s->position = (uint16_t)next;
    return s->position;
}

/* Eight pixels of the source row into four bytes of planar graphics at
 * `planes`: plane p's byte holds bit p of each pixel, the first pixel in the
 * top bit. */
static void pack_planes(GameState *g, uint16_t planes, const uint16_t samples[GROUP])
{
    for (uint8_t plane = 0; plane < PLANES; plane++) {
        uint8_t bits = 0;
        for (unsigned i = 0; i < GROUP; i++)
            bits = (uint8_t)(bits << 1 | (samples[i] >> plane & 1));
        wram_write8(g, in_scaler_bank(plane, planes), bits);
    }
}

/* One output row sampled from the source row at `source`. The pairs are a
 * 16-bit count, so 0 is 65536 of them. */
static void sample_row(GameState *g, uint16_t source, bool round_up)
{
    Sampler s = { 0, round_up };
    uint16_t step = scene_scaler_column_step(g);
    uint16_t planes = scene_scaler_plane_output(g);
    uint16_t samples[GROUP] = { 0 };
    uint16_t pairs = scene_scaler_pairs(g);
    do {
        for (unsigned group = 0; group < 2; group++) {
            for (unsigned i = 0; i < GROUP; i++)
                samples[i] = wram_read16(g, in_scaler_bank(source, (uint16_t)(next_sample(&s, step) >> 8)));
            pack_planes(g, planes, samples);
            uint32_t next = (uint32_t)planes + PLANES;
            s.round_up = next > 0xFFFF;
            planes = (uint16_t)next;
        }
    } while (--pairs != 0);
    ORIGINAL_EFFECT(leave_samples(g, samples));
}

/* The row the same as the last one: its 32 bytes copied. */
static void repeat_last_row(GameState *g, uint16_t output)
{
    ORIGINAL_EFFECT(scene_time_block_move(true));
    for (uint16_t i = 0; i < ROW_BYTES; i++)
        wram_write8(g, in_scaler_bank(0, (uint16_t)(output + i)),
                    wram_read8(g, in_scaler_bank(0, (uint16_t)(output - ROW_BYTES + i))));
    ORIGINAL_EFFECT(scene_time_block_move(false));
}

/* ---- Rows ------------------------------------------------------------------------------------ */

/* The bitmap has ended: clear what the previous bitmap drew beyond it, and
 * mark where this one ends. The original measures that with the rounding its
 * last test left, so when the rows ran out (and the last row's sum didn't wrap)
 * it clears one byte fewer than when the source reached its limit. */
static void end_bitmap(GameState *g, bool one_short)
{
    uint16_t output = scene_scaler_output(g), end = scene_scaler_output_end(g);
    int16_t left = (int16_t)(end - output - (one_short ? 1 : 0));
    ORIGINAL_EFFECT(leave_flags_of_measuring(end, output, one_short));
    if (left > 0) {
        /* a zero word, copied on a byte at a time */
        wram_write16(g, in_scaler_bank(0, output), 0);
        ORIGINAL_EFFECT(scene_time_block_move(true));
        for (uint16_t i = 0; i <= (uint16_t)left; i++)
            wram_write8(g, in_scaler_bank(0, (uint16_t)(output + i + 1)),
                        wram_read8(g, in_scaler_bank(0, (uint16_t)(output + i))));
        ORIGINAL_EFFECT(scene_time_block_move(false));
    }
    set_scene_scaler_output_end(g, output);
}

typedef enum { ROW_DRAWN, BITMAP_ENDED } RowOutcome;

// $C3:0309
/* The next output row, or the end of the bitmap. `round_up` is the rounding
 * the last row left, and what this one leaves for the next. */
static RowOutcome scale_row(GameState *g, bool *round_up)
{
    uint16_t rows_left = (uint16_t)(scene_scaler_rows_left(g) - 1);
    set_scene_scaler_rows_left(g, rows_left);
    if (rows_left == 0) {
        end_bitmap(g, !*round_up);
        return BITMAP_ENDED;
    }
    uint16_t accumulator = (uint16_t)(scene_scaler_row_position(g) + scene_scaler_row_step(g) + (*round_up ? 1 : 0));
    set_scene_scaler_row_position(g, accumulator);
    uint16_t source_row = (accumulator >> 1) & SOURCE_ROW_POSITION;
    uint16_t limit = scene_scaler_source_limit(g);
    /* compared by the sign of the difference, as the original does: a limit
     * past $8000 is behind every row */
    if ((int16_t)(source_row - limit) >= 0) {
        end_bitmap(g, source_row < limit);
        return BITMAP_ENDED;
    }

    uint16_t output = scene_scaler_output(g);
    uint16_t source = (uint16_t)(source_row + scene_scaler_source_base(g));
    uint16_t previous = scene_scaler_source_row(g);
    if (source == previous) {
        repeat_last_row(g, output);
    } else {
        set_scene_scaler_source_row(g, source);
        sample_row(g, source, source > previous);
    }
    uint32_t next = (uint32_t)output + ROW_BYTES;
    *round_up = next > 0xFFFF;
    set_scene_scaler_output(g, (uint16_t)next);
    set_scene_scaler_plane_output(g, (uint16_t)next);
    return ROW_DRAWN;
}

/* ---- Entries: 16-bit A and index registers, on the scene's page, bank $7F ------------------- */

/* The scene programs draw their bitmaps in the second bank of work RAM, and
 * the port reaches them there. It keeps its samples in locals where the original
 * writes each to the scene's scratch as it reads it, which is the same only
 * while no sample is read from past the end of the bank, where the original's
 * reads reach low RAM and its own scratch: the rows' reach, from the source's
 * base through the last row the limit lets it take and a row's 256 samples, has
 * to stay in the bank. The race's reaches $0EFF. */
enum { LAST_ROW_BEFORE_ANY_LIMIT = 0x7FC0, SAMPLES_REACH = 0x0101 };

bool scene_scaler_in_its_bank(Snes *s)
{
    if (!scene_on_scene_page(s) || s->cpu.db != SCALER_BANK)
        return false;
    vars_peek(true);
    GameState *g = state_of(s);
    uint16_t base = scene_scaler_source_base(g), limit = scene_scaler_source_limit(g);
    vars_peek(false);
    if (limit == 0)
        return true;   /* every row is past it: nothing is sampled */
    uint32_t last_row = limit <= 0x8000 ? (uint32_t)(limit - 1) : LAST_ROW_BEFORE_ANY_LIMIT;
    return (uint32_t)base + last_row + SAMPLES_REACH <= 0x10000;
}

/* One row per call (see the top of the file), with the carry the rounding the
 * last row left, and back at the row loop's head (the port's stop) with the
 * rounding this one leaves. At the return, the flags are what the original
 * leaves; Y isn't read after either. */
void scene_scale_bitmap_row(Snes *s)
{
    ScalerLeaves leaves = { m_flag(s, CPU_FLAG_C), m_flag(s, CPU_FLAG_V) };
    scaler_leaves = &leaves;
    bool round_up = m_flag(s, CPU_FLAG_C);
    scene_time_entry(s);
    RowOutcome outcome = scale_row(state_of(s), &round_up);
    scene_time_entry(NULL);
    if (outcome == BITMAP_ENDED) {
        m_set_flag(s, CPU_FLAG_C, leaves.carry);
        m_set_flag(s, CPU_FLAG_V, leaves.overflow);
        m_rtl(s);
        return;
    }
    m_set_flag(s, CPU_FLAG_C, round_up);
    m_stop_at(s, ROW_LOOP, DC_V);
}

// $C3:0307
/* A bitmap starts with the row accumulator at 0; its rows are the loop's. */
void scene_scale_bitmap(Snes *s)
{
    set_scene_scaler_row_position(state_of(s), 0);
    m_stop_at(s, ROW_LOOP, 0);
}
