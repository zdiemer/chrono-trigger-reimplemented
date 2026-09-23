/* Field: window shapes. A quadrilateral rasterized into the per-line table the
 * field's window line effect reads, so that a four-cornered shape of light or
 * shadow can cover part of the map: the searchlight cones in the Black Omen.
 * The field copies the corners in from field_window_shape_quad and points the
 * table at field_window_edge_lines or its copy. The code lives in bank C3,
 * beside the scene engine's circle, and this port stands in for the edge
 * drawing it runs too ($C3:0F35-$C3:1025).
 *
 * The table has two bytes a line, the window's left and right X. The four
 * edges are drawn in order round the shape from its topmost corner: the edges
 * going down are its left side and write the left column, and from the first
 * edge going up (whose lower end is where the right side starts) they are its
 * right side and write the right column. An edge writes its X on each line
 * from its upper end down to the line before its lower end, stepped with an
 * error term as Bresenham's line is. Then every line below the right side's
 * start, and above the top, is marked empty.
 *
 * The table is a buffer the caller locates (its pointer is
 * field_window_shape_table), so it is reached by address. */
#include "field/field.h"

#include "field/bridge.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

enum { CORNERS = 4, EDGES = 4 };
enum { LINE_BYTES = 2, RIGHT_COLUMN = 1 };
enum { LAST_LINE = 0xFF };
enum { EMPTY_LEFT = 1, EMPTY_RIGHT = 0 };   /* a line the window doesn't cover: right of its left */
enum { FILL_BANK = 0x7E, FILL_BANK_ODD = 0x7F };

typedef struct {
    uint8_t x, y;
} Point;

/* An edge, from its start to its end round the shape. */
typedef struct {
    Point from, to;
} Edge;

static Point corner(const GameState *g, unsigned c)
{
    return (Point){ field_window_shape_corners_x(g, c), field_window_shape_corners_y(g, c) };
}

/* ---- For the original's code and shadow mode (field/scratch.h) -------------------------- */

/* What the original leaves in its working cells when it is done: the last
 * edge drawn, upper end first ($53-$5A); the fill's length ($F0) and no edges
 * left ($F4); whether the right side was still to come ($F6); and the last
 * sloped edge's lines left, width, height and error ($F2, $F8, $FA, $FC).
 * Nothing the port reads. */
typedef struct {
    Edge last;
    uint16_t fill_length;
    bool right_side_found;
    bool sloped;              /* an edge with height was drawn */
    uint16_t width, error;
    uint8_t height;
} Leftovers;

static void leave_step_cell(GameState *g, uint8_t cell, uint16_t v)
{
    set_field_window_shape_steps_byte(g, (unsigned)(cell - 0xF0), (uint8_t)v);
    set_field_window_shape_steps_byte(g, (unsigned)(cell - 0xF0 + 1), (uint8_t)(v >> 8));
}

static void leave_for_original(GameState *g, const Leftovers *l)
{
    const uint8_t ends[8] = { l->last.from.x, 0, l->last.from.y, 0, l->last.to.x, 0, l->last.to.y, 0 };
    for (unsigned i = 0; i < 8; i++)
        set_field_window_shape_edge_byte(g, i, ends[i]);
    leave_step_cell(g, 0xF0, l->fill_length);
    leave_step_cell(g, 0xF4, 0);
    leave_step_cell(g, 0xF6, l->right_side_found ? 0 : 0x0F35);
    set_field_window_shape_steps_byte(g, 0xFB - 0xF0, 0);
    if (l->sloped) {
        set_field_window_shape_steps_byte(g, 0xF2 - 0xF0, 0);
        leave_step_cell(g, 0xF8, l->width);
        set_field_window_shape_steps_byte(g, 0xFA - 0xF0, l->height);
        leave_step_cell(g, 0xFC, l->error);
    }
}

/* ---- The rasterizer ----------------------------------------------------------------------- */

/* The column the edges write: the table's address, a byte on for the right
 * side, in the table's bank. */
typedef struct {
    uint8_t bank;
    uint16_t address;
} Column;

// $C3:1001
/* A horizontal edge: its two ends' X on its line and the next, the smaller
 * first where the ends' difference is negative as a signed byte (a shape
 * wider than half the screen puts them the other way round). */
static void draw_level_edge(GameState *g, Column column, Edge e)
{
    uint32_t line = ((uint32_t)column.bank << 16 | column.address) + (uint16_t)(e.from.y * LINE_BYTES);
    bool from_first = (int8_t)(e.from.x - e.to.x) < 0;
    wram_write8(g, line, from_first ? e.from.x : e.to.x);
    wram_write8(g, line + LINE_BYTES, from_first ? e.to.x : e.from.x);
}

/* The error an edge's stepping starts from. An edge running left starts one
 * step behind the mirror of one running right, so the two sides of a
 * symmetric shape don't round alike: the cones lean a pixel. */
static int16_t edge_start_error(bool rightward, int16_t width, int16_t height)
{
    return rightward ? (int16_t)(width - height) : (int16_t)(height - width - 1);
}

// $C3:1025
/* A sloped edge: its X on each line from its upper end down, the X moving a
 * pixel at a time while the error lasts. */
static void draw_sloped_edge(GameState *g, Column column, Edge e, Leftovers *l)
{
    int16_t height = (int16_t)(e.to.y - e.from.y);
    int16_t dx = (int16_t)(e.to.x - e.from.x);
    bool rightward = dx >= 0;
    int16_t width = rightward ? dx : (int16_t)-dx;
    int16_t error = edge_start_error(rightward, width, height);
    int16_t x = e.from.x;
    uint16_t at = (uint16_t)(e.from.y * LINE_BYTES + column.address);
    for (int16_t line = 0; line < height; line++) {
        while (error >= 0) {
            x = (int16_t)(rightward ? x + 1 : x - 1);
            error = (int16_t)(error - height);
        }
        error = (int16_t)(error + width);
        wram_write8(g, (uint32_t)column.bank << 16 | at, (uint8_t)x);
        at = (uint16_t)(at + LINE_BYTES);
    }
    l->last = e;
    l->sloped = true;
    l->width = (uint16_t)width;
    l->height = (uint8_t)height;
    l->error = (uint16_t)error;
}

/* An edge, upper end first. */
static void draw_edge(GameState *g, Column column, Edge e, Leftovers *l)
{
    if (e.to.y == e.from.y) {
        draw_level_edge(g, column, e);
        l->last = e;
        return;
    }
    if (e.to.y < e.from.y)
        e = (Edge){ e.to, e.from };
    draw_sloped_edge(g, column, e, l);
}

/* The corner the drawing starts from: the topmost, the last of equals. */
static unsigned topmost_corner(const GameState *g)
{
    unsigned first = 0;
    for (unsigned c = 1; c < CORNERS; c++)
        if (corner(g, c).y <= corner(g, first).y)
            first = c;
    return first;
}

/* Copy the line at `from` down the table, `bytes` bytes a byte at a time, so
 * that it repeats line after line, in the bank the fill uses. */
static void repeat_line(GameState *g, uint8_t bank, uint16_t from, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; i++) {
        uint16_t at = (uint16_t)(from + i);
        wram_write8(g, (uint32_t)bank << 16 | (uint16_t)(at + LINE_BYTES), wram_read8(g, (uint32_t)bank << 16 | at));
    }
}

// $C3:0FA5
/* Mark the lines outside the shape empty: from the right side's start to the
 * last line, and from the first line to the top. The empty lines are copied
 * down from the first, in bank $7E or $7F by the table bank's low bit,
 * whatever the table's bank; and a shape whose edges never went up leaves the
 * table's pointer a byte back, where the next shape is drawn. Returns the
 * fill's length. */
static uint16_t empty_outside_lines(GameState *g, Column column)
{
    uint8_t bank = (column.bank & 1) ? FILL_BANK_ODD : FILL_BANK;
    uint16_t table = (uint16_t)(column.address - RIGHT_COLUMN);
    set_field_window_shape_table_address(g, table);
    uint16_t start = field_window_shape_right_start(g);
    uint16_t below = (uint16_t)((LAST_LINE - start) * LINE_BYTES - 1);
    uint16_t at = (uint16_t)(start * LINE_BYTES + table);
    uint32_t first = (uint32_t)column.bank << 16 | table;
    wram_write8(g, first, EMPTY_LEFT);
    wram_write8(g, first + 1, EMPTY_RIGHT);
    uint32_t line = ((uint32_t)bank << 16) + at;
    wram_write8(g, line, EMPTY_LEFT);
    wram_write8(g, line + 1, EMPTY_RIGHT);
    if (below == 0)
        return below;
    repeat_line(g, bank, at, (uint32_t)below + 1);
    uint16_t top = field_window_shape_top(g);
    if (top != 0)
        repeat_line(g, bank, table, (uint32_t)(uint16_t)(top * LINE_BYTES - 1) + 1);
    return below;
}

// $C3:0011
/* Rasterize the quadrilateral in field_window_shape_corners into the table. */
static void rasterize_quad(GameState *g)
{
    unsigned first = topmost_corner(g);
    set_field_window_shape_top(g, corner(g, first).y);
    Column column = { field_window_shape_table_bank(g), field_window_shape_table_address(g) };
    Leftovers l = { 0 };
    for (unsigned k = 0; k < EDGES; k++) {
        Edge e = { corner(g, (first + k) % CORNERS), corner(g, (first + k + 1) % CORNERS) };
        if (e.to.y < e.from.y && !l.right_side_found) {
            column.address = (uint16_t)(column.address + RIGHT_COLUMN);
            set_field_window_shape_right_start(g, e.from.y);
            l.right_side_found = true;
        }
        draw_edge(g, column, e, &l);
    }
    l.fill_length = empty_outside_lines(g, column);
    leave_for_original(g, &l);
}

/* The guard: the table where the game puts it, in bank $7F far enough from
 * the bank's end that neither the table nor the fill leaves it, and the right
 * side starting on a line. Where else a table can be, the fill (which can
 * repeat 64 KiB) runs over the stack the original's registers are saved on,
 * and the port writes the table but doesn't reproduce the registers that
 * would come back from it. The game points the table at
 * field_window_edge_lines or its copy. */
enum { TABLE_BANK = 0x7F, TABLE_END = 0xFC00, LINES = 0x100 };

bool field_window_shape_table_in_place(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = field_window_shape_table_bank(g) == TABLE_BANK && field_window_shape_table_address(g) < TABLE_END &&
              field_window_shape_right_start(g) < LINES;
    vars_peek(false);
    return ok;
}

/* Entered through the JMP at $C3:0011 with 16-bit X; it sets its own direct
 * page and restores the caller's, and nothing it leaves in the registers is
 * read after (the entry's don't-care set). */
void field_rasterize_quad(Snes *s)
{
    rasterize_quad(state_of(s));
    m_rtl(s);
}
