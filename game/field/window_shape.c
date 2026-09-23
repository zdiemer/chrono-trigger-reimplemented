/* Field: window shapes. A quadrilateral rasterized into a per-line table of
 * left and right X positions, which the field's window line effect reads (the
 * light cone in the Black Omen: the field copies the corners from
 * field_window_shape_quad and points the table at field_window_edge_lines or
 * its copy). The code lives in bank C3, beside the scene engine's circle, and
 * this port also stands in for the edge sequence it runs, $C3:0F35-$C3:1025.
 *
 * The table has two bytes a line, left X then right X. The four edges are
 * drawn in order round the quadrilateral from its topmost corner: every edge
 * going down writes the left column, and from the first edge going up (whose
 * lower end is where the right-hand side starts) the table is moved a byte on,
 * so the edges back up write the right column. An edge writes its X on each
 * line from its upper end down to the line before its lower end, stepping X
 * with an error term as Bresenham's line does; a horizontal edge writes its
 * two ends. Then the lines below the right-hand side's start, and those above
 * the top, are filled as empty (left 1, right 0).
 *
 * The rounding is the original's, quirks included: an edge running left
 * starts its error one step behind an edge running right, and the carries of
 * its sums follow the 65816's. The table is a buffer the caller locates (its
 * pointer is field_window_shape_table), so it is reached by address. */
#include "field/field.h"

#include "field/bridge.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

enum { CORNERS = 4, EDGES = 4 };
enum { LINE_BYTES = 2, RIGHT_COLUMN = 1 };
enum { EMPTY_LINE = 0x0001 };   /* left 1, right 0: a line the window doesn't cover */
enum { LAST_LINE = 0xFF };
enum { FILL_BANK = 0x7E, FILL_BANK_ODD = 0x7F };

/* An edge, from its start to its end, as the original keeps it: words, the X
 * and the Y of each end. */
typedef struct {
    uint16_t x0, y0, x1, y1;
} Edge;

static uint32_t table_pointer(const GameState *g)
{
    uint16_t address = field_window_shape_table_address(g);
    return (uint32_t)field_window_shape_table_bank(g) << 16 | address;
}

/* ---- For the original's code and shadow mode (field/scratch.h) -------------------------- */

/* The cells the original draws with: the edge's ends ($53-$5A), where it
 * entered the edge sequence and how many edges are left ($F0, $F4), whether
 * the right-hand side is still to come ($F6), and an edge's lines left,
 * width, height and error ($F2, $F8, $FA, $FC). */
static void leave_step_cell(GameState *g, uint8_t cell, uint16_t v)
{
    set_field_window_shape_steps_byte(g, (unsigned)(cell - 0xF0), (uint8_t)v);
    set_field_window_shape_steps_byte(g, (unsigned)(cell - 0xF0 + 1), (uint8_t)(v >> 8));
}

static void leave_edge(GameState *g, Edge e)
{
    const uint16_t words[4] = { e.x0, e.y0, e.x1, e.y1 };
    for (unsigned i = 0; i < 4; i++) {
        set_field_window_shape_edge_byte(g, 2 * i, (uint8_t)words[i]);
        set_field_window_shape_edge_byte(g, 2 * i + 1, (uint8_t)(words[i] >> 8));
    }
}

/* The edge sequence is entered at the topmost corner's block of code; the
 * first block's address marks the right-hand side as still to come. */
static void leave_entry(GameState *g, unsigned first)
{
    leave_step_cell(g, 0xF6, 0x0F35);
    leave_step_cell(g, 0xF4, EDGES);
    leave_step_cell(g, 0xF0, (uint16_t)(0x0F35 + 0x13 * first));
}

static void leave_height_high(GameState *g) { set_field_window_shape_steps_byte(g, 0xFB - 0xF0, 0); }

static void leave_slope(GameState *g, uint16_t width, uint8_t height, uint16_t error)
{
    set_field_window_shape_steps_byte(g, 0xFA - 0xF0, height);
    leave_step_cell(g, 0xF8, width);
    leave_step_cell(g, 0xFC, error);
    set_field_window_shape_steps_byte(g, 0xF2 - 0xF0, 0);
}

/* ---- The rasterizer ----------------------------------------------------------------------- */

// $C3:1001
/* A horizontal edge: its two ends' X on its line and the next, in the column
 * the table points at, the leftmost first when the ends' difference is
 * negative as a signed byte. */
static void draw_level_edge(GameState *g, Edge e)
{
    uint32_t line = table_pointer(g) + (uint16_t)(e.y0 * LINE_BYTES);
    bool start_first = (uint8_t)(e.x0 - e.x1) & 0x80;
    wram_write8(g, line, (uint8_t)(start_first ? e.x0 : e.x1));
    wram_write8(g, line + LINE_BYTES, (uint8_t)(start_first ? e.x1 : e.x0));
}

// $C3:1025
/* One edge: its X on each line from its upper end down, in the column the
 * table points at. Returns the edge as drawn, upper end first. */
static Edge draw_edge(GameState *g, Edge e)
{
    leave_height_high(g);
    uint8_t height = (uint8_t)(e.y1 - e.y0);
    if (height == 0) {
        draw_level_edge(g, e);
        return e;
    }
    if (e.y1 < e.y0) {
        e = (Edge){ e.x1, e.y1, e.x0, e.y0 };
        height = (uint8_t)(e.y1 - e.y0);
    }
    uint8_t bank = field_window_shape_table_bank(g);
    uint16_t at = (uint16_t)(e.y0 * LINE_BYTES + field_window_shape_table_address(g));
    uint16_t x = e.x0;
    uint16_t dx = (uint16_t)(e.x1 - e.x0);
    bool rightward = !(dx & 0x8000);
    uint16_t width = rightward ? dx : (uint16_t)-dx;
    uint16_t error = rightward ? (uint16_t)(width - height) : (uint16_t)(height - width - 1);
    for (uint8_t lines = height; lines != 0; lines--) {
        while (!(error & 0x8000)) {
            x = rightward ? (uint16_t)(x + 1) : (uint16_t)(x - 1);
            error = (uint16_t)(error - height);
        }
        error = (uint16_t)(error + width);
        wram_write8(g, (uint32_t)bank << 16 | at, (uint8_t)x);
        at = (uint16_t)(at + LINE_BYTES);
    }
    leave_slope(g, width, height, error);
    return e;
}

/* The corner the drawing starts from: the topmost, the last of equals. */
static unsigned topmost_corner(const GameState *g)
{
    unsigned first = 0;
    for (unsigned c = 1; c < CORNERS; c++)
        if (field_window_shape_corners_y(g, c) <= field_window_shape_corners_y(g, first))
            first = c;
    return first;
}

/* A block move within one bank, a byte at a time from the lowest address, so
 * that a move onto the bytes after its source repeats them; the addresses
 * wrap inside the bank. */
static void repeat_bytes(GameState *g, uint8_t bank, uint16_t from, uint16_t to, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        wram_write8(g, (uint32_t)bank << 16 | to, wram_read8(g, (uint32_t)bank << 16 | from));
        from = (uint16_t)(from + 1);
        to = (uint16_t)(to + 1);
    }
}

// $C3:0FA5
/* Mark the lines outside the shape empty: from the right-hand side's start to
 * the last line, and from the first line to the top, each by repeating an
 * empty line down the table (the table's bank's low bit picks bank $7E or $7F
 * for the repeat, whatever the pointer's bank). */
static void empty_outside_lines(GameState *g)
{
    uint8_t bank = (field_window_shape_table_bank(g) & 1) ? FILL_BANK_ODD : FILL_BANK;
    set_field_window_shape_table_address(g, (uint16_t)(field_window_shape_table_address(g) - RIGHT_COLUMN));
    uint16_t start = field_window_shape_right_start(g);
    uint16_t below = (uint16_t)((uint16_t)((uint16_t)(LAST_LINE - start) * LINE_BYTES) - 1);
    leave_step_cell(g, 0xF0, below);
    uint16_t at = (uint16_t)(start * LINE_BYTES + field_window_shape_table_address(g));
    uint32_t table = table_pointer(g);
    wram_write8(g, table, (uint8_t)EMPTY_LINE);
    wram_write8(g, table + 1, (uint8_t)(EMPTY_LINE >> 8));
    uint32_t line = ((uint32_t)bank << 16) + at;
    wram_write8(g, line, (uint8_t)EMPTY_LINE);
    wram_write8(g, line + 1, (uint8_t)(EMPTY_LINE >> 8));
    if (below == 0)
        return;
    repeat_bytes(g, bank, at, (uint16_t)(at + LINE_BYTES), (uint32_t)below + 1);
    uint16_t top = field_window_shape_top(g);
    if (top == 0)
        return;
    uint16_t first = field_window_shape_table_address(g);
    repeat_bytes(g, bank, first, (uint16_t)(first + LINE_BYTES), (uint32_t)(uint16_t)(top * LINE_BYTES - 1) + 1);
}

// $C3:0011
/* Rasterize the quadrilateral in field_window_shape_corners into the table. */
static void rasterize_quad(GameState *g)
{
    unsigned first = topmost_corner(g);
    set_field_window_shape_top(g, field_window_shape_corners_y(g, first));
    leave_entry(g, first);
    bool right_side_to_come = true;
    for (unsigned k = 0; k < EDGES; k++) {
        unsigned from = (first + k) % CORNERS, to = (from + 1) % CORNERS;
        Edge e = { field_window_shape_corners_x(g, from), field_window_shape_corners_y(g, from),
                   field_window_shape_corners_x(g, to), field_window_shape_corners_y(g, to) };
        if (e.y1 < e.y0 && right_side_to_come) {
            set_field_window_shape_table_address(g, (uint16_t)(field_window_shape_table_address(g) + RIGHT_COLUMN));
            set_field_window_shape_right_start(g, e.y0);
            right_side_to_come = false;
            leave_step_cell(g, 0xF6, 0);
        }
        leave_edge(g, draw_edge(g, e));
        leave_step_cell(g, 0xF4, (uint16_t)(EDGES - 1 - k));
    }
    empty_outside_lines(g);
}

/* The guard: the table where the game puts it, in bank $7F far enough from
 * the bank's end that neither the table nor the fill leaves it, and the
 * right-hand side starting on a line. The port writes the table as the
 * original does; what it doesn't reproduce is the rest of the original's
 * machine, its registers saved on the stack, which a table in bank $7E can
 * run over (the fill repeats up to 64 KiB). The game points the table at
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
