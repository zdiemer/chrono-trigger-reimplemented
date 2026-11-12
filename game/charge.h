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

/* Work the original spends on memory accesses the C keeps in locals instead:
 * `accesses` work RAM accesses at the current operation's rate, so the frame
 * takes the time the game's does (docs/TODO.md, "Per-path work counts"). */
void charge_accesses(GameState *g, uint32_t accesses);

/* The same estimate for the original's multiplier or upload-cursor accesses,
 * which cost a different time from work RAM's. */
void charge_io_accesses(GameState *g, uint32_t accesses);

/* Reads of the original's cartridge tables the port no longer makes, at the selected
 * cartridge speed. No table bytes are read or retained. */
void charge_rom_accesses(GameState *g, uint32_t accesses);


/* The time of the game's block move moving `bytes` bytes: what the original
 * spends where a port fills or copies with wram_fill8, wram_fill16 or
 * wram_copy (wram.h), which take none of the port's own time. */
void charge_block_move(GameState *g, uint32_t bytes);

/* Aggregate nested work and price it at the named operation's retained rate.
 * Used for operations such as decompression whose cost spans many helpers. */
uint32_t charge_retired_begin(GameState *g);
void charge_retired_end(GameState *g, uint32_t mark, uint32_t routine, uint32_t accesses);


/* Accesses left out of the estimate: a port that writes once what the
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
