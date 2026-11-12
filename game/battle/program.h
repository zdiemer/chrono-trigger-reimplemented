/* Battle: the battle as the program fights it (core/program.c,
 * docs/PLAYING.md, "The native program").
 *
 * The field asks for a battle (field/requests.c) and the battle runs to its
 * end on the main thread, its frames paced by its own vblank and line
 * handlers, which the program's table sends the interrupts to. A state the
 * port doesn't take is a broken contract, which stops the program there
 * (program_breaks_a_contract). */
#ifndef CT_GAME_BATTLE_PROGRAM_H
#define CT_GAME_BATTLE_PROGRAM_H

#include <stdbool.h>

#include "platform/platform.h"
#include "state.h"

/* A battle fought from the field ($C1:0000 to the return from $C1:0006):
 * the battle's memory and screen set up, the fight, and what the field keeps
 * of it handed back. False where it went no further. */
bool battle_fought(GameState *g, Platform *pf);

void battle_vblank(GameState *g, Platform *pf);
void battle_line(GameState *g, Platform *pf);
void battle_closing_vblank(GameState *g, Platform *pf);

/* Report a broken state contract: false, for the caller to pass on. */
bool battle_contract_broken(GameState *g, uint32_t routine, const char *what);

#endif
