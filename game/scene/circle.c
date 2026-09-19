/* Scenes: a circle as a per-line window table.
 *
 * $C3:0077 fills a table of 224 lines, two bytes each (the window's left and
 * right X), with a circle: the program's per-line window effect reads it to
 * open or close the picture as an iris (the endings, the End of Time's gate).
 * Its cells are on the decompressor's page ($0300): the center's X and line,
 * the radius, the table's address and, in bit 0 of the bank cell, whether the
 * table is in bank $7E or $7F. The circle's outline comes from the midpoint
 * algorithm, one octant pair a step from the center line up; the mode in A
 * says how the lower half is made from the upper one (mirrored, or every other
 * line for a circle squashed to half height) and, with bit 7, that the lines
 * outside the circle are left alone rather than closed (left 1, right 0). A
 * radius of 0 closes every line.
 *
 * The original saves and restores every register, so the port is its memory
 * writes alone. Its additions chain the carry from one to the next, which
 * moves some edges by a pixel; `carry` follows it through, and the direct
 * page's scratch cells are written as the original writes them. */
#include "scene/scene.h"

#include "port/machine.h"
#include "ram.h"

enum {
    CENTER_X = RAM_SCENE_CIRCLE_CENTER_X_DP,
    CENTER_Y = RAM_SCENE_CIRCLE_CENTER_Y_DP,
    RADIUS = RAM_SCENE_CIRCLE_RADIUS_DP,
    TABLE = RAM_SCENE_CIRCLE_TABLE_DP,
    TABLE_BANK = RAM_SCENE_CIRCLE_TABLE_BANK_DP,
    /* scratch */
    COUNT = 0xF0,
    DECISION = 0xF2,
    ROW = 0xF4,
    CENTER_ROW_HALF = 0xF6,
};

enum { CLOSED_LINE = 0x0001 };   /* left 1, right 0: nothing inside */
enum { LINES = 0xE0, LAST_LINE = 0xDF, RIGHT_EDGE = 0xFF };
enum { MODE_MIRROR = 0, MODE_HALF_HEIGHT = 1, MODE_LEAVE_OUTSIDE = 0x80 };

enum { SHAPE_PAGE = 0x0300 };

static uint16_t cell(Snes *s, uint8_t dp) { return m_read16(s, m_dp(s, dp)); }
static void set_cell(Snes *s, uint8_t dp, uint16_t v) { m_write16(s, m_dp(s, dp), v); }

static uint16_t adc(bool *carry, uint16_t a, uint16_t b)
{
    uint32_t sum = (uint32_t)a + b + (*carry ? 1 : 0);
    *carry = sum > 0xFFFF;
    return (uint16_t)sum;
}

static uint16_t sbc(bool *carry, uint16_t a, uint16_t b) { return adc(carry, a, (uint16_t)~b); }

static bool negative(uint16_t v) { return v & 0x8000; }

static uint8_t table_bank(Snes *s) { return (m_read8(s, m_dp(s, TABLE_BANK)) & 1) ? 0x7F : 0x7E; }

/* A table word, through the data bank (which is the table's). */
static uint16_t line_word(Snes *s, uint16_t at) { return m_read16(s, m_abs(s, 0) + at); }
static void set_line_word(Snes *s, uint16_t at, uint16_t v) { m_write16(s, m_abs(s, 0) + at, v); }
static void set_line_right(Snes *s, uint16_t at, uint8_t v) { m_write8(s, m_abs(s, 1) + at, v); }

// $C3:01C6, $C3:01CA
/* Close the lines from `at` on: the first word, then MVN in the table's bank
 * repeats it over the next count + 1 bytes, the count from its cell. */
static void close_lines(Snes *s, uint16_t at)
{
    set_line_word(s, at, CLOSED_LINE);
    uint32_t bank = (uint32_t)table_bank(s) << 16;
    uint16_t count = cell(s, COUNT);
    for (uint32_t i = 0; i <= count; i++)
        m_write8(s, bank | (uint16_t)(at + 2 + i), m_read8(s, bank | (uint16_t)(at + i)));
}

/* A line's edges around the center: the left edge clamped at 0 is stored as a
 * word, and the right one clamped at RIGHT_EDGE as its second byte. */
static void set_edges(Snes *s, uint16_t at, uint16_t left, uint16_t right)
{
    set_line_word(s, at, negative(left) ? 0 : left);
    set_line_right(s, at, (uint8_t)(negative((uint16_t)(right - RIGHT_EDGE)) ? right : RIGHT_EDGE));
}

// $C3:0077
static void draw_circle(Snes *s, uint8_t mode)
{
    for (uint8_t dp = CENTER_X + 1; dp <= RADIUS + 1; dp += 2)
        m_write8(s, m_dp(s, dp), 0);   /* the byte cells' high bytes */
    s->cpu.db = table_bank(s);
    if (cell(s, RADIUS) == 0) {
        set_cell(s, COUNT, 2 * LINES);
        close_lines(s, cell(s, TABLE));
        return;
    }

    /* The midpoint circle, from the center line up. x and y are the original's
     * X and Y, which it keeps in $F0 in turn and reloads from there; the cells
     * are read where it reads them. */
    uint16_t x = cell(s, RADIUS), y = 0;
    bool carry = false;   /* the ASL of the radius */
    set_cell(s, DECISION, (uint16_t)(x << 1));
    set_cell(s, DECISION, sbc(&carry, 3, cell(s, DECISION)));
    carry = false;
    uint16_t center_row = adc(&carry, (uint16_t)(cell(s, CENTER_Y) << 1), cell(s, TABLE));
    set_cell(s, ROW, center_row);
    carry = center_row & 1;
    set_cell(s, CENTER_ROW_HALF, center_row >> 1);
    for (;;) {
        set_cell(s, COUNT, x);
        uint16_t half = sbc(&carry, cell(s, CENTER_ROW_HALF), cell(s, COUNT));
        carry = negative(half);
        uint16_t upper = (uint16_t)(half << 1);   /* the line x above the center */
        uint16_t left = adc(&carry, (uint16_t)-y, cell(s, CENTER_X));
        uint16_t right = adc(&carry, y, cell(s, CENTER_X));
        carry = right >= RIGHT_EDGE;
        set_edges(s, upper, left, right);

        x = cell(s, COUNT);
        set_cell(s, COUNT, y);
        uint16_t row = (uint16_t)(cell(s, ROW) - 2);   /* the line y above the center */
        set_cell(s, ROW, row);
        left = adc(&carry, (uint16_t)-x, cell(s, CENTER_X));
        carry = false;
        right = adc(&carry, x, cell(s, CENTER_X));
        carry = right >= RIGHT_EDGE;
        set_edges(s, row, left, right);
        y = cell(s, COUNT);

        uint16_t decision = cell(s, DECISION);
        uint16_t step;
        if (negative(decision)) {
            carry = (y >> 14) & 1;   /* shifted out by the second ASL */
            step = adc(&carry, (uint16_t)(y << 2), 6);
        } else {
            set_cell(s, COUNT, x);
            x--;
            uint16_t dx = sbc(&carry, y, cell(s, COUNT));
            carry = negative((uint16_t)(dx << 1));
            step = adc(&carry, (uint16_t)(dx << 2), 0x0A);
        }
        set_cell(s, DECISION, adc(&carry, step, cell(s, DECISION)));
        set_cell(s, COUNT, y);
        y++;
        uint16_t last_y = cell(s, COUNT);
        carry = x >= last_y;
        if (negative((uint16_t)(x - last_y)))
            break;
    }

    /* the lower half, from the center line down */
    uint16_t from = center_row, to = center_row;
    set_cell(s, COUNT, (uint16_t)(cell(s, RADIUS) + 1));
    set_cell(s, DECISION, mode);
    uint16_t height;
    if ((mode & ~MODE_LEAVE_OUTSIDE) == MODE_MIRROR) {
        do {
            from = (uint16_t)(from - 2);
            set_line_word(s, to, line_word(s, from));
            to = (uint16_t)(to + 2);
            set_cell(s, COUNT, (uint16_t)(cell(s, COUNT) - 1));
        } while (cell(s, COUNT) != 0);
        if (cell(s, DECISION) & MODE_LEAVE_OUTSIDE)
            return;
        height = cell(s, RADIUS);
    } else if ((mode & ~MODE_LEAVE_OUTSIDE) == MODE_HALF_HEIGHT) {
        set_cell(s, COUNT, cell(s, COUNT) >> 1);
        uint16_t lines = cell(s, COUNT);
        if (negative(lines) || lines == 0)
            return;
        do {
            from = (uint16_t)(from - 4);
            set_line_word(s, to, line_word(s, from));
            to = (uint16_t)(to + 2);
            set_cell(s, COUNT, (uint16_t)(cell(s, COUNT) - 1));
        } while (cell(s, COUNT) != 0);
        if (cell(s, DECISION) & MODE_LEAVE_OUTSIDE)
            return;
        height = cell(s, RADIUS) >> 1;
    } else {
        return;
    }

    /* close the lines below the circle and above it */
    uint16_t bottom = (uint16_t)(height + cell(s, CENTER_Y));
    if (negative((uint16_t)(bottom - LAST_LINE))) {
        set_cell(s, COUNT, bottom);
        uint16_t at = (uint16_t)((bottom << 1) + cell(s, TABLE));
        uint16_t count = (uint16_t)(((uint16_t)(LINES - cell(s, COUNT) - 1) << 1) - 1);
        set_cell(s, COUNT, count);
        close_lines(s, at);
    }
    uint16_t top = (uint16_t)(cell(s, CENTER_Y) - cell(s, RADIUS));
    if (negative((uint16_t)(top - 1)) || top == 1)
        return;
    set_cell(s, COUNT, (uint16_t)(((uint16_t)(top - 1) << 1) - 1));
    close_lines(s, cell(s, TABLE));
}

/* The mode in A's low byte. Every register comes back as it was, the flags
 * too except N and Z, which the last pull (PLB) sets from the data bank. */
void scene_draw_circle(Snes *s)
{
    Cpu *c = &s->cpu;
    uint16_t d = c->d;
    uint8_t db = c->db;
    c->d = SHAPE_PAGE;
    draw_circle(s, (uint8_t)c->a);
    c->d = d;
    c->db = db;
    m_set_nz8(s, db);
    m_rtl(s);
}
