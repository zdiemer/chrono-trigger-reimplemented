/* Field mode: per-frame arithmetic for the three background layers' scrolling. */
#ifndef CT_GAME_FIELD_LAYERS_H
#define CT_GAME_FIELD_LAYERS_H

#include <stdbool.h>

#include "snes/system.h"

/* A layer's edge runs: the stretches of its tile map that come into view as
 * the layer scrolls, which field/layers.c works out every frame and the
 * deferred uploads (field/display.c) send. There are four: the view's top
 * row and the row below its bottom, each from a column left of the screen
 * across the view, split where it crosses into the tile map's other half and
 * where it wraps round; and the view's left column and the column right of
 * it, each from a row above the screen down, split where it wraps round the
 * map. The original keeps them as two tables of words, the VRAM addresses
 * (field_layer_edge_vram, ten a layer) and the parts' sizes
 * (field_layer_edge_sizes, five): these say which word is which. The two
 * rows share their sizes, as do the two columns. */
enum {
    EDGE_TOP_ROW = 0,          /* three addresses: from the start, the other half, the wrap */
    EDGE_BOTTOM_ROW = 3,       /* the same three */
    EDGE_LEFT_COLUMN = 6,      /* two addresses: from the start, the wrap */
    EDGE_RIGHT_COLUMN = 8,     /* the same two */
    EDGE_ADDRESSES = 10,
};
enum {
    EDGE_ROW_SIZES = 0,        /* three sizes, the rows' parts */
    EDGE_COLUMN_SIZES = 3,     /* two, the columns' */
    EDGE_SIZES = 5,
};

/* field/layers.c */
void field_compute_layer_edges(Snes *s);
void field_update_layer_scroll(Snes *s);
void field_unpack_tile_maps(Snes *s);
bool field_unpack_stays_clear(Snes *s);

#endif
