/* Field mode: building the OAM buffer from the objects' sprites. */
#ifndef CT_GAME_FIELD_SPRITES_H
#define CT_GAME_FIELD_SPRITES_H

#include "snes/system.h"

/* field/sprites.c */
void field_build_sprite_table(Snes *s);
void field_draw_object_sprites(Snes *s);
void field_update_object_sprites(Snes *s);
void field_position_sprites(Snes *s);
void field_load_and_position_sprites(Snes *s);

#endif
