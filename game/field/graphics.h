/* Field mode: building object sprite graphics when an animation frame changes. */
#ifndef CT_GAME_FIELD_GRAPHICS_H
#define CT_GAME_FIELD_GRAPHICS_H

#include <stdbool.h>

#include "field/object.h"
#include "snes/system.h"
#include "state.h"

/* A frame's tile as its description names it: the tile's number in the
 * graphics set (FIELD_TILE_NUMBER), and whether it is drawn mirrored left to
 * right. A tile is FIELD_TILE_BYTES of 4-bit pixels; the frame builder draws
 * them into bank FIELD_BUILD_BANK, and the copies keep the caller's Y in a
 * cell of the field's page (FIELD_SAVED_Y_CELL) for the original's code. */
enum { FIELD_TILE_MIRRORED = 0x4000, FIELD_TILE_NUMBER = 0x07FF, FIELD_TILE_BYTES = 32, FIELD_BUILD_BANK = 0x7F,
       FIELD_SAVED_Y_CELL = 0xC5 };

/* Where a tile named `tile` starts in its graphics set: bytes from the set's
 * start, and from its bank's (past $FFFF where the tile runs into the next
 * bank; the copies carry it as the original's reads do). */
static inline uint16_t field_tile_offset(uint16_t tile) { return (uint16_t)((tile & FIELD_TILE_NUMBER) * FIELD_TILE_BYTES); }
static inline uint32_t field_set_tile_start(ObjectGraphics set, uint16_t tile)
{
    return (uint32_t)set.address + field_tile_offset(tile);
}

/* field/graphics.c: tile `tile` of the graphics set, drawn into bank
 * FIELD_BUILD_BANK at `to`, straight or mirrored as its name says. */
void field_draw_frame_tile(GameState *g, ObjectGraphics set, uint16_t tile, uint16_t to);
void field_copy_sprite_tile(Snes *s);
void field_copy_sprite_tile_flipped(Snes *s);
bool field_tile_copy_in_build_bank(Snes *s);

#endif
