/* The game's state with no machine under it: the backing of game/state.h,
 * vars.h, cpu_io.h, wram.h and charge.h for ct_native
 * (docs/PLAYING.md, "The native program").
 *
 * All state uses generated C records and typed buffers (vars_host.h). The
 * address view projects their fields; retired cells and padding have no storage.
 * The cartridge's 8 KiB of save RAM retains its file layout. The multiplier, divider and work RAM port work
 * out in C rather than by registers. The game's work is timed for the
 * frame-level time model (platform/time_model.h): its accesses at the rates
 * the machine calibrated, and the charges as the machine makes them. What the
 * handle is stays this file's business: game/ never sees inside it.
 *
 * An access the backing has no memory for -- a register, or anything outside
 * work RAM, save RAM and the cartridge -- is a machine assumption the build
 * found: it is counted, the first few kept, and the access reads 0. The
 * cartridge's data is read from the asset store (game/data/assets.h), with
 * port/rom_host.c the backing of game/rom.h. */
#ifndef CT_PORT_STATE_HOST_H
#define CT_PORT_STATE_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "state.h"
#include "port/vars_host.h"

/* A fresh state: work RAM as the console powers on (the original leaves it to
 * the chips; $55 throughout here, as the reference emulator has it) and save
 * RAM from `sram` (8 KiB), or erased. */
GameState *state_host_create(const uint8_t *sram);
/* The same, placed in the arena (port/arena.h) for a run that snapshots it:
 * at the same address in every run that makes the same allocations first. */
GameState *state_host_create_lasting(const uint8_t *sram);
/* The bytes of a GameState, and of the game's work RAM and save RAM in it
 * (ct_native --measure). */
size_t state_host_bytes(void);
size_t state_host_vars_bytes(void);
void state_host_destroy(GameState *g);

/* The state as bytes for a snapshot, and put back from them: the game's
 * records, save RAM, the program's own state (its handlers, the menus'
 * coroutines), the time owed. Pointers in it are the run's own; a restore is
 * good in a run laid out as the snapshot's was (port/arena.h). */
size_t state_host_snapshot_bytes(void);
void state_host_snapshot(const GameState *g, uint8_t *out);
void state_host_restore(GameState *g, const uint8_t *in);

/* The game's time since the timing backend last took it, at the front of the
 * state after its storage, so that an accessor charges its access with no
 * call. The rate is the routine's the work is timed as
 * (charge_time_as_begin), as whole clocks and thousandths of one; the work is
 * whole master clocks and thousandths, kept under MILLI_LIMIT and made whole
 * as they pass it: the same sum as their product in thousandths, in 32 bits. */
typedef struct {
    uint32_t raw_bus;         /* accesses inside a retired operation, before its rate is applied */
    unsigned uncounted;       /* nested stretches whose accesses aren't the game's own time */
    uint32_t rate;            /* the cost_milli the game's accesses are timed at now; 0, none */
    uint32_t rate_clocks, rate_milli;
    uint32_t work_clocks, work_milli;
    bool reporting;           /* the run keeps the per-routine totals (state_host_record_time) */
} StateHostAccount;
/* A state's first member, its head: the storage, then the time account. The
 * accessors reach both with no call, through C's own rule that a pointer to a
 * structure, converted, points to its first member; what they then read and
 * write are the head's own members. */
typedef struct {
    VarsHost vars;
    StateHostAccount account;
} StateHostHead;
static inline StateHostHead *state_host_head(const GameState *g) { return (StateHostHead *)(void *)(uintptr_t)g; }

/* Generated accessors only: module storage, never exposed to game logic. */
static inline VarsHost *state_host_vars(GameState *g) { return &state_host_head(g)->vars; }
static inline const VarsHost *state_host_vars_const(const GameState *g) { return &state_host_head(g)->vars; }

uint8_t state_host_read8(const GameState *g, uint32_t addr);
/* `len` bytes from `addr` on, each as state_host_read8 reads it: the sources
 * the video service's sends name by address, read in one call. */
void state_host_read_span(const GameState *g, uint32_t addr, uint8_t *out, uint32_t len);
void state_host_write8(GameState *g, uint32_t addr, uint8_t v);
/* Whether an address is the cartridge's, whose bytes the asset store serves
 * (rom_host.c) rather than the host's memory. */
bool state_host_is_cartridge(uint32_t addr);

/* The cartridge's save RAM, which the driver hands the save service's host
 * half (runtime/platform/native/host.h) and keeps. */
enum { STATE_HOST_SRAM_BYTES = 0x2000 };
uint8_t *state_host_sram(GameState *g);

/* Accesses the backing had no memory for: how many, and the first few addresses. */
enum { STATE_HOST_STRAYS_KEPT = 8 };
unsigned long state_host_strays(const GameState *g, uint32_t *first, unsigned *kept);

/* The original's time of the game's work since the last call: its own
 * accesses at the calibrated rates and what it was charged (charge.h,
 * platform/time_model.h), in master clocks, which the timing backend takes as
 * the main thread waits (TmHostProgram.owed). */
uint32_t state_host_take_owed(GameState *g);
/* An access the game made (its accessors, its reads of the cartridge, save
 * RAM through the save service), of `clocks` master clocks on the machine,
 * timed at the rate of the routine its work is timed as. */
/* An access the inline path takes: its clocks times a rate's whole clocks and
 * thousandths can't carry either sum past 2^32 while every rate is at most
 * 4,096 whole clocks (tests/test_time_rates.py holds the table to it). */
enum { STATE_HOST_MILLI_LIMIT = 0x40000000, STATE_HOST_ONE_ACCESS_MOST = 0xFFFF };
#define STATE_HOST_CLOCKS_NEAR_FULL 0xF0000000u

/* An access of `clocks` the machine's bus takes, at the current rate. */
void state_host_game_access_slowly(const GameState *g, uint32_t clocks);
static inline void state_host_game_access(const GameState *g, uint32_t clocks)
{
    StateHostAccount *a = &state_host_head(g)->account;
    if (a->uncounted || a->reporting || clocks > STATE_HOST_ONE_ACCESS_MOST ||
        a->work_milli >= STATE_HOST_MILLI_LIMIT || a->work_clocks >= STATE_HOST_CLOCKS_NEAR_FULL) {
        state_host_game_access_slowly(g, clocks);
        return;
    }
    a->raw_bus += clocks;
    a->work_clocks += clocks * a->rate_clocks;
    a->work_milli += clocks * a->rate_milli;
}
/* The clocks a cartridge read takes, which a read through a pointer the
 * data holds takes too, into RAM as into the cartridge. */
enum { STATE_HOST_CARTRIDGE_CLOCKS = 6 };

/* Optional accounting for ct_native --timing-report. Access clocks (actual
 * and legacy replacements) and charge_as clocks are kept separately;
 * milli_clocks is their calibrated time before the frame scheduler rounds
 * it. Tags count timed scopes and retired charge spans, not machine calls.
 * The untagged row has routine 0. */
typedef struct {
    uint32_t routine;
    uint64_t tags, bus_clocks, charged_clocks, milli_clocks;
} StateHostTime;
bool state_host_record_time(GameState *g);
const StateHostTime *state_host_time(const GameState *g, unsigned *count);

/* The most scenes a driver starts the game in, one after another (program_starts_in_a_scene). */
enum { STATE_HOST_FIRST_SCENES = 4 };

/* Where the program went no further than its C reaches (program_fail,
 * game/core/program.h): the routine only the machine runs, and what it is. */
bool state_host_stopped(const GameState *g, uint32_t *routine, const char **what);

#endif
