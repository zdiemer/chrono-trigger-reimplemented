/* Battle: a window cut by a line through the middle of the screen.
 *
 * A screen script can shape either window as the part of the screen on one
 * side of a line through its middle, at an angle the script keeps and turns:
 * a frame at a time, the cut sweeps round the screen like a clock's hand
 * (command $97). The line is drawn down one half of the screen, from where it
 * leaves the screen's edge to the middle (or from the middle to the edge),
 * and the other half is the first turned about the middle: so the window is
 * every line's columns on one side of the line, left of it while the line
 * points up and right of it while it points down.
 *
 * A script that has shaped a window this way can also carry the window over
 * unchanged into the coming frame's buffer (command $B2).
 *
 * Angles are 256 a turn, 0 pointing right and a quarter turn pointing down,
 * as the battle's sine table has them. */
#include "battle/screen_script.h"

#include "battle/screen_data.h"
#include "battle/screen_lines.h"
#include "original_effects.h"

/* ---- The line ---------------------------------------------------------------------------- */

/* The middle of the screen, which every cut passes through. */
enum { MIDDLE_ACROSS = 0x80, MIDDLE_DOWN = 0x70, MIDDLE_LINE = SCREEN_LINES / 2 };

/* A cut's line, from its upper end to its lower: how far it runs across and
 * down, and which way across. */
typedef struct {
    BattlePoint upper, lower;
    bool leftward;
    uint8_t across, down;
} CutLine;

/* The line from the screen's edge at an angle to the middle, from whichever
 * of the two is higher. */
static CutLine cut_line_at(const GameState *g, uint8_t angle)
{
    BattlePoint edge = screen_edge_toward(g, angle), middle = { MIDDLE_ACROSS, MIDDLE_DOWN };
    bool edge_above = middle.y >= edge.y;
    BattlePoint upper = edge_above ? edge : middle, lower = edge_above ? middle : edge;
    return (CutLine){ .upper = upper,
                      .lower = lower,
                      .leftward = upper.x > lower.x,
                      .across = (uint8_t)(upper.x > lower.x ? upper.x - lower.x
                                                                      : lower.x - upper.x),
                      .down = (uint8_t)(lower.y - upper.y) };
}

/* Which side of the line the window takes: left of it while the line points
 * up (the second half of the turn), and right of it otherwise. The edge the
 * line draws is the window's right edge in the first case and its left in
 * the second; the window's other edge stays at the screen's side. */
static bool line_points_up(uint8_t angle) { return (angle & SCREEN_HALF_TURN) != 0; }
static WhichEdge drawn_edge(uint8_t angle) { return line_points_up(angle) ? RIGHT_EDGE : LEFT_EDGE; }

enum { SCREEN_LEFT = 0x00, SCREEN_RIGHT = 0xFF };

/* ---- Drawing the cut ------------------------------------------------------------------------ */

/* The window's other edge, on every line, at the screen's side. */
static void open_to_the_side(GameState *g, Window w, uint8_t angle)
{
    bool left = line_points_up(angle);
    for (unsigned line = 0; line < SCREEN_LINES; line++)
        set_line_edge(g, coming_line(g, line), w, left ? LEFT_EDGE : RIGHT_EDGE, left ? SCREEN_LEFT : SCREEN_RIGHT);
}

/* Above a line that points up, the lines from the top to its upper end reach
 * as far across as that end: nothing where the end is on the screen's left
 * side, everything where it is on the right (and then the same lines at the
 * bottom start at the right, which the turn about the middle makes nothing). */
static void cover_above(GameState *g, Window w, BattlePoint upper)
{
    for (unsigned line = 0; line < upper.y; line++) {
        TableLine at = coming_line(g, line);
        if (upper.x == SCREEN_LEFT) {
            set_line_edges(g, at, w, EDGES_EMPTY);
        } else if (upper.x == SCREEN_RIGHT) {
            set_line_edge(g, (unsigned)at, w, RIGHT_EDGE, SCREEN_RIGHT);
            set_line_edge(g, (unsigned)coming_line(g, SCREEN_LINES - 1 - line), w, LEFT_EDGE, SCREEN_RIGHT);
        } else {
            set_line_edge(g, (unsigned)at, w, RIGHT_EDGE, upper.x);
        }
    }
}

/* Down the line, a column across for each line it falls: the column where it
 * crosses each line, from just below its upper end, the whole columns of its
 * run across shared out evenly. Returns where it ends across; a line that
 * falls no lines ends at column 0, as the original leaves it. */
static uint8_t trace_line(GameState *g, Window w, WhichEdge edge, const CutLine *l)
{
    if (l->down == 0)
        return SCREEN_LEFT;
    uint8_t across = l->upper.x;
    for (unsigned fallen = 1; fallen <= l->down; fallen++) {
        uint8_t moved = (uint8_t)(fallen * l->across / l->down);
        across = (uint8_t)(l->leftward ? l->upper.x - moved : l->upper.x + moved);
        set_line_edge(g, (unsigned)coming_line(g, l->upper.y + fallen - 1), w, edge, across);
    }
    return across;
}

/* Below a line that points down, to the bottom: nothing where it ended on
 * the screen's right side, its end column otherwise. Where it ended on the
 * left side the original, indexing by that column, also takes the right edge
 * of as many lines from the top to 0. */
static void cover_below(GameState *g, Window w, unsigned from, uint8_t end)
{
    for (unsigned line = from, top = 0; line < SCREEN_LINES; line++, top++) {
        unsigned at = (unsigned)coming_line(g, line);
        if (end == SCREEN_RIGHT) {
            set_line_edges(g, (TableLine)at, w, EDGES_EMPTY);
        } else {
            set_line_edge(g, at, w, LEFT_EDGE, end);
            if (end == SCREEN_LEFT)
                set_line_edge(g, (unsigned)coming_line(g, top), w, RIGHT_EDGE, 0);
        }
    }
}

/* The half not drawn is the drawn half turned about the middle: each line's
 * edge mirrored across. */
static void turn_about_the_middle(GameState *g, Window w, WhichEdge edge, bool to_the_bottom)
{
    for (unsigned line = 0; line < MIDDLE_LINE; line++) {
        unsigned top = (unsigned)coming_line(g, line), bottom = (unsigned)coming_line(g, SCREEN_LINES - 1 - line);
        if (to_the_bottom)
            set_line_edge(g, bottom, w, edge, (uint8_t)(SCREEN_RIGHT - line_edge(g, top, w, edge)));
        else
            set_line_edge(g, top, w, edge, (uint8_t)(SCREEN_RIGHT - line_edge(g, bottom, w, edge)));
    }
}

/* What the original leaves in its cells: the run across, twice (its count
 * of the columns still to share out ends where it started), and the run
 * down, with the count of lines still to go, 0. */
static void leave_cut(GameState *g, const CutLine *l)
{
    screen_leave_word(g, 0x45, l->across);
    screen_leave_word(g, 0x47, l->across);
    screen_leave_word(g, 0x49, l->down);
    screen_leave_word(g, 0x4B, 0);
}

// $CE:EA7E, $CE:E885
/* Shape window `w` in the coming frame as the side of the line `l` that an
 * angle takes. A line pointing straight right leaves the bottom half empty. */
static void cut_window(GameState *g, Window w, uint8_t angle, const CutLine *l)
{
    WhichEdge edge = drawn_edge(angle);
    open_to_the_side(g, w, angle);
    if (line_points_up(angle))
        cover_above(g, w, l->upper);
    uint8_t end = trace_line(g, w, edge, l);
    ORIGINAL_EFFECT(leave_cut(g, l));
    if (line_points_up(angle)) {
        turn_about_the_middle(g, w, edge, true);
        return;
    }
    if (angle == 0) {
        for (unsigned line = MIDDLE_LINE; line < SCREEN_LINES; line++)
            set_line_edges(g, coming_line(g, line), w, EDGES_EMPTY);
        return;
    }
    unsigned below = l->upper.y + l->down;
    if (below != SCREEN_LINES)
        cover_below(g, w, below, end);
    turn_about_the_middle(g, w, edge, false);
}

/* ---- The commands ----------------------------------------------------------------------------- */

/* Each channel keeps an angle for each window's cut: window 1's in the low
 * byte of its kept word, window 2's in the high. */
static uint8_t cut_angle(const GameState *g, ScreenChannel channel, Window w)
{
    return (uint8_t)(battle_screen_kept_word(g, channel) >> (w == WINDOW_2 ? 8 : 0));
}

static void set_cut_angle(GameState *g, ScreenChannel channel, Window w, uint8_t angle)
{
    uint16_t kept = battle_screen_kept_word(g, channel);
    set_battle_screen_kept_word(g, channel, w == WINDOW_2 ? (uint16_t)(angle << 8 | (kept & 0x00FF))
                                                          : (uint16_t)((kept & 0xFF00) | angle));
}

/* The original hands its drawing the line in cells of its own: the angle,
 * the two ends (the upper first), which way and how far it runs (a step
 * across of -1, 0 or 1, and down of 0 or 1), and the upper end again in its
 * scratch cells. Only its drawing reads them. */
enum { STEP_LEFT = 0xFF, NO_STEP = 0, STEP_ON = 1 };

static bool steps_left(uint8_t step) { return (int8_t)step < 0; }

static void leave_line(GameState *g, uint8_t angle, const CutLine *l)
{
    set_battle_cut_angle(g, angle);
    set_battle_cut_ends_across(g, 0, l->upper.x);
    set_battle_cut_ends_down(g, 0, l->upper.y);
    set_battle_cut_ends_across(g, 1, l->lower.x);
    set_battle_cut_ends_down(g, 1, l->lower.y);
    set_battle_cut_steps(g, 0, l->across == 0 ? NO_STEP : l->leftward ? STEP_LEFT : STEP_ON);
    set_battle_cut_steps(g, 1, l->down ? STEP_ON : NO_STEP);
    set_battle_cut_run(g, 0, l->across);
    set_battle_cut_run(g, 1, l->down);
    screen_leave_cell(g, 0x4F, l->upper.x);
    screen_leave_cell(g, 0x51, l->upper.y);
}

/* Bits of the command's last operand that no script sets: they would move
 * the line onto another window's bytes of the table. */
enum { STRAY_CUT_BITS = 0x7D };

static bool cut_names_its_window(const GameState *g, const ScreenCommand *c)
{
    (void)g;
    return c->sets_angle || c->stray_bits == 0;
}

// $CD:30BE, $CD:3134
/* Screen command $97: set the angle of a window's cut, or turn it on by an
 * amount and cut the window in the coming frame at its new angle. */
static ScreenFlow turn_cut(GameState *g, ScreenChannel channel, uint8_t window, bool sets_angle, uint8_t angle,
                           uint8_t turn)
{
    Window w = window ? WINDOW_2 : WINDOW_1;
    if (sets_angle) {
        set_cut_angle(g, channel, w, angle);
        return screen_on();
    }
    uint8_t turned = (uint8_t)(cut_angle(g, channel, w) + turn);
    set_cut_angle(g, channel, w, turned);
    CutLine l = cut_line_at(g, turned);
    ORIGINAL_EFFECT(leave_line(g, turned, &l));
    cut_window(g, w, turned, &l);
    return screen_on();
}
SCREEN_OPERATION(turn_cut, channel, window, sets_angle, angle, turn)

/* ---- Carrying a window over --------------------------------------------------------------------- */

/* The lines the carrying covers: the battlefield's, in the original's eight
 * runs of 27. */
enum { CARRIED_LINES = 216 };

static bool names_a_window(const GameState *g, const ScreenCommand *c)
{
    (void)g;
    return c->window < WINDOWS;
}

/* The original counts the carried lines in bytes of the table, from where the
 * window's edges start in a line, and leaves where its count ends. */
enum { CARRIED_RUN_BYTES = 27 * LINE_BYTES };

// $CE:E000, $CD:20D3
/* Screen command $B2: carry a window's shape over from the frame on screen to
 * the coming frame, on the battlefield's lines. */
static ScreenFlow carry_window_over(GameState *g, uint8_t window)
{
    Window w = window ? WINDOW_2 : WINDOW_1;
    for (unsigned line = 0; line < CARRIED_LINES; line++)
        set_line_edges(g, coming_line(g, line), w, line_edges(g, shown_line(g, line), w));
    ORIGINAL_EFFECT(screen_leave_word(g, 0x45, (uint16_t)(window * 2 + CARRIED_RUN_BYTES)));
    return screen_on();
}
SCREEN_OPERATION(carry_window_over, window)

SCREEN_OPCODES(screen_cut_opcodes,
    { 0x97, OPERATION(turn_cut), .needs = cut_names_its_window, .length = 3, .angle = BYTE(0), .turn = BYTE(1),
      .window = BIT(2, 1), .sets_angle = TOP_BIT(2), .stray_bits = MASKED(2, STRAY_CUT_BITS) },
    { 0xB2, OPERATION(carry_window_over), .needs = names_a_window, .length = 1, .window = BYTE(0) },
);

/* ---- The entries ------------------------------------------------------------------------- */

/* $CE:EA7E, called with the line in the original's cells (leave_line) and the
 * window in A as the byte its edges start at in a line (0 or 2). */
void battle_cut_window(Snes *s)
{
    GameState *g = state_of(s);
    CutLine l = { .upper = { screen_cell(g, 0x4F), screen_cell(g, 0x51) },
                  .leftward = steps_left(battle_cut_steps(g, 0)),
                  .across = battle_cut_run(g, 0),
                  .down = battle_cut_run(g, 1) };
    cut_window(g, (s->cpu.a & 0x02) ? WINDOW_2 : WINDOW_1, battle_cut_angle(g), &l);
    s->cpu.a = 0;
    m_rtl(s);
}

/* $CE:E000, called with the command's operand at the script's place. */
void battle_carry_window_over(Snes *s)
{
    carry_window_over(state_of(s), screen_command_operand(state_of(s), 0));
    s->cpu.a = 0;
    m_rtl(s);
}

/* The guards: on the battle's page with A's high byte clear; the cut's window
 * one of the two, its line within the screen, running across no further than
 * the screen's sides and falling no more lines than its run across leaves
 * room to share out in a byte (the original's count of the columns to share
 * out is one); the carried window one of the two. */
enum { A_BYTE_HOLDS = 0x100 };

bool battle_cut_fits(Snes *s)
{
    if (!battle_screen_page_a_clear(s) || ((uint8_t)s->cpu.a & ~0x02u) != 0)
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t across = screen_cell(g, 0x4F), down = screen_cell(g, 0x51);
    uint8_t run_across = battle_cut_run(g, 0), run_down = battle_cut_run(g, 1);
    bool leftward = steps_left(battle_cut_steps(g, 0));
    vars_peek(false);
    bool across_fits = leftward ? across >= run_across : across + run_across <= SCREEN_RIGHT;
    return down + run_down <= SCREEN_LINES && across_fits && run_across + run_down <= A_BYTE_HOLDS;
}

bool battle_carried_window_is_one(Snes *s)
{
    vars_peek(true);
    bool one = screen_command_operand(state_of(s), 0) < WINDOWS;
    vars_peek(false);
    return battle_screen_page(s) && one;
}
