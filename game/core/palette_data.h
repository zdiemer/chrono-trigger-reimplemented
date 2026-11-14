/* What the palette animations read from the ROM: the sets of animations a
 * location starts, and the frames of the animations that copy their colors in
 * (core/palette.c).
 *
 * Every such read goes through these accessors, so a build that loads the data
 * from somewhere other than the ROM image changes this file and nothing in the
 * animations. */
#ifndef CT_GAME_CORE_PALETTE_DATA_H
#define CT_GAME_CORE_PALETTE_DATA_H

#include "rom.h"

/* A color of the palette animations' frames, which lie in bank $F6, as a
 * BGR555 word. `at` is its offset in the bank (a slot's frames, then 2 bytes a
 * color); offsets wrap within the bank. */
static inline uint16_t palette_frame_color(const GameState *g, uint16_t at)
{
    return asset_read16_at_place(g, ASSET_PALETTE_ANIMATION_FRAMES, at);
}

/* The palette animation set of the location whose record (location_records,
 * 14 bytes each at $F6:0000) is at byte offset `record`: its byte 3. */
static inline uint8_t location_palette_animation_set(const GameState *g, uint16_t record)
{
    return asset_read8(g, ASSET_LOCATION_RECORDS, 0x3u + record);
}

/* Where set `set` starts in the stream of palette animation sets: the table
 * of words at $FD:F9C7, indexed by the set times 2 taken 8-bit (so sets past
 * $7F wrap), each an offset into the stream at $FD:FA77. */
static inline uint16_t palette_animation_set_start(const GameState *g, uint8_t set)
{
    return asset_read16(g, ASSET_PALETTE_ANIMATION_SET_STARTS, (uint8_t)(set * 2));
}

/* A byte of the stream at offset `at` (16-bit), and the long address of that
 * byte, which a slot keeps as its frame table. */
static inline uint32_t palette_animation_set_address(uint16_t at)
{
    uint16_t start = (uint16_t)asset_address(ASSET_PALETTE_ANIMATION_SETS);
    return asset_place_address(ASSET_PALETTE_ANIMATION_SETS, (uint16_t)(start + at));
}

static inline uint8_t palette_animation_set_byte(const GameState *g, uint16_t at)
{
    return asset_read8(g, ASSET_PALETTE_ANIMATION_SETS, at);
}

/* A frame number of a frame table: a byte of the cartridge at `at`, a slot's
 * table plus its current frame. */
static inline uint8_t palette_frame_table_entry(const GameState *g, uint32_t at) { return rom_read8(g, at); }

#endif
