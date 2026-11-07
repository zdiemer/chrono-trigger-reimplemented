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
void (*overworld_machine_form(Snes *s, uint32_t routine))(Snes *s);

/* The original's call, from the objects' loop ($C2:053E), of the
 * overworld's handler at `routine`, where its form doesn't run: with what
 * the handler doesn't read of the registers declared. False, and no call made, for a routine that
 * isn't one of the overworld's handlers. */
bool overworld_call_the_handler(Snes *s, uint32_t routine);

#endif
