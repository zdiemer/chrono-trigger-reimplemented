/* Native work accounting. Timing keys identify authored cost estimates;
 * no operation dispatches through them. Rates live in port/time_rates.c. */
#ifndef CT_GAME_CHARGE_H
#define CT_GAME_CHARGE_H

#include <stdint.h>

#include "state.h"

/* The time of `accesses` memory accesses of the routine at `routine` (the current operation
 * included), at that routine's own
 * calibrated rate. */
void charge_as(GameState *g, uint32_t routine, uint32_t accesses);

/* Work formerly counted through a retired port's temporary memory accesses
 * on the legacy machine. Keep its calibrated beam-budget estimate without
 * reconstructing those cells, at the current operation's rate. The native
 * program charges the same work: removing comparison scaffolding must not
 * remove work from the calibrated estimate. */
void charge_legacy_accesses(GameState *g, uint32_t accesses);

/* The same estimate for former multiplier or upload-cursor accesses,
 * whose machine bus price differs from work RAM's. */
void charge_legacy_io_accesses(GameState *g, uint32_t accesses);

/* Reads of the retired operation's cartridge tables, at the selected
 * cartridge speed. No table bytes are read or retained. */
void charge_legacy_rom_accesses(GameState *g, uint32_t accesses);


/* The time of the game's block move moving `bytes` bytes: what the original
 * spends where a port fills or copies with wram_fill8, wram_fill16 or
 * wram_copy (wram.h), which take none of the port's own time. */
void charge_block_move(GameState *g, uint32_t bytes);

/* Aggregate nested work and price it at the named operation's retained rate.
 * Used for operations such as decompression whose cost spans many helpers. */
uint32_t charge_retired_begin(GameState *g);
void charge_retired_end(GameState *g, uint32_t mark, uint32_t routine, uint32_t accesses);


/* Accesses left out of the estimate: scaffolding that writes once what the
 * original writes over and over, whose time a charge already stands for. */
uint32_t charge_uncounted_begin(GameState *g);
void charge_uncounted_end(GameState *g, uint32_t mark);

int charge_time_as_begin(GameState *g, uint32_t routine);
void charge_time_as_end(GameState *g, int was);
#define TIMED_AS(g, routine, statement)                           \
    do {                                                          \
        int timed_as_was_ = charge_time_as_begin((g), (routine)); \
        statement;                                                \
        charge_time_as_end((g), timed_as_was_);                   \
    } while (0)

#endif
