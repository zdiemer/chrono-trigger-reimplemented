/* The retired core services as callers still under the gate call them on the
 * machine (core/machine_forms.c). */
#ifndef CT_GAME_CORE_MACHINE_FORMS_H
#define CT_GAME_CORE_MACHINE_FORMS_H

#include <stdbool.h>

#include "snes/system.h"

/* Whether `entry` is a retired service's machine form and the machine's state
 * is one its contract takes, for a bridge that runs a sibling as C under the
 * original's call (field/bridge.c, battle/bridge.c). A machine form given a
 * state its contract doesn't take is a broken contract, reported; false then,
 * and for anything that isn't a machine form, and the bridge makes the call on
 * the machine. */
bool core_machine_form_takes(Snes *s, void (*entry)(Snes *));

#endif
