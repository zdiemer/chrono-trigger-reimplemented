/* The sprite-object mode's sprites (menu/sprites.c): each object's sprite
 * script, which shows its sprite groups frame by frame, and the list they are
 * drawn from. */
#ifndef CT_GAME_MENU_SPRITES_H
#define CT_GAME_MENU_SPRITES_H

#include <stdint.h>

#include "menu/object.h"
#include "state.h"

/* Give an object one of the sprite scripts by its number, from its start. */
void menu_give_sprite_script(GameState *g, MenuObject o, uint8_t script);

/* Run the running object's sprite script for this frame: it shows the
 * object's sprites where the object is. */
void menu_object_animate(GameState *g);

#endif
