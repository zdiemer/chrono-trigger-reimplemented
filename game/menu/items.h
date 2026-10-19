/* The items as the menus show them (game/menu/items.c). */
#ifndef CT_GAME_MENU_ITEMS_H
#define CT_GAME_MENU_ITEMS_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* An item's category and its place in it; the lookup notes both as the
 * item looked up last. The equipment's four categories are its slots too. */
typedef enum { MENU_WEAPONS, MENU_ARMOUR, MENU_HELMETS, MENU_ACCESSORIES, MENU_OTHER_ITEMS } MenuItemCategory;
enum { MENU_EQUIPMENT_SLOTS = MENU_OTHER_ITEMS };
typedef struct {
    uint8_t category, index;
} MenuItemPlace;
MenuItemPlace menu_item_place(GameState *g, uint8_t item);

/* The same, noting nothing. */
MenuItemCategory menu_item_category_of(const GameState *g, uint8_t item);

/* The item and the count at a place of the inventory (0-255): its places
 * run on past its last into the bytes after the items and the counts, which
 * the menus take as places too. */
enum { MENU_INVENTORY_PLACES = 256 };
uint8_t menu_inventory_item_at(const GameState *g, unsigned place);
uint8_t menu_inventory_count_at(const GameState *g, unsigned place);

/* The first item of each category of equipment is what a slot holds when
 * nothing is in it: no weapon (Ayla's fists), no armour, no helmet, no
 * accessory. Nobody carries one. */
enum { MENU_NO_WEAPON = 0x00, MENU_NO_ARMOUR = 0x5A, MENU_NO_HELMET = 0x7B, MENU_NO_ACCESSORY = 0x94 };
bool menu_item_is_a_slots_nothing(uint8_t item);

/* The party's items added to: `count` more of an item, in the place that
 * holds it or the first empty one, 99 at most. A slot's nothing is not
 * added, and an inventory with no empty place takes no new item. */
typedef enum { MENU_ITEMS_ADDED, MENU_ITEMS_NOTHING_TO_ADD, MENU_ITEMS_NO_ROOM } MenuItemsAdded;
MenuItemsAdded menu_add_items(GameState *g, uint8_t item, uint8_t count);

/* ... and taken from: `count` of an item, its place emptied when none is
 * left. Nothing is taken where the party has none of it, or fewer. */
typedef enum { MENU_ITEMS_TAKEN, MENU_ITEMS_NOT_HELD, MENU_ITEMS_TOO_FEW } MenuItemsTaken;
MenuItemsTaken menu_take_items(GameState *g, uint8_t item, uint8_t count);

/* How many of an item the party holds. */
uint8_t menu_items_held(GameState *g, uint8_t item);

/* Whether the character whose record is loaded (menu_load_member) can equip
 * an item; who can is noted for the windows that show it. */
bool menu_loaded_member_can_use(GameState *g, uint8_t item);

/* An item's attack or defence (menu_item_powers). An accessory has none of
 * its own: the original reads the bytes past the table the menus build, as
 * they stand (docs/TODO.md), and so does this. */
uint8_t menu_power_of_item(const GameState *g, uint8_t item);

/* Where an item's data is in bank $CC; where the item looked up last has
 * its figures. Each notes it for the item screens. */
uint16_t menu_item_data_at_of(GameState *g, uint8_t item);
uint16_t menu_item_figures_at_of_last(GameState *g);

/* As the menus open: the inventory cleaned up, the story's and the place's
 * notes, the items' tables; and the copy of the bytes after the names. */
void menu_build_item_tables(GameState *g);
void menu_copy_past_the_names_now(GameState *g);

/* The place's second note, as the menus open: the party on one of the
 * locations, not a map of the world, where the story doesn't hold the
 * place's items back. There item $C8's kind loses the bit the place takes,
 * and the main menu doesn't save (menu/saving.c). And, for a guard, whether
 * the note is one the menus' opening makes. */
bool menu_place_is_noted_as_a_location(const GameState *g);
bool menu_place_note_is_known(const GameState *g);

void menu_build_items(Snes *s);
void menu_add_operand_items(Snes *s);
void menu_take_operand_items(Snes *s);
void menu_ask_who_can_use(Snes *s);
bool menu_operand_item_is_known(Snes *s);
bool menu_loaded_character_is_known(Snes *s);
void menu_look_up_item(Snes *s);
void menu_find_item_data(Snes *s);
void menu_find_item_figures(Snes *s);
bool menu_items_are_buildable(Snes *s);
bool menu_item_figures_are_findable(Snes *s);

#endif
