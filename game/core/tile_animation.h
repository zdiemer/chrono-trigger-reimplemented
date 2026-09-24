/* Animated background tiles, shared by every game mode (core/tile_animation.c). */
#ifndef CT_GAME_CORE_TILE_ANIMATION_H
#define CT_GAME_CORE_TILE_ANIMATION_H

#include "snes/system.h"

void tile_animation_upload(Snes *s);
bool tile_animation_upload_runs(Snes *s);

#endif
