/* Field mode: building object sprite graphics when an animation frame changes. */
#ifndef CT_GAME_FIELD_GRAPHICS_H
#define CT_GAME_FIELD_GRAPHICS_H

#include <stdbool.h>

#include "snes/system.h"

#include "state.h"

/* A frame's tile as its description names it: the tile's number in the
 * graphics set, and whether it is drawn mirrored left to right. */
enum { FIELD_TILE_MIRRORED = 0x4000 };

/* field/graphics.c */
void field_draw_frame_tile(GameState *g, uint16_t tile);
void field_copy_sprite_tile(Snes *s);
void field_copy_sprite_tile_flipped(Snes *s);
bool field_tile_copy_keeps_saved_y(Snes *s);

#endif
