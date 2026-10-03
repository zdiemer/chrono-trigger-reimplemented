/* The packed graphics blocks: the compressed graphics the field's tile sets
 * and the battle's effects unpack (the unpacker, data/decompress.c), named by
 * a byte, $FF for none.
 *
 * Every read of the blocks' table goes through here, so a build that loads
 * the blocks from somewhere other than the ROM image changes this file and
 * nothing that uses it. */
#ifndef CT_GAME_DATA_PACKED_GRAPHICS_H
#define CT_GAME_DATA_PACKED_GRAPHICS_H

#include <stdint.h>

#include "rom.h"
#include "state.h"

enum { NO_GRAPHICS_BLOCK = 0xFF };

/* Where a block is: a long pointer per id ($F6:2220). */
static inline uint32_t graphics_block_address(const GameState *g, uint8_t id)
{
    uint32_t at = 0xF62220u + 3u * id;
    return rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16;
}

#endif
