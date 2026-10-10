/* Field mode: a location's map, unpacked (field/map.c). */
#ifndef CT_GAME_FIELD_MAP_H
#define CT_GAME_FIELD_MAP_H

#include "snes/system.h"
#include "state.h"

/* Loading a location's map, in its three parts around the unpacker
 * (data/decompress.h) and the tile maps' unpacking (field/layers.h): what to
 * unpack, and where; what the field keeps of its header -- the layers' sizes
 * and how they move, the screens and the color math -- with where each
 * layer's map repeats and how far the screen and the leader go; and the
 * tiles the location's exits are on. */
void field_ask_to_unpack_the_map(GameState *g);
void field_take_the_map_in(GameState *g);
void field_mark_the_exits(GameState *g);

// $C0:A33B
void field_unpack_the_map(Snes *s);
enum { FIELD_UNPACK_THE_MAP_ENTRY = 0xC0A33B };
/* The map's chests and walkable tiles marked, as its screen is set up
 * (event/chests.h). */
void field_mark_the_chests(Snes *s);

#endif
