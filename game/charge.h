/* The original's time, charged for work a port does another way.
 *
 * While the gate is byte-exact, a port consumes emulated time as an estimate:
 * its own memory accesses times one scalar, calibrated over its calls
 * (docs/REIMPLEMENTATION.md, "The timing gate"). Where a port does in one plain step what
 * the original does at length -- a block move done as a fill, a search done in
 * locals -- that estimate misses the original's time, and keeping the
 * original's shape so it gets counted is the wrong fix (the review's item 36).
 * These say what the time was instead. They are the original's time, not
 * scaffolding, so they are made plainly, as TIMED_AS is, never through
 * ORIGINAL_EFFECT: the native shape (the machine build without the
 * scaffolding, which must play the routes as the default build does) keeps
 * the original's pace with them, and a build with no machine times the game's
 * work with them (runtime/port/state_host.c: the charges at the routines'
 * calibrated rates, including those kept after entries retire). The
 * unpacker's (data/decompress.c), whose original spends half its time on its
 * direct page's cells, is charge_retired, since its entry retired. An
 * uncounted stretch (charge_uncounted_begin) is plain for the same reason:
 * the charge after it stands for the accesses it leaves out.
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

/* Comparison-only accesses that the native program never performed. Keep
 * their calibrated cost on the machine after deleting the temporary cells;
 * the host keeps its existing estimate of the actual C operation. */
void charge_machine_accesses(GameState *g, uint32_t accesses);
void charge_machine_io_accesses(GameState *g, uint32_t accesses);
void charge_machine_rom_accesses(GameState *g, uint32_t accesses);

/* The time of the game's block move moving `bytes` bytes: what the original
 * spends where a port fills or copies with wram_fill8, wram_fill16 or
 * wram_copy (wram.h), which take none of the port's own time. */
void charge_block_move(GameState *g, uint32_t bytes);

/* The time of the work a retired routine does -- one whose entry left the
 * registry with its subsystem's gate (the unpacker, data/decompress.c) --
 * from charge_retired_begin to charge_retired_end, with `accesses` more
 * charged as charge_as charges them. Both backends time the port's own accesses
 * and the charged ones at the rate the routine's entry was calibrated at when
 * it retired, as its estimate was made while it had one. Not
 * scaffolding: it is the routine's whole time. */
uint32_t charge_retired_begin(GameState *g);
void charge_retired_end(GameState *g, uint32_t mark, uint32_t routine, uint32_t accesses);

/* On the machine, playing the hand ports, the time charged so far passes
 * before the port goes on, as it did where the port made a call through the
 * machine: for a caller still on the gate whose call to a retired routine
 * was such a call, so that it keeps the pace it had. Nothing elsewhere. */
void charge_settle(GameState *g);

/* Accesses left out of the estimate: scaffolding that writes once what the
 * original writes over and over, whose time a charge already stands for. */
uint32_t charge_uncounted_begin(GameState *g);
void charge_uncounted_end(GameState *g, uint32_t mark);

/* The time of the work a statement does, estimated as the routine at
 * `routine`'s, for logic that runs another routine's work inline: the same
 * tag as NATIVE_TIME_AS (runtime/port/native.h) for code that has no machine.
 * A tag, never a number. Unlike the charges above it isn't scaffolding: it
 * says which routine's rate the logic's own accesses are estimated at, so the
 * native-shape build keeps it too, and runs on the machine at the default
 * build's pace but for the scaffolding's own time. Without it the statement's
 * accesses went at the rate of the port around it, and a New Game+'s start,
 * at the menus' ports' rates, took 58 frames where it takes 14 (history/reimplementation-chunks.md,
 * "The refusals in real play, retired"). A build with no machine uses the
 * same calibrated rate in its frame-level time model. */
int charge_time_as_begin(GameState *g, uint32_t routine);
void charge_time_as_end(GameState *g, int was);
#define TIMED_AS(g, routine, statement)                           \
    do {                                                          \
        int timed_as_was_ = charge_time_as_begin((g), (routine)); \
        statement;                                                \
        charge_time_as_end((g), timed_as_was_);                   \
    } while (0)

#endif
