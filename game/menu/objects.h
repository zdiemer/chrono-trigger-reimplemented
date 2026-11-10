/* The sprite-object mode's entries on the original's machine: its objects
 * (menu/objects.c), their sprites (menu/sprites.c) and the wave
 * (menu/wave.c); the command scripts' are in menu/script_bridge.h. */
#ifndef CT_GAME_MENU_OBJECTS_H
#define CT_GAME_MENU_OBJECTS_H

#include "menu/object.h"
#include "snes/system.h"

/* menu/objects.c */
/* A handler's guard: its machine half (menu_object_handler_runs) and the
 * state half it names, which takes the running object. */
bool menu_object_handler_takes(Snes *s, bool (*takes)(const GameState *g, MenuObject self));
void menu_run_objects(Snes *s);
bool menu_objects_on_page(Snes *s);
bool menu_object_can_move(Snes *s);
bool menu_object_handler_runs(Snes *s);

/* menu/sprites.c */
void menu_reset_sprite_nodes(Snes *s);
void menu_hide_all_sprites(Snes *s);
void menu_draw_sprite_nodes(Snes *s);
bool menu_sprites_on_page(Snes *s);
bool menu_sprite_list_is_drawable(Snes *s);
bool menu_sprite_list_is_resettable(Snes *s);

/* menu/wave.c */
void menu_object_wave_hdma(Snes *s);

#endif
