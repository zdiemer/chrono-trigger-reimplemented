/* Field mode: a smaller view's camera (core/view_framing.h).
 *
 * The field's screen already follows the party leader across the room and
 * stops at the room's edges, and its scripts pan it for a scene. A smaller
 * view shows part of that screen, and keeps on it what the player follows:
 * the leader, where the leader is on the screen and can be seen, or else
 * the screen's middle, where a scene the screen pans to plays. The dialog
 * window stays whole, at the view's top or bottom as it is at the screen's,
 * and the camera frames the leader in the lines it leaves open. */
#ifndef CT_GAME_FIELD_CAMERA_H
#define CT_GAME_FIELD_CAMERA_H

#include "platform/platform.h"
#include "state.h"

/* Each vblank, after the frame's picture is set (field/nmi.c). */
void field_frame_the_view(GameState *g, Platform *pf);

#endif
