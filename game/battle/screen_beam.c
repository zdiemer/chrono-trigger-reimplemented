/* Battle: the window quadrilaterals, and the beam drawn with one.
 *
 * The battle has two window quadrilaterals (battle_window_quads), each four
 * corners, which the screen scripts shape: as a beam that falls from the top
 * of the screen onto a combatant (here), or as a band between two animation
 * positions (screen_between.c). One is drawn into the coming frame's window
 * table, as the window of its own number, by sorting its corners from the top
 * line down and following its left and right sides down the lines between
 * the top corner and the bottom one, each side a straight edge from corner to
 * corner, its column moving by a fixed amount each line. */
#include "battle/screen_script.h"

#include "battle/bridge.h"
#include "battle/screen_lines.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "port/native.h"

/* ---- The quadrilaterals -------------------------------------------------------------------- */

typedef BattlePoint Corner;

enum { CORNERS = 4, QUADS = SCREEN_WINDOW_QUADS };

/* The two quadrilaterals' bytes are interleaved: each coordinate of the
 * first, then the same of the second. */
static unsigned coordinate(unsigned quad, unsigned corner, bool down) { return (2 * corner + down) * QUADS + quad; }

static Corner quad_corner(const GameState *g, unsigned quad, unsigned corner)
{
    return (Corner){ battle_window_quads(g, coordinate(quad, corner, false)),
                     battle_window_quads(g, coordinate(quad, corner, true)) };
}

void screen_set_quad_corner(GameState *g, unsigned quad, unsigned corner, BattlePoint c)
{
    set_battle_window_quads(g, coordinate(quad, corner, false), c.x);
    set_battle_window_quads(g, coordinate(quad, corner, true), c.y);
}

static void swap_corners(Corner *a, Corner *b)
{
    Corner t = *a;
    *a = *b;
    *b = t;
}

// $D1:F8EB
/* Put the corners in order down the screen: a corner moves past the next
 * where it isn't above it. The original's sort also swaps corners on the
 * same line, which decides which of them the rasterizer takes first. */
static void sort_corners_by_line(Corner c[CORNERS])
{
    for (unsigned pass = 0; pass < CORNERS; pass++)
        for (unsigned i = 0; i + 1 < CORNERS; i++)
            if (c[i].y >= c[i + 1].y)
                swap_corners(&c[i], &c[i + 1]);
}

/* An edge's slope: the columns it moves a line, 8.8 fixed point, leftward
 * negative. */
typedef uint16_t Slope;
enum { SLOPE_FRACTION_BITS = 8, EDGES = 4 };

/* An edge along one line has no slope: the division by no lines gives the
 * divider's answer for nothing, $FFFF, and the rasterizer is handed that. */
enum { DIVIDED_BY_NOTHING = 0xFFFF };

// $D1:F972
static Slope slope_across(uint8_t from_x, uint8_t to_x, uint8_t lines)
{
    bool leftward = to_x < from_x;
    uint8_t columns = leftward ? (uint8_t)(from_x - to_x) : (uint8_t)(to_x - from_x);
    uint16_t per_line = lines ? (uint16_t)((columns << SLOPE_FRACTION_BITS) / lines) : (uint16_t)DIVIDED_BY_NOTHING;
    return leftward ? (Slope)-per_line : per_line;
}

static uint8_t lines_between(Corner from, Corner to) { return (uint8_t)(to.y - from.y); }

static Slope edge_slope(Corner from, Corner to) { return slope_across(from.x, to.x, lines_between(from, to)); }

/* The four edges: down the left side from the top corner to the third and
 * from the third to the bottom one, and down the right side from the top
 * corner to the second and from the second to the bottom one. */
typedef enum { TOP_TO_THIRD, THIRD_TO_BOTTOM, TOP_TO_SECOND, SECOND_TO_BOTTOM } QuadEdge;

static const struct {
    uint8_t from, to;
} edges[EDGES] = { { 0, 2 }, { 2, 3 }, { 0, 1 }, { 1, 3 } };

// $D1:F91D
static void edge_slopes(const Corner c[CORNERS], Slope slopes[EDGES])
{
    for (unsigned e = 0; e < EDGES; e++)
        slopes[e] = edge_slope(c[edges[e].from], c[edges[e].to]);
}

/* ---- Rasterizing ------------------------------------------------------------------------------ */

/* A stretch of lines down which each side follows one edge. */
typedef struct {
    uint8_t lines;
    QuadEdge left, right;
} Stretch;

enum { MAX_STRETCHES = 3 };

typedef struct {
    Corner c[CORNERS];   /* as the rasterizer orders them */
    uint8_t left_from, right_from;
    Stretch stretch[MAX_STRETCHES];
    unsigned stretches;
} QuadSides;

/* The sides of corners sorted by line. A quadrilateral with a flat top is
 * drawn down to its third corner's line only, from its top corners taken
 * left to right, with its bottom ones left to right too; the bottom is
 * assumed flat as well (the beam's is). Otherwise both sides start at the top
 * corner, the left one turning at the left one of the middle corners and the
 * right one at the right one. */
static QuadSides sides_of(const Corner sorted[CORNERS])
{
    QuadSides q = { .stretches = 0 };
    for (unsigned i = 0; i < CORNERS; i++)
        q.c[i] = sorted[i];
    Corner *c = q.c;
    if (c[0].y == c[1].y) {
        if (c[0].x >= c[1].x)
            swap_corners(&c[0], &c[1]);
        if (c[2].x >= c[3].x)
            swap_corners(&c[2], &c[3]);
        q.left_from = c[0].x;
        q.right_from = c[1].x;
        q.stretch[q.stretches++] = (Stretch){ lines_between(c[1], c[2]), TOP_TO_THIRD, SECOND_TO_BOTTOM };
        return q;
    }
    if (c[2].x >= c[1].x)
        swap_corners(&c[1], &c[2]);
    q.left_from = q.right_from = c[0].x;
    if (c[2].y <= c[1].y) {
        q.stretch[q.stretches++] = (Stretch){ lines_between(c[0], c[2]), TOP_TO_THIRD, TOP_TO_SECOND };
        q.stretch[q.stretches++] = (Stretch){ lines_between(c[2], c[1]), THIRD_TO_BOTTOM, TOP_TO_SECOND };
        q.stretch[q.stretches++] = (Stretch){ lines_between(c[1], c[3]), THIRD_TO_BOTTOM, SECOND_TO_BOTTOM };
    } else {
        q.stretch[q.stretches++] = (Stretch){ lines_between(c[0], c[1]), TOP_TO_THIRD, TOP_TO_SECOND };
        q.stretch[q.stretches++] = (Stretch){ lines_between(c[1], c[2]), TOP_TO_THIRD, SECOND_TO_BOTTOM };
        q.stretch[q.stretches++] = (Stretch){ lines_between(c[2], c[3]), THIRD_TO_BOTTOM, SECOND_TO_BOTTOM };
    }
    return q;
}

/* A side's column, 8.8 fixed point. */
typedef uint16_t Column;

/* The two sides go down a stretch as one running sum, the left side's column
 * below the right side's: what a side's step carries past its whole columns
 * goes into the next one stepped, the right side's on the same line and the
 * left side's on the line after, a 256th of a column (a stretch starts with
 * nothing carried). Where that 256th brings a fraction round, the side lands
 * a column further right on that line and the ones after it than its slope
 * alone would put it: on screen, a side of the quadrilateral that should run
 * straight steps out by a column partway down. */
typedef struct {
    Column left, right;
    bool carried;
} Sides;

static Column step(Column at, Slope slope, bool *carried)
{
    uint32_t sum = (uint32_t)at + slope + *carried;
    *carried = sum > 0xFFFF;
    return (Column)sum;
}

static void step_down_a_line(Sides *s, Slope left, Slope right)
{
    s->left = step(s->left, left, &s->carried);
    s->right = step(s->right, right, &s->carried);
}

static uint8_t whole_column(Column c) { return (uint8_t)(c >> SLOPE_FRACTION_BITS); }

/* What the original leaves of the drawing: the corners in their order, the
 * slopes and the top line in its cells, and of the work RAM port it draws
 * through, where it stops and what its word writes leave on port 3. */
enum { CORNER_CELLS = 0x12, SLOPE_CELLS = 0x03, TOP_LINE_CELL = 0x0F, EDGE_CELLS = 0x0B };
enum { WINDOW_TABLE_BANK = 0x7E, WINDOW_TABLE = 0xC161 };

static void leave_corners(GameState *g, const Corner c[CORNERS])
{
    for (unsigned i = 0; i < CORNERS; i++) {
        set_battle_low_scratch(g, CORNER_CELLS + 2 * i, c[i].x);
        set_battle_low_scratch(g, CORNER_CELLS + 2 * i + 1, c[i].y);
    }
}

static void leave_slopes(GameState *g, const Corner c[CORNERS], const Slope slopes[EDGES])
{
    for (unsigned e = 0; e < EDGES; e++) {
        set_battle_low_scratch(g, SLOPE_CELLS + 2 * e, (uint8_t)slopes[e]);
        set_battle_low_scratch(g, SLOPE_CELLS + 2 * e + 1, (uint8_t)(slopes[e] >> 8));
    }
    set_battle_low_scratch(g, TOP_LINE_CELL, c[0].y);
}

/* Working out a slope leaves the columns over the lines in the divider; the
 * last edge's ends and lines stay in the cells. */
static void leave_last_edge(GameState *g, Corner from, Corner to)
{
    uint8_t columns = to.x < from.x ? (uint8_t)(from.x - to.x) : (uint8_t)(to.x - from.x);
    set_battle_low_scratch(g, EDGE_CELLS, to.x);
    set_battle_low_scratch(g, EDGE_CELLS + 1, from.x);
    set_battle_low_scratch(g, EDGE_CELLS + 2, lines_between(from, to));
    hw_set_dividend(g, (uint16_t)(columns << SLOPE_FRACTION_BITS));
    hw_divide_by(g, lines_between(from, to));
}

static void leave_drawing(GameState *g, Window w, const QuadSides *q, const Slope slopes[EDGES], unsigned lines,
                          Sides last)
{
    uint8_t top = q->c[0].y;
    leave_corners(g, q->c);
    leave_slopes(g, q->c, slopes);
    leave_last_edge(g, q->c[edges[EDGES - 1].from], q->c[edges[EDGES - 1].to]);
    screen_leave_word(g, 0x45, 0);
    screen_leave_word(g, 0x47, q->stretches == 1 ? (uint16_t)(odd_frame(g) * 2) : 0);
    if (q->stretches > 1)
        screen_leave_word(g, 0x49, 0);
    uint16_t buffer = (uint16_t)(WINDOW_TABLE + (odd_frame(g) ? SCREEN_LINES * LINE_BYTES : 0) + w * 2);
    hw_port_seek(g, (uint16_t)(buffer + (top + lines) * LINE_BYTES), WINDOW_TABLE_BANK);
    if (lines)
        hw_port_leave_spill(g, (uint8_t)last.right);
}

// $D1:F9E7
/* Draw corners sorted by line into the coming frame's window table, as
 * window `w`: each line from the top corner's down to the bottom's (or the
 * third's) gets the two sides' whole columns as its edges. */
static void rasterize(GameState *g, Window w, const Corner sorted[CORNERS])
{
    QuadSides q = sides_of(sorted);
    Slope slopes[EDGES];
    edge_slopes(q.c, slopes);
    Sides s = { (Column)(q.left_from << SLOPE_FRACTION_BITS), (Column)(q.right_from << SLOPE_FRACTION_BITS), false };
    unsigned line = q.c[0].y;
    for (unsigned i = 0; i < q.stretches; i++) {
        s.carried = false;
        for (unsigned n = 0; n < q.stretch[i].lines; n++, line++) {
            step_down_a_line(&s, slopes[q.stretch[i].left], slopes[q.stretch[i].right]);
            set_line_edges(g, coming_line(g, line), w, edges_between(whole_column(s.left), whole_column(s.right)));
        }
    }
    ORIGINAL_EFFECT(leave_drawing(g, w, &q, slopes, line - q.c[0].y, s));
}

bool screen_quad_fits_the_table(const GameState *g, const BattlePoint c[CORNERS])
{
    uint8_t lowest = 0;
    for (unsigned i = 0; i < CORNERS; i++)
        if (c[i].y > lowest)
            lowest = c[i].y;
    return !odd_frame(g) || lowest <= SCREEN_LINES;
}

// $D1:F9AF
/* Draw a window quadrilateral into the coming frame's window table, as the
 * window of its own number. */
void screen_draw_window_quad(GameState *g, unsigned quad)
{
    Corner c[CORNERS];
    for (unsigned i = 0; i < CORNERS; i++)
        c[i] = quad_corner(g, quad, i);
    sort_corners_by_line(c);
    ORIGINAL_EFFECT(set_battle_window_quad_index(g, (uint8_t)quad));
    rasterize(g, quad ? WINDOW_2 : WINDOW_1, c);
}

/* ---- The beam --------------------------------------------------------------------------------- */

/* The beam's top, 32 columns wide, sways about column $C0 by up to 40 columns
 * (40 times the sine of the frame count); its foot is 32 wide about the
 * position. */
enum { BEAM_WIDTH = 0x20, BEAM_TOP_LEFT = 0xB0, BEAM_SWAY = 0x28 };

/* The beam's foot is the first animation position's line. */
static uint8_t beam_foot(const GameState *g) { return (uint8_t)(battle_animation_positions(g, 0) >> 8); }

// $D1:EBFF, $CD:1805
/* Screen command $F4: shape window quadrilateral `quad` as a beam from the top
 * of the screen down to the first animation position, and draw it. */
static void draw_beam(GameState *g, uint8_t quad)
{
    uint8_t frame = (uint8_t)battle_frame_count(g), sway = screen_scaled_sine(g, frame, BEAM_SWAY);
    ORIGINAL_EFFECT(hw_set_multiplicand(g, BEAM_SWAY));
    ORIGINAL_EFFECT(hw_multiply_by(g, battle_sine_magnitude(g, frame)));
    uint8_t top_left = (uint8_t)(BEAM_TOP_LEFT + sway);
    uint8_t foot_left = (uint8_t)((uint8_t)battle_animation_positions(g, 0) - BEAM_WIDTH / 2), foot = beam_foot(g);
    screen_set_quad_corner(g, quad, 0, (Corner){ top_left, 0 });
    screen_set_quad_corner(g, quad, 1, (Corner){ (uint8_t)(top_left + BEAM_WIDTH), 0 });
    screen_set_quad_corner(g, quad, 2, (Corner){ foot_left, foot });
    screen_set_quad_corner(g, quad, 3, (Corner){ (uint8_t)(foot_left + BEAM_WIDTH), foot });
    screen_draw_window_quad(g, quad);
}

static bool beam_fits(const GameState *g) { return !odd_frame(g) || beam_foot(g) <= SCREEN_LINES; }

static ScreenFlow beam(GameState *g, uint8_t quad)
{
    draw_beam(g, quad);
    return screen_on();
}
SCREEN_OPERATION(beam, quad)

static bool names_a_quad_that_fits(const GameState *g, const ScreenCommand *c)
{
    return c->quad < QUADS && beam_fits(g);
}

SCREEN_OPCODES(screen_beam_opcodes,
    { 0xF4, OPERATION(beam), .needs = names_a_quad_that_fits, .length = 1, .quad = BYTE(0) },
);

/* ---- The entries ------------------------------------------------------------------------ */

/* The rasterizer's own entries find the corners in the battle page's low
 * cells, as its callers hand them over (CORNER_CELLS), and the edges' ends and
 * lines at EDGE_CELLS. */
static void corners_in_cells(const GameState *g, Corner c[CORNERS])
{
    for (unsigned i = 0; i < CORNERS; i++)
        c[i] = (Corner){ battle_low_scratch(g, CORNER_CELLS + 2 * i), battle_low_scratch(g, CORNER_CELLS + 2 * i + 1) };
}

/* The guards: the rasterizer runs with the data bank on the I/O registers it
 * draws through, and A's high byte clear; the quadrilateral drawn is one of
 * the two; the corners it is handed are sorted by line, and on an odd frame
 * the bottom one is within the screen, so that the lines it draws stay in the
 * table. */
bool battle_quad_page(Snes *s) { return m_direct_page(s) == BATTLE_PAGE && m_io_bank(s->cpu.db) && (s->cpu.a & 0xFF00) == 0; }

bool battle_quad_is_one(Snes *s)
{
    if (!battle_screen_page(s) || s->cpu.x >= QUADS)
        return false;
    vars_peek(true);
    Corner c[CORNERS];
    for (unsigned i = 0; i < CORNERS; i++)
        c[i] = quad_corner(state_of(s), s->cpu.x, i);
    bool fits = screen_quad_fits_the_table(state_of(s), c);
    vars_peek(false);
    return fits;
}

bool battle_beam_quad_is_one(Snes *s)
{
    vars_peek(true);
    bool one = screen_command_operand(state_of(s), 0) < QUADS && beam_fits(state_of(s));
    vars_peek(false);
    return battle_screen_page_a_clear(s) && one;
}

bool battle_quad_corners_fit(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    Corner c[CORNERS];
    corners_in_cells(g, c);
    bool sorted = c[0].y <= c[1].y && c[1].y <= c[2].y && c[2].y <= c[3].y;
    bool fits = !odd_frame(g) || c[3].y <= SCREEN_LINES;
    vars_peek(false);
    return battle_quad_page(s) && sorted && fits;
}

void battle_sort_quad_corners(Snes *s)
{
    GameState *g = state_of(s);
    Corner c[CORNERS];
    corners_in_cells(g, c);
    sort_corners_by_line(c);
    leave_corners(g, c);
    m_rts(s);
}

void battle_quad_edge_slope(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t to_x = battle_low_scratch(g, EDGE_CELLS), from_x = battle_low_scratch(g, EDGE_CELLS + 1);
    uint8_t lines = battle_low_scratch(g, EDGE_CELLS + 2);
    s->cpu.y = slope_across(from_x, to_x, lines);
    ORIGINAL_EFFECT(hw_set_dividend(g, (uint16_t)((to_x < from_x ? from_x - to_x : to_x - from_x) << SLOPE_FRACTION_BITS)));
    ORIGINAL_EFFECT(hw_divide_by(g, lines));
    s->cpu.a = 0;
    m_rts(s);
}

void battle_quad_edge_slopes(Snes *s)
{
    GameState *g = state_of(s);
    Corner c[CORNERS];
    Slope slopes[EDGES];
    corners_in_cells(g, c);
    edge_slopes(c, slopes);
    leave_slopes(g, c, slopes);
    ORIGINAL_EFFECT(leave_last_edge(g, c[edges[EDGES - 1].from], c[edges[EDGES - 1].to]));
    s->cpu.a = 0;
    m_rts(s);
}

/* $D1:F9E7, entered with the corners sorted in the cells and the window in
 * bit 0 of battle_window_quad_index. */
void battle_rasterize_window_quad(Snes *s)
{
    GameState *g = state_of(s);
    Corner c[CORNERS];
    corners_in_cells(g, c);
    rasterize(g, (battle_window_quad_index(g) & 1) ? WINDOW_2 : WINDOW_1, c);
    s->cpu.a = 0;
    m_rts(s);
}

void battle_draw_window_quad(Snes *s)
{
    screen_draw_window_quad(state_of(s), s->cpu.x);
    s->cpu.a = 0;
    m_rtl(s);
}

void battle_screen_draw_beam(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t quad = screen_command_operand(g, 0);
    NATIVE_TIME_AS(s, 0xD1F9AF, draw_beam(g, quad));
    s->cpu.a = 0;
    m_rtl(s);
}
