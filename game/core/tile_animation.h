/* Animated background tiles, shared by every game mode (core/tile_animation.c). */
#ifndef CT_GAME_CORE_TILE_ANIMATION_H
#define CT_GAME_CORE_TILE_ANIMATION_H

#include "snes/system.h"
#include "state.h"

/* Start the animated tiles of the current location's tile set. */
void tile_animations_load(GameState *g);
void tile_animation_load(Snes *s);

void tile_animation_upload(Snes *s);
bool tile_animation_upload_runs(Snes *s);

#endif
