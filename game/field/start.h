/* Field mode: the field's state as it starts (field/start.c). */
#ifndef CT_GAME_FIELD_START_H
#define CT_GAME_FIELD_START_H

#include "snes/system.h"
#include "state.h"

void field_start_state(GameState *g);
void field_start(Snes *s);

#endif
