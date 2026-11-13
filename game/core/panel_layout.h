/* A wider view's arrangement of the panels: the menus' screens and the battle
 * window (core/panel_layout.c).
 *
 * A panel is a tilemap one screen across, 32 columns, that the game draws for
 * the original's screen. On a view wider than that (vid_view) a mode lays the
 * panel out across the view instead (VidMapLayout): its windows stretched to
 * the view's sides, their borders moved out and their patterns repeated
 * between, and its parts -- a list's columns, a member's name and numbers --
 * spread apart. The layout says only where the map's columns are drawn, so
 * what the panel shows, and everything the game decides, stays the
 * original's; the sprites the game draws over a panel move with it.
 *
 * A layout is built each frame, beside the game's memory, from what the panel
 * holds, and sent in the vblank with the panel. Places on it are the screen's
 * tile columns: the original's screen from 0 to 31, the left margin's
 * negative and the right one's past 31. */
#ifndef CT_GAME_CORE_PANEL_LAYOUT_H
#define CT_GAME_CORE_PANEL_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/video.h"
#include "state.h"

enum { PANEL_COLUMNS = 32, PANEL_COLUMN_DOTS = 8 };

typedef struct {
    VidMapLayout map;
    int8_t margin;   /* the columns the view shows either side of the original's screen, rounded up */
} PanelLayout;
/* Module storage supplied by the state owner, independent of mapped RAM. */
PanelLayout *panel_layout(GameState *g);

/* A run of the panel's rows. */
typedef struct {
    uint8_t first, count;
} PanelRows;

/* A layout begun for `view`: every row the original's arrangement, with
 * nothing in the margins. */
PanelLayout *panel_layout_begin(GameState *g, VidView view);

/* The map's columns from `from` on, `count` of them, shown on `rows` from
 * the screen's column `at` on. */
void panel_show_columns(PanelLayout *l, PanelRows rows, uint8_t from, uint8_t count, int8_t at);
/* Nothing shown on `rows` from the screen's column `at` on, `count` columns. */
void panel_show_nothing(PanelLayout *l, PanelRows rows, int8_t at, uint8_t count);
/* The window whose borders are the map's columns `left` and `right`, drawn
 * with its borders at the screen's columns `to_left` and `to_right`: its
 * inside's pattern, four columns round, repeated between them. */
void panel_stretch_window(PanelLayout *l, PanelRows rows, uint8_t left, uint8_t right, int8_t to_left, int8_t to_right);

/* The layout sent for the map at `map_base`, for this frame. */
void panel_layout_send(const PanelLayout *l, Platform *pf, uint16_t map_base);

#endif
