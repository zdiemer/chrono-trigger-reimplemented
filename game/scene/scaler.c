/* Scenes: a bitmap scaled into tile graphics.
 *
 * The jet bike race draws its racers and the endings their zooming pictures as
 * bitmaps scaled on the fly: a source bitmap of a byte per pixel (the low four
 * bits the color), sixty-four pixels a row, is sampled into rows of 4-bit planar
 * tile graphics, 32 bytes an output row. Each output row takes the source row a
 * row accumulator picks, and samples it a pair of 8-pixel groups at a time,
 * every `column_step` (8.8 fixed point) along it. A source row the same as the
 * last output row's is copied rather than sampled again. When the rows run out,
 * or the accumulator reaches the source's limit, the bitmap ends, and what is
 * left of the previous bitmap's output beyond this one's is cleared. Both live in
 * the second bank of work RAM, where the scene programs put them.
 *
 * The whole bitmap is drawn at once, after a check of the complete output
 * extent, the first row's nudge included.
 *
 * The original never clears the carry between its sums, and three of them move
 * what the player sees: a row's samples start 1/256 of a pixel further on when
 * its source row lies further along the bitmap than the last row's, a sample
 * whose position wrapped past the row's 256th pixel moves those after it on by
 * 1/256 (group_positions), and a row's source is picked 1/128
 * of a row further on when the call before left the carry set (the caller's
 * `row_nudge`). The rest of its carries come from sums that wrap past the end
 * of the bank, which no scene's bitmap reaches; a scene that hands the scaler
 * such a bitmap goes no further. */
#include "scene/scene.h"

#include "charge.h"
#include "core/program.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

enum { ROW_BYTES = 0x20, PLANES = 4, GROUP = 8, GROUPS_PER_PAIR = 2 };
/* The positions are fixed point: a sample's along its row in 1/256ths of a
 * pixel, a row's down the bitmap in 1/128ths of a row, whose source is $40
 * bytes. */
enum { PIXEL_UNITS = 256, ROW_UNITS = 128, SOURCE_ROW_BYTES = 0x40 };
/* What the original's 16-bit sums hold, positions along a row or bytes in the
 * bank. */
enum { POSITIONS = 0x10000 };

/* The row loop's entry, used to attribute the drawing's time. */
enum { ROW_LOOP = 0xC30309, SAMPLE_ROW = 0xC3034A };

/* An address in the scaler's bank, `offset` past `at` (running on into the next
 * bank, as the original's indexed addressing does). */
static uint32_t in_scaler_bank(uint16_t at, uint16_t offset)
{
    return ((uint32_t)SCENE_UPPER_WORK_RAM << 16 | at) + offset;
}

/* ---- A bitmap handed over ------------------------------------------------------------------------- */

/* The scaler draws every bitmap from the start of its output. */
enum { OUTPUT_START = 0x0200 };

void scene_scaler_start(GameState *g, SceneScaling bitmap)
{
    set_scene_scaler_source_base(g, bitmap.source);
    set_scene_scaler_rows_left(g, bitmap.rows);
    set_scene_scaler_pairs(g, bitmap.pairs);
    set_scene_scaler_row_step(g, bitmap.row_step);
    set_scene_scaler_column_step(g, (uint16_t)(bitmap.row_step << 1));
    set_scene_scaler_source_limit(g, bitmap.limit);
    set_scene_scaler_output(g, OUTPUT_START);
    set_scene_scaler_plane_output(g, OUTPUT_START);
    set_scene_scaler_row_position(g, 0);
}

/* ---- Sampling ---------------------------------------------------------------------------------- */

/* The pixel at `position` (in PIXEL_UNITS) along the source row at `source`. */
static uint8_t pixel(const GameState *g, uint16_t source, uint16_t position)
{
    return wram_read8(g, in_scaler_bank(source, (uint16_t)(position / PIXEL_UNITS)));
}

/* Eight pixels into four bytes of planar graphics at `planes`: plane p's byte
 * holds bit p of each pixel, the first pixel in the top bit. */
static void pack_planes(GameState *g, uint16_t planes, const uint8_t pixels[GROUP])
{
    for (uint8_t plane = 0; plane < PLANES; plane++) {
        uint8_t bits = 0;
        for (unsigned i = 0; i < GROUP; i++)
            bits = (uint8_t)(bits << 1 | (pixels[i] >> plane & 1));
        wram_write8(g, in_scaler_bank(plane, planes), bits);
    }
}

/* Where a group of eight samples falls along its source row, each a step on
 * from the last (in PIXEL_UNITS), the first from `last`. A bitmap stretched so
 * wide that its samples run past the row's 256th pixel wraps back to the row's
 * start, and the original's add takes the bit that spilled from that sum into
 * the next: every wrap moves the samples after it on by 1/256 of a pixel, which
 * the player sees where it carries one of them onto the next source pixel. The
 * bit goes no further than the group, whose packing clears it. `moved_on` moves
 * the first sample on by 1/256 too (the row's first group: see the top of the
 * file). Returns the last sample's position. */
static uint16_t group_positions(uint16_t last, uint16_t step, bool moved_on, uint16_t positions[GROUP])
{
    for (unsigned i = 0; i < GROUP; i++) {
        uint32_t sum = (uint32_t)last + step + (moved_on ? 1 : 0);
        last = positions[i] = (uint16_t)sum;
        moved_on = sum >= POSITIONS;
    }
    return last;
}

/* One output row sampled from the source row at `source`, into the graphics at
 * the plane pointer, a group of eight samples at a time. The first sample is
 * moved on where the row's source lies further along the bitmap than the last
 * row's (the original's comparison of the two). */
static void sample_row(GameState *g, uint16_t source, bool row_moved_on)
{
    uint16_t step = scene_scaler_column_step(g);
    uint16_t planes = scene_scaler_plane_output(g);
    uint32_t groups = (uint32_t)scene_scaler_pairs(g) * GROUPS_PER_PAIR;
    uint8_t pixels[GROUP] = { 0 };
    uint16_t position = 0;
    for (uint32_t group = 0; group < groups; group++) {
        uint16_t positions[GROUP];
        position = group_positions(position, step, group == 0 && row_moved_on, positions);
        for (unsigned i = 0; i < GROUP; i++)
            pixels[i] = pixel(g, source, positions[i]);
        pack_planes(g, planes, pixels);
        planes = (uint16_t)(planes + PLANES);
    }
    charge_accesses(g, GROUP + 4);
}

/* The row the same as the last one: its 32 bytes copied. */
static void repeat_last_row(GameState *g, uint16_t output)
{
    wram_copy(g, in_scaler_bank(0, (uint16_t)(output - ROW_BYTES)), in_scaler_bank(0, output), ROW_BYTES);
    charge_block_move(g, ROW_BYTES);
}

/* ---- Rows ------------------------------------------------------------------------------------ */

/* What of the previous bitmap is left past this one's end: the bytes from here
 * to where it ended and, as the original counts them, one byte past that --
 * or none past it, where the original's measure borrowed the carry the rows
 * running out left clear. No bytes where the previous bitmap ended here or
 * before. */
static uint16_t left_over(uint16_t output, uint16_t previous_end, bool borrowed)
{
    int16_t beyond = (int16_t)(previous_end - output - (borrowed ? 1 : 0));
    return beyond > 0 ? (uint16_t)(beyond + 2) : 0;
}

/* The bitmap has ended: clear what the previous bitmap drew beyond it, and
 * mark where this one ends. */
static void end_bitmap(GameState *g, bool borrowed)
{
    uint16_t output = scene_scaler_output(g);
    uint16_t bytes = left_over(output, scene_scaler_output_end(g), borrowed);
    if (bytes) {
        wram_fill8(g, in_scaler_bank(0, output), bytes, 0);
        charge_block_move(g, (uint32_t)bytes - 1);
    }
    set_scene_scaler_output_end(g, output);
}

typedef enum { ROW_DRAWN, BITMAP_ENDED } RowOutcome;

static void next_output_row(GameState *g)
{
    uint16_t next = (uint16_t)(scene_scaler_output(g) + ROW_BYTES);
    set_scene_scaler_output(g, next);
    set_scene_scaler_plane_output(g, next);
}

// $C3:0309
/* The next output row, or the end of the bitmap. `row_nudge` picks the row's
 * source 1/128 of a row further on (see the top of the file); where the rows
 * have run out, it is what the measure of the leftover borrows against. */
static RowOutcome scale_row(GameState *g, bool row_nudge)
{
    uint16_t rows_left = (uint16_t)(scene_scaler_rows_left(g) - 1);
    set_scene_scaler_rows_left(g, rows_left);
    if (rows_left == 0) {
        end_bitmap(g, !row_nudge);
        return BITMAP_ENDED;
    }
    uint16_t accumulator = (uint16_t)(scene_scaler_row_position(g) + scene_scaler_row_step(g) + (row_nudge ? 1 : 0));
    set_scene_scaler_row_position(g, accumulator);
    uint16_t source_row = (uint16_t)(accumulator / ROW_UNITS * SOURCE_ROW_BYTES);
    if (source_row >= scene_scaler_source_limit(g)) {
        end_bitmap(g, false);
        return BITMAP_ENDED;
    }

    uint16_t output = scene_scaler_output(g);
    uint16_t source = (uint16_t)(source_row + scene_scaler_source_base(g));
    uint16_t previous = scene_scaler_source_row(g);
    if (source == previous) {
        repeat_last_row(g, output);
    } else {
        TIMED_AS(g, SAMPLE_ROW, {
            set_scene_scaler_source_row(g, source);
            sample_row(g, source, source > previous);
        });
    }
    next_output_row(g);
    return ROW_DRAWN;
}

/* ---- The bitmaps the port draws --------------------------------------------------------------- */

/* The bitmaps the port draws: in the scaler's bank, with the plane pointer
 * not wrapping past the end of the bank, the source's samples in the bank,
 * and a source limit a row's position can be compared with either way (at
 * most $8000; the original compares by the sign of the difference). Past
 * those the original's carries reach its plane pointer and its reads its own
 * scratch, and the port draws no such bitmap. No scene's reaches them. */
enum { LARGEST_LIMIT = 0x8000, SAMPLES_REACH = 0x0101 };

bool scene_scaler_draws(const GameState *g)
{
    uint32_t pairs = scene_scaler_pairs(g) ? scene_scaler_pairs(g) : 0x10000u;
    uint32_t planes = scene_scaler_plane_output(g);
    uint32_t base = scene_scaler_source_base(g), limit = scene_scaler_source_limit(g);
    bool planes_fit = planes + pairs * GROUPS_PER_PAIR * PLANES <= POSITIONS;
    bool limit_compares = limit <= LARGEST_LIMIT;
    bool source_in_bank = limit == 0 || base + (limit - 1) + SAMPLES_REACH <= POSITIONS;
    return planes_fit && limit_compares && source_in_bank;
}

/* The whole bitmap at once: the first
 * row nudged by `row_nudge`, each after it by the carry of the output's step
 * before it, which only a step past the bank's end leaves. False where a row
 * is one the port doesn't draw, which the program can't go on from. */
static void draw_bitmap(GameState *g, bool row_nudge)
{
    for (;;) {
        if (scale_row(g, row_nudge) == BITMAP_ENDED)
            return;
        row_nudge = scene_scaler_output(g) < ROW_BYTES;
    }
}

static bool bitmap_fits(const GameState *g, uint16_t position, bool row_nudge);

bool scene_scale_the_bitmap(GameState *g, bool row_nudge)
{
    /* Native callers have just handed over a new picture with scaler_start. */
    if (!bitmap_fits(g, 0, row_nudge)) {
        program_fail(g, ROW_LOOP, "a bitmap the port's scaler doesn't draw");
        return false;
    }
    draw_bitmap(g, row_nudge);
    return true;
}

/* Follow the output extents without drawing. A short source can end a tall
 * requested picture early, so checking only rows * stride would refuse
 * valid pictures. No bitmap pixels or persistent state change here. */
static bool bitmap_fits(const GameState *g, uint16_t position, bool row_nudge)
{
    if (!scene_scaler_draws(g))
        return false;
    uint16_t rows = scene_scaler_rows_left(g);
    uint16_t output = scene_scaler_output(g);
    uint32_t row_bytes = (uint32_t)scene_scaler_pairs(g) * GROUPS_PER_PAIR * PLANES;
    uint16_t step = scene_scaler_row_step(g), limit = scene_scaler_source_limit(g);
    for (;;) {
        if (--rows == 0)
            return true;
        position = (uint16_t)(position + step + (row_nudge ? 1 : 0));
        if (position / ROW_UNITS * SOURCE_ROW_BYTES >= limit)
            return true;
        output = (uint16_t)(output + ROW_BYTES);
        if ((uint32_t)output + row_bytes > POSITIONS)
            return false;
        row_nudge = output < ROW_BYTES;
    }
}
