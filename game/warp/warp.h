/* The time warp: the tunnel the party falls through between eras
 * (warp/warp.c says how its pieces fit together). */
#ifndef CT_GAME_WARP_H
#define CT_GAME_WARP_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* ---- The picture (warp/picture.c) ---------------------------------------------------------------- */

/* The screen set up and the picture drawn: the plane's tiles and map, the
 * palette, the light's sprites and tiles. */
void warp_draw_picture(GameState *g, Platform *pf);

/* The plane's line effects (its matrix, its centre and its scroll down the
 * screen) and the circle's started, their lists pointed at their tables and the
 * plane's scales worked out. */
void warp_start_line_effects(GameState *g, Platform *pf);

/* The plane's four line effects, in the order their lists lie. */
typedef enum { WARP_PLANE_FIRST_ROW, WARP_PLANE_SECOND_ROW, WARP_PLANE_CENTRE, WARP_PLANE_SCROLL } WarpPlaneEffect;

/* ---- The circle (warp/circle.c) ------------------------------------------------------------------ */

/* The circle's two window tables, a line effect's 256 lines each. */
enum { WARP_CIRCLE_TABLES = 2, WARP_CIRCLE_TABLE_LINES = 256 };

/* Build the circle of the radius warp_circle_radius holds into the next table.
 * Returns how many times the original steps the circle's point round its
 * quarter, which it does at length with the multiplier where the port works
 * it out in locals: the time a frame charges for (warp/warp.c). */
uint16_t warp_build_circle(GameState *g);

/* The circle window's list pointed at one of its tables (0 or 1). */
void warp_show_circle_table(GameState *g, unsigned table);

/* ---- The work cells (scaffolding) ---------------------------------------------------------------- */

/* What a routine leaves in warp_work, by the cell's place in it. Scaffolding
 * only: nothing but shadow mode reads the cells. */
void warp_leave_work_byte(GameState *g, unsigned cell, uint8_t v);
void warp_leave_work_word(GameState *g, unsigned cell, uint16_t v);

/* ---- The entries (warp/warp.c) ------------------------------------------------------------------- */

void warp_set_up(Snes *s);
void warp_long_frame(Snes *s);
void warp_short_frame(Snes *s);
void warp_shortest_frame(Snes *s);
void warp_flickering_frame(Snes *s);
void warp_go_dark(Snes *s);
void warp_put_back_the_story_flags(Snes *s);
void warp_wait_for_frame(Snes *s);
void warp_wait_for_frame_loop(Snes *s);
void warp_screen_on_wait_for_vblank_loop(Snes *s);
void warp_turn_the_screen_on(Snes *s);
void warp_vblank(Snes *s);

/* The guards: the warp's own page, with the data bank reaching the I/O
 * registers and the warp's zero in place (its frames, its waits and its end);
 * the data bank alone (the screen's turning on and going dark). */
bool warp_on_its_page(Snes *s);
bool warp_bank_reaches_io(Snes *s);

#endif
