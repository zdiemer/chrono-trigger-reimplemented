/* Battle: a window shape's straight sides, drawn down the line table
 * (battle/screen_sides.h). */
#include "battle/screen_sides.h"

#include "cpu_io.h"
#include "ram.h"

// $D1:F972, $CD:38E5
Slope sides_slope(uint8_t from, uint8_t to, uint8_t lines)
{
    bool leftward = sides_leftward(from, to);
    uint8_t run = leftward ? (uint8_t)(from - to) : (uint8_t)(to - from);
    uint16_t per_line = lines ? (uint16_t)((run << SLOPE_FRACTION_BITS) / lines) : (uint16_t)DIVIDED_BY_NOTHING;
    return leftward ? (Slope)-per_line : per_line;
}

/* The two sides go down a stretch as one running sum, the left side's column
 * below the right side's: whatever comes out of the top of one side's step
 * goes into the next one stepped, the right side's on the same line and the
 * left side's on the line after, as a 256th of a column (a stretch starts
 * with nothing carried).
 *
 * The step is a word, a leftward slope its two's complement, so what comes
 * out of the top depends on the side's way:
 * - a side stepping rightward carries only where its fraction comes round
 *   past a whole column, so its neighbour gains a 256th now and then;
 * - a side stepping leftward carries on every line its column doesn't fall
 *   below 0 (adding a two's complement wraps past $FFFF), so its neighbour
 *   gains a 256th almost every line, and over a long stretch that adds up to
 *   whole columns.
 * On screen, an edge beside a leftward side lands a column or more to the
 * right of where its slope alone would put it, more the longer they run. */
typedef struct {
    Column left, right;
    bool carried;
} Sides;

static Column step_carrying_a_256th(Column at, Slope slope, bool *carried)
{
    uint32_t sum = (uint32_t)at + slope + *carried;
    *carried = sum > 0xFFFF;
    return (Column)sum;
}

static void step_down_a_line(Sides *s, Slope left, Slope right)
{
    s->left = step_carrying_a_256th(s->left, left, &s->carried);
    s->right = step_carrying_a_256th(s->right, right, &s->carried);
}

SidesDrawn draw_window_sides(GameState *g, Window w, const WindowSides *shape)
{
    Sides s = { sides_column_at(shape->left_from), sides_column_at(shape->right_from), false };
    unsigned line = shape->top;
    for (unsigned i = 0; i < shape->stretches; i++) {
        const SidesStretch *stretch = &shape->stretch[i];
        s.carried = false;
        for (unsigned n = 0; n < stretch->lines; n++, line++) {
            step_down_a_line(&s, stretch->left, stretch->right);
            set_line_edges(g, coming_line(g, line), w,
                           edges_between(sides_whole_column(s.left), sides_whole_column(s.right)));
        }
    }
    return (SidesDrawn){ line - shape->top, s.right };
}

bool sides_fit_the_table(const GameState *g, uint8_t bottom) { return !odd_frame(g) || bottom <= SCREEN_LINES; }

enum { WINDOW_TABLE_BANK = RAM_BATTLE_WINDOW_LINES >> 16 };

void sides_leave_port(GameState *g, Window w, unsigned line_after, SidesDrawn drawn)
{
    unsigned line = (unsigned)coming_line(g, line_after);
    hw_port_seek(g, (uint16_t)(RAM_BATTLE_WINDOW_LINES + line * LINE_BYTES + w * sizeof(Edges)), WINDOW_TABLE_BANK);
    if (drawn.lines)
        hw_port_leave_spill(g, (uint8_t)drawn.last_right);
}
