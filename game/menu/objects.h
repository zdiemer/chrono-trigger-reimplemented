/* Menu: sprite objects, their scripts and sprite drawing. */
#ifndef CT_GAME_MENU_OBJECTS_H
#define CT_GAME_MENU_OBJECTS_H

#include "snes/system.h"

/* menu/objects.c */
void menu_reset_sprite_nodes(Snes *s);
void menu_hide_all_sprites(Snes *s);
void menu_run_objects(Snes *s);
void menu_draw_sprite_nodes(Snes *s);
void menu_insert_sprite_node(Snes *s);
void menu_run_sprite_script(Snes *s);
void menu_run_object_script(Snes *s);
void menu_object_wave_hdma(Snes *s);
void menu_draw_record_sprites(Snes *s);

#endif
