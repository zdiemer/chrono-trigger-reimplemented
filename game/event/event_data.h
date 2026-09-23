/* What the event commands read from the ROM: small tables in bank $C0, which
 * the original reaches through a data bank of $00 (its upper half is the
 * start of the ROM's first bank). Every read goes through an accessor named
 * for what the table is, so a build that loads them from elsewhere changes
 * this file and nothing that uses it. */
#ifndef CT_GAME_EVENT_DATA_H
#define CT_GAME_EVENT_DATA_H

#include <stdint.h>

#include "rom.h"
#include "state.h"

/* The byte with bit n set ($C0:FF20), and the byte with every bit but n set
 * ($C0:FF28), for n = 0-7; the scripts' bit numbers index them directly, so a
 * larger one reads what follows the table. */
static inline uint8_t bit_mask(const GameState *g, uint8_t n) { return rom_read8(g, 0xC0FF20u + n); }
static inline uint8_t bit_clear_mask(const GameState *g, uint8_t n) { return rom_read8(g, 0xC0FF28u + n); }

/* The game's 256 random bytes ($C0:FE00), which it steps through. */
static inline uint8_t random_byte(const GameState *g, uint8_t i) { return rom_read8(g, 0xC0FE00u + i); }

/* The facing (0-3) an object moving in direction d takes ($C0:F700): the
 * directions are angles, a byte to a turn. */
static inline uint8_t facing_for_direction(const GameState *g, uint8_t d) { return rom_read8(g, 0xC0F700u + d); }

/* The address in bank $C0 of the handler of each of the 256 commands ($C0:5D6E). */
static inline uint16_t command_handler(const GameState *g, uint8_t command)
{
    return rom_read16(g, 0xC05D6Eu + 2u * command);
}

#endif
