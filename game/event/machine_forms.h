#ifndef CT_GAME_EVENT_MACHINE_FORMS_H
#define CT_GAME_EVENT_MACHINE_FORMS_H

#include "snes/system.h"

/* A retained form's full contract, reporting a break for a known form. */
bool event_machine_form_takes(Snes *s, void (*entry)(Snes *));

#endif
