/* The menus' graphics: the tile sets every menu screen draws with, and the
 * window design's tiles (game/menu/graphics.c). */
#ifndef CT_GAME_MENU_GRAPHICS_H
#define CT_GAME_MENU_GRAPHICS_H

#include <stdbool.h>

#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* Build the menus' tile sets and send them to VRAM, load their colours, and
 * the window design the player chose. */
void menu_build_graphics(GameState *g, Platform *pf);

/* The chosen window design's tiles queued for VRAM, and its colours. */
void menu_load_window_design(GameState *g);

/* What the graphics leave on the machine, for a caller that runs them as C. */
void menu_leave_graphics_built(Snes *s);

void menu_build_graphics_entry(Snes *s);
void menu_load_window_design_entry(Snes *s);
bool menu_graphics_are_buildable(Snes *s);
bool menu_window_design_is_loadable(Snes *s);

#endif
