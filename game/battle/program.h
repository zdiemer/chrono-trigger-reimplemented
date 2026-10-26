/* Battle: the battle as the program fights it, with no machine under it
 * (core/program.c, docs/PLAYING.md, "The native program").
 *
 * The field asks for a battle (field/requests.c) and the battle runs to its
 * end on the main thread, its frames paced by its own vblank and line
 * handlers, which the program's table sends the interrupts to. Each is the
 * same sequence the machine's entries run, with its calls on the battle's
 * other routines made as C; where one is still only the machine's, or its
 * guard turns the state away, the program goes no further there
 * (program_needs_the_machine). */
#ifndef CT_GAME_BATTLE_PROGRAM_H
#define CT_GAME_BATTLE_PROGRAM_H

#include <stdbool.h>

#include "platform/platform.h"
#include "state.h"

/* A battle fought from the field ($C1:0000 to the return from $C1:0006):
 * the battle's memory and screen set up, the fight, and what the field keeps
 * of it handed back. False where it went no further. */
bool battle_fought(GameState *g, Platform *pf);

/* The battle's handlers, by the stubs' addresses: the vblank's, the line's,
 * and the small one its start and end wait on. */
enum { BATTLE_VBLANK_HANDLER = 0xD1E77C, BATTLE_LINE_HANDLER = 0xD1FE97, BATTLE_CLOSING_VBLANK_HANDLER = 0xD1F4C0 };
void battle_vblank(GameState *g, Platform *pf);
void battle_line(GameState *g, Platform *pf);
void battle_closing_vblank(GameState *g, Platform *pf);

/* The program going no further at `routine`, which only the machine runs
 * yet or whose guard turns the state away: false, for the caller to pass on. */
bool battle_needs_the_machine(GameState *g, uint32_t routine, const char *what);

#endif
