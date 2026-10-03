/* Battle: the windows' line table, as the screen effects that shape it see it.
 *
 * The battle's two windows mask the layers and the color math, and their
 * edges can change on every line of the screen: the line effect
 * (battle_setup_window_hdma, $CD:0AEE) sends each line both windows' left and
 * right edges from a table. The table is kept twice, and the line effect
 * shows one buffer while the effects write the other for the coming frame:
 * the second buffer on odd frames, the first on even ones. An empty window
 * has its left edge right of its right one ($FF, 0); a full one runs from 0 to
 * $FF. Four lines of the top border come before the first buffer.
 *
 * screen_window.c's commands (bands, wipes, waves, fills), screen_divide.c's
 * line through the screen's middle and screen_beam.c's quadrilaterals all
 * write it through these. */
#ifndef CT_GAME_BATTLE_SCREEN_LINES_H
#define CT_GAME_BATTLE_SCREEN_LINES_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"
#include "vars.h"

typedef enum { WINDOW_1, WINDOW_2 } Window;
enum { WINDOWS = 2 };

/* A window's edges on a line as one word, the left edge in the low byte. */
typedef uint16_t Edges;
enum { EDGES_EMPTY = 0x00FF, EDGES_FULL = 0xFF00 };

static inline Edges edges_between(uint8_t left, uint8_t right) { return (Edges)(right << 8 | left); }
static inline uint8_t left_edge(Edges e) { return (uint8_t)e; }
static inline uint8_t right_edge(Edges e) { return (uint8_t)(e >> 8); }

enum {
    SCREEN_LINES = 224,
    TABLE_LINES = 2 * SCREEN_LINES,
    /* the battlefield, above the menus: the lines the bands and waves cover */
    BATTLEFIELD_LINES = 212,
    TOP_BORDER_LINES = BATTLE_WINDOW_TOP_LINES_COUNT,
};

/* A line of the table, counted from the first buffer's first: the second
 * buffer's follow the first's (224-447), and the top border's four come
 * before them (-4 to -1). */
typedef int TableLine;

static inline Edges line_edges(const GameState *g, TableLine line, Window w)
{
    if (line < 0)
        return w == WINDOW_1 ? battle_window_top_lines_w1(g, TOP_BORDER_LINES + line)
                             : battle_window_top_lines_w2(g, TOP_BORDER_LINES + line);
    return w == WINDOW_1 ? battle_window_lines_w1(g, line) : battle_window_lines_w2(g, line);
}

static inline void set_line_edges(GameState *g, TableLine line, Window w, Edges e)
{
    if (line < 0) {
        if (w == WINDOW_1)
            set_battle_window_top_lines_w1(g, TOP_BORDER_LINES + line, e);
        else
            set_battle_window_top_lines_w2(g, TOP_BORDER_LINES + line, e);
    } else if (w == WINDOW_1) {
        set_battle_window_lines_w1(g, line, e);
    } else {
        set_battle_window_lines_w2(g, line, e);
    }
}

/* One edge of a window on a line of the two buffers. */
typedef enum { LEFT_EDGE, RIGHT_EDGE } WhichEdge;

static inline uint8_t line_edge(const GameState *g, unsigned line, Window w, WhichEdge edge)
{
    if (w == WINDOW_1)
        return edge == LEFT_EDGE ? battle_window_lines_w1_left(g, line) : battle_window_lines_w1_right(g, line);
    return edge == LEFT_EDGE ? battle_window_lines_w2_left(g, line) : battle_window_lines_w2_right(g, line);
}

static inline void set_line_edge(GameState *g, unsigned line, Window w, WhichEdge edge, uint8_t v)
{
    if (w == WINDOW_1) {
        if (edge == LEFT_EDGE)
            set_battle_window_lines_w1_left(g, line, v);
        else
            set_battle_window_lines_w1_right(g, line, v);
    } else if (edge == LEFT_EDGE) {
        set_battle_window_lines_w2_left(g, line, v);
    } else {
        set_battle_window_lines_w2_right(g, line, v);
    }
}

/* The original counts lines in bytes of the table, four a line, and leaves
 * such counts in its cells. */
enum { LINE_BYTES = 4 };

/* The buffer the coming frame shows, and the one on screen now. */
static inline bool odd_frame(const GameState *g) { return battle_frame_count(g) & 0x01; }
static inline TableLine coming_line(const GameState *g, unsigned line) { return (odd_frame(g) ? SCREEN_LINES : 0) + (int)line; }
static inline TableLine shown_line(const GameState *g, unsigned line) { return (odd_frame(g) ? 0 : SCREEN_LINES) + (int)line; }

#endif
