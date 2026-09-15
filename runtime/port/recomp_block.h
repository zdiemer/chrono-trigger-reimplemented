/* Included by the blocks tools/recomp generates from the ROM (docs/RECOMP.md). */
#ifndef CT_PORT_RECOMP_BLOCK_H
#define CT_PORT_RECOMP_BLOCK_H

#include "port/recomp_snes.h"
#include "snes/system.h"

/* Instructions executed in generated code (native.c reports them). */
extern uint64_t recomp_executed;
#define RC_INSN(c) ((void)(c), recomp_executed++)

/* Master clocks a block may spend before a loop's back edge returns to the
 * runtime, so time passes and interrupts arrive; 0 (shadow mode): at every
 * back edge. */
extern uint32_t recomp_yield_clocks;
#define RC_YIELD(c)                                                                                          \
    (recomp_yield_clocks == 0 ||                                                                             \
     ((SnesBus *)(c)->bus.ctx)->clocks + 6 * (c)->io_cycles >= recomp_yield_clocks)

#endif
