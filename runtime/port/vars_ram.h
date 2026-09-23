/* The machine as a GameState: the bridge a registered entry's wrapper crosses.
 *
 * While the game's state lives in emulated work RAM (runtime/port/vars_ram.c),
 * a GameState is the machine that holds it, and nothing more -- the handle is
 * never dereferenced, in game/ or here, so `struct GameState` has no definition
 * and one machine is one state. A wrapper takes the `Snes *` the runtime calls
 * it with, turns it into a GameState and calls the port's logic, which is where
 * the machine stops. Where the routine's cells are on a page the caller
 * chooses, the wrapper also turns the caller's D into the instance of that page
 * kind the accessors take (port/vars_pages.h, generated with them).
 *
 * When the second backing arrives this header goes with it (docs/PLAN.md,
 * "Phase 3a"): game/ never includes it outside an entry wrapper. */
#ifndef CT_PORT_VARS_RAM_H
#define CT_PORT_VARS_RAM_H

#include "port/vars_pages.h"
#include "snes/system.h"
#include "state.h"

static inline GameState *state_of(Snes *s) { return (GameState *)s; }

/* The reads take a `const GameState *` because they don't change the game's
 * state; a bus read does change the machine (open bus, the clock), so the
 * machine comes back unqualified. */
static inline Snes *state_snes(const GameState *g) { return (Snes *)(void *)g; }

#endif
