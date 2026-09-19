/* Scenes: what the scene code reads from the ROM (the asset seam, docs/PLAN.md
 * Phase 3). Every read goes through here, so a build that loads the scenes'
 * data from files maps these addresses and nothing else. */
#ifndef CT_GAME_SCENE_DATA_H
#define CT_GAME_SCENE_DATA_H

#include "port/machine.h"

/* The scene programs' resource table at $FE:0000 (MD2: 73 24-bit stream
 * pointers). Entry 1 is the program itself, whose stream the boot decompresses
 * from bank $C3 by the entry's low word. */
enum { SCENE_RESOURCES = 0xFE0000, SCENE_RESOURCE_SIZE = 3, SCENE_RESOURCE_PROGRAM = 1 };
enum { SCENE_PROGRAM_STREAM_BANK = 0xC3 };

static inline uint16_t scene_program_stream_offset(Snes *s)
{
    return m_read16(s, SCENE_RESOURCES + SCENE_RESOURCE_PROGRAM * SCENE_RESOURCE_SIZE);
}

/* Bank $FE's data (the resource table and what the scene program copies out
 * of the bank by address). */
enum { SCENE_DATA_BANK = 0xFE };

static inline uint8_t scene_data_byte(Snes *s, uint16_t addr)
{
    return m_read8(s, (uint32_t)SCENE_DATA_BANK << 16 | addr);
}

/* The race's matrix code generator's table ($C3:09D9): eight words, the
 * offset each eighth of the generated routine adds to its sources. */
enum { SCENE_MATRIX_TABLE = 0xC309D9 };

static inline uint16_t scene_matrix_table_word(Snes *s, uint16_t index)
{
    return m_read16(s, SCENE_MATRIX_TABLE + index * 2u);
}

#endif
