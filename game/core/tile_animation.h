/* Animated background tiles, shared by every game mode (core/tile_animation.c). */
#ifndef CT_GAME_CORE_TILE_ANIMATION_H
#define CT_GAME_CORE_TILE_ANIMATION_H

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* Start the animated tiles of the current location's tile set. */
void tile_animations_load(GameState *g);
void tile_animation_load(Snes *s);

/* A frame of them: the count moved on and this frame's half of the slots
 * sent, as the line handlers do after the picture ($FD:E022). */
void tile_animations_send(GameState *g, Platform *pf);
enum { TILE_ANIMATIONS_SEND_ENTRY = 0xFDFFFD };
void tile_animation_upload(Snes *s);
bool tile_animation_upload_runs(Snes *s);

#endif
