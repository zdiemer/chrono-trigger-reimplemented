/* The cartridge's data, by address: what the named resource accessors in the
 * subsystems' *_data.h headers read through, and nothing else.
 *
 * A port never reads ROM by address (CLAUDE.md, "ROM data is loaded, not
 * addressed"): it calls an accessor named for the resource, and the accessor
 * is where the address lives. This is what those accessors reach the data with
 * when the port holds the game's state rather than the machine. Today the
 * backing (runtime/port/state_io.c) reads the cartridge on the emulated bus; a
 * build that loads the resources from files backs the accessors instead, and
 * no port changes. */
#ifndef CT_GAME_ROM_H
#define CT_GAME_ROM_H

#include <stdint.h>

#include "state.h"

uint8_t rom_read8(const GameState *g, uint32_t at);
uint16_t rom_read16(const GameState *g, uint32_t at);

#endif
