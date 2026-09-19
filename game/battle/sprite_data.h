/* Battle: what the object drawing and motion in bank CF read from the ROM.
 *
 * Every read of cartridge data they make goes through here, by what the data
 * is (the asset seam, docs/PLAN.md Phase 3), at the index the original uses. */
#ifndef CT_GAME_BATTLE_SPRITE_DATA_H
#define CT_GAME_BATTLE_SPRITE_DATA_H

#include "port/machine.h"

/* The number of parts of an object's frame, by kind * 2 ($CC:F50A). */
static inline uint8_t battle_frame_part_count(Snes *s, uint16_t kind_x2) { return m_read8(s, 0xCCF50Au + kind_x2); }
/* Where in bank $7E an object's sprite tiles are built, by object * 2 ($CC:F84D). */
static inline uint16_t battle_object_tile_source(Snes *s, uint16_t object_x2) { return m_read16(s, 0xCCF84Du + object_x2); }
/* Where in the effect sprite data an effect sprite set starts, by set * 2 ($CC:F7C0). */
static inline uint16_t battle_effect_sprite_set_start(Snes *s, uint16_t set_x2) { return m_read16(s, 0xCCF7C0u + set_x2); }
/* A byte of a marker frame's sprite (X, Y, tile, attributes), by the frame's
 * offset ($CC:F6D4). */
static inline uint8_t battle_marker_sprite(Snes *s, uint16_t frame, uint8_t byte)
{
    return m_read8(s, 0xCCF6D4u + byte + frame);
}
/* A path's X and Y deltas, by path * 8 + step ($CC:F494, $CC:F594). */
static inline uint8_t battle_path_delta_x(Snes *s, uint16_t at) { return m_read8(s, 0xCCF494u + at); }
static inline uint8_t battle_path_delta_y(Snes *s, uint16_t at) { return m_read8(s, 0xCCF594u + at); }
/* How far the number sprites rise by row, and the second table ($D1:5E10, $D1:5E49). */
static inline uint8_t battle_number_rise_row(Snes *s, bool second, uint16_t row)
{
    return m_read8(s, (second ? 0xD15E49u : 0xD15E10u) + row);
}
/* The party effect color cycles: where a pose's cycle starts ($CC:F754, by
 * pose * 2) and its colors' bytes ($CE:0100, by a 16-bit offset plus the
 * byte). */
static inline uint16_t battle_party_cycle_start(Snes *s, uint16_t pose_x2) { return m_read16(s, 0xCCF754u + pose_x2); }
static inline uint8_t battle_party_cycle_byte(Snes *s, uint16_t at, uint8_t byte)
{
    return m_read8(s, 0xCE0100u + byte + at);
}

#endif
