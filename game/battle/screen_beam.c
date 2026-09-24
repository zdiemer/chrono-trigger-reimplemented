/* Battle: the beam, and the window quadrilaterals it is drawn with.
 *
 * A beam that falls from the top of the screen onto a combatant is a window
 * shaped as a quadrilateral: its top a short span swaying along the top of
 * the screen, its foot a span about the first animation position. The battle
 * has two such quadrilaterals (battle_window_quads), each four corners; one
 * is drawn into the window table by sorting its corners from the top line
 * down, working out how many columns each edge moves a line, and handing
 * both to the rasterizer ($D1:F9E7), which fills in each line's edges. The
 * scripts draw other quadrilaterals the same way ($CD:2495).
 *
 * The rasterizer stays the original's: it writes the window table through the
 * work RAM port with 16-bit stores, so each line's edge also goes, as a low
 * byte, to APU port 3, a write no service expresses (docs/TODO.md). It finds
 * the corners and the slopes in the battle page's low cells. */
#include "battle/screen.h"

#include "battle/bridge.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "port/native.h"

/* ---- The quadrilaterals -------------------------------------------------------------------- */

typedef struct {
    uint8_t x, y;
} Corner;

enum { CORNERS = 4, QUADS = 2 };

/* The two quadrilaterals' bytes are interleaved: each coordinate of the
 * first, then the same of the second. */
static unsigned coordinate(unsigned quad, unsigned corner, bool y) { return (2 * corner + y) * QUADS + quad; }

static Corner quad_corner(const GameState *g, unsigned quad, unsigned corner)
{
    return (Corner){ battle_window_quads(g, coordinate(quad, corner, false)),
                     battle_window_quads(g, coordinate(quad, corner, true)) };
}

static void set_quad_corner(GameState *g, unsigned quad, unsigned corner, Corner c)
{
    set_battle_window_quads(g, coordinate(quad, corner, false), c.x);
    set_battle_window_quads(g, coordinate(quad, corner, true), c.y);
}

// $D1:F8EB
/* Put the corners in order down the screen: a corner moves past the next
 * where it isn't above it. The original's sort also swaps corners on the
 * same line, which decides which of them the rasterizer takes first. */
static void sort_corners_by_line(Corner c[CORNERS])
{
    for (unsigned pass = 0; pass < CORNERS; pass++) {
        for (unsigned i = 0; i + 1 < CORNERS; i++) {
            if (c[i].y >= c[i + 1].y) {
                Corner lower = c[i];
                c[i] = c[i + 1];
                c[i + 1] = lower;
            }
        }
    }
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

/* The four edges, from the top corner down one side to the second and third
 * corners and from those down to the bottom one. */
static const struct {
    uint8_t from, to;
} edges[EDGES] = { { 0, 2 }, { 2, 3 }, { 0, 1 }, { 1, 3 } };

// $D1:F91D
static void edge_slopes(const Corner c[CORNERS], Slope slopes[EDGES])
{
    for (unsigned e = 0; e < EDGES; e++)
        slopes[e] = edge_slope(c[edges[e].from], c[edges[e].to]);
}

/* ---- The beam --------------------------------------------------------------------------------- */

/* The beam's top, 32 columns wide, sways about column $C0 by up to 40 columns
 * (40 times the sine of the frame count); its foot is 32 wide about the
 * position. */
enum { BEAM_WIDTH = 0x20, BEAM_TOP_LEFT = 0xB0, BEAM_SWAY = 0x28, HALF_TURN = 0x80, SINE_FRACTION_BITS = 8 };

// $D1:EBFF
/* Screen command $F4: shape window quadrilateral `quad` as a beam from the top
 * of the screen down to the first animation position. */
static void shape_beam(GameState *g, unsigned quad)
{
    uint8_t frame = (uint8_t)battle_frame_count(g), sine = battle_sine_magnitude(g, frame);
    uint8_t sway = (uint8_t)(BEAM_SWAY * sine >> SINE_FRACTION_BITS);
    if (frame & HALF_TURN)
        sway = (uint8_t)-sway;
    ORIGINAL_EFFECT(hw_set_multiplicand(g, BEAM_SWAY));
    ORIGINAL_EFFECT(hw_multiply_by(g, sine));
    uint16_t at = battle_animation_positions(g, 0);
    uint8_t top_left = (uint8_t)(BEAM_TOP_LEFT + sway), foot_left = (uint8_t)((uint8_t)at - BEAM_WIDTH / 2);
    uint8_t foot = (uint8_t)(at >> 8);
    set_quad_corner(g, quad, 0, (Corner){ top_left, 0 });
    set_quad_corner(g, quad, 1, (Corner){ (uint8_t)(top_left + BEAM_WIDTH), 0 });
    set_quad_corner(g, quad, 2, (Corner){ foot_left, foot });
    set_quad_corner(g, quad, 3, (Corner){ (uint8_t)(foot_left + BEAM_WIDTH), foot });
}

/* ---- The rasterizer's cells ---------------------------------------------------------------- */

/* The rasterizer finds the corners at $12-$19 (x, y each), the slopes at
 * $03-$0A and the top line at $0F of the battle page; the slopes' routine
 * leaves the last edge's end and start columns and its lines at $0B-$0D. */
enum { CORNER_CELLS = 0x12, SLOPE_CELLS = 0x03, TOP_LINE_CELL = 0x0F, EDGE_CELLS = 0x0B };

static void corners_in_cells(const GameState *g, Corner c[CORNERS])
{
    for (unsigned i = 0; i < CORNERS; i++)
        c[i] = (Corner){ battle_low_scratch(g, CORNER_CELLS + 2 * i), battle_low_scratch(g, CORNER_CELLS + 2 * i + 1) };
}

static void hand_corners(GameState *g, const Corner c[CORNERS])
{
    for (unsigned i = 0; i < CORNERS; i++) {
        set_battle_low_scratch(g, CORNER_CELLS + 2 * i, c[i].x);
        set_battle_low_scratch(g, CORNER_CELLS + 2 * i + 1, c[i].y);
    }
}

static void hand_slopes(GameState *g, const Corner c[CORNERS], const Slope slopes[EDGES])
{
    for (unsigned e = 0; e < EDGES; e++) {
        set_battle_low_scratch(g, SLOPE_CELLS + 2 * e, (uint8_t)slopes[e]);
        set_battle_low_scratch(g, SLOPE_CELLS + 2 * e + 1, (uint8_t)(slopes[e] >> 8));
    }
    set_battle_low_scratch(g, TOP_LINE_CELL, c[0].y);
}

/* What working out a slope leaves in the divider: the columns over the
 * lines. */
static void leave_division(GameState *g, uint8_t from_x, uint8_t to_x, uint8_t lines)
{
    uint8_t columns = to_x < from_x ? (uint8_t)(from_x - to_x) : (uint8_t)(to_x - from_x);
    hw_set_dividend(g, (uint16_t)(columns << SLOPE_FRACTION_BITS));
    hw_divide_by(g, lines);
}

/* ... and working out the four, the last edge's end and start columns and its
 * lines in the cells as well. */
static void leave_last_edge(GameState *g, Corner from, Corner to)
{
    set_battle_low_scratch(g, EDGE_CELLS, to.x);
    set_battle_low_scratch(g, EDGE_CELLS + 1, from.x);
    set_battle_low_scratch(g, EDGE_CELLS + 2, lines_between(from, to));
    leave_division(g, from.x, to.x, lines_between(from, to));
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The guards: the rasterizer calls the slopes' routines with the data bank
 * it sets for the work RAM port, and A's high byte clear; the quadrilateral
 * drawn is one of the two. */
bool battle_quad_page(Snes *s) { return m_direct_page(s) == BATTLE_PAGE && m_io_bank(s->cpu.db) && (s->cpu.a & 0xFF00) == 0; }

bool battle_quad_is_one(Snes *s) { return battle_screen_page(s) && s->cpu.x < QUADS; }

bool battle_beam_quad_is_one(Snes *s)
{
    vars_peek(true);
    bool one = screen_command_operand(state_of(s), 0) < QUADS;
    vars_peek(false);
    return battle_screen_page_a_clear(s) && one;
}

void battle_sort_quad_corners(Snes *s)
{
    GameState *g = state_of(s);
    Corner c[CORNERS];
    corners_in_cells(g, c);
    sort_corners_by_line(c);
    hand_corners(g, c);
    m_rts(s);
}

void battle_quad_edge_slope(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t to_x = battle_low_scratch(g, EDGE_CELLS), from_x = battle_low_scratch(g, EDGE_CELLS + 1);
    uint8_t lines = battle_low_scratch(g, EDGE_CELLS + 2);
    s->cpu.y = slope_across(from_x, to_x, lines);
    ORIGINAL_EFFECT(leave_division(g, from_x, to_x, lines));
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
    hand_slopes(g, c, slopes);
    ORIGINAL_EFFECT(leave_last_edge(g, c[edges[EDGES - 1].from], c[edges[EDGES - 1].to]));
    s->cpu.a = 0;
    m_rts(s);
}

// $D1:F9AF
/* Draw window quadrilateral X into the coming frame's window table: its
 * corners, sorted, to the rasterizer, which runs with the data bank on the
 * I/O registers (for the work RAM port), kept on the stack as the original
 * keeps it. */
void battle_draw_window_quad(Snes *s)
{
    GameState *g = state_of(s);
    unsigned quad = s->cpu.x;
    Corner c[CORNERS];
    set_battle_window_quad_index(g, (uint8_t)quad);
    for (unsigned i = 0; i < CORNERS; i++)
        c[i] = quad_corner(g, quad, i);
    sort_corners_by_line(c);
    hand_corners(g, c);
    uint8_t bank = s->cpu.db;
    m_push8(s, bank);
    s->cpu.db = 0x00;
    s->cpu.a = 0;
    if (!battle_call(s, 0xD1F9E2, 0xD1F9E7, DC_X | DC_Y | DC_FLAGS | DC_AL))
        return;
    s->cpu.db = m_pull8(s);
    m_rtl(s);
}

/* The beam draws its quadrilateral with the routine above, called as the
 * original's JSL calls it (its return on the stack, where the rasterizer's
 * stack sits), and goes on where it returns. */
enum { DRAW_QUAD_CALL = 0xD1EC5A, DRAW_QUAD = 0xD1F9AF };

static bool draw_quad_as_called(Snes *s, unsigned quad)
{
    uint16_t sp = s->cpu.s;
    m_push8(s, (uint8_t)(DRAW_QUAD_CALL >> 16));
    m_push16(s, (uint16_t)(DRAW_QUAD_CALL + BATTLE_JSL_SIZE - 1));
    s->cpu.pb = (uint8_t)(DRAW_QUAD >> 16);
    s->cpu.pc = (uint16_t)DRAW_QUAD;
    s->cpu.x = (uint16_t)quad;
    NATIVE_TIME_AS(s, DRAW_QUAD, battle_draw_window_quad(s));
    bool returned = s->cpu.s == sp && s->cpu.pb == (uint8_t)(DRAW_QUAD_CALL >> 16)
                    && s->cpu.pc == (uint16_t)(DRAW_QUAD_CALL + BATTLE_JSL_SIZE);
    return returned && battle_back(s, DRAW_QUAD_CALL + BATTLE_JSL_SIZE, BATTLE_PAGE, BATTLE_DATA_BANK);
}

void battle_screen_draw_beam(Snes *s)
{
    GameState *g = state_of(s);
    unsigned quad = screen_command_operand(g, 0);
    shape_beam(g, quad);
    /* A is as the rasterizer leaves it */
    if (draw_quad_as_called(s, quad))
        m_rtl(s);
}
