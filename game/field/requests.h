/* Field mode: the main loop's turn at the screen's changes and the requests
 * (field/requests.c). */
#ifndef CT_GAME_FIELD_REQUESTS_H
#define CT_GAME_FIELD_REQUESTS_H

#include <stdbool.h>

#include "platform/platform.h"
#include <stdint.h>
#include "state.h"

void field_step_screen_changes(GameState *g);
/* Where the pass goes from the requests: on; the field started again for a
 * new place after a location change; no further, at a request the port
 * doesn't answer (the program going no further there); or round the
 * main loop, the pass over, after a script's menu, whose fade in waited
 * its own frames. */
typedef enum { FIELD_PASS_GOES_ON, FIELD_STARTS_ANEW, FIELD_PASS_STOPS, FIELD_PASS_GOES_ROUND } FieldPass;
FieldPass field_requests_handled(GameState *g, Platform *pf);
/* The requests the port answers: a switch stepped on and a chest opened
 * inside the map, and a location change's leader one the port reads. */
bool field_requests_can_be_answered(const GameState *g);
enum { FIELD_HANDLE_REQUESTS_ENTRY = 0xC00C76 };

#endif
