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

/* Whether the object's sprite script reads as the language: every command it
 * can reach one of its eight. */
bool menu_sprite_script_is_well_formed(const GameState *g, MenuObject o);
/* ... and whether one of the game's sprite scripts, by its number, does. */
bool menu_given_sprite_script_is_well_formed(const GameState *g, uint8_t script);

/* Whether the list the sprites are shown on is as the mode's start lays it
 * out: every link a node or a head, and a node free to show a group with. */
bool menu_sprite_list_is_sound(const GameState *g);

#endif
