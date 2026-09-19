/* What the palette animations read from the ROM: the sets of animations a
 * location starts, and the frames of the animations that copy their colors in
 * (types 1 and 8, core/palette.c).
 *
 * Every such read goes through these accessors, so a build that loads the data
 * from somewhere other than the ROM image changes this file and nothing in the
 * animations. */
#ifndef CT_GAME_CORE_PALETTE_DATA_H
#define CT_GAME_CORE_PALETTE_DATA_H

#include "port/machine.h"

/* A color of a palette animation's frames, which lie in bank $F6, as a BGR555
 * word. `at` is the original's address in the bank: a slot's frames address
 * plus frame * colors * 2 plus 2 per color, 16-bit. */
static inline uint16_t palette_frame_color(Snes *s, uint16_t at) { return m_read16(s, 0xF60000u + at); }

/* The palette animation set of the location whose record (location_records,
 * 14 bytes each at $F6:0000) is at byte offset `record`: its byte 3. */
static inline uint8_t location_palette_animation_set(Snes *s, uint16_t record)
{
    return m_read8(s, 0xF60003u + record);
}

/* Where set `set` starts in the stream of palette animation sets: the table
 * of words at $FD:F9C7, indexed by the set times 2 taken 8-bit (so sets past
 * $7F wrap), each an offset into the stream at $FD:FA77. */
static inline uint16_t palette_animation_set_start(Snes *s, uint8_t set)
{
    return m_read16(s, 0xFDF9C7u + (uint8_t)(set * 2));
}

/* A byte of the stream at offset `at` (16-bit), and the long address of that
 * byte, which a slot keeps as its frame table. */
static inline uint32_t palette_animation_set_address(uint16_t at) { return 0xFD0000u | (uint16_t)(0xFA77u + at); }
static inline uint8_t palette_animation_set_byte(Snes *s, uint16_t at) { return m_read8(s, 0xFDFA77u + at); }

#endif
