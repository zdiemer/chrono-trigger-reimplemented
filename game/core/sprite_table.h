/* The sprite table every mode fills and the vblank sends (core/sprite_table.c). */
#ifndef CT_GAME_CORE_SPRITE_TABLE_H
#define CT_GAME_CORE_SPRITE_TABLE_H

#include <stdint.h>

#include "platform/video.h"
#include "state.h"
#include "vars.h"

/* The table's bytes in the order the video hardware reads them: the
 * entries (oam_buffer: X, Y, tile and attributes each), then the high
 * table's two bits a sprite (oam_high_table). */
enum {
    SPRITE_TABLE_ENTRIES_BYTES = OAM_BUFFER_COUNT * VID_OAM_ENTRY_BYTES,
    SPRITE_TABLE_BYTES = SPRITE_TABLE_ENTRIES_BYTES + OAM_HIGH_TABLE_BYTES,
};

/* A byte of the table set by its place in that order, for the modes that
 * keep their cursor into it as a byte offset rather than a sprite: the
 * scenes' (which can leave it between entries) and the field's ranges. */
void sprite_table_set_byte(GameState *g, unsigned at, uint8_t v);

#endif
