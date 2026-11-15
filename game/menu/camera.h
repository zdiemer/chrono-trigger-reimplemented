/* The menus: a smaller view's camera (core/view_framing.h).
 *
 * The menus' screens are drawn for the original's 224 lines, and a smaller
 * view shows part of them: the camera follows the cursor up and down, so
 * what the player points at is always in view and the rest of a screen
 * comes into view as the cursor goes to it; across, a view 240 dots wide
 * shows them whole. The world map, and a screen without a cursor, are
 * framed at the screen's middle. The naming is laid out again to fit
 * whole (menu/camera.c); the other screens laid out again down the view
 * are issue #468. */
#ifndef CT_GAME_MENU_CAMERA_H
#define CT_GAME_MENU_CAMERA_H

#include "platform/platform.h"
#include "state.h"

/* Each vblank, with the screen's layout (menu/display.c). */
void menu_frame_the_view(GameState *g, Platform *pf);

#endif
