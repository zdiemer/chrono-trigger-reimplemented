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
 * hole in the abstraction is that it names no hardware: a backing that keeps the
 * game's state in its own storage still has work RAM to put these buffers in
 * (docs/PLAN.md, "The game's state as C"). */
#ifndef CT_GAME_WRAM_H
#define CT_GAME_WRAM_H

#include <stdint.h>

#include "state.h"
#include "platform/platform.h"

uint8_t wram_read8(const GameState *g, uint32_t at);
uint16_t wram_read16(const GameState *g, uint32_t at);
/* A byte of RAM a pointer in the cartridge's data names -- a script or text
 * the game copied or built in work RAM, or save RAM -- where the data's
 * accessors read on (rom.h's rom_read8): the game's own RAM, read at the time
 * the cartridge's bytes take, as the original's long reads through the
 * pointer do. */
uint8_t wram_data_read8(const GameState *g, uint32_t at);
void wram_write8(GameState *g, uint32_t at, uint8_t v);
void wram_write16(GameState *g, uint32_t at, uint16_t v);

/* Plain fills and copies, forward within the bank `at` is in (the offset
 * wraps, as the 65816's block move does). They take none of the port's own
 * time: a port charges what the original spends on the move with
 * charge_block_move (charge.h). A copy that overlaps its destination reads what
 * it has already written, as a forward block move does. */
void wram_fill8(GameState *g, uint32_t at, uint32_t bytes, uint8_t v);
void wram_fill16(GameState *g, uint32_t at, uint32_t words, uint16_t v);
void wram_copy(GameState *g, uint32_t from, uint32_t to, uint32_t bytes);

/* Clear a transition's buffer, wrapping within its bank, as the original's
 * transfer at `o` does from the Mode 7 multiplier the caller has set to zero:
 * the buffer cleared, and the transfer's time kept in the state's work
 * account. `bytes` is a count: 65536 is a full bank. */
void wram_clear(GameState *g, PfOrigin o, uint32_t at, uint32_t bytes);

/* A run of work RAM written as the work RAM port writes it: from `at` (a
 * 17-bit place in both banks), running on from bank $7E into $7F and round
 * to the start: `bytes` of one value, or a resource's `bytes` copied in. Like
 * the plain fills, they take none of the port's own time; the caller charges
 * the transfer's (tm_charge_transfer). */
void wram_port_fill(GameState *g, uint32_t at, uint32_t bytes, uint8_t v);
void wram_port_copy(GameState *g, PfData from, uint32_t at, uint32_t bytes);

#endif
