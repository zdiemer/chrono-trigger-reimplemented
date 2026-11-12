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
#include "charge.h"
#include "battle/screen_script.h"
#include "battle/effect_noise.h"

#include "battle/bridge.h"
#include "battle/screen_lines.h"
#include "battle/screen_sides.h"
#include "cpu_io.h"

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

static uint8_t lines_between(Corner from, Corner to) { return (uint8_t)(to.y - from.y); }

static Slope edge_slope(Corner from, Corner to) { return sides_slope(from.x, to.x, lines_between(from, to)); }

/* The four edges: down the left side from the top corner to the third and
 * from the third to the bottom one, and down the right side from the top
 * corner to the second and from the second to the bottom one. */
typedef enum { TOP_TO_THIRD, THIRD_TO_BOTTOM, TOP_TO_SECOND, SECOND_TO_BOTTOM, EDGES } QuadEdge;

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

static SidesStretch stretch_along(uint8_t lines, const Slope slopes[EDGES], QuadEdge left, QuadEdge right)
{
    return (SidesStretch){ lines, slopes[left], slopes[right] };
}

/* The sides of corners sorted by line, put in the rasterizer's order. A
 * quadrilateral with a flat top is drawn down to its third corner's line
 * only, from its top corners taken left to right, with its bottom ones left
 * to right too; the bottom is assumed flat as well (the beam's is). Otherwise
 * both sides start at the top corner, the left one turning at the left one of
 * the middle corners and the right one at the right one. */
static WindowSides sides_of(Corner c[CORNERS], Slope slopes[EDGES])
{
    WindowSides q = { .top = c[0].y, .stretches = 0 };
    if (c[0].y == c[1].y) {
        if (c[0].x >= c[1].x)
            swap_corners(&c[0], &c[1]);
        if (c[2].x >= c[3].x)
            swap_corners(&c[2], &c[3]);
        edge_slopes(c, slopes);
        q.left_from = c[0].x;
        q.right_from = c[1].x;
        q.stretch[q.stretches++] = stretch_along(lines_between(c[1], c[2]), slopes, TOP_TO_THIRD, SECOND_TO_BOTTOM);
        return q;
    }
    if (c[2].x >= c[1].x)
        swap_corners(&c[1], &c[2]);
    edge_slopes(c, slopes);
    q.left_from = q.right_from = c[0].x;
    if (c[2].y <= c[1].y) {
        q.stretch[q.stretches++] = stretch_along(lines_between(c[0], c[2]), slopes, TOP_TO_THIRD, TOP_TO_SECOND);
        q.stretch[q.stretches++] = stretch_along(lines_between(c[2], c[1]), slopes, THIRD_TO_BOTTOM, TOP_TO_SECOND);
        q.stretch[q.stretches++] = stretch_along(lines_between(c[1], c[3]), slopes, THIRD_TO_BOTTOM, SECOND_TO_BOTTOM);
    } else {
        q.stretch[q.stretches++] = stretch_along(lines_between(c[0], c[1]), slopes, TOP_TO_THIRD, TOP_TO_SECOND);
        q.stretch[q.stretches++] = stretch_along(lines_between(c[1], c[2]), slopes, TOP_TO_THIRD, SECOND_TO_BOTTOM);
        q.stretch[q.stretches++] = stretch_along(lines_between(c[2], c[3]), slopes, THIRD_TO_BOTTOM, SECOND_TO_BOTTOM);
    }
    return q;
}

/* What the original leaves of the drawing: the corners in their order, the
 * slopes and the top line in its cells, and of the work RAM port it draws
 * through, where it stops and what its word writes leave on port 3. */
enum { CORNER_CELLS = 0x12, SLOPE_CELLS = 0x03, EDGE_CELLS = 0x0B };

// $D1:F9E7
/* Draw corners sorted by line into the coming frame's window table, as
 * window `w`: each line from the top corner's down to the bottom's (or the
 * third's) gets the two sides' whole columns as its edges. */
static void rasterize(GameState *g, Window w, const Corner sorted[CORNERS])
{
    Corner c[CORNERS];
    for (unsigned i = 0; i < CORNERS; i++)
        c[i] = sorted[i];
    Slope slopes[EDGES];
    WindowSides q = sides_of(c, slopes);
    (void)draw_window_sides(g, w, &q);
    battle_note_curtain_noise(g, (uint16_t)((uint8_t)(slopes[TOP_TO_SECOND] >> 8)
                                          | (uint16_t)(uint8_t)slopes[SECOND_TO_BOTTOM] << 8));
}

bool screen_quad_fits_the_table(const GameState *g, const BattlePoint c[CORNERS])
{
    uint8_t lowest = 0;
    for (unsigned i = 0; i < CORNERS; i++)
        if (c[i].y > lowest)
            lowest = c[i].y;
    return sides_fit_the_table(g, lowest);
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
