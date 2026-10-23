/* Field mode: the main loop's turn at the screen's changes and the requests
 * (field/requests.c). */
#ifndef CT_GAME_FIELD_REQUESTS_H
#define CT_GAME_FIELD_REQUESTS_H

#include <stdbool.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

void field_step_screen_changes(GameState *g);
void field_handle_requests(Snes *s);
/* As C: where the pass goes from the requests, a location change starting
 * the field again for the new place, and a request only the machine answers
 * yet stopping it (the program going no further there). */
typedef enum { FIELD_PASS_GOES_ON, FIELD_STARTS_ANEW, FIELD_PASS_STOPS } FieldPass;
FieldPass field_requests_handled(GameState *g, Platform *pf);
bool field_requests_can_run(Snes *s);

#endif
