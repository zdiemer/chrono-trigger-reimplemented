/* Scenes: a circle as a per-line window table.
 *
 * $C3:0077 fills a table of 224 lines, two bytes each (the window's left and
 * right X), with a circle: the program's per-line window effect reads it to
 * open or close the picture as an iris (the endings, the End of Time's gate).
 * The caller leaves the circle in the scene's own cells -- the center's X and
 * line, the radius, the table's address and, in bit 0 of the bank cell, whether
 * the table is in bank $7E or $7F. The outline comes from the midpoint
 * algorithm, one octant pair a step from the center line up; the mode says how
 * the lower half is made from the upper one (mirrored, or every other line for a
 * circle squashed to half height) and, with bit 7, that the lines outside the
 * circle are left alone rather than closed (left 1, right 0). A radius of 0
 * closes every line.
 *
 * The original saves and restores every register, so the port is its memory
 * writes alone. Its additions chain the carry from one to the next, which moves
 * some edges by a pixel; `carry` follows it through, and the scratch cells are
 * written as the original writes them.
 *
 * The table itself is the one thing here with no name: the caller says where it
 * is, so the port reaches it through game/wram.h rather than game/vars.h. */
#include "scene/scene.h"

#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

enum { CLOSED_LINE = 0x0001 };   /* left 1, right 0: nothing inside */
enum { LINES = 0xE0, LAST_LINE = 0xDF, RIGHT_EDGE = 0xFF };
enum { MODE_MIRROR = 0, MODE_HALF_HEIGHT = 1, MODE_LEAVE_OUTSIDE = 0x80 };

static uint16_t adc(bool *carry, uint16_t a, uint16_t b)
{
    uint32_t sum = (uint32_t)a + b + (*carry ? 1 : 0);
    *carry = sum > 0xFFFF;
    return (uint16_t)sum;
}

static uint16_t sbc(bool *carry, uint16_t a, uint16_t b) { return adc(carry, a, (uint16_t)~b); }

static bool negative(uint16_t v) { return v & 0x8000; }

static uint8_t table_bank(const GameState *g)
{
    return (scene_circle_table_bank(g) & 1) ? 0x7F : 0x7E;
}

/* A line of the table, at its byte position from the table's start. */
static uint16_t line_word(const GameState *g, uint8_t bank, uint16_t at)
{
    return wram_read16(g, (uint32_t)bank << 16 | at);
}

static void set_line_word(GameState *g, uint8_t bank, uint16_t at, uint16_t v)
{
    wram_write16(g, (uint32_t)bank << 16 | at, v);
}

static void set_line_right(GameState *g, uint8_t bank, uint16_t at, uint8_t v)
{
    wram_write8(g, ((uint32_t)bank << 16) + 1 + at, v);
}

// $C3:01C6, $C3:01CA
/* Close the lines from `at` on: the first word, then a block move repeats it
 * over the next count + 1 bytes, the count from the scratch cell. The first
 * word goes in the bank the caller is working in; the block move takes its bank
 * from the cell again, which is not the same thing when the table has just been
 * written over that cell. */
static void close_lines(GameState *g, uint8_t bank, uint16_t at)
{
    set_line_word(g, bank, at, CLOSED_LINE);
    uint32_t copy = (uint32_t)table_bank(g) << 16;
    uint16_t count = scene_circle_count(g);
    for (uint32_t i = 0; i <= count; i++)
        wram_write8(g, copy | (uint16_t)(at + 2 + i), wram_read8(g, copy | (uint16_t)(at + i)));
}

/* A line's edges around the center: the left edge clamped at 0 is stored as a
 * word, and the right one clamped at RIGHT_EDGE as its second byte. */
static void set_edges(GameState *g, uint8_t bank, uint16_t at, uint16_t left, uint16_t right)
{
    set_line_word(g, bank, at, negative(left) ? 0 : left);
    set_line_right(g, bank, at, (uint8_t)(negative((uint16_t)(right - RIGHT_EDGE)) ? right : RIGHT_EDGE));
}

// $C3:0077
static void draw_circle(GameState *g, uint8_t mode)
{
    /* The center and the radius are 8-bit values in 16-bit cells: clear the high
     * halves, as the original does, so the arithmetic below reads them as words. */
    set_scene_circle_center_x(g, scene_circle_center_x(g) & 0xFF);
    set_scene_circle_center_y(g, scene_circle_center_y(g) & 0xFF);
    set_scene_circle_radius(g, scene_circle_radius(g) & 0xFF);

    uint8_t bank = table_bank(g);
    if (scene_circle_radius(g) == 0) {
        set_scene_circle_count(g, 2 * LINES);
        close_lines(g, bank, scene_circle_table(g));
        return;
    }

    /* The midpoint circle, from the center line up. x and y are the original's
     * X and Y, which it keeps in the count cell in turn and reloads from there;
     * the cells are read where it reads them. */
    uint16_t x = scene_circle_radius(g), y = 0;
    bool carry = false;   /* the ASL of the radius */
    set_scene_circle_decision(g, (uint16_t)(x << 1));
    set_scene_circle_decision(g, sbc(&carry, 3, scene_circle_decision(g)));
    carry = false;
    uint16_t center_row = adc(&carry, (uint16_t)(scene_circle_center_y(g) << 1), scene_circle_table(g));
    set_scene_circle_row(g, center_row);
    carry = center_row & 1;
    set_scene_circle_center_row_half(g, center_row >> 1);
    for (;;) {
        set_scene_circle_count(g, x);
        uint16_t half = sbc(&carry, scene_circle_center_row_half(g), scene_circle_count(g));
        carry = negative(half);
        uint16_t upper = (uint16_t)(half << 1);   /* the line x above the center */
        uint16_t left = adc(&carry, (uint16_t)-y, scene_circle_center_x(g));
        uint16_t right = adc(&carry, y, scene_circle_center_x(g));
        carry = right >= RIGHT_EDGE;
        set_edges(g, bank, upper, left, right);

        x = scene_circle_count(g);
        set_scene_circle_count(g, y);
        uint16_t row = (uint16_t)(scene_circle_row(g) - 2);   /* the line y above the center */
        set_scene_circle_row(g, row);
        left = adc(&carry, (uint16_t)-x, scene_circle_center_x(g));
        carry = false;
        right = adc(&carry, x, scene_circle_center_x(g));
        carry = right >= RIGHT_EDGE;
        set_edges(g, bank, row, left, right);
        y = scene_circle_count(g);

        uint16_t decision = scene_circle_decision(g);
        uint16_t step;
        if (negative(decision)) {
            carry = (y >> 14) & 1;   /* shifted out by the second ASL */
            step = adc(&carry, (uint16_t)(y << 2), 6);
        } else {
            set_scene_circle_count(g, x);
            x--;
            uint16_t dx = sbc(&carry, y, scene_circle_count(g));
            carry = negative((uint16_t)(dx << 1));
            step = adc(&carry, (uint16_t)(dx << 2), 0x0A);
        }
        set_scene_circle_decision(g, adc(&carry, step, scene_circle_decision(g)));
        set_scene_circle_count(g, y);
        y++;
        uint16_t last_y = scene_circle_count(g);
        carry = x >= last_y;
        if (negative((uint16_t)(x - last_y)))
            break;
    }

    /* the lower half, from the center line down */
    uint16_t from = center_row, to = center_row;
    set_scene_circle_count(g, (uint16_t)(scene_circle_radius(g) + 1));
    set_scene_circle_decision(g, mode);
    uint16_t height;
    if ((mode & ~MODE_LEAVE_OUTSIDE) == MODE_MIRROR) {
        do {
            from = (uint16_t)(from - 2);
            set_line_word(g, bank, to, line_word(g, bank, from));
            to = (uint16_t)(to + 2);
            set_scene_circle_count(g, (uint16_t)(scene_circle_count(g) - 1));
        } while (scene_circle_count(g) != 0);
        if (scene_circle_decision(g) & MODE_LEAVE_OUTSIDE)
            return;
        height = scene_circle_radius(g);
    } else if ((mode & ~MODE_LEAVE_OUTSIDE) == MODE_HALF_HEIGHT) {
        set_scene_circle_count(g, scene_circle_count(g) >> 1);
        uint16_t lines = scene_circle_count(g);
        if (negative(lines) || lines == 0)
            return;
        do {
            from = (uint16_t)(from - 4);
            set_line_word(g, bank, to, line_word(g, bank, from));
            to = (uint16_t)(to + 2);
            set_scene_circle_count(g, (uint16_t)(scene_circle_count(g) - 1));
        } while (scene_circle_count(g) != 0);
        if (scene_circle_decision(g) & MODE_LEAVE_OUTSIDE)
            return;
        height = scene_circle_radius(g) >> 1;
    } else {
        return;
    }

    /* close the lines below the circle and above it */
    uint16_t bottom = (uint16_t)(height + scene_circle_center_y(g));
    if (negative((uint16_t)(bottom - LAST_LINE))) {
        set_scene_circle_count(g, bottom);
        uint16_t at = (uint16_t)((bottom << 1) + scene_circle_table(g));
        uint16_t count = (uint16_t)(((uint16_t)(LINES - scene_circle_count(g) - 1) << 1) - 1);
        set_scene_circle_count(g, count);
        close_lines(g, bank, at);
    }
    uint16_t top = (uint16_t)(scene_circle_center_y(g) - scene_circle_radius(g));
    if (negative((uint16_t)(top - 1)) || top == 1)
        return;
    set_scene_circle_count(g, (uint16_t)(((uint16_t)(top - 1) << 1) - 1));
    close_lines(g, bank, scene_circle_table(g));
}

/* The circle as a C call, for the routines that call $C3:0077: its registers
 * come back as they were, because it never touches them. */
void scene_circle_draw(Snes *s, uint8_t mode)
{
    draw_circle(state_of(s), mode);
}

/* The mode in A's low byte. Every register comes back as it was, the flags
 * too except N and Z, which the last pull (PLB) sets from the data bank. */
void scene_draw_circle(Snes *s)
{
    scene_circle_draw(s, (uint8_t)s->cpu.a);
    m_set_nz8(s, s->cpu.db);
    m_rtl(s);
}
