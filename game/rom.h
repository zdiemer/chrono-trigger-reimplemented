/* The cartridge's data, by the address the original keeps it at: what the
 * named resource accessors in the subsystems' *_data.h headers read through,
 * and nothing else.
 *
 * A port never reads ROM by address (CLAUDE.md, "ROM data is loaded, not
 * addressed"): it calls an accessor named for the resource, and the accessor
 * is where the address lives. The address is a key: game/data/assets.c finds
 * the resource of the catalogue it lies in and reads that resource's bytes
 * from the store the driver supplied (data/assets.h), the asset pack in
 * ct_play. No port knows which. */
#ifndef CT_GAME_ROM_H
#define CT_GAME_ROM_H

#include <stdint.h>

#include "state.h"
#include "platform/platform.h"

uint8_t rom_read8(const GameState *g, uint32_t at);
uint16_t rom_read16(const GameState *g, uint32_t at);

/* A named graphics accessor's transfer, with no game-time charge. Validates
 * every cartridge byte, including a transfer that wraps inside its bank.
 * RAM sources retain their backend address. */
PfData rom_data(uint32_t at, uint32_t length);
/* A script-selected transfer may repeat one byte or walk backwards. */
PfData rom_data_mode(uint32_t at, uint32_t length, uint8_t mode);

/* The backing's side of each of those reads: the machine's read of the
 * cartridge at `at` where there is a machine (the state backend), with
 * the time it takes. `served` is the store's copy of the byte, which is what
 * the read gives, the machine's own checked against it; NULL where the store
 * has none (no store, or `at` not the cartridge's: work RAM a pointer in the
 * data names), when the machine's own is what it gives. A build without a
 * machine gives the store's byte, and reads its work RAM where `at` is that. */
uint8_t rom_cartridge_read(const GameState *g, uint32_t at, const uint8_t *served);

#endif
