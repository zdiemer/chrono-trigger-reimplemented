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

/* Accesses left out of the estimate: scaffolding that writes once what the
 * original writes over and over, whose time a charge already stands for. */
uint32_t charge_uncounted_begin(GameState *g);
void charge_uncounted_end(GameState *g, uint32_t mark);

/* The time of the work a statement does, estimated as the routine at
 * `routine`'s, for logic that runs another routine's work inline: the same
 * tag as NATIVE_TIME_AS (runtime/port/native.h) for code that has no machine.
 * A tag, never a number. The native-shape build runs the statement alone. */
int charge_time_as_begin(GameState *g, uint32_t routine);
void charge_time_as_end(GameState *g, int was);
#ifdef CT_WITHOUT_ORIGINAL_EFFECTS
#define TIMED_AS(g, routine, statement) \
    do {                                \
        (void)(g);                      \
        statement;                      \
    } while (0)
#else
#define TIMED_AS(g, routine, statement)                           \
    do {                                                          \
        int timed_as_was_ = charge_time_as_begin((g), (routine)); \
        statement;                                                \
        charge_time_as_end((g), timed_as_was_);                   \
    } while (0)
#endif

#endif
