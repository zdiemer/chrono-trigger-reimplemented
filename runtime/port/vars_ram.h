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

/* A registry guard runs between the original's instructions, in the
 * machine's time, and may look at the game's state to decide whether the
 * port is the same routine there. While it does (vars_peek(true) ... false),
 * the accessors' reads take none of that time and leave the bus as it was
 * (snes_bus_peek): otherwise the time would land in shadow mode's record of
 * the original's run, which the generated blocks are checked against. The
 * cartridge's accessors (game/rom.h) peek too, so an entry's wrapper can
 * look again at ROM data its logic read. */
void vars_peek(bool on);
bool vars_peeking(void);

#endif
