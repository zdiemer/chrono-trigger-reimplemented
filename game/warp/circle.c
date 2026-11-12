/* The circle the time warp's tunnel opens and closes in.
 *
 * The circle is window 1, its edges set line by line by a line effect from a
 * table of 256 lines (the picture's 224 run from its line 16). Each frame the
 * main loop builds the circle into one of two tables in turn, and the vblank
 * handler shows the one built last, so a table is never rebuilt while it is
 * shown. A radius of 0 is no circle at all: every line's window empty. */
#include "warp/warp.h"

#include "core/program.h"
#include "platform/video.h"
#include "ram.h"
#include "vars.h"

/* The circle's centre, across the screen and down the table: the middle. */
enum { CIRCLE_MIDDLE = 0x80, SCREEN_RIGHT = 0xFF };

/* The lines a build writes: one short of the table, whose last line keeps
 * what it had. An empty line has its left edge past its right. */
enum { LINES_BUILT = 255, EMPTY_LEFT = SCREEN_RIGHT, EMPTY_RIGHT = 0 };

/* ---- The half widths ------------------------------------------------------------------------------ */

/* Half the circle's width on each line out from its middle, worked out as a
 * point stepped round a quarter of the circle in 256ths of a pixel (Minsky's
 * way): each step down takes the line's distance over the radius from the
 * width, and the width over the radius moves the point on down. The original
 * keeps a width as a word whose fraction lands on the next line's entry until
 * that line is reached, as the C does. */
enum { MOST_LINES = 0x80, ONE = 0x100, HALF = 0x80, NEGATIVE = 0x8000 };

typedef struct {
    uint8_t width[MOST_LINES + 2];  /* the widths, and the fraction past the last */
    uint8_t step;                   /* a line's share of the radius, in 256ths */
    uint16_t area, run;             /* where the point is */
    uint16_t steps;                 /* how many times the point was stepped */
} HalfWidths;

static void work_out_half_widths(uint8_t radius, HalfWidths *h)
{
    h->step = (uint8_t)(ONE / radius);
    h->area = (uint16_t)(radius << 8 | HALF);
    h->run = 0;
    h->steps = 0;
    for (;;) {
        uint8_t line = (uint8_t)(h->run >> 8);
        h->steps++;
        h->area = (uint16_t)(h->area - h->step * line);
        if (h->area & NEGATIVE || line + 1u >= sizeof h->width)
            return;
        h->width[line] = (uint8_t)(h->area >> 8);
        h->width[line + 1] = (uint8_t)h->area;
        h->run = (uint16_t)(h->run + h->step * (h->area >> 8));
    }
}

/* ---- The table -------------------------------------------------------------------------------------- */

typedef struct {
    GameState *g;
    unsigned at;    /* the next line, from the table's start in warp_circle_windows */
} Builder;

static void put_line(Builder *b, uint8_t left, uint8_t right)
{
    set_warp_circle_windows(b->g, b->at++, vid_line_window_edges(left, right));
}

static void put_empty_lines(Builder *b, uint8_t lines)
{
    while (lines--)
        put_line(b, EMPTY_LEFT, EMPTY_RIGHT);
}

/* A line of the circle, `width` either side of its centre, kept on the screen. */
static void put_circle_line(Builder *b, uint8_t width)
{
    uint8_t left = width > CIRCLE_MIDDLE ? 0 : (uint8_t)(CIRCLE_MIDDLE - width);
    uint8_t right = CIRCLE_MIDDLE + width > SCREEN_RIGHT ? SCREEN_RIGHT : (uint8_t)(CIRCLE_MIDDLE + width);
    put_line(b, left, right);
}

enum { BUILD_THE_CIRCLE = 0xFDEB8B };

// $FD:EB8B
/* Build the circle of the radius warp_circle_radius holds into the table not
 * shown: the lines above it empty, the circle's upper half from its widest line
 * up, its lower half from its widest line down, and the lines below it empty.
 * The radius is one the warp's ramp gives (2-127) or 0: the upper half's count
 * runs from the radius less one and would wrap for a radius of 1. That is a
 * claim the game keeps (the ramp is the cartridge's), checked here: a build
 * rejects any other radius, and the clamps below keep the
 * lines on the screen whatever it is. Returns how many times the circle's
 * point was stepped (warp.h). */
uint16_t warp_build_circle(GameState *g)
{
    unsigned table = (uint8_t)(warp_circle_tables_built(g) + 1) % WARP_CIRCLE_TABLES;
    set_warp_circle_tables_built(g, (uint8_t)(warp_circle_tables_built(g) + 1));
    uint8_t radius = warp_circle_radius(g);
    if (radius == 1 || radius > MOST_LINES - 1)
        program_fail(g, BUILD_THE_CIRCLE, "a warp's circle of a radius its ramp never gives");
    Builder b = { g, table * WARP_CIRCLE_TABLE_LINES };
    if (radius == 0) {
        put_empty_lines(&b, LINES_BUILT);
        return 0;
    }
    HalfWidths h;
    work_out_half_widths(radius, &h);
    if (radius <= CIRCLE_MIDDLE)
        put_empty_lines(&b, (uint8_t)(CIRCLE_MIDDLE - radius));
    for (unsigned line = (radius < CIRCLE_MIDDLE ? radius : CIRCLE_MIDDLE) - 1u; line > 0; line--)
        put_circle_line(&b, h.width[line]);
    uint8_t lower_lines = (uint8_t)(CIRCLE_MIDDLE + radius) < SCREEN_RIGHT ? radius : (uint8_t)(SCREEN_RIGHT - CIRCLE_MIDDLE);
    for (unsigned line = 0; line < lower_lines; line++)
        put_circle_line(&b, h.width[line]);
    put_empty_lines(&b, (uint8_t)(0u - radius - CIRCLE_MIDDLE));
    return h.steps;
}
