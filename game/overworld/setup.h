/* The world map set up, and its map loaded again (game/overworld/setup.c). */
#ifndef CT_GAME_OVERWORLD_SETUP_H
#define CT_GAME_OVERWORLD_SETUP_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* Unpack the stream at `stream` to `to`, as the original's call at `site`
 * does; false where the unpacking didn't come back (the caller then stops). */
typedef struct {
    bool (*unpack)(void *ctx, uint32_t site, uint32_t stream, uint32_t to);
    void *ctx;
} Unpacker;

/* The world map set up after the mode's own start, up to its main loop; its
 * map loaded again after the menus or the map view; and the map's tiles,
 * which the map view loads too. Each false where an unpacking stopped. */
bool overworld_set_up(GameState *g, const Unpacker *u, Platform *pf);
bool overworld_load_the_map_again(GameState *g, const Unpacker *u, Platform *pf);
bool overworld_load_the_map_tiles(GameState *g, const Unpacker *u);

/* Whether the map can be loaded again as it stands: on a world map whose
 * record is noted, the camera on the map and the upload buffer in work RAM
 * (the game's always are once the map is set up). */
bool overworld_map_can_be_loaded_again(const GameState *g);
/* Whether the world map can be set up as it stands: a world map's location,
 * the camera put on the party on the map (the set up's guard's state half). */
bool overworld_can_be_set_up(const GameState *g);

/* Clear the label the sign's name is set in (sign.c), where the map view
 * sets its markers too. */
void overworld_clear_the_sign_label(GameState *g);

/* The unpacker on the original's machine: its entry run as the original's
 * call to it reaches it. */
Unpacker overworld_unpacker_on_the_machine(Snes *s);
/* The unpacker as C (data/decompress.h). */
Unpacker overworld_unpacker(GameState *g);

/* The registered entries and their guards. */
void overworld_set_up_entry(Snes *s);
void overworld_load_the_map_again_entry(Snes *s);
void overworld_load_the_map_tiles_entry(Snes *s);
void overworld_finish_the_graphics_entry(Snes *s);
bool overworld_graphics_finish_runs(Snes *s);
bool overworld_set_up_runs(Snes *s);
bool overworld_map_again_runs(Snes *s);
bool overworld_map_tiles_run(Snes *s);

#endif
