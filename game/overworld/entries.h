/* The overworld's machine forms, for its callers still under the gate
 * (game/overworld/entries.c). */
#ifndef CT_GAME_OVERWORLD_ENTRIES_H
#define CT_GAME_OVERWORLD_ENTRIES_H

#include <stdint.h>

#include "snes/system.h"

/* The machine form of the overworld's `routine` -- an object's handler, or a
 * routine a scene's script calls -- for a caller that runs it under the
 * original's call (the mode's object loop, the command scripts' bridge),
 * where the shipping mode runs the retired overworld as C and the machine's
 * state is one the routine's contract takes. A state the contract doesn't
 * take is a broken contract, reported. NULL then, elsewhere, and for anything
 * that isn't one of the overworld's: the caller makes the original's call. */
/* Select a named form without entering it. The call bridge must validate it. */
void (*overworld_machine_entry(uint32_t routine))(Snes *);

void (*overworld_machine_form(Snes *s, uint32_t routine))(Snes *s);

#endif
