/* The menus: a smaller view's camera (core/view_framing.h).
 *
 * The menus' screens are drawn for the original's 224 lines, and a smaller
 * view shows part of them: where a screen has no layout of its own, the
 * camera follows the cursor up and down, so what the player points at is
 * always in view and the rest of a screen comes into view as the cursor
 * goes to it; across, a view 240 dots wide
 * shows them whole. The world map, and a screen without a cursor, are
 * framed at the screen's middle. Every windowed screen is laid out again
 * down the view as runs of its lines, starting and ending only between rows
 * of text (menu/camera.c); where one window's insides still have more lines
 * than the view, they show the part round the cursor (issue #468). */
#ifndef CT_GAME_MENU_CAMERA_H
#define CT_GAME_MENU_CAMERA_H

#include "platform/platform.h"
#include "state.h"

/* Each vblank, with the screen's layout (menu/display.c). */
void menu_frame_the_view(GameState *g, Platform *pf);

#endif
