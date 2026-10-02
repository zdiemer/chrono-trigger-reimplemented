/* Scenes: what the scene code reads from the ROM (the asset seam, docs/PLAN.md
 * Phase 3). Every read goes through here, so a build that loads the scenes'
 * data from files maps these addresses and nothing else.
 *
 * The scene ports' logic reads the data with the game's state in hand, not the
 * machine (game/rom.h). The menu mode's stream tables are still read by a port
 * on the machine (menu/object_commands.c), so theirs take the machine until
 * that port is rewritten. */
#ifndef CT_GAME_SCENE_DATA_H
#define CT_GAME_SCENE_DATA_H

#include "port/machine.h"
#include "rom.h"

/* The scene programs' resource table at $FE:0000 (MD2: 73 24-bit stream
 * pointers). Entry 1 is the program itself, whose stream the boot decompresses
 * from bank $C3 by the entry's low word. */
enum { SCENE_RESOURCES = 0xFE0000, SCENE_RESOURCE_SIZE = 3, SCENE_RESOURCE_PROGRAM = 1 };
enum { SCENE_PROGRAM_STREAM_BANK = 0xC3 };

static inline uint16_t scene_program_stream_offset(const GameState *g)
{
    return rom_read16(g, SCENE_RESOURCES + SCENE_RESOURCE_PROGRAM * SCENE_RESOURCE_SIZE);
}

/* Bank $FE's data (the resource table and what the scene program copies out
 * of the bank by address). */
enum { SCENE_DATA_BANK = 0xFE };

static inline uint8_t scene_data_byte(const GameState *g, uint16_t addr)
{
    return rom_read8(g, (uint32_t)SCENE_DATA_BANK << 16 | addr);
}

/* The race's matrix code generator's table ($C3:09D9): eight words, the
 * offset each eighth of the generated routine adds to its sources. */
enum { SCENE_MATRIX_TABLE = 0xC309D9 };

static inline uint16_t scene_matrix_offset(const GameState *g, uint16_t index)
{
    return rom_read16(g, SCENE_MATRIX_TABLE + index * 2u);
}

/* The game's sine table at $C0:F900, as the scene program reads it: 1024
 * steps a turn, each the magnitude in 256ths (the program gives it its sign). */
enum { SCENE_SINE_TABLE = 0xC0F900, SCENE_SINE_STEPS = 0x400 };

static inline uint8_t scene_sine_magnitude(const GameState *g, uint16_t step)
{
    return rom_read8(g, SCENE_SINE_TABLE + (step & (SCENE_SINE_STEPS - 1)));
}

/* The game's table of random bytes at $C0:FE00, 256 of them, which the scene
 * program steps through. */
enum { SCENE_RANDOM_TABLE = 0xC0FE00 };

static inline uint8_t scene_random_byte(const GameState *g, uint8_t step) { return rom_read8(g, SCENE_RANDOM_TABLE + step); }

/* The menu mode's scene stream tables in bank C6 (MD2 resources, 24-bit
 * pointers): scene graphics at $C6:FDF0, and the streams at $C6:FEA0, all in
 * bank C3. `offset` is the byte offset the loaders index them with. */
enum { SCENE_GRAPHICS_STREAMS = 0xC6FDF0, SCENE_C3_STREAMS = 0xC6FEA0 };

static inline uint16_t scene_stream_word(Snes *s, uint32_t table, uint16_t offset)
{
    return m_read16(s, table + offset);
}

static inline uint8_t scene_stream_bank(Snes *s, uint32_t table, uint16_t offset)
{
    return m_read8(s, table + offset + 2);
}

#endif
