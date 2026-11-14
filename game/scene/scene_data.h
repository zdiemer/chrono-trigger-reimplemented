/* Scenes: what the scene code reads from the ROM (the asset seam, docs/PLAN.md
 * Phase 3). Every read goes through here, so a build that loads the scenes'
 * data from files maps these addresses and nothing else.
 *
 * The scenes read the data with the game's state in hand (game/rom.h). */
#ifndef CT_GAME_SCENE_DATA_H
#define CT_GAME_SCENE_DATA_H

#include <stdbool.h>

#include "rom.h"

/* The scene programs' resource table at $FE:0000 (MD2: 73 24-bit stream
 * pointers). Entry 1 is the program itself, whose stream the boot decompresses
 * from bank $C3 by the entry's low word. */
enum { SCENE_RESOURCE_SIZE = 3, SCENE_RESOURCE_PROGRAM = 1, SCENE_RESOURCE_COUNT = 73 };
enum { SCENE_PROGRAM_STREAM_BANK = 0xC3 };

/* The resources the program copies as they are (the scenes' colours, the
 * planet's order of tiles), by their number in the table: the catalogue's
 * row each lies in and where in it. The rest are streams the program unpacks
 * from where the table points (scene_resource_stream). */
typedef struct {
    bool copied;   /* the rest are left out of the list */
    AssetId asset;
    uint16_t offset;
} SceneResource;
static const SceneResource SCENE_RESOURCE_ROWS[SCENE_RESOURCE_COUNT] = {
    [18] = { true, ASSET_ATTRACT_COLORS_FE4B7A, 0x000 },
    [19] = { true, ASSET_ATTRACT_COLORS_FE4B7A, 0x020 },
    [20] = { true, ASSET_ATTRACT_COLORS_FE4B7A, 0x040 },
    [21] = { true, ASSET_ATTRACT_PLANET_TILE_ORDER, 0x000 },
    [37] = { true, ASSET_ATTRACT_DIVE_SPRITE_COLORS, 0x000 },
    [48] = { true, ASSET_ATTRACT_COLORS_FEF91E, 0x000 },
    [49] = { true, ASSET_ATTRACT_COLORS_FEF91E, 0x040 },
    [50] = { true, ASSET_ATTRACT_COLORS_FEF91E, 0x0A0 },
    [51] = { true, ASSET_ATTRACT_COLORS_FEF91E, 0x1A0 },
    [52] = { true, ASSET_ATTRACT_COLORS_FEF91E, 0x2A0 },
    [53] = { true, ASSET_ATTRACT_COLORS_FEF91E, 0x3A0 },
    [54] = { true, ASSET_ATTRACT_COLORS_FEF91E, 0x4A0 },
    [55] = { true, ASSET_ATTRACT_COLORS_FEF91E, 0x4C0 },
    [56] = { true, ASSET_ATTRACT_COLORS_FEF91E, 0x4E0 },
    [57] = { true, ASSET_ATTRACT_COLORS_FEF91E, 0x500 },
};

/* Where a resource lies in bank $FE, by its number in the table: its
 * pointer's low word, which the program's copy reads first (the copy takes
 * the bank as $FE). */
static inline uint16_t scene_resource_offset(const GameState *g, unsigned resource)
{
    return asset_read16(g, ASSET_ATTRACT_RESOURCE_PTRS, resource * SCENE_RESOURCE_SIZE);
}

/* A byte of a resource the program copies as it is, by its number in the
 * table, `at` bytes in, `from` being where the table says it lies. A
 * resource no scene copies (a stream, or a number past the table) is read
 * there, wrapping in bank $FE as the original's copy does. */
static inline uint8_t scene_resource_byte(const GameState *g, unsigned resource, uint16_t from, uint16_t at)
{
    const SceneResource *r = resource < SCENE_RESOURCE_COUNT ? &SCENE_RESOURCE_ROWS[resource] : NULL;
    if (r && r->copied)
        return asset_read8(g, r->asset, (uint32_t)r->offset + at);
    return rom_read8(g, 0xFE0000u | (uint16_t)(from + at));
}

/* The game's sine table, as the scene program reads it: 1024 steps a turn,
 * each the magnitude in 256ths (the program gives it its sign). */
enum { SCENE_SINE_STEPS = 0x400 };

/* Character graphics and pose lists, also used by the field and battle.
 * A pose has sixteen tile indices and four pairs of sprite offsets. */
static inline uint32_t scene_figure_graphics(const GameState *g, unsigned character)
{
    unsigned at = character * 3u;
    return asset_read16(g, ASSET_SETUP_CHARACTER_GRAPHICS, at) |
           (uint32_t)asset_read8(g, ASSET_SETUP_CHARACTER_GRAPHICS, at + 2) << 16;
}

static inline uint32_t scene_figure_pose_table(const GameState *g, unsigned character)
{
    unsigned at = character * 3u;
    return asset_read16(g, ASSET_SETUP_TILE_LISTS, at) | (uint32_t)asset_read8(g, ASSET_SETUP_TILE_LISTS, at + 2) << 16;
}

static inline uint16_t scene_figure_tile(const GameState *g, uint32_t at) { return rom_read16(g, at); }

static inline uint8_t scene_figure_graphics_byte(const GameState *g, uint32_t at) { return rom_read8(g, at); }

static inline uint16_t scene_figure_frames(const GameState *g, unsigned character)
{
    return asset_read16(g, ASSET_SETUP_FRAME_LISTS, character * 2u);
}

static inline uint16_t scene_figure_delays(const GameState *g, unsigned character)
{
    return asset_read16(g, ASSET_SETUP_DELAY_LISTS, character * 2u);
}

/* A byte of a figure's animations, in bank $E4 where its tables put them. */
static inline uint8_t scene_figure_animation_byte(const GameState *g, uint16_t at)
{
    return rom_read8(g, 0xE40000 | at);
}

static inline uint16_t scene_program_stream_offset(const GameState *g)
{
    return asset_read16(g, ASSET_ATTRACT_RESOURCE_PTRS, SCENE_RESOURCE_PROGRAM * SCENE_RESOURCE_SIZE);
}

/* A resource's stream, by its place in the table as the program indexes it (a
 * byte offset, three bytes an entry): its long pointer. */
static inline uint32_t scene_resource_stream(const GameState *g, uint16_t at)
{
    return asset_read16(g, ASSET_ATTRACT_RESOURCE_PTRS, at) | (uint32_t)asset_read8(g, ASSET_ATTRACT_RESOURCE_PTRS, at + 2u) << 16;
}

/* The race's matrix code generator's table: eight words, the offset each
 * eighth of the generated routine adds to its sources. */
static inline uint16_t scene_matrix_offset(const GameState *g, uint16_t index)
{
    return asset_read16(g, ASSET_SCENE_MATRIX_TABLE, index * 2u);
}

static inline uint8_t scene_sine_magnitude(const GameState *g, uint16_t step)
{
    return asset_read8(g, ASSET_SINES, step & (SCENE_SINE_STEPS - 1));
}

/* The game's table of random bytes, 256 of them, which the scene program
 * steps through. */
static inline uint8_t scene_random_byte(const GameState *g, uint8_t step) { return asset_read8(g, ASSET_RANDOM_BYTES, step); }

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
