/* Battle: the screen scripts' window triangles.
 *
 * A screen script can shape either window as a triangle (screen_shapes.c
 * keeps the two triangles' records: their corners, centers and sizes). Drawing
 * one fills the coming frame's window table between its sides, from its top
 * corner's line down to its bottom one's, each side a straight edge from
 * corner to corner (battle/screen_sides.h). Here are that drawing, which every
 * command that draws a triangle uses; the right triangle hung from the top of
 * the screen, a wedge with the window above it filled up to the screen's top
 * edge; and the center of a triangle, where two of its medians cross, which
 * the scripts center a triangle (and the channel's layer) on. */
#include "charge.h"
#include "battle/screen_script.h"

#include "battle/bridge.h"
#include "battle/screen_sides.h"
#include "cpu_io.h"

typedef BattlePoint Corner;
enum { CORNERS = SCREEN_TRIANGLE_CORNERS };

static void swap_corners(Corner *a, Corner *b)
{
    Corner t = *a;
    *a = *b;
    *b = t;
}

static Window window_numbered(unsigned window) { return (window & 1) ? WINDOW_2 : WINDOW_1; }

static uint8_t lines_between(Corner from, Corner to) { return (uint8_t)(to.y - from.y); }

static Slope edge_slope(Corner from, Corner to) { return sides_slope(from.x, to.x, lines_between(from, to)); }

/* ---- A triangle's sides ------------------------------------------------------------------ */

/* Put the corners in order down the screen: three times over, a corner moves
 * past the next where it isn't above it (so of two on one line, which comes
 * first depends on where the third was). */
static void sort_by_line(Corner c[CORNERS])
{
    if (c[0].y >= c[1].y)
        swap_corners(&c[0], &c[1]);
    if (c[1].y >= c[2].y)
        swap_corners(&c[1], &c[2]);
    if (c[0].y >= c[1].y)
        swap_corners(&c[0], &c[1]);
}

/* How the sides go down a triangle: from a flat top to the bottom corner;
 * from the top corner to a flat bottom; or from the top corner with the left
 * side turning at its middle corner's line, or the right side at its. */
typedef enum { FLAT_TOP, FLAT_BOTTOM, LEFT_SIDE_TURNS, RIGHT_SIDE_TURNS } TriangleForm;

/* The sides of a triangle: its corners as they are taken (the top one, or the
 * top left one, first; then the left side's next, then the right side's), its
 * form, the slopes the sides start down with and the one a side turns onto,
 * and the stretches of lines. */
typedef struct {
    Corner c[CORNERS];
    TriangleForm form;
    Slope left, right, turned;
    WindowSides sides;
} TriangleSides;

static void add_stretch(WindowSides *s, uint8_t lines, Slope left, Slope right)
{
    s->stretch[s->stretches++] = (SidesStretch){ lines, left, right };
}

static TriangleSides sides_of(ScreenTriangle t)
{
    TriangleSides ts = { .form = FLAT_TOP };
    Corner *c = ts.c;
    for (unsigned i = 0; i < CORNERS; i++)
        c[i] = t.corner[i];
    sort_by_line(c);
    WindowSides *s = &ts.sides;
    s->top = c[0].y;
    if (c[0].y == c[1].y) {
        if (c[0].x >= c[1].x)
            swap_corners(&c[0], &c[1]);
        ts.form = FLAT_TOP;
        ts.left = edge_slope(c[0], c[2]);
        ts.right = edge_slope(c[1], c[2]);
        s->left_from = c[0].x;
        s->right_from = c[1].x;
        add_stretch(s, lines_between(c[0], c[2]), ts.left, ts.right);
        add_stretch(s, 0, ts.left, ts.right);   /* no side turns */
        return ts;
    }
    if (c[1].x >= c[2].x)
        swap_corners(&c[1], &c[2]);
    s->left_from = s->right_from = c[0].x;
    ts.left = edge_slope(c[0], c[1]);
    ts.right = edge_slope(c[0], c[2]);
    if (c[1].y == c[2].y) {
        ts.form = FLAT_BOTTOM;
        add_stretch(s, lines_between(c[0], c[2]), ts.left, ts.right);
        add_stretch(s, 0, ts.left, ts.right);
        return ts;
    }
    /* Of the middle corners, the one whose edge from the top runs further
       right down the screen is the right side's: where both edges run the
       same way, the one with the greater slope (of two leftward ones, the
       greater word is the one leaning less to the left). */
    if (sides_leftward(c[0].x, c[1].x) == sides_leftward(c[0].x, c[2].x) && ts.left >= ts.right) {
        swap_corners(&c[1], &c[2]);
        Slope was_left = ts.left;
        ts.left = ts.right;
        ts.right = was_left;
    }
    if (c[2].y >= c[1].y) {
        ts.form = LEFT_SIDE_TURNS;
        ts.turned = edge_slope(c[1], c[2]);
        add_stretch(s, lines_between(c[0], c[1]), ts.left, ts.right);
        add_stretch(s, lines_between(c[1], c[2]), ts.turned, ts.right);
    } else {
        ts.form = RIGHT_SIDE_TURNS;
        ts.turned = edge_slope(c[2], c[1]);
        add_stretch(s, lines_between(c[0], c[2]), ts.left, ts.right);
        add_stretch(s, lines_between(c[2], c[1]), ts.left, ts.turned);
    }
    return ts;
}

static uint8_t bottom_of(ScreenTriangle t)
{
    uint8_t bottom = 0;
    for (unsigned i = 0; i < CORNERS; i++)
        if (t.corner[i].y > bottom)
            bottom = t.corner[i].y;
    return bottom;
}

bool screen_triangle_fits_the_table(const GameState *g, ScreenTriangle t) { return sides_fit_the_table(g, bottom_of(t)); }

/* ---- What the original leaves of the drawing --------------------------------------------- */

/* The original works in the battle page's low cells: the corners in the
 * order it takes them ($12-$17), the slopes ($03, $05, $07: the left, the
 * right and the turned onto), the first edge's way ($00), the last edge it
 * measured ($0B-$0D, and the divider its division leaves), the top line and
 * the lines each stretch ends on ($0F, $11, $10), and a stretch's count run
 * down to 0 ($01-$02). */
enum { COUNT_CELL = 0x01, EDGE_CELLS = 0x0B, CORNER_CELLS = 0x12 };

/* ---- Drawing --------------------------------------------------------------------------- */

// $CD:367C, $CD:3685
void screen_draw_window_triangle(GameState *g, unsigned window, ScreenTriangle t)
{
    TriangleSides ts = sides_of(t);
    Window w = window_numbered(window);
    (void)draw_window_sides(g, w, &ts.sides);
}

/* The hung triangle's corner away from the top: where the upper corner's
 * column meets the lower one's line. Its top is then always flat: two of its
 * corners share the upper corner's line. */
static Corner right_angle(Corner a, Corner b) { return a.y >= b.y ? (Corner){ a.x, b.y } : (Corner){ b.x, a.y }; }

// $CD:3924, $CD:3952, $CD:3B10
void screen_hang_window_triangle(GameState *g, unsigned window, BattlePoint a, BattlePoint b)
{
    ScreenTriangle t = { { a, b, right_angle(a, b) } };
    TriangleSides ts = sides_of(t);
    Window w = window_numbered(window);
    Edges above = edges_between(ts.c[0].x, ts.c[1].x);
    for (unsigned line = 0; line < ts.sides.top; line++)
        set_line_edges(g, coming_line(g, line), w, above);
    (void)draw_window_sides(g, w, &ts.sides);
}

/* ---- The center ---------------------------------------------------------------------------- */

/* The original finds a triangle's center where the median from the middle of
 * its first and second corners to the third crosses the one from the middle
 * of its first and third to the second: each median a line y = slope * x +
 * intercept, the slope in 256ths of a line a column, and the crossing's
 * column the intercepts' difference over the slopes'. */
typedef struct {
    Corner from, to;    /* left to right */
    Slope slope;
    uint16_t intercept;
} Median;

/* The middle of two corners' columns or lines, rounded toward the second. */
static uint8_t middle(uint8_t a, uint8_t b) { return (uint8_t)(b + ((int)a - (int)b) / 2); }

static Corner middle_of(Corner a, Corner b) { return (Corner){ middle(a.x, b.x), middle(a.y, b.y) }; }

/* A slope times a column, in whole lines, its fraction cut toward zero. */
static uint16_t rise_over(uint8_t x, Slope slope)
{
    bool falling = slope & 0x8000;
    uint16_t magnitude = falling ? (uint16_t)-slope : slope;
    uint16_t rise = (uint16_t)((uint32_t)x * magnitude >> SLOPE_FRACTION_BITS);
    return falling ? (uint16_t)-rise : rise;
}

static Median median_between(Corner a, Corner b)
{
    if (a.x >= b.x)
        swap_corners(&a, &b);
    Median m = { a, b, sides_slope(a.y, b.y, (uint8_t)(b.x - a.x)), 0 };
    m.intercept = (uint16_t)(a.y - rise_over(a.x, m.slope));
    return m;
}

/* Two medians starting less than four columns (or lines) apart are moved
 * apart: the first one's start four on. */
enum { MEDIANS_APART = 4 };

static bool too_close(uint8_t a, uint8_t b) { return (uint8_t)(a - b + MEDIANS_APART) < 2 * MEDIANS_APART; }

typedef struct {
    Median first, second;
} Medians;

static Medians medians_of(ScreenTriangle t)
{
    Corner first = middle_of(t.corner[0], t.corner[1]), second = middle_of(t.corner[0], t.corner[2]);
    if (too_close(first.x, second.x))
        first.x = (uint8_t)(first.x + MEDIANS_APART);
    if (too_close(first.y, second.y))
        first.y = (uint8_t)(first.y + MEDIANS_APART);
    return (Medians){ median_between(first, t.corner[2]), median_between(second, t.corner[1]) };
}

/* The crossing's column is the intercepts' difference times 256 over the
 * slopes' difference, worked out as one word over another: as much of the 256
 * as keeps the difference within a word multiplies it, and the rest divides
 * the slopes' (which loses their low bits). */
typedef struct {
    uint16_t over, under;    /* the two words */
    uint16_t slopes_apart;   /* the slopes' difference, before it was divided */
    unsigned scale;          /* how many of the 256's eight doublings went on the intercepts' */
    bool negative;
} Crossing;

static unsigned top_bit(uint16_t v)
{
    unsigned bit = 0;
    while (v >>= 1)
        bit++;
    return bit;
}

enum { WORD_TOP_BIT = 15, DOUBLINGS = SLOPE_FRACTION_BITS };

static Crossing crossing_of(Medians m)
{
    int16_t intercepts = (int16_t)(m.second.intercept - m.first.intercept);
    int16_t slopes = (int16_t)(m.first.slope - m.second.slope);
    uint16_t over = (uint16_t)(intercepts < 0 ? -intercepts : intercepts);
    uint16_t under = (uint16_t)(slopes < 0 ? -slopes : slopes);
    unsigned scale = over >> DOUBLINGS ? WORD_TOP_BIT - top_bit(over) : DOUBLINGS;
    return (Crossing){ (uint16_t)(over << scale), (uint16_t)(under >> (DOUBLINGS - scale)), under, scale,
                       (intercepts < 0) != (slopes < 0) };
}

/* The original's division is a word long: it divides exactly while the
 * divisor is at most half a word, and loses the remainder's top bit past
 * that. A triangle whose medians would need that is the original's to work
 * out (the commands' needs refuse it). */
enum { HALF_A_WORD = 0x8000 };

bool screen_triangle_center_divides(ScreenTriangle t) { return crossing_of(medians_of(t)).under <= HALF_A_WORD; }

static bool divides(Crossing x) { return x.over && x.under; }

/* Parallel medians (or ones crossing on the first's start) put the center at
 * column 0. */
static uint16_t quotient(Crossing x) { return divides(x) ? (uint16_t)(x.over / x.under) : 0; }
static uint16_t remainder_of(Crossing x) { return divides(x) ? (uint16_t)(x.over % x.under) : 0; }

/* The cells $23-$2A, which the battle uses for other things between effects
 * (battle_direction_work), are where the original divides and multiplies,
 * and it leaves its last values there: the center's column, what the
 * division leaves of the word it divided, the first median's slope, that
 * slope times the column (its intercept added to the whole lines), and the
 * division's remainder's high byte. They are written plainly, as the
 * direction's measure writes them (screen_between.c).
 *
 * The division shifts the dividend out of its word a bit at a time and the
 * last bit the scaling shifted out of the divisor in: only that is left, on
 * top (where the divisor wasn't shifted but its high byte taken, it is the
 * dividend's own top bit, set; where it was left whole, nothing). Where it
 * doesn't divide, the scaled dividend is left as it was. */
static bool last_bit_scaled_out(Crossing x)
{
    if (x.scale == 0)
        return true;
    if (x.scale == DOUBLINGS)
        return false;
    return (x.slopes_apart >> (DOUBLINGS - 1 - x.scale)) & 1;
}

static uint8_t left_of_the_dividend(Crossing x)
{
    if (!divides(x))
        return (uint8_t)(x.over >> 8);
    return last_bit_scaled_out(x) ? 0x80 : 0;
}

static void keep_center_work(GameState *g, Medians m, Crossing x, uint8_t column, uint32_t product, uint16_t line)
{
    const uint8_t work[] = { column, left_of_the_dividend(x), (uint8_t)m.first.slope, (uint8_t)(m.first.slope >> 8),
                             (uint8_t)product, (uint8_t)line, (uint8_t)(line >> 8), (uint8_t)(remainder_of(x) >> 8) };
    for (unsigned i = 0; i < sizeof work; i++)
        set_battle_direction_work(g, i, work[i]);
}

// $CD:21C0
BattlePoint screen_triangle_center(GameState *g, ScreenTriangle t)
{
    Medians m = medians_of(t);
    Crossing x = crossing_of(m);
    uint16_t q = quotient(x);
    uint8_t column = (uint8_t)(x.negative ? -q : q);
    uint32_t product = (uint32_t)column * m.first.slope;
    uint16_t line = (uint16_t)((product >> SLOPE_FRACTION_BITS) + m.first.intercept);
    keep_center_work(g, m, x, column, product, line);
    return (BattlePoint){ column, (uint8_t)line };
}
