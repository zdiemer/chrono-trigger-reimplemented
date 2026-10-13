/* What the animated tiles read from the ROM: the sets of animations a
 * location's tile set starts (core/tile_animation.c).
 *
 * Every such read goes through these accessors, so a build that loads the data
 * from somewhere other than the ROM image changes this file and nothing in the
 * animations. */
#ifndef CT_GAME_CORE_TILE_ANIMATION_DATA_H
#define CT_GAME_CORE_TILE_ANIMATION_DATA_H

#include "rom.h"

/* The sets are numbered by a tile set's low six bits: the set of the location
 * whose record (location_records, 14 bytes each at $F6:0000) is at byte offset
 * `record` is its tile set's, the record's byte 1. */
enum { TILE_ANIMATION_SET_BITS = 0x3F };

static inline uint8_t location_tile_animation_set(const GameState *g, uint16_t record)
{
    return rom_read8(g, 0xF60001u + record) & TILE_ANIMATION_SET_BITS;
}

/* Where set `set` starts in the stream of sets: a word per set at $FD:F290,
 * an offset into the stream at $FD:F310. */
static inline uint16_t tile_animation_set_start(const GameState *g, uint8_t set)
{
    return rom_read16(g, 0xFDF290u + (uint8_t)((set & TILE_ANIMATION_SET_BITS) * 2));
}

/* A byte of the stream at offset `at`; the offset is added to the stream's
 * address as a long index, carrying into the next bank. */
static inline uint8_t tile_animation_set_byte(const GameState *g, uint16_t at) { return rom_read8(g, 0xFDF310u + at); }

#endif
