/* Battle: a window shape's straight sides, drawn down the line table.
 *
 * The screen scripts' quadrilaterals (screen_beam.c) and triangles
 * (screen_triangles.c) are drawn the same way: from the shape's top line
 * down, each line's window runs between a left side and a right side, and
 * each side is a straight edge from one corner to the next, its column moving
 * by a fixed slope each line. A shape is a top line, the two sides' columns
 * there, and up to three stretches of lines, down each of which each side
 * follows one edge; where a corner turns a side, a stretch ends.
 *
 * Columns and slopes are 8.8 fixed point: 256ths of a column. */
#ifndef CT_GAME_BATTLE_SCREEN_SIDES_H
#define CT_GAME_BATTLE_SCREEN_SIDES_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/positions.h"
#include "battle/screen_lines.h"

/* An edge's slope: the 256ths of a column it moves each line, as a word,
 * a leftward edge's the two's complement of its run. */
typedef uint16_t Slope;
/* A side's column, in 256ths. */
typedef uint16_t Column;
enum { SLOPE_FRACTION_BITS = 8 };

/* An edge along no lines has no slope: dividing by no lines gives the
 * divider's answer for nothing, $FFFF (leftward, its negation, 1), and that
 * is the slope the sides are handed. */
enum { DIVIDED_BY_NOTHING = 0xFFFF };

/* The slope of an edge from column `from` to column `to` over `lines` lines.
 * (The same division gives a line's rise per column, its ends and its run
 * given down and across instead.) */
Slope sides_slope(uint8_t from, uint8_t to, uint8_t lines);

/* Whether the edge from `from` to `to` runs leftward (its slope negative). */
static inline bool sides_leftward(uint8_t from, uint8_t to) { return to < from; }

static inline uint8_t sides_whole_column(Column c) { return (uint8_t)(c >> SLOPE_FRACTION_BITS); }
static inline Column sides_column_at(uint8_t x) { return (Column)(x << SLOPE_FRACTION_BITS); }

/* A stretch of lines down which each side follows one edge. */
typedef struct {
    uint8_t lines;
    Slope left, right;
} SidesStretch;

enum { SIDES_MOST_STRETCHES = 3 };

typedef struct {
    uint8_t top;                   /* the first line drawn */
    uint8_t left_from, right_from; /* the sides' columns there, before their first step */
    SidesStretch stretch[SIDES_MOST_STRETCHES];
    unsigned stretches;
} WindowSides;

/* What drawing the sides came to: how many lines were drawn, and the right
 * side's column on the last of them (for what the original leaves). */
typedef struct {
    unsigned lines;
    Column last_right;
} SidesDrawn;

/* Draw a shape's sides into the coming frame's window table, as window `w`:
 * each line from the top down gets the two sides' whole columns, a side
 * stepping by its slope before each line. */
SidesDrawn draw_window_sides(GameState *g, Window w, const WindowSides *shape);

/* Whether the lines a shape covers, down to `bottom`, stay in the window
 * table: on an odd frame the coming buffer is the table's second, and a shape
 * reaching below the screen's last line would be drawn on past its end, as
 * the original does into the window's registers' images after it. */
bool sides_fit_the_table(const GameState *g, uint8_t bottom);

/* What the original leaves of the work RAM port it draws through: where it
 * stops, the line after the last, and the low byte of the last column its
 * word stores put on the sound port (scaffolding, for ORIGINAL_EFFECT). */
void sides_leave_port(GameState *g, Window w, unsigned line_after, SidesDrawn drawn);

#endif
