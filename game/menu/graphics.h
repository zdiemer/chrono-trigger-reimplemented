/* The menus' graphics: the tile sets every menu screen draws with, and the
 * window design's tiles (game/menu/graphics.c). */
#ifndef CT_GAME_MENU_GRAPHICS_H
#define CT_GAME_MENU_GRAPHICS_H

#include <stdbool.h>

#include "platform/video.h"
#include <stdint.h>
#include "state.h"
#include "vars.h"

/* Build the menus' tile sets and send them to VRAM, load their colours, and
 * the window design the player chose. */
void menu_build_graphics(GameState *g, Platform *pf);

/* The chosen window design's tiles queued for VRAM, and its colours. */
void menu_load_window_design(GameState *g);

#endif
