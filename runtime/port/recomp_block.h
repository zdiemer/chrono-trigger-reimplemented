/* Included by the blocks tools/recomp generates from the ROM (docs/RECOMP.md). */
#ifndef CT_PORT_RECOMP_BLOCK_H
#define CT_PORT_RECOMP_BLOCK_H

#include "port/recomp_snes.h"
#include "snes/system.h"

/* Instructions executed in generated code (native.c reports them). */
extern uint64_t recomp_executed;

/* At the start of every instruction of a block (pc16 is the instruction's
 * address in the block's bank). In ON mode, a block whose time has passed its
 * limit (the system's horizon, see snes_horizon) stops here with PC at the
 * instruction, so the system can act between instructions exactly where the
 * interpreter would; it resumes at the same instruction later (the function
 * starts where PC points). The stop continues at RC_STOP, at the end of the
 * block's function. */
#define RC_INSN(c, pc16)                                                \
    do {                                                                \
        uint32_t rc_t_ = s->bus.clocks + 6 * (c)->io_cycles;            \
        if (rc_t_ > rc_timing.limit) {                                  \
            (c)->pc = (pc16);                                           \
            goto rc_stop;                                               \
        }                                                               \
        rc_timing.insn_start = rc_t_;                                   \
        recomp_executed++;                                              \
    } while (0)

/* `self` is the block's function, recorded so it resumes where it stopped. */
#define RC_STOP(self)                                                   \
    rc_stop:                                                            \
    rc_timing.stopped = (self)

/* At a loop's back edge: outside ON mode (shadow replays), return to the
 * runtime on every pass, so each pass is checked from the loop head. In ON mode
 * the loop runs on and the head's RC_INSN stops it when time is up. */
#define RC_YIELD(c) ((void)(c), rc_timing.s == NULL)

/* After an instruction that clears I: an IRQ waiting for it must be taken
 * after this instruction, so an ON mode block stops at the next one. */
#define RC_IRQ_UNMASKED(c)                                              \
    do {                                                                \
        if (rc_timing.s && !((c)->p & CPU_FLAG_I))                      \
            rc_timing.limit = 0;                                        \
    } while (0)

#endif
