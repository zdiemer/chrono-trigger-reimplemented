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

/* field/graphics.c: tile `tile` of the graphics set, drawn into bank
 * FIELD_BUILD_BANK at `to`, straight or mirrored as its name says. */
void field_draw_frame_tile(GameState *g, ObjectGraphics set, uint16_t tile, uint16_t to);
void field_copy_sprite_tile(Snes *s);
void field_copy_sprite_tile_flipped(Snes *s);
bool field_tile_copy_in_build_bank(Snes *s);

#endif
