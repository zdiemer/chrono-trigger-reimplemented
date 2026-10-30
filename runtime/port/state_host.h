/* The game's state with no machine under it: the backing of game/state.h,
 * vars.h, cpu_io.h, wram.h, charge.h and original_frame.h for ct_native
 * (docs/PLAYING.md, "The native program").
 *
 * The layout is the original's, as the RAM-backed accessors have it
 * (runtime/port/vars_ram.c, built with CT_VARS_HOST): work RAM's 128 KiB and the
 * cartridge's 8 KiB of save RAM held in the host's memory and reached at the
 * original's addresses, and the multiplier, divider and work RAM port worked
 * out in C rather than by registers. Of the time a port is charged, a load's is
 * kept for the frame-level time model (platform/time_model.h); the original's
 * frames have nothing to stand in for. What the
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
#include <stdint.h>

#include "state.h"

/* A fresh state: work RAM as the console powers on (the original leaves it to
 * the chips; $55 throughout here, as the reference emulator has it) and save
 * RAM from `sram` (8 KiB), or erased. */
GameState *state_host_create(const uint8_t *sram);
void state_host_destroy(GameState *g);

uint8_t state_host_read8(const GameState *g, uint32_t addr);
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

/* The original's time a load was charged since the last call (charge.h,
 * platform/time_model.h), in master clocks, which the timing backend takes as
 * the main thread waits (TmHostProgram.owed). */
uint32_t state_host_take_owed(GameState *g);

/* The most scenes a driver starts the game in, one after another (program_starts_in_a_scene). */
enum { STATE_HOST_FIRST_SCENES = 4 };

/* Where the program went no further than its C reaches (program_needs_the_machine,
 * game/core/program.h): the routine only the machine runs, and what it is. */
bool state_host_stopped(const GameState *g, uint32_t *routine, const char **what);

#endif
