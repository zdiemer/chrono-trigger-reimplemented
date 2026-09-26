/* The items as the menus show them (game/menu/items.c). */
#ifndef CT_GAME_MENU_ITEMS_H
#define CT_GAME_MENU_ITEMS_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* An item's category (0 weapons, 1 armour, 2 helmets, 3 accessories, 4 the
 * rest) and its place in it; the lookup notes both as the item looked up
 * last. */
typedef struct {
    uint8_t category, index;
} MenuItemPlace;
MenuItemPlace menu_item_place(GameState *g, uint8_t item);

/* Where an item's data is in bank $CC; where the item looked up last has
 * its figures. Each notes it for the item screens. */
uint16_t menu_item_data_at_of(GameState *g, uint8_t item);
uint16_t menu_item_figures_at_of_last(GameState *g);

/* As the menus open: the inventory cleaned up, the story's and the place's
 * notes, the items' tables; and the copy of the bytes after the names. */
void menu_build_item_tables(GameState *g);
void menu_copy_past_the_names_now(GameState *g);

void menu_build_items(Snes *s);
void menu_look_up_item(Snes *s);
void menu_find_item_data(Snes *s);
void menu_find_item_figures(Snes *s);
bool menu_items_are_buildable(Snes *s);
bool menu_item_figures_are_findable(Snes *s);

#endif
