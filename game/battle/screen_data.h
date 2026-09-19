/* Battle: what the screen effects in bank D1 read from the ROM.
 *
 * Every read the screen effects make of cartridge data goes through here, by
 * what the data is (the asset seam, docs/PLAN.md Phase 3). Each accessor takes
 * the offset the original indexes with, 16-bit wrap-around and all, so a build
 * that loads these tables from somewhere else maps them in this one file. */
#ifndef CT_GAME_BATTLE_SCREEN_DATA_H
#define CT_GAME_BATTLE_SCREEN_DATA_H

#include "port/machine.h"

/* The HP gradient: nine sets of 14 colors, one per HP level, at byte offsets
 * given per level ($D1:E9EF, indexed by level * 2). */
static inline uint16_t gradient_set_offset(Snes *s, uint16_t level_x2) { return m_read16(s, 0xD1E9EFu + level_x2); }
static inline uint16_t gradient_color(Snes *s, uint16_t offset) { return m_read16(s, 0xD0FBE2u + offset); }
static inline uint8_t gradient_color_byte(Snes *s, uint16_t offset) { return m_read8(s, 0xD0FBE2u + offset); }

/* How far each of the gradient's three palette rows is rotated, by phase. */
static inline uint8_t gradient_row_rotation(Snes *s, int row, uint16_t phase)
{
    static const uint32_t tables[3] = { 0xD1EA21u, 0xD1EA2Fu, 0xD1EA3Du };
    return m_read8(s, tables[row] + phase);
}

/* The colors the shifted palette rows start from ($D0:FD00, 48 colors). */
static inline uint16_t shift_palette_color(Snes *s, uint16_t offset) { return m_read16(s, 0xD0FD00u + offset); }

/* An effect's graphics: four compressed block ids per effect ($FF: none), the
 * blocks' compressed data pointers, the effect's 48-color palette and the
 * compressed tilemap-area graphics. */
static inline uint8_t effect_graphics_block_id(Snes *s, uint16_t index) { return m_read8(s, 0xD1ECDBu + index); }
static inline uint32_t graphics_block_pointer(Snes *s, uint16_t offset)
{
    return m_read16(s, 0xF62220u + offset) | (uint32_t)m_read8(s, 0xF62222u + offset) << 16;
}
static inline uint16_t effect_palette_offset(Snes *s, uint16_t id_x2) { return m_read16(s, 0xD1ECCFu + id_x2); }
static inline uint16_t effect_palette_color(Snes *s, uint16_t offset) { return m_read16(s, 0xD0FA00u + offset); }
static inline uint32_t effect_extra_graphics_pointer(Snes *s, uint16_t offset)
{
    return m_read8(s, 0xD0E000u + offset) | (uint32_t)m_read8(s, 0xD0E001u + offset) << 8
           | (uint32_t)m_read8(s, 0xD0E002u + offset) << 16;
}

/* The battle's sine table ($CE:F48E): 256 steps per turn, magnitudes 0-255. */
static inline uint8_t battle_sine(Snes *s, uint16_t index) { return m_read8(s, 0xCEF48Eu + index); }

/* The random bytes the battle picks from ($C0:FE00). */
static inline uint8_t battle_random_byte(Snes *s, uint16_t index) { return m_read8(s, 0xC0FE00u + index); }

/* The screen shake presets: period and rate by a 3-bit selector, and the
 * layers each axis moves by a 2-bit one. */
static inline uint8_t shake_period(Snes *s, uint16_t index) { return m_read8(s, 0xD1F69Cu + index); }
static inline uint8_t shake_rate(Snes *s, uint16_t index) { return m_read8(s, 0xD1F6A4u + index); }
static inline uint8_t shake_layers_x(Snes *s, uint16_t index) { return m_read8(s, 0xD1F6ACu + index); }
static inline uint8_t shake_layers_y(Snes *s, uint16_t index) { return m_read8(s, 0xD1F6B0u + index); }

/* The palette preset tables ($CE:F0E6): two eight-byte halves of the two
 * preset slots per preset, then the first colors and counts by selector. */
static inline uint8_t palette_preset_byte(Snes *s, uint16_t offset) { return m_read8(s, 0xCEF0E6u + offset); }
static inline uint8_t palette_preset_range(Snes *s, uint16_t offset) { return m_read8(s, 0xCEF126u + offset); }
static inline uint8_t palette_preset_step(Snes *s, uint16_t offset) { return m_read8(s, 0xCEF146u + offset); }

/* A word of a screen-effect dispatch table in bank D1 (as JSR (abs,X) reads it). */
static inline uint16_t screen_dispatch_target(Snes *s, uint16_t table, uint16_t index)
{
    return m_read16(s, 0xD10000u | (uint16_t)(table + index));
}

/* Whether the battle's debug frame advance is on: a byte of the ROM ($CD:3FFF),
 * 0 in the released game. */
static inline bool battle_frame_advance_enabled(Snes *s) { return m_read8(s, 0xCD3FFFu) != 0; }

#endif
