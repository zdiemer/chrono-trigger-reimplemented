/* What the scenes played on the sprite-object mode's map call by address
 * from their scripts, and their objects' handlers (game/menu/map_scenes.c). */
#ifndef CT_GAME_MENU_MAP_SCENES_H
#define CT_GAME_MENU_MAP_SCENES_H

#include <stdbool.h>

#include "menu/object.h"
#include "snes/system.h"
#include "state.h"

/* Whether the running object is in view (or just out of it, its sprites
 * reaching in). */
bool menu_scene_object_in_view(GameState *g);

/* A frame of the object that drifts the map along. */
void menu_scene_drift_the_map(GameState *g, MenuObject self);

/* The registered entries and their guards. */
void menu_scene_object_in_view_entry(Snes *s);
void menu_scene_drift_the_map_entry(Snes *s);
bool menu_scene_object_in_view_runs(Snes *s);
bool menu_scene_drift_runs(Snes *s);

#endif
