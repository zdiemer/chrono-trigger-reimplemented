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

/* Accessors called with an index past what re/ram.toml declares: an element
 * past an array's count, a byte past a buffer's, a row or column past a
 * table's, an instance past a page kind's. Such an access reaches whatever
 * variable follows in work RAM, which works only while the storage is the
 * original's layout, so the backing counts it, per variable, and lets it
 * happen as it did. Only a build with CT_ACCESSOR_CHECKS counts (ct_dev,
 * ct_fuzz and the tests; not ct_play): without it vars_range_faults gives
 * none. ct_dev prints an `accessor out of range` line for each variable with
 * a count, and tools/suite.py fails a movie that prints one (docs/DISASSEMBLY.md,
 * "The accessors"). */
typedef struct {
    const char *accessor;   /* the accessor called, e.g. "set_field_object_role" */
    const char *bound;      /* which limit: "count", "bytes", "rows", "columns", "instances" */
    unsigned index;
    unsigned limit;
    uint32_t pc;            /* the running entry's address, or the machine's PC */
} VarsRangeHit;
enum { VARS_RANGE_HITS_KEPT = 4 };
typedef struct {
    const char *var;
    unsigned long count;
    unsigned kept;          /* how many of first[] are filled */
    VarsRangeHit first[VARS_RANGE_HITS_KEPT];
} VarsRangeFault;
const VarsRangeFault *vars_range_faults(unsigned *count);
void vars_range_reset(void);

#endif
