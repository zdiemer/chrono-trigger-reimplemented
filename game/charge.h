/* The original's time, charged for work a port does another way.
 *
 * While the gate is byte-exact, a port consumes emulated time as an estimate:
 * its own memory accesses times one scalar, calibrated over its calls
 * (docs/RECOMP.md, "The timing gate"). Where a port does in one plain step what
 * the original does at length -- a block move done as a fill, a search done in
 * locals -- that estimate misses the original's time, and keeping the
 * original's shape so it gets counted is the wrong fix (the review's item 36).
 * These say what the time was instead. They are scaffolding: call them through
 * ORIGINAL_EFFECT (original_effects.h), and a native build has none of them.
 * runtime/port/state_io.c is the backing (runtime/port/native.h,
 * native_charge_as). */
#ifndef CT_GAME_CHARGE_H
#define CT_GAME_CHARGE_H

#include <stdint.h>

#include "original_effects.h"
#include "state.h"

/* The time of `accesses` memory accesses of the routine at `routine` (a hand
 * port registered there, the running one included), at that routine's own
 * calibrated rate. */
void charge_as(GameState *g, uint32_t routine, uint32_t accesses);

/* The time of the game's block move moving `bytes` bytes: what the original
 * spends where a port fills or copies with wram_fill8, wram_fill16 or
 * wram_copy (wram.h), which take none of the port's own time. */
void charge_block_move(GameState *g, uint32_t bytes);

/* The work of `statement` as the routine registered at `routine` doing it: a
 * dispatcher running its handlers inline, whose time follows which handlers
 * ran, has each estimated at its handler's calibrated rate
 * (runtime/port/native.h, native_time_as; for a port on the machine,
 * NATIVE_TIME_AS). `routine` is a tag, the handler's `// $BB:AAAA`, and the
 * statement runs either way. */
int charge_begin_as(GameState *g, uint32_t routine);
void charge_end(GameState *g, int was);
#define CHARGED_AS(g, routine, statement)                                              do {                                                                                 int charged_was_ = ORIGINAL_EFFECTS ? charge_begin_as((g), (routine)) : 0;         statement;                                                                       ORIGINAL_EFFECT(charge_end((g), charged_was_));                                } while (0)

#endif
