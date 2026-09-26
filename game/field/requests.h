/* Field mode: the main loop's turn at the screen's changes and the requests
 * (field/requests.c). */
#ifndef CT_GAME_FIELD_REQUESTS_H
#define CT_GAME_FIELD_REQUESTS_H

#include <stdbool.h>

#include "snes/system.h"
#include "state.h"

void field_step_screen_changes(GameState *g);
void field_handle_requests(Snes *s);
bool field_requests_can_run(Snes *s);

#endif
