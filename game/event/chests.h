/* The map's chests (event/chests.c): the one before the leader, which the
 * player opens with the confirm button (event/contact.c). */
#ifndef CT_GAME_EVENT_CHESTS_H
#define CT_GAME_EVENT_CHESTS_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* What an opened chest gives the party: an item, by its number, or gold; or
 * nothing of its own (a chest already open, or one whose location's script
 * does the rest). */
typedef enum { GIFT_NOTHING, GIFT_ITEM, GIFT_GOLD } GiftKind;
typedef struct {
    GiftKind kind;
    uint16_t amount;   /* the item, or the gold */
} ChestGift;

/* The player opens the chest before the leader, if there is one and no
 * dialog is open: what it gives, which the party's inventory takes before
 * the field announces it. */
ChestGift open_the_chest_before_the_leader(GameState *g);
void announce_the_gift(GameState *g, ChestGift gift);

/* The guards' part: the tiles the leader looks at for a chest are the map's
 * (or with nobody leading any, past the map too), and the chest there, if
 * any, gives an item the inventory holds (with nobody leading, none is). */
bool chest_look_fits(const GameState *g);
bool chest_gift_fits(const GameState *g);

/* As a map's screen is set up: every tile one the leader may walk onto but
 * the location's chests', which name their chests, and the chests opened
 * before shown open ($C0:A712). */
void chests_mark_the_map(GameState *g);
enum { CHESTS_MARK_THE_MAP_ENTRY = 0xC0A712 };

#endif
