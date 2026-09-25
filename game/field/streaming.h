/* The map scrolling in (field/streaming.c): each frame the layers take their
 * moves, and the rows and columns of the map that come into view are built
 * into the layers' staging buffers for the vblank's uploads. */
#ifndef CT_GAME_FIELD_STREAMING_H
#define CT_GAME_FIELD_STREAMING_H

#include <stdbool.h>

#include "snes/system.h"
#include "state.h"

/* Every frame, after the layers' scroll ($C0:93E1). */
void stream_the_layers(GameState *g);

/* A layer drawn into the staging buffers at a map's load, which the load
 * sends to VRAM ($C0:75E9, $C0:78EC, $C0:7CB5). */
void draw_layer_at_the_maps_load(GameState *g, unsigned layer);

/* The registered entries: the streaming, from the walk and the recentring,
 * and the drawings, from the map's load; and their guard. */
void field_stream_the_layers(Snes *s);
void field_draw_bg1_at_the_load(Snes *s);
void field_draw_bg2_at_the_load(Snes *s);
void field_draw_bg3_at_the_load(Snes *s);
bool field_streaming_can_run(Snes *s);
bool field_bg2_drawing_can_run(Snes *s);

#endif
