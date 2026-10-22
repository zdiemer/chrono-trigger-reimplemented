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
 * The original draws every row in one call that spans several frames, so the
 * port draws a row per call and stops at the row loop's head ($C3:0309, an
 * entry of its own): each row is compared on its own.
 *
 * The original never clears the carry between its sums, and three of them move
 * what the player sees: a row's samples start 1/256 of a pixel further on when
 * its source row lies further along the bitmap than the last row's, a sample
 * whose position wrapped past the row's 256th pixel moves those after it on by
 * 1/256 (group_positions), and a row's source is picked 1/128
 * of a row further on when the call before left the carry set (the entry's
 * `row_nudge`). The rest of its carries come from sums that wrap past the end
 * of the bank, which no scene's bitmap reaches, and the entry's guard hands
 * such bitmaps to the original. */
#include "scene/scene.h"

#include "charge.h"
#include "original_effects.h"
#include "port/machine.h"
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

/* The row loop's head, where the port stops after each row. */
enum { ROW_LOOP = 0xC30309 };

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

/* The original packs the last group's pixels in the scene's scratch, a word a
 * sample, and shifts each plane out of them: the scratch ends with each pixel
 * shifted down a nibble and the last sample's second byte, and its count of
 * pairs run down to 0. */
static void leave_samples(GameState *g, const uint8_t pixels[GROUP], uint8_t last_second_byte)
{
    for (unsigned i = 0; i < GROUP; i++)
        set_scene_bitplane_scratch_byte(g, i, (uint8_t)(pixels[i] >> PLANES));
    set_scene_bitplane_scratch_byte(g, GROUP, last_second_byte);
    set_scene_scaler_pair_counter(g, 0);
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
    ORIGINAL_EFFECT(leave_samples(g, pixels, wram_read8(g, in_scaler_bank(source, (uint16_t)(position / PIXEL_UNITS + 1)))));
}

/* The row the same as the last one: its 32 bytes copied. */
static void repeat_last_row(GameState *g, uint16_t output)
{
    wram_copy(g, in_scaler_bank(0, (uint16_t)(output - ROW_BYTES)), in_scaler_bank(0, output), ROW_BYTES);
    ORIGINAL_EFFECT(charge_block_move(g, ROW_BYTES));
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
        ORIGINAL_EFFECT(charge_block_move(g, (uint32_t)bytes - 1));
    }
    set_scene_scaler_output_end(g, output);
}

typedef enum { ROW_DRAWN, BITMAP_ENDED } RowOutcome;

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
        set_scene_scaler_source_row(g, source);
        sample_row(g, source, source > previous);
    }
    uint16_t next = (uint16_t)(output + ROW_BYTES);
    set_scene_scaler_output(g, next);
    set_scene_scaler_plane_output(g, next);
    return ROW_DRAWN;
}

/* ---- Entries: 16-bit A and index registers, on the scene's page, bank $7F ------------------- */

/* The bitmaps the port draws: in the scaler's bank, with the plane pointer
 * not wrapping past the end of the bank, the source's samples in the bank,
 * and a source limit a row's position can be compared with either way (at
 * most $8000; the original compares by the sign of the difference). Past
 * those the original's carries reach its plane pointer and its reads its own
 * scratch, and the bitmap is the original's to draw. No scene's reaches them. */
enum { LARGEST_LIMIT = 0x8000, SAMPLES_REACH = 0x0101 };

bool scene_scaler_in_its_bank(Snes *s)
{
    if (!scene_on_scene_page(s) || s->cpu.db != SCENE_UPPER_WORK_RAM)
        return false;
    vars_peek(true);
    GameState *g = state_of(s);
    uint32_t pairs = scene_scaler_pairs(g) ? scene_scaler_pairs(g) : 0x10000u;
    uint32_t planes = scene_scaler_plane_output(g);
    uint32_t base = scene_scaler_source_base(g), limit = scene_scaler_source_limit(g);
    vars_peek(false);
    bool planes_fit = planes + pairs * GROUPS_PER_PAIR * PLANES <= POSITIONS;
    bool limit_compares = limit <= LARGEST_LIMIT;
    bool source_in_bank = limit == 0 || base + (limit - 1) + SAMPLES_REACH <= POSITIONS;
    return planes_fit && limit_compares && source_in_bank;
}

/* The flags the original leaves where the bitmap ends: those of its measure of
 * the leftover, the previous end less this one's (less the borrow). */
static void leave_flags_of_measuring(Snes *s, uint16_t previous_end, uint16_t output, bool borrowed)
{
    uint16_t beyond = (uint16_t)(previous_end - output - (borrowed ? 1 : 0));
    m_set_flag(s, CPU_FLAG_C, (uint32_t)output + (borrowed ? 1 : 0) <= previous_end);
    m_set_flag(s, CPU_FLAG_V, ((previous_end ^ output) & (previous_end ^ beyond) & 0x8000) != 0);
}

/* One row per call (see the top of the file), the carry the row's nudge. Back
 * at the row loop's head (the port's stop), the carry is that of the output
 * pointer's step, which wraps only past the bank's end; at the return, the flags
 * are those of the measure of the leftover. */
void scene_scale_bitmap_row(Snes *s)
{
    GameState *g = state_of(s);
    bool row_nudge = m_flag(s, CPU_FLAG_C);
    uint16_t previous_end = scene_scaler_output_end(g);
    RowOutcome outcome = scale_row(g, row_nudge);
    if (outcome == BITMAP_ENDED) {
        bool rows_ran_out = scene_scaler_rows_left(g) == 0;
        leave_flags_of_measuring(s, previous_end, scene_scaler_output(g), rows_ran_out && !row_nudge);
        m_rtl(s);
        return;
    }
    m_set_flag(s, CPU_FLAG_C, scene_scaler_output(g) < ROW_BYTES);
    m_stop_at(s, ROW_LOOP, DC_Y | DC_V);
}

// $C3:0307
/* A bitmap starts with the row accumulator at 0; its rows are the loop's. */
void scene_scale_bitmap(Snes *s)
{
    set_scene_scaler_row_position(state_of(s), 0);
    m_stop_at(s, ROW_LOOP, 0);
}
