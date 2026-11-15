/* The GBA cartridge's 32 KiB of battery-backed SRAM: the game's save, where
 * the game reads and writes it, and room the program borrows past it. mGBA
 * keeps the SRAM in the .sav beside the .gba.
 *
 * The SNES's 8 KiB save is the SRAM's first 8 KiB as the .srm has it, then a
 * mark that says it is there; an SRAM without the mark (a new cartridge, or
 * mGBA's first run) holds no save, and the game starts with its save RAM
 * erased, as ct_native does with no --sram. After the mark, kept across
 * nothing, is the top of the game's work RAM, $7F:B000 on (the build's
 * VARS_FLAT_COLD_FROM; runtime/port/vars_host.h's vars_host_cold), which
 * starts afresh each time the GBA does: the coldest of it, the far end of
 * the map's event script and the menus' and scenes' buffers, under 0.5% of
 * the game's reads and writes of work RAM over the GBA's movies
 * (docs/PLATFORM.md, "The GBA build, as built"). The SRAM is on
 * an 8-bit bus, so all of it is read and written a byte at a time. */
#ifndef CT_GBA_CARTRIDGE_SAVE_H
#define CT_GBA_CARTRIDGE_SAVE_H

#include <stdint.h>

#include "platform/save.h"

/* The save's SAVE_SIZE bytes, in the SRAM: as the cartridge kept them, or
 * erased and marked where it had none. */
uint8_t *cartridge_save_open(void);

#endif
