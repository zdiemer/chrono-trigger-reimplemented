/* Field mode: building object sprite graphics when an animation frame changes. */
#ifndef CT_GAME_FIELD_GRAPHICS_H
#define CT_GAME_FIELD_GRAPHICS_H

#include <stdbool.h>

#include "snes/system.h"

/* field/graphics.c */
void field_copy_sprite_tile(Snes *s);
void field_copy_sprite_tile_flipped(Snes *s);
bool field_tile_copy_keeps_saved_y(Snes *s);

#endif
