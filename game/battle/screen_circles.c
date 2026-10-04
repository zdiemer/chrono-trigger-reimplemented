/* Battle: the screen script's circles.
 *
 * A screen script can shape either window as a circle: a center, a radius
 * (battle_screen_circle_*) and a kind that says what shape the circle is
 * drawn as -- round, flattened below, hung from the top of the screen, tall,
 * wide or twice the size. Scripts place a circle, center it on an animation
 * position, widen and narrow it by the animation scripts' variables, and wait
 * for it to open fully or to close; and draw it, as the window of its own
 * number, into the coming frame's window table.
 *
 * Drawing a circle works out the half width of each of its rows from the
 * middle row outward, the way a midpoint circle does, the first eighth of the
 * turn a row at a time and the second a column at a time; lays each row's
 * edges, about the circle's column and cut off at the screen's sides, into a
 * column of worked rows above and below a middle (battle_circle_edges); and
 * copies the rows the circle covers into the window table about its line,
 * cut off at the top of the screen and at the bottom of the battlefield. A
 * script can also draw a square of window the same way (a box as wide as a
 * circle, laid about the first circle's center). */
#include "battle/screen_script.h"

#include "battle/bridge.h"
#include "battle/screen_lines.h"
#include "charge.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "ram.h"

/* ---- The scripts' variables the circles follow ------------------------------------------------- */

/* An animation script sets these for the screen script to size its circles
 * by. */
enum { NARROWING_STEP = 9, WIDENING_STEP = 12, FULL_RADIUS = 14, SET_RADIUS = 15 };

static uint8_t script_variable(const GameState *g, unsigned v) { return battle_script_variables(g, v); }

/* ---- Placing and sizing ---------------------------------------------------------------------- */

/* The lowest a circle's center goes: the battlefield's last lines. */
enum { CIRCLE_LOWEST = 0xD3 };

static bool names_a_circle(const GameState *g, const ScreenCommand *c)
{
    (void)g;
    return c->circle < SCREEN_CIRCLES;
}

// $CD:2FED
/* Place a circle, and say how it is drawn. */
static ScreenFlow place_circle(GameState *g, uint8_t circle, CircleKind kind, uint8_t x, uint8_t y, uint8_t radius)
{
    set_battle_screen_circle_kind(g, circle, (uint8_t)kind);
    set_battle_screen_circle_x(g, circle, x);
    set_battle_screen_circle_y(g, circle, y);
    set_battle_screen_circle_radius(g, circle, radius);
    return screen_on();
}
SCREEN_OPERATION(place_circle, circle, circle_kind, x, y, radius)

// $CD:1E36
static ScreenFlow set_circle_kind(GameState *g, uint8_t circle, CircleKind kind)
{
    set_battle_screen_circle_kind(g, circle, (uint8_t)kind);
    return screen_on();
}
SCREEN_OPERATION(set_circle_kind, circle, circle_kind)

// $CD:2FBC
/* Move a circle and change its radius; it goes no lower than the
 * battlefield's last lines. */
static ScreenFlow move_circle(GameState *g, uint8_t circle, uint8_t x, uint8_t y, uint8_t radius)
{
    set_battle_screen_circle_x(g, circle, (uint8_t)(battle_screen_circle_x(g, circle) + x));
    uint8_t lower = (uint8_t)(battle_screen_circle_y(g, circle) + y);
    set_battle_screen_circle_y(g, circle, lower >= CIRCLE_LOWEST ? CIRCLE_LOWEST : lower);
    set_battle_screen_circle_radius(g, circle, (uint8_t)(battle_screen_circle_radius(g, circle) + radius));
    return screen_on();
}
SCREEN_OPERATION(move_circle, circle, x, y, radius)

// $CD:24F3
/* Center a circle on an animation position. */
static ScreenFlow center_circle_on_position(GameState *g, uint8_t circle, uint8_t position)
{
    screen_keep_positions_on_the_battlefield(g);
    set_battle_screen_circle_x(g, circle, screen_position_across(g, position));
    set_battle_screen_circle_y(g, circle, screen_position_down(g, position));
    return screen_on();
}
SCREEN_OPERATION(center_circle_on_position, circle, position)

static bool names_a_position(const GameState *g, const ScreenCommand *c)
{
    (void)g;
    return screen_names_a_position(c->position);
}

// $CD:1A9C
/* Widen a circle by the widening step, up to the full radius. */
static ScreenFlow widen_circle(GameState *g, uint8_t circle)
{
    uint8_t wider = (uint8_t)(battle_screen_circle_radius(g, circle) + script_variable(g, WIDENING_STEP));
    uint8_t full = script_variable(g, FULL_RADIUS);
    set_battle_screen_circle_radius(g, circle, wider < full ? wider : full);
    return screen_on();
}
SCREEN_OPERATION(widen_circle, circle)

// $CD:1A8B
/* Narrow a circle by the narrowing step, down to nothing. */
static ScreenFlow narrow_circle(GameState *g, uint8_t circle)
{
    uint8_t radius = battle_screen_circle_radius(g, circle), step = script_variable(g, NARROWING_STEP);
    set_battle_screen_circle_radius(g, circle, radius >= step ? (uint8_t)(radius - step) : 0);
    return screen_on();
}
SCREEN_OPERATION(narrow_circle, circle)

// $CD:182A
static ScreenFlow set_circle_radius(GameState *g, uint8_t circle)
{
    set_battle_screen_circle_radius(g, circle, script_variable(g, SET_RADIUS));
    return screen_on();
}
SCREEN_OPERATION(set_circle_radius, circle)

// $CD:1A7A
/* Wait for a circle to close. */
static ScreenFlow wait_for_circle_closed(GameState *g, uint16_t at, uint8_t circle, uint8_t back)
{
    return screen_wait_back(g, battle_screen_circle_radius(g, circle) != 0, at, back);
}
SCREEN_OPERATION(wait_for_circle_closed, at, circle, back)

// $CD:1A66
/* Wait for a circle to open to the full radius. */
static ScreenFlow wait_for_circle_open(GameState *g, uint16_t at, uint8_t circle, uint8_t back)
{
    return screen_wait_back(g, battle_screen_circle_radius(g, circle) != script_variable(g, FULL_RADIUS), at, back);
}
SCREEN_OPERATION(wait_for_circle_open, at, circle, back)

/* ---- A circle's rows ------------------------------------------------------------------------- */

/* The worked rows: the middle's in the middle of battle_circle_edges, the
 * rows above it going up from just before it, the rows below going down from
 * it.
 *
 * A circle's rows are worked out for the low seven bits of its radius, so
 * that they fit, but the run copied into the window table is the whole
 * radius's. A circle the scripts grow past 127 is drawn as a small one, with
 * the rows earlier circles left above and below it; at 128 nothing is drawn. */
enum { MIDDLE_ROW = BATTLE_CIRCLE_EDGES_COUNT / 2, ROWS_RADIUS = 0x7F };

static uint8_t rows_radius(uint8_t radius) { return radius & ROWS_RADIUS; }

/* The half widths of a circle's rows over the first eighth of the turn, from
 * the middle row out, as the midpoint circle works them out: a row a step,
 * the error term (a byte, which wraps) taking the row's step off and, where
 * it would fall below zero, giving a column back. Returns how many rows. */
static unsigned first_eighth(uint8_t radius, uint8_t widths[MIDDLE_ROW])
{
    uint8_t error = radius, x = radius, y = 0;
    unsigned rows = 0;
    while (x >= y) {
        widths[rows++] = x;
        uint8_t step = (uint8_t)(2 * y + 1);
        bool below_zero = error < step;
        error = (uint8_t)(error - step);
        y++;
        if (below_zero) {
            x--;
            error = (uint8_t)(error + 2 * x);
        }
    }
    return rows;
}

/* The rows over the second eighth come a column at a time: a row for each
 * column the first eighth's widths step in, its half width the first
 * eighth's last less one, and one less for each step back toward the middle
 * row (whether or not a row comes of it). */
typedef struct {
    uint8_t half[MIDDLE_ROW + 1];
    unsigned rows;
    unsigned first_eighth_rows;   /* the rest are the second eighth's */
    uint8_t widths[MIDDLE_ROW + 1];
} CircleRows;

static CircleRows rows_of(uint8_t radius)
{
    CircleRows r = { .rows = 0 };
    unsigned n = first_eighth(radius, r.widths);
    r.widths[n] = 0;
    r.first_eighth_rows = n;
    for (unsigned y = 0; y < n; y++)
        r.half[r.rows++] = r.widths[y];
    uint8_t half = (uint8_t)(r.widths[n - 1] - 1);
    for (unsigned y = n - 1; y >= 1; y--, half--)
        if (r.widths[y] != r.widths[y - 1])
            r.half[r.rows++] = half;
    return r;
}

/* A row's edges about a column, cut off at the screen's sides. */
static Edges row_about(uint8_t column, uint8_t half)
{
    uint8_t left = column >= half ? (uint8_t)(column - half) : 0;
    uint8_t right = (unsigned)column + half > 0xFF ? 0xFF : (uint8_t)(column + half);
    return edges_between(left, right);
}

/* The kinds: a wide circle's rows are twice as wide, a flattened one's half
 * below the middle takes only every other row (and so is half as tall). */
static bool is_wide(CircleKind kind) { return kind == CIRCLE_WIDE || kind == CIRCLE_TWICE_THE_SIZE; }
static bool is_flattened_below(CircleKind kind) { return kind == CIRCLE_FLATTENED_BELOW || kind == CIRCLE_HUNG_FROM_THE_TOP; }

/* Lay a circle's rows out about the middle of the worked rows, every row
 * above it and (flattened below) every other row below; returns how many
 * rows went below. */
static unsigned lay_rows_out(GameState *g, const CircleRows *r, uint8_t column, CircleKind kind)
{
    unsigned below = 0;
    for (unsigned i = 0; i < r->rows; i++) {
        uint8_t half = is_wide(kind) ? (uint8_t)(r->half[i] * 2) : r->half[i];
        Edges e = row_about(column, half);
        set_battle_circle_edges(g, MIDDLE_ROW - 1 - i, e);
        if (!is_flattened_below(kind) || i % 2 == 0)
            set_battle_circle_edges(g, MIDDLE_ROW + below++, e);
    }
    return below;
}

/* ---- Into the window table -------------------------------------------------------------------- */

/* A circle is laid down to the battlefield's bottom, the line before $DE. */
enum { CIRCLES_END_LINE = 0xDE };

/* The run of worked rows a circle covers, and the line it starts on. */
typedef struct {
    int first, end;   /* worked rows: from first up to end */
    int top;
} RowRun;

/* A round or wide circle: the radius's rows each side of the middle, from
 * its line less its radius; cut off at the top of the screen, and where it
 * would reach the battlefield's bottom. */
static RowRun round_run(uint8_t radius, uint8_t line)
{
    RowRun run = { MIDDLE_ROW - radius, MIDDLE_ROW + radius, line - radius };
    if (run.top < 0) {
        run.first -= run.top;
        run.top = 0;
    }
    if (2 * radius + run.top >= CIRCLES_END_LINE)
        run.end -= 2 * radius + run.top - CIRCLES_END_LINE;
    return run;
}

/* A circle flattened below: the radius's rows above the middle and half as
 * many below. */
static RowRun flattened_run(uint8_t radius, uint8_t line)
{
    RowRun run = { MIDDLE_ROW - radius, MIDDLE_ROW + radius / 2, line - radius };
    if (run.top < 0) {
        run.first -= run.top;
        run.top = 0;
    }
    int bottom = radius / 2 + radius + run.top;
    if (bottom >= CIRCLES_END_LINE)
        run.end -= bottom - CIRCLES_END_LINE;
    return run;
}

/* A tall circle (or one twice the size): each row two lines, from its line
 * less twice its radius; cut off at the top a row for every two lines above
 * it, and at the bottom a row for every two lines its unclipped bottom passes
 * the battlefield's (the original measures the bottom from the circle's line,
 * not from where the top was cut). */
static RowRun tall_run(uint8_t radius, uint8_t line)
{
    RowRun run = { MIDDLE_ROW - radius, MIDDLE_ROW + radius, line - 2 * radius };
    if (run.top < 0) {
        run.first += -run.top / 2;
        run.top = 0;
    }
    if (2 * radius + line >= CIRCLES_END_LINE)
        run.end -= (2 * radius + line - CIRCLES_END_LINE) / 2;
    return run;
}

static bool is_tall(CircleKind kind) { return kind == CIRCLE_TALL || kind == CIRCLE_TWICE_THE_SIZE; }

static RowRun run_of(CircleKind kind, uint8_t radius, uint8_t line)
{
    if (is_tall(kind))
        return tall_run(radius, line);
    return is_flattened_below(kind) ? flattened_run(radius, line) : round_run(radius, line);
}

static unsigned lines_of(CircleKind kind, RowRun run) { return (unsigned)(run.end - run.first) * (is_tall(kind) ? 2 : 1); }

// $CE:E7C8
/* A run of worked rows, from `first` on, into the window table from line
 * `line` of window `w` on, `rows` of them. */
static void copy_circle_lines(GameState *g, unsigned first, TableLine line, Window w, unsigned rows)
{
    for (unsigned i = 0; i < rows; i++)
        set_line_edges(g, line + (TableLine)i, w, battle_circle_edges(g, first + i));
}

/* A hung circle's window goes on up from its middle line to the top of the
 * screen, as wide as its middle row below. */
static void fill_above(GameState *g, Window w, uint8_t line)
{
    Edges middle = battle_circle_edges(g, MIDDLE_ROW);
    for (unsigned above = 0; above < line; above++)
        set_line_edges(g, coming_line(g, above), w, middle);
}

static void lay_run_into_the_table(GameState *g, Window w, CircleKind kind, RowRun run, uint8_t line)
{
    if (!is_tall(kind)) {
        TIMED_AS(g, 0xCEE7C8,
                 copy_circle_lines(g, (unsigned)run.first, coming_line(g, (unsigned)run.top), w, (unsigned)(run.end - run.first)));
    } else {
        for (int row = run.first, at = run.top; row < run.end; row++, at += 2) {
            Edges e = battle_circle_edges(g, (unsigned)row);
            set_line_edges(g, coming_line(g, (unsigned)at), w, e);
            set_line_edges(g, coming_line(g, (unsigned)at + 1), w, e);
        }
    }
    if (kind == CIRCLE_HUNG_FROM_THE_TOP)
        fill_above(g, w, line);
}

/* ---- What the original leaves ------------------------------------------------------------------- */

/* The original keeps the circle it draws in battle_circle_drawn, works the
 * first eighth's half widths out into a list (ended by a 0), and the rows in
 * the battle page's cells $45-$52, and leaves them there; it writes the rows
 * below the middle through the work RAM port. */
static void leave_rows(GameState *g, const CircleRows *r, uint8_t column, CircleKind kind, unsigned below)
{
    for (unsigned y = 0; y <= r->first_eighth_rows; y++)
        set_battle_circle_widths(g, y, r->widths[y]);
    screen_leave_cell(g, 0x4D, column);
    screen_leave_cell(g, 0x51, is_wide(kind) ? (uint8_t)(r->half[r->rows - 1] * 2) : (uint8_t)r->first_eighth_rows);
    hw_port_seek(g, (uint16_t)(RAM_BATTLE_CIRCLE_EDGES + (MIDDLE_ROW + below) * sizeof(Edges)), RAM_BATTLE_CIRCLE_EDGES >> 16);
}

/* Where the original's copy jumps into its unrolled run: six bytes a row
 * back from its end. */
enum { UNROLLED_COPY_END = 0xE7C7, UNROLLED_LINE_BYTES = 6, UNROLLED_LINES = 222 };

static int unclipped_top(CircleKind kind, uint8_t radius, uint8_t line)
{
    return is_tall(kind) ? line - 2 * radius : line - radius;
}

/* ... and of laying the run into the table: the run's first row and its top
 * line, the circle's line, how far the top was cut where it was (in rows),
 * and where the copy started or the run ended. */
static void leave_run(GameState *g, CircleKind kind, Window w, RowRun run, uint8_t radius, uint8_t line)
{
    int top = unclipped_top(kind, radius, line);
    unsigned rows = (unsigned)(run.end - run.first);
    uint16_t window_offset = (uint16_t)(w * sizeof(Edges));
    screen_leave_word(g, 0x47, line);
    screen_leave_word(g, 0x49, (uint16_t)run.first);
    screen_leave_word(g, 0x4F, (uint16_t)run.top);
    if (top < 0)
        screen_leave_word(g, 0x51, (uint16_t)(is_tall(kind) ? -top / 2 : -top));
    bool copied = !is_tall(kind) && (rows || !is_flattened_below(kind));
    screen_leave_word(g, 0x45, copied ? (uint16_t)(run.first * sizeof(Edges)) : window_offset);
    screen_leave_word(g, 0x4B, copied ? (uint16_t)(UNROLLED_COPY_END - UNROLLED_LINE_BYTES * rows)
                                      : (uint16_t)(run.end * sizeof(Edges)));
    if (kind == CIRCLE_HUNG_FROM_THE_TOP)
        screen_leave_word(g, 0x45, (uint16_t)(line * LINE_BYTES + window_offset));
}

/* ---- Drawing --------------------------------------------------------------------------------- */

/* Whether a run of lines from `top` stays in the coming frame's buffer of
 * the window table (on an odd frame the second, which ends the table). */
static bool lines_fit(const GameState *g, int top, unsigned lines)
{
    return top + (int)lines <= (odd_frame(g) ? SCREEN_LINES : TABLE_LINES);
}

/* Whether the circle's run is one the original copies as a run: rows of the
 * worked ones to copy (a tall one copies at least one, a hung one fills at
 * least its middle line), within its unrolled copy and the window table. */
static bool run_fits(const GameState *g, CircleKind kind, RowRun run, uint8_t line)
{
    if (run.first < 0 || run.end > BATTLE_CIRCLE_EDGES_COUNT)
        return false;
    if (run.end < run.first || (is_tall(kind) && run.end == run.first))
        return false;
    if (!is_tall(kind) && run.end - run.first > UNROLLED_LINES)
        return false;
    if (kind == CIRCLE_HUNG_FROM_THE_TOP && (line == 0 || !lines_fit(g, 0, line)))
        return false;
    return lines_fit(g, run.top, lines_of(kind, run));
}

/* The original works each row out at length -- the midpoint's step, the
 * row's edges cut off at the screen's sides, its two stores and the port's --
 * where the port works it out in locals: what it charges is that, at the
 * drawing's own rate (charge.h). */
enum { DRAW_CIRCLE = 0xCD2FB7, ACCESSES_A_ROW = 32 };

// $CD:2FB7, $CD:3189-$CD:3656
/* Draw a circle into the coming frame's window table, as the window of its
 * own number. A circle of no radius draws nothing. */
static ScreenFlow draw_circle(GameState *g, uint8_t circle)
{
    uint8_t radius = battle_screen_circle_radius(g, circle);
    ORIGINAL_EFFECT(set_battle_circle_drawn(g, circle));
    if (!rows_radius(radius))
        return screen_on();
    CircleKind kind = (CircleKind)battle_screen_circle_kind(g, circle);
    uint8_t column = battle_screen_circle_x(g, circle), line = battle_screen_circle_y(g, circle);
    Window w = circle ? WINDOW_2 : WINDOW_1;
    CircleRows rows = rows_of(rows_radius(radius));
    unsigned below = lay_rows_out(g, &rows, column, kind);
    ORIGINAL_EFFECT(charge_as(g, DRAW_CIRCLE, ACCESSES_A_ROW * rows.rows));
    RowRun run = run_of(kind, radius, line);
    lay_run_into_the_table(g, w, kind, run, line);
    ORIGINAL_EFFECT(leave_rows(g, &rows, column, kind, below));
    ORIGINAL_EFFECT(leave_run(g, kind, w, run, radius, line));
    return screen_on();
}
SCREEN_OPERATION(draw_circle, circle)

/* A circle is drawn as C where it is one of the two, of one of the kinds,
 * and its run is one the original copies. */
static bool circle_can_be_drawn(const GameState *g, const ScreenCommand *c)
{
    if (!names_a_circle(g, c))
        return false;
    uint8_t radius = battle_screen_circle_radius(g, c->circle), kind = battle_screen_circle_kind(g, c->circle);
    if (!rows_radius(radius))
        return true;
    if (kind >= CIRCLE_KINDS)
        return false;
    uint8_t line = battle_screen_circle_y(g, c->circle);
    return run_fits(g, (CircleKind)kind, run_of((CircleKind)kind, radius, line), line);
}

/* ---- A square ------------------------------------------------------------------------------------ */

/* A square's half: its rows each side of the middle, as many as a circle's
 * radius, fit the worked rows up to 128. */
enum { LARGEST_SQUARE = MIDDLE_ROW };

/* An empty square still leaves its row's edges and column in its cells. */
static void leave_no_square(GameState *g, Edges row, uint8_t column)
{
    screen_leave_word(g, 0x45, row);
    screen_leave_cell(g, 0x49, column);
}

// $CD:1F53
/* Draw a square into the coming frame's window table: as wide as a circle
 * (its column less and plus its radius) and as tall, its rows laid about the
 * middle of the worked rows like a circle's; but laid into window 1 about the
 * first circle's line, as tall as the first circle's radius, as the first
 * circle is laid. */
static ScreenFlow draw_square(GameState *g, uint8_t circle)
{
    uint8_t half = battle_screen_circle_radius(g, circle), column = battle_screen_circle_x(g, circle);
    Edges row = row_about(column, half);
    if (!half) {
        ORIGINAL_EFFECT(leave_no_square(g, row, column));
        return screen_on();
    }
    for (unsigned i = 0; i < half; i++) {
        set_battle_circle_edges(g, MIDDLE_ROW + i, row);
        set_battle_circle_edges(g, MIDDLE_ROW - 1 - i, row);
    }
    uint8_t radius = battle_screen_circle_radius(g, 0), line = battle_screen_circle_y(g, 0);
    RowRun run = round_run(radius, line);
    lay_run_into_the_table(g, WINDOW_1, CIRCLE_ROUND, run, line);
    ORIGINAL_EFFECT(set_battle_circle_drawn(g, 0));
    ORIGINAL_EFFECT(leave_run(g, CIRCLE_ROUND, WINDOW_1, run, radius, line));
    return screen_on();
}
SCREEN_OPERATION(draw_square, circle)

static bool square_can_be_drawn(const GameState *g, const ScreenCommand *c)
{
    if (!names_a_circle(g, c))
        return false;
    uint8_t half = battle_screen_circle_radius(g, c->circle);
    if (!half)
        return true;
    uint8_t radius = battle_screen_circle_radius(g, 0), line = battle_screen_circle_y(g, 0);
    return half <= LARGEST_SQUARE && run_fits(g, CIRCLE_ROUND, round_run(radius, line), line);
}

SCREEN_OPCODES(screen_circle_opcodes,
    { 0x8A, OPERATION(place_circle), .length = 4, .circle = LOW_BIT(0), .circle_kind = HIGH_NIBBLE(0), .x = BYTE(1), .y = BYTE(2), .radius = BYTE(3) },
    { 0xBD, OPERATION(set_circle_kind), .length = 1, .circle = LOW_BIT(0), .circle_kind = HIGH_NIBBLE(0) },
    { 0x89, OPERATION(move_circle), .needs = names_a_circle, .length = 4, .circle = BYTE(0), .x = BYTE(1), .y = BYTE(2), .radius = BYTE(3) },
    { 0xAB, OPERATION(center_circle_on_position), .needs = names_a_position, .length = 1, .circle = TOP_BIT(0), .position = LOW_SEVEN(0) },
    { 0xD3, OPERATION(widen_circle), .needs = names_a_circle, .length = 1, .circle = BYTE(0) },
    { 0xD4, OPERATION(narrow_circle), .needs = names_a_circle, .length = 1, .circle = BYTE(0) },
    { 0xF0, OPERATION(set_circle_radius), .needs = names_a_circle, .length = 1, .circle = BYTE(0) },
    { 0xD5, OPERATION(wait_for_circle_closed), .needs = names_a_circle, .length = 2, .circle = BYTE(0), .back = BYTE(1) },
    { 0xD6, OPERATION(wait_for_circle_open), .needs = names_a_circle, .length = 2, .circle = BYTE(0), .back = BYTE(1) },
    { 0x8B, OPERATION(draw_circle), .needs = circle_can_be_drawn, .length = 1, .circle = BYTE(0) },
    { 0xB5, OPERATION(draw_square), .needs = square_can_be_drawn, .length = 1, .circle = BYTE(0) },
    { 0xB6, OPERATION(draw_square), .needs = square_can_be_drawn, .length = 1, .circle = BYTE(0) },
);

/* ---- The entries --------------------------------------------------------------------------------- */

/* $CE:E7C8, entered with 16-bit registers as the circles' drawing ($CD:3616)
 * calls it: X the place in the window table of the run's first line and
 * window, Y the place of its first worked row, and the cell $4B where in the
 * original's unrolled copy to start, which counts the lines back from its
 * end. */
typedef struct {
    unsigned first, rows;
    TableLine line;
    Window w;
} CopiedRun;

static bool copied_run(const Snes *s, CopiedRun *r)
{
    const GameState *g = state_of((Snes *)s);
    uint16_t start = screen_cell_word(g, 0x4B), to = s->cpu.x, from = s->cpu.y;
    unsigned table_byte = (uint16_t)(to - RAM_BATTLE_WINDOW_LINES), worked_byte = (uint16_t)(from - RAM_BATTLE_CIRCLE_EDGES);
    unsigned back = (uint16_t)(UNROLLED_COPY_END - start);
    if (back % UNROLLED_LINE_BYTES || back / UNROLLED_LINE_BYTES > UNROLLED_LINES || table_byte % 2 || worked_byte % 2)
        return false;
    *r = (CopiedRun){ worked_byte / 2, back / UNROLLED_LINE_BYTES, (TableLine)(table_byte / LINE_BYTES),
                      table_byte % LINE_BYTES ? WINDOW_2 : WINDOW_1 };
    return r->line + r->rows <= TABLE_LINES && r->first + r->rows <= BATTLE_CIRCLE_EDGES_COUNT;
}

bool battle_circle_run_fits(Snes *s)
{
    CopiedRun r = { 0, 0, 0, WINDOW_1 };
    vars_peek(true);
    bool fits = copied_run(s, &r);
    vars_peek(false);
    return m_direct_page(s) == BATTLE_PAGE && s->cpu.db == BATTLE_DATA_BANK && fits;
}

void battle_copy_circle_lines(Snes *s)
{
    CopiedRun r = { 0, 0, 0, WINDOW_1 };
    copied_run(s, &r);
    copy_circle_lines(state_of(s), r.first, r.line, r.w, r.rows);
    m_rtl(s);
}
