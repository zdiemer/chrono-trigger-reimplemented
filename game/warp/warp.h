/* The time warp: the tunnel the party falls through between eras
 * (warp/warp.c says how its pieces fit together). */
#ifndef CT_GAME_WARP_H
#define CT_GAME_WARP_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
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

/* The trip a field's
 * event asks for, by its scene's number (WARP_FIRST_SCENE on), from the
 * field's end to its start again, false where a step went no further; and
 * the warp's vblank and line handlers. */
enum { WARP_FIRST_SCENE = 0x80 };
bool warp_runs(GameState *g, Platform *pf, uint8_t scene);
void warp_vblank(GameState *g, Platform *pf);
void warp_line(GameState *g, Platform *pf);

#endif
