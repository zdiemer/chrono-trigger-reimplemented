/* What the field's code reads from the ROM, apart from the map and event data.
 *
 * Every such read goes through an accessor here, named after what the data is
 * for, so a build that loads the data from somewhere other than the ROM image
 * changes this file and nothing that uses it. The accessors take the game's
 * state (game/rom.h), not the machine. */
#ifndef CT_GAME_FIELD_DATA_H
#define CT_GAME_FIELD_DATA_H

#include <stdint.h>

#include "rom.h"
#include "state.h"

/* The wave a waving layer's scroll follows: signed 16-bit horizontal offsets,
 * a word each, at $C0:FF30. `at` is the original's byte index; the line
 * effects read 16 words 4 bytes apart from an even start of at most $3E, so
 * the table runs to $C0:FFAB. */
static inline uint16_t field_wave_offset(const GameState *g, uint16_t at) { return rom_read16(g, 0xC0FF30u + at); }

/* The bit-reversed value of every byte, at $C0:FD00: a sprite tile mirrored
 * left to right, a byte (8 pixels of one bit plane) at a time. */
static inline uint8_t field_mirrored_byte(const GameState *g, uint8_t b) { return rom_read8(g, 0xC0FD00u + b); }

/* Sprite tile graphics, wherever a graphics set's pointer puts them: the ROM's
 * banks $D2-$D5, or graphics built in bank $7F. `at` is a 24-bit address,
 * carried into the next bank as the original's indexed reads carry. */
static inline uint8_t field_sprite_graphics_byte(const GameState *g, uint32_t at) { return rom_read8(g, at); }
static inline uint16_t field_sprite_graphics_word(const GameState *g, uint32_t at) { return rom_read16(g, at); }

/* An animation's frame numbers, four entries an animation, in the bank its
 * object's frame table pointer names; $FF ends the animation. */
static inline uint8_t field_animation_frame_entry(const GameState *g, uint32_t at) { return rom_read8(g, at); }

/* An animation's frame delays in bank $E4, at the object's delay table plus
 * animation * 4 + frame; 0 ends the animation. */
enum { FIELD_ANIMATION_DELAY_BANK = 0xE4 };
static inline uint8_t field_animation_delay(const GameState *g, uint16_t at)
{
    return rom_read8(g, (uint32_t)FIELD_ANIMATION_DELAY_BANK << 16 | at);
}

/* A direction's angle within its quadrant, at $C0:F300, looked up by the two
 * distances packed into an index (the across distance in the low five bits,
 * the along one above). */
static inline uint8_t field_quadrant_angle(const GameState *g, uint16_t index) { return rom_read8(g, 0xC0F300u + index); }

/* The velocity factor of each of the 256 directions, at $C0:F800: the
 * magnitude on one axis, which the caller signs by the half circle. */
static inline uint8_t field_direction_factor(const GameState *g, uint8_t direction)
{
    return rom_read8(g, 0xC0F800u + direction);
}

/* 256 random directions, at $C0:FE00, which a blocked object tries in turn. */
static inline uint8_t field_random_direction(const GameState *g, uint8_t index) { return rom_read8(g, 0xC0FE00u + index); }

#endif
