/* Work RAM the game locates at run time: the escape from game/vars.h.
 *
 * Some of what the game reads and writes is not a variable with a name but a
 * buffer whose address the game itself computes -- the circle's per-line window
 * table, whose address and bank are in its own cells ($C3:0077). vars.h cannot
 * name it, so a port reaches it here, by address, with the bank in the top byte
 * as the 65816 writes it.
 *
 * Every use is a buffer the RAM map hasn't typed, so every use is a finding, and
 * the list is meant to empty: `grep -rn wram_ game/` is the list. Prefer a name
 * in re/ram.toml whenever the address is fixed. What keeps this from being a
 * hole in the abstraction is that it names no machine: a backing that keeps the
 * game's state in its own storage still has work RAM to put these buffers in
 * (docs/PLAN.md, "Phase 3a"). */
#ifndef CT_GAME_WRAM_H
#define CT_GAME_WRAM_H

#include <stdint.h>

#include "state.h"

uint8_t wram_read8(const GameState *g, uint32_t at);
uint16_t wram_read16(const GameState *g, uint32_t at);
void wram_write8(GameState *g, uint32_t at, uint8_t v);
void wram_write16(GameState *g, uint32_t at, uint16_t v);

#endif
