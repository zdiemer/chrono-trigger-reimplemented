/* Included by the blocks tools/generate generates from the ROM (docs/GENERATED.md). */
#ifndef CT_PORT_GENERATED_BLOCK_H
#define CT_PORT_GENERATED_BLOCK_H

#include <string.h>

#include "port/generated_snes.h"
#include "snes/system.h"

/* Instructions executed in generated code (native.c reports them). */
extern uint64_t generated_executed;
/* A tool's observer of every instruction, or NULL (native.h). */
extern void (*generated_insn_hook)(Snes *s, uint32_t pc24);

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
        generated_executed++;                                              \
        if (generated_insn_hook)                                           \
            generated_insn_hook(s, (uint32_t)(c)->pb << 16 | (pc16));      \
    } while (0)

/* `self` is the block's function, recorded so it resumes where it stopped. */
#define RC_STOP(self)                                                   \
    rc_stop:                                                            \
    rc_timing.stopped = (self)

/* The same for a block of RAM code, which resumes only if its guard still holds. */
#define RC_STOP_GUARDED(self, guard)                                    \
    rc_stop:                                                            \
    rc_timing.stopped = (self);                                         \
    rc_timing.stopped_guard = (guard)

/* At the start of a block of RAM code: the ranges of its instructions' bytes
 * (RcTiming.code). */
#define RC_CODE(ranges, count) (rc_timing.code = (ranges), rc_timing.code_count = (count))

/* A RAM block's guard: the `len` work RAM bytes at `wram_off` equal `image` where
 * `mask` is set (the bytes the block's code was decoded from, as opposed to
 * operands it fetches at run time). Only peeks. */
static inline bool rc_ram_matches(const Snes *s, uint32_t wram_off, const uint8_t *image, const uint8_t *mask,
                                  uint32_t len)
{
    const uint8_t *w = s->bus.wram + wram_off;
    uint32_t i = 0;
    for (; i + 8 <= len; i += 8) {
        uint64_t a, b, m;
        memcpy(&a, w + i, 8);
        memcpy(&b, image + i, 8);
        memcpy(&m, mask + i, 8);
        if ((a ^ b) & m)
            return false;
    }
    for (; i < len; i++)
        if ((w[i] ^ image[i]) & mask[i])
            return false;
    return true;
}

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
