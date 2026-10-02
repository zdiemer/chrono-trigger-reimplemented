/* Scenes: an iris -- a circle drawn into a per-line window table.
 *
 * The endings, the End of Time's gate and the menus' sprite objects open and
 * close the picture as an iris: a window whose left and right edge on each of
 * the 224 lines a per-line effect reads from a table, two bytes a line. This
 * fills that table with a circle. The caller leaves the circle in the scene's
 * cells -- its center's X and line, its radius, the table's address and, in bit
 * 0 of the bank cell, whether the table is in the second bank of work RAM -- and
 * passes a mode: how the lower half is made from the upper one (mirrored, or
 * every other line of it, for a circle squashed to half its height) and, with
 * bit 7, that the lines outside the circle are left alone rather than closed.
 * A radius of 0 closes every line.
 *
 * The upper half's outline is the midpoint circle, stepped from the top of the
 * circle and from its center line toward the diagonal; the original's sums round
 * it a pixel here and there, which edge_rounding and decision_step say where.
 * Every line outside the circle is closed: left 1, right 0, nothing inside.
 *
 * The original keeps its loop state in cells high on the decompressor's page;
 * the port keeps it in locals and leaves the cells as the original leaves them,
 * for shadow mode (the cells are other code's scratch too, which writes them
 * before it reads them). */
#include "scene/scene.h"

#include "charge.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

enum { LINES = 224, LINE_BYTES = 2, LAST_OPEN_BOTTOM = LINES - 1 };
enum { RIGHT_EDGE = 0xFF };                    /* where a line's right edge is clamped */
enum { CLOSED_LINE = 0x0001 };                  /* left 1, right 0 */

/* The circle, as the caller describes it. */
typedef struct {
    int16_t center_x, center_y, radius;   /* bytes */
    uint8_t bank;                          /* the table's */
    uint16_t table;                        /* its address in the bank */
    uint16_t center_row;                   /* the table address of the center line */
} Circle;

/* ---- The table ------------------------------------------------------------------------------ */

static uint32_t in_table(const Circle *c, uint16_t at) { return (uint32_t)c->bank << 16 | at; }

/* A line's edges, clamped to the screen: a left edge left of it is 0, a right
 * edge at or past its right edge RIGHT_EDGE. */
static void set_line(GameState *g, const Circle *c, uint16_t at, int left, int right)
{
    wram_write8(g, in_table(c, at), (uint8_t)(left < 0 ? 0 : left));
    wram_write8(g, in_table(c, (uint16_t)(at + 1)), (uint8_t)(right < RIGHT_EDGE ? right : RIGHT_EDGE));
}

static void copy_line(GameState *g, const Circle *c, uint16_t from, uint16_t to)
{
    wram_write16(g, in_table(c, to), wram_read16(g, in_table(c, from)));
}

static uint16_t line_at(const Circle *c, int line) { return (uint16_t)(c->table + line * LINE_BYTES); }

// $C3:01C6, $C3:01CA
/* Close `lines` lines of the table from `first_line` on. The original closes
 * the first and copies it on over the rest with a block move, whose time is
 * charged here. */
static void close_lines(GameState *g, const Circle *c, int first_line, int lines)
{
    wram_fill16(g, in_table(c, line_at(c, first_line)), (uint32_t)lines, CLOSED_LINE);
    ORIGINAL_EFFECT(charge_block_move(g, (uint32_t)(lines - 1) * LINE_BYTES));
}

/* The original closes a whole table with a block move that runs three bytes
 * past it -- the pattern of closed lines, on into whatever follows the table.
 * Nothing reads them as lines; they are written as the original writes them. */
enum { SPILL_BYTES = 3 };

static void spill_past_table(GameState *g, const Circle *c)
{
    wram_fill16(g, in_table(c, line_at(c, LINES)), 1, CLOSED_LINE);
    wram_fill8(g, in_table(c, (uint16_t)(line_at(c, LINES) + LINE_BYTES)), 1, (uint8_t)CLOSED_LINE);
    charge_block_move(g, SPILL_BYTES);
}

/* ---- The outline ---------------------------------------------------------------------------- */

/* Where the outline is: the midpoint circle's x (from the radius down) and y
 * (from 0 up), its decision value, and the table address of the near line --
 * the line y + 1 above the center, which moves up a line a step. */
typedef struct {
    int16_t x, y, decision;
    uint16_t near_row;
    bool first;
} Octant;

/* A step of the outline draws two lines of the upper half: the far one, x above
 * the center, y either side of it; and the near one, y + 1 above the center, x
 * either side. */
typedef struct {
    uint16_t far_row;
    int far_left, far_right, near_left, near_right;
} Step;

/* How the original's sums round a step, which the picture shows. It adds each
 * edge to the center without clearing the carry the sum before it left, so:
 * - the first step's far line (the top of the circle) is a line higher when
 *   the center line's table address is even, which it always is in the game;
 * - a far line above the start of the table's bank has its left edge a pixel
 *   to the right (no scene's table is that low);
 * - the far lines' right edge is a pixel further right unless their left edge
 *   is off the left of the screen, so the top and bottom of the circle lean a
 *   pixel right;
 * - and a near line's left edge is a pixel to the right when the far line's
 *   right edge reached the right of the screen. */
static Step edge_rounding(const Circle *c, const Octant *o)
{
    Step s;
    int16_t higher = (o->first && !(c->center_row & 1)) ? 1 : 0;
    int16_t far_half = (int16_t)((c->center_row >> 1) - o->x - higher);
    int16_t above_bank = far_half < 0 ? 1 : 0;
    s.far_row = (uint16_t)(far_half * 2);
    s.far_left = c->center_x - o->y + above_bank;
    bool left_on_screen = o->y > 0 && c->center_x + above_bank >= o->y;
    s.far_right = c->center_x + o->y + (left_on_screen ? 1 : 0);
    s.near_left = c->center_x - o->x + (s.far_right >= RIGHT_EDGE ? 1 : 0);
    s.near_right = c->center_x + o->x;
    return s;
}

/* The midpoint test's step, as the original rounds it. While the decision is
 * negative only y steps, by 4y + 6. Otherwise x steps too, by 4(y - x) + 10 --
 * with y - x one less while the near line's right edge is on screen, one more
 * while it is negative, and one more again on the last steps before the
 * diagonal, where it is -1 or -2 (the original's sum carries into the next). */
static int16_t decision_step(const Octant *o, int near_right)
{
    if (o->decision < 0)
        return (int16_t)(4 * o->y + 6);
    int dx = o->y - o->x - (near_right >= RIGHT_EDGE ? 0 : 1);
    return (int16_t)(4 * dx + 10 + (dx < 0 ? 1 : 0) + (dx == -1 || dx == -2 ? 1 : 0));
}

/* Draw the upper half, stepping toward the diagonal. Returns where it ended:
 * the near line, its x and y and the decision value there, which the lower
 * half starts from (the cells the original leaves hold the mode, not the
 * decision: leave_cells). */
static Octant draw_upper_half(GameState *g, const Circle *c)
{
    Octant o = { c->radius, 0, (int16_t)(2 - 2 * c->radius), c->center_row, true };
    for (;;) {
        Step s = edge_rounding(c, &o);
        set_line(g, c, s.far_row, s.far_left, s.far_right);
        o.near_row = (uint16_t)(o.near_row - LINE_BYTES);
        set_line(g, c, o.near_row, s.near_left, s.near_right);
        int16_t step = decision_step(&o, s.near_right);
        if (o.decision >= 0)
            o.x--;
        o.decision = (int16_t)(o.decision + step);
        o.first = false;
        if (o.x < o.y++)
            return o;
    }
}

/* The lower half from the upper: each line below the center a copy of the line
 * as far above it, or of the line twice as far for a circle squashed to half
 * its height. */
static void draw_lower_half(GameState *g, const Circle *c, int lines, int step)
{
    uint16_t from = c->center_row, to = c->center_row;
    for (int i = 0; i < lines; i++) {
        from = (uint16_t)(from - step * LINE_BYTES);
        copy_line(g, c, from, to);
        to = (uint16_t)(to + LINE_BYTES);
    }
}

/* The original's cells as it leaves them: the last count it kept (a block
 * move's count, or the lower half's lines run down), the mode (or, with a
 * radius of 0, the cell as it was), the near line's address, and half the
 * center line's. */
static void leave_cells(GameState *g, const Circle *c, uint16_t count, uint8_t mode, const Octant *o)
{
    set_scene_circle_count(g, count);
    if (c->radius == 0)
        return;
    set_scene_circle_decision(g, mode);
    set_scene_circle_row(g, o->near_row);
    set_scene_circle_center_row_half(g, (uint16_t)(c->center_row >> 1));
}

/* The center and the radius are bytes in word cells, which the original reads
 * as words with their high halves cleared; the callers write only the bytes, so
 * nothing reads the high halves but the original clearing them again. */
static void leave_center_and_radius_as_bytes(GameState *g, const Circle *c)
{
    set_scene_circle_center_x(g, (uint16_t)c->center_x);
    set_scene_circle_center_y(g, (uint16_t)c->center_y);
    set_scene_circle_radius(g, (uint16_t)c->radius);
}

/* The byte counts the original's cells keep for a run of closed lines: its
 * block move's count. */
static uint16_t closing_count(int lines) { return (uint16_t)(lines * LINE_BYTES - 3); }

// $C3:0077
static void draw_circle(GameState *g, uint8_t mode)
{
    Circle c = { (int16_t)(scene_circle_center_x(g) & 0xFF), (int16_t)(scene_circle_center_y(g) & 0xFF),
                 (int16_t)(scene_circle_radius(g) & 0xFF),
                 (scene_circle_table_bank(g) & 1) ? SCENE_UPPER_WORK_RAM : SCENE_LOWER_WORK_RAM, scene_circle_table(g), 0 };
    c.center_row = line_at(&c, c.center_y);
    ORIGINAL_EFFECT(leave_center_and_radius_as_bytes(g, &c));

    if (c.radius == 0) {
        close_lines(g, &c, 0, LINES);
        ORIGINAL_EFFECT(spill_past_table(g, &c));
        ORIGINAL_EFFECT(leave_cells(g, &c, (uint16_t)(LINES * LINE_BYTES), mode, NULL));
        return;
    }

    Octant o = draw_upper_half(g, &c);
    uint16_t count = (uint16_t)(c.radius + 1);   /* what the original's count ends as */
    int height;
    switch (mode & ~SCENE_CIRCLE_KEEPS_OUTSIDE) {
    case SCENE_CIRCLE_ROUND:
        draw_lower_half(g, &c, c.radius + 1, 1);
        height = c.radius;
        break;
    case SCENE_CIRCLE_SQUASHED:
        draw_lower_half(g, &c, (c.radius + 1) / 2, 2);
        height = c.radius / 2;
        break;
    default:
        ORIGINAL_EFFECT(leave_cells(g, &c, count, mode, &o));
        return;
    }
    count = 0;

    if (!(mode & SCENE_CIRCLE_KEEPS_OUTSIDE)) {
        /* close the lines below the circle, from its last line on; with only
         * the table's last line left, the original's count would run its block
         * move through the whole bank, and it closes none */
        int bottom = height + c.center_y;
        if (bottom < LAST_OPEN_BOTTOM) {
            close_lines(g, &c, bottom, LINES - bottom);
            count = closing_count(LINES - bottom);
        }
        /* and the lines above it; with a single line above, the original's
         * count for it would run the block move through the whole bank, and it
         * closes none, leaving the top line as it was */
        int top = c.center_y - c.radius;
        if (top > 1) {
            close_lines(g, &c, 0, top);
            count = closing_count(top);
        }
    }
    ORIGINAL_EFFECT(leave_cells(g, &c, count, mode, &o));
}

/* The iris as a C call, for the menus' sprite objects, which draw one. */
void scene_circle_draw(GameState *g, uint8_t mode) { draw_circle(g, mode); }

/* The port keeps its loop state in locals, where the original reads the
 * decompressor page's cells back: the two are the same routine only while the
 * table (which a step can reach up to a radius's lines before its start, and
 * the closing a table's length after it) stays clear of those cells. No scene's
 * table comes near them. */
enum { CIRCLE_CELLS = 0x0350, CIRCLE_CELLS_END = 0x03F8, REACH_BEFORE = 0x0200, REACH_AFTER = 0x0400 };

static bool ranges_meet(uint16_t a, uint16_t a_bytes, uint16_t b, uint16_t b_bytes)
{
    return (uint16_t)(b - a) < a_bytes || (uint16_t)(a - b) < b_bytes;
}

bool scene_circle_table_is_clear(Snes *s)
{
    vars_peek(true);
    GameState *g = state_of(s);
    bool upper = scene_circle_table_bank(g) & 1;
    uint16_t table = scene_circle_table(g);
    vars_peek(false);
    return upper || !ranges_meet((uint16_t)(table - REACH_BEFORE), REACH_BEFORE + REACH_AFTER, CIRCLE_CELLS,
                                 CIRCLE_CELLS_END - CIRCLE_CELLS);
}

/* The mode in A's low byte. Every register comes back as it was, the flags
 * too except N and Z, which the last pull (PLB) sets from the data bank. */
void scene_draw_circle(Snes *s)
{
    draw_circle(state_of(s), (uint8_t)s->cpu.a);
    m_set_nz8(s, s->cpu.db);
    m_rtl(s);
}
