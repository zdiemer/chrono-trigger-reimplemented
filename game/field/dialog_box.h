/* Field mode: the dialog window laid out again for a smaller view
 * (field/dialog_box.c). */
#ifndef CT_GAME_FIELD_DIALOG_BOX_H
#define CT_GAME_FIELD_DIALOG_BOX_H

#include "core/view_framing.h"
#include "platform/platform.h"
#include "state.h"

/* The window drawn in a smaller view this frame, at the view's top or
 * bottom as it is at the screen's; returns the view's lines it leaves the
 * camera. Where the window isn't placed, all of them. */
ViewLines field_lay_out_the_dialog_box(const GameState *g, Platform *pf, VidView view);

#endif
