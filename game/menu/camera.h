/* The menus: a smaller view's camera (core/view_framing.h).
 *
 * The menus' screens are drawn for the original's 224 lines, and a smaller
 * view shows part of them: the camera follows the cursor, so what the
 * player points at is always in view and the rest of a screen comes into
 * view as the cursor goes to it. The world map, and a screen without a
 * cursor, are framed at the screen's middle. The screens themselves laid
 * out again for the smaller view are issue #366's follow-up. */
#ifndef CT_GAME_MENU_CAMERA_H
#define CT_GAME_MENU_CAMERA_H

#include "platform/platform.h"
#include "state.h"

/* Each vblank, with the screen's layout (menu/display.c). */
void menu_frame_the_view(GameState *g, Platform *pf);

#endif
