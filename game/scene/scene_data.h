/* Scenes: what the scene code reads from the ROM (the asset seam, docs/PLAN.md
 * Phase 3). Every read goes through here, so a build that loads the scenes'
 * data from files maps these addresses and nothing else.
 *
 * The scenes read the data with the game's state in hand (game/rom.h). */
#ifndef CT_GAME_SCENE_DATA_H
#define CT_GAME_SCENE_DATA_H

#include "rom.h"

/* The scene programs' resource table at $FE:0000 (MD2: 73 24-bit stream
 * pointers). Entry 1 is the program itself, whose stream the boot decompresses
 * from bank $C3 by the entry's low word. */
enum { SCENE_RESOURCES = 0xFE0000, SCENE_RESOURCE_SIZE = 3, SCENE_RESOURCE_PROGRAM = 1 };
enum { SCENE_PROGRAM_STREAM_BANK = 0xC3 };

/* Bank $FE's data (the resource table and what the scene program copies out
 * of the bank by address). */
enum { SCENE_DATA_BANK = 0xFE };

/* The race's matrix code generator's table ($C3:09D9): eight words, the
 * offset each eighth of the generated routine adds to its sources. */
enum { SCENE_MATRIX_TABLE = 0xC309D9 };

/* The game's sine table at $C0:F900, as the scene program reads it: 1024
 * steps a turn, each the magnitude in 256ths (the program gives it its sign). */
enum { SCENE_SINE_TABLE = 0xC0F900, SCENE_SINE_STEPS = 0x400 };

/* The game's table of random bytes at $C0:FE00, 256 of them, which the scene
 * program steps through. */
enum { SCENE_RANDOM_TABLE = 0xC0FE00 };

/* Character graphics and pose lists, also used by the field and battle.
 * A pose has sixteen tile indices and four pairs of sprite offsets. */
static inline uint32_t scene_figure_graphics(const GameState *g, unsigned character)
{
    uint32_t at = 0xE42000 + character * 3;
    return rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16;
}

static inline uint32_t scene_figure_pose_table(const GameState *g, unsigned character)
{
    uint32_t at = 0xE42300 + character * 3;
    return rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16;
}

static inline uint16_t scene_figure_tile(const GameState *g, uint32_t at) { return rom_read16(g, at); }

static inline uint8_t scene_figure_graphics_byte(const GameState *g, uint32_t at) { return rom_read8(g, at); }

static inline uint16_t scene_figure_frames(const GameState *g, unsigned character)
{
    return rom_read16(g, 0xE42600 + character * 2);
}

static inline uint16_t scene_figure_delays(const GameState *g, unsigned character)
{
    return rom_read16(g, 0xE42800 + character * 2);
}

static inline uint8_t scene_figure_animation_byte(const GameState *g, uint16_t at)
{
    return rom_read8(g, 0xE40000 | at);
}

static inline uint16_t scene_program_stream_offset(const GameState *g)
{
    return rom_read16(g, SCENE_RESOURCES + SCENE_RESOURCE_PROGRAM * SCENE_RESOURCE_SIZE);
}

/* Where a resource lies in bank $FE, by its number in the table: its
 * pointer's low word (the program's copies of resources take the bank as
 * $FE). */
static inline uint16_t scene_resource_offset(const GameState *g, unsigned resource)
{
    return rom_read16(g, SCENE_RESOURCES + resource * SCENE_RESOURCE_SIZE);
}

/* A resource's stream, by its place in the table as the program indexes it (a
 * byte offset, three bytes an entry): its long pointer. */
static inline uint32_t scene_resource_stream(const GameState *g, uint16_t at)
{
    return rom_read16(g, SCENE_RESOURCES + at) | (uint32_t)rom_read8(g, SCENE_RESOURCES + at + 2) << 16;
}

static inline uint8_t scene_data_byte(const GameState *g, uint16_t addr)
{
    return rom_read8(g, (uint32_t)SCENE_DATA_BANK << 16 | addr);
}

static inline uint16_t scene_matrix_offset(const GameState *g, uint16_t index)
{
    return rom_read16(g, SCENE_MATRIX_TABLE + index * 2u);
}

static inline uint8_t scene_sine_magnitude(const GameState *g, uint16_t step)
{
    return rom_read8(g, SCENE_SINE_TABLE + (step & (SCENE_SINE_STEPS - 1)));
}

static inline uint8_t scene_random_byte(const GameState *g, uint8_t step) { return rom_read8(g, SCENE_RANDOM_TABLE + step); }

/* The race restores a column either from the plane map at $D2:0000 or
 * from its edited work-RAM copy. Both keep the original transfer address. */
static inline PfData scene_race_column_data(uint32_t source, uint16_t bytes)
{
    return rom_data(source, bytes);
}

/* Scene-script graphics selected for a direct upload. */
static inline PfData scene_upload_data(uint32_t source, uint32_t bytes, uint8_t mode)
{
    return rom_data_mode(source, bytes, mode);
}

#endif
