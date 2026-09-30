/* Battle: the party's inventory and gold, as the game changes them.
 *
 * The inventory is a list of places, each an item and how many of it the
 * party holds (core's inventory_items and inventory_counts); the gold is a
 * number up to 9,999,999 (party_gold). The battle's code keeps the operations
 * on them -- whether an item is held, one more of it, one fewer, whether the
 * party has some gold, gold given and taken -- and the rest of the game asks
 * for them by number through one routine ($C1:8003: a chest's gift, an
 * event's, a shop's), the battle calling them itself (a stolen item, the
 * items and gold won at its end). The battle's item menu lists the items it
 * can use from a list the battle builds from the inventory as it starts
 * (battle_item_list) (battle/inventory.c). */
#ifndef CT_GAME_BATTLE_INVENTORY_H
#define CT_GAME_BATTLE_INVENTORY_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* No item: an empty place holds it, and adding it adds nothing. */
enum { NO_ITEM = 0x00 };

/* $C1:D005: one more of `item` for the party: where a place holds it, one
 * more there, up to 99; otherwise it takes the first empty place. How many
 * the party holds now, or 0 where nothing was added (no item, or 99 held). */
uint8_t battle_inventory_add(GameState *g, uint8_t item);

/* $C1:CFE9: whether the party holds some of `item`. */
bool battle_inventory_holds(GameState *g, uint8_t item);

/* $C1:D056: one fewer of `item`: how many are held now ($FF where that was
 * the last, and the place is emptied), or 0 where none was held. */
enum { LAST_ONE_TAKEN = 0xFF };
uint8_t battle_inventory_take(GameState *g, uint8_t item);

/* $C1:D086, $C1:D0A2, $C1:D0ED: whether the party has `amount` gold (or the
 * amount is none); `amount` given, false where the gold reached its most;
 * `amount` taken, false (and nothing taken) where the party hasn't it. */
bool battle_gold_covers(GameState *g, uint16_t amount);
bool battle_give_gold(GameState *g, uint16_t amount);
bool battle_take_gold(GameState *g, uint16_t amount);

/* $C1:CDFF: the battle's item list, built afresh from the inventory as a
 * fight starts. */
void battle_list_usable_items(GameState *g);

/* $C1:B4E9: the list's counts back into the inventory as the battle ends,
 * with the items the members chose and never used ($C1:B6D1, a member's). */
void battle_put_items_back(GameState *g);
void battle_return_chosen_item(GameState *g, uint8_t member);

#endif
