/* Battle: what the drawing of the battlefield's objects and their motion read
 * from the ROM.
 *
 * Every read of cartridge data battle/draw_objects.c, battle/object_motion.c
 * and battle/effect_paths.c make goes through here, by what the data is (the
 * asset seam, docs/PLAN.md Phase 3), so a build that loads these tables from
 * somewhere else maps them in this one file. Each takes the index the game
 * keeps, and does the original's index arithmetic, wraparound included. */
#ifndef CT_GAME_BATTLE_SPRITE_DATA_H
#define CT_GAME_BATTLE_SPRITE_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "rom.h"
#include "state.h"

/* How many sprites an object's frame is made of, by the object's graphics
 * kind ($CC:F50A, a word a kind). */
static inline uint8_t battle_frame_parts(const GameState *g, uint8_t kind)
{
    return rom_read8(g, 0xCCF50Au + (uint8_t)(kind << 1));
}

/* Where an effect sprite set starts in the effect sprites, in bytes, by set
 * ($CC:F7C0, a word a set). */
static inline uint16_t battle_effect_sprite_set_start(const GameState *g, uint8_t set)
{
    return rom_read16(g, 0xCCF7C0u + (uint16_t)(set << 1));
}

/* A marker's sprite: its X and Y from the object's position, its tile and its
 * attributes ($CC:F6D4, four sprites a marker, one for each of its frames).
 * `frame_sprite` is the marker's four bytes a frame on from its first. */
typedef struct {
    int8_t x, y;
    uint8_t tile, attributes;
} MarkerSprite;

static inline MarkerSprite battle_marker_sprite(const GameState *g, uint8_t frame_sprite)
{
    uint32_t at = 0xCCF6D4u + frame_sprite;
    return (MarkerSprite){ (int8_t)rom_read8(g, at), (int8_t)rom_read8(g, at + 1), rom_read8(g, at + 2),
                           rom_read8(g, at + 3) };
}

/* How many frames each frame of a marker shows, by its kind from the first
 * ($CC:F5FD). */
static inline uint8_t battle_marker_pace(const GameState *g, uint8_t kind)
{
    return rom_read8(g, 0xCCF5FDu + kind);
}

/* The held pose's tint: the two colours it puts in a party member's palette
 * ($CC:F77B, a word each). */
static inline uint16_t battle_held_tint(const GameState *g, unsigned k)
{
    return rom_read16(g, 0xCCF77Bu + k * 2);
}

/* A scripted move's step: how far a combatant moves on each of its eight
 * steps, by move ($CC:F494 across, $CC:F594 down, eight bytes a move). */
enum { BATTLE_SCRIPTED_MOVE_STEPS = 8 };

static inline int8_t battle_scripted_move_dx(const GameState *g, uint8_t move, uint8_t step)
{
    return (int8_t)rom_read8(g, 0xCCF494u + (uint16_t)(move * BATTLE_SCRIPTED_MOVE_STEPS + step));
}
static inline int8_t battle_scripted_move_dy(const GameState *g, uint8_t move, uint8_t step)
{
    return (int8_t)rom_read8(g, 0xCCF594u + (uint16_t)(move * BATTLE_SCRIPTED_MOVE_STEPS + step));
}

/* How far the damage numbers rise on each row of their rise, by row: the
 * first table, and the second for the combatants that rise by it ($D1:5E10,
 * $D1:5E49). The first table's rows end with a marker. */
enum { BATTLE_NUMBER_RISE_END = 0x80 };

static inline uint8_t battle_number_rise_table(const GameState *g, bool second, uint8_t row)
{
    return rom_read8(g, (second ? 0xD15E49u : 0xD15E10u) + row);
}

/* The party's color cycles: where a pose's cycle starts in the colors
 * ($CC:F754, a word a pose), and the color at a place in them ($CE:0100). */
static inline uint16_t battle_color_cycle_start(const GameState *g, uint8_t pose)
{
    return rom_read16(g, 0xCCF754u + (uint8_t)(pose << 1));
}
static inline uint16_t battle_color_cycle_color(const GameState *g, uint16_t at)
{
    return (uint16_t)(rom_read8(g, 0xCE0100u + at) | rom_read8(g, 0xCE0101u + at) << 8);
}

/* The sine of an angle, 256 steps a turn: its magnitude in 256ths, from the
 * table at $C0:F900 (every fourth byte), and the sign by the half turn. */
static inline uint8_t battle_sine_magnitude(const GameState *g, uint8_t angle)
{
    return rom_read8(g, 0xC0F900u + (uint16_t)(angle << 2));
}

/* The angle (256 steps a turn) of a direction within a quarter turn, by how
 * far it goes across and down, each in steps of 8 pixels: a table of 32 rows
 * down by 32 across ($C0:F300). */
enum { BATTLE_ARCTANGENT_STEP = 8, BATTLE_ARCTANGENT_COLUMNS = 32 };

static inline uint16_t battle_arctangent_index(uint8_t across, uint8_t down)
{
    return (uint16_t)(down / BATTLE_ARCTANGENT_STEP * BATTLE_ARCTANGENT_COLUMNS + across / BATTLE_ARCTANGENT_STEP);
}
static inline uint8_t battle_arctangent(const GameState *g, uint8_t across, uint8_t down)
{
    return rom_read8(g, 0xC0F300u + battle_arctangent_index(across, down));
}

/* The facing an object moving at an angle takes ($C0:F700). */
static inline uint8_t battle_facing_for_angle(const GameState *g, uint8_t angle) { return rom_read8(g, 0xC0F700u + angle); }

#endif
