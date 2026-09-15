/* Field mode: per-frame arithmetic for the three background layers' scrolling. */
#ifndef CT_GAME_FIELD_LAYERS_H
#define CT_GAME_FIELD_LAYERS_H

#include "snes/system.h"

/* field/layers.c */
void field_compute_layer_edges(Snes *s);
void field_update_layer_scroll(Snes *s);
void field_unpack_tile_maps(Snes *s);

#endif
