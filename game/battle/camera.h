/* Battle: a smaller view's camera (battle/camera.c). */
#ifndef CT_GAME_BATTLE_CAMERA_H
#define CT_GAME_BATTLE_CAMERA_H

#include "platform/platform.h"
#include "state.h"

/* Each vblank, with the window's layout (battle/uploads.c). */
void battle_frame_the_view(GameState *g, Platform *pf);

#endif
