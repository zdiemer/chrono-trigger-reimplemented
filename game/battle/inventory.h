/* Battle: the party's inventory, as the battle adds to it.
 *
 * The inventory is a list of places, each an item and how many of it the
 * party holds (core's inventory_items and inventory_counts). The battle adds
 * the items it gives -- a stolen item, the items won at its end, a chest's
 * gift on the field, which asks the battle's code for it -- one at a time
 * (battle/inventory.c). */
#ifndef CT_GAME_BATTLE_INVENTORY_H
#define CT_GAME_BATTLE_INVENTORY_H

#include <stdint.h>

#include "state.h"

/* No item: an empty place holds it, and adding it adds nothing. */
enum { NO_ITEM = 0x00 };

/* $C1:D005: one more of `item` for the party: where a place holds it, one
 * more there, up to 99; otherwise it takes the first empty place. How many
 * the party holds now, or 0 where nothing was added (no item, or 99 held). */
uint8_t battle_inventory_add(GameState *g, uint8_t item);

#endif
