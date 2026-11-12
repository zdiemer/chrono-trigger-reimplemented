/* Native accessor diagnostics and legacy wrapper conversions.
 *
 * Native GameState storage is defined privately by state_host.c. The conversion
 * helpers remain for legacy machine wrappers; native accessors never use them.
 * Page handles and the address projection are described in docs/STATE.md. */
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

/* Inspection reads (including ROM inspection) do not charge game time. */
void vars_peek(bool on);
bool vars_peeking(void);

/* Indexed accessors retain the original address on an invalid index and use
 * the native projection, without indexing outside C storage. Builds with
 * CT_ACCESSOR_CHECKS record these reaches for tests. */
typedef struct {
    const char *accessor;   /* the accessor called, e.g. "set_field_object_role" */
    const char *bound;      /* which limit: "count", "bytes", "rows", "columns", "instances" */
    unsigned index;
    unsigned limit;
    uint32_t pc;            /* reserved diagnostic address; native accessors record zero */
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
