/* The dialog window (event/dialog.c), for what opens one other than the
 * scripts' commands: the field's own messages (a chest's, event/chests.c). */
#ifndef CT_GAME_EVENT_DIALOG_H
#define CT_GAME_EVENT_DIALOG_H

#include <stdbool.h>
#include <stdint.h>

#include "field/object.h"
#include "state.h"

/* Whether a dialog window is open, or opening. */
bool dialog_on_screen(const GameState *g);

/* The field opens one of its own messages, `line` of them, for the speaker,
 * in a window where the game decides; no script waits on it. */
void field_message_opens(GameState *g, FieldObject speaker, uint8_t line);

#endif
