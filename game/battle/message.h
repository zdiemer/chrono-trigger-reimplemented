/* Battle: the battle message's text (battle/message.c). */
#ifndef CT_GAME_BATTLE_MESSAGE_H
#define CT_GAME_BATTLE_MESSAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "platform/platform.h"
#include "state.h"

void battle_type_the_message(GameState *g);
void battle_clear_the_message_tiles(GameState *g);

/* Whether a message being set up is one the menus' text code goes on from. */
bool battle_message_typing_fits(const GameState *g);
enum { BATTLE_TYPE_THE_MESSAGE_ENTRY = 0xCD04DF };

/* The window naming a combatant, with its HP or without ($CD:002D,
 * $CD:0030), and a tech's or an item's description ($CD:0027): false
 * where the program went no further. */
bool battle_combatant_named(GameState *g, uint8_t combatant, bool with_hp);
bool battle_described(GameState *g, uint8_t description);
/* An enemy's message shown ($CD:0033). */
bool battle_show_an_enemy_message(GameState *g, Platform *pf, uint8_t message);
/* One of the battle's own messages shown, the battle held for it
 * ($CD:0021). */
bool battle_show_a_battle_message(GameState *g, Platform *pf, uint8_t message);
enum { BATTLE_CLEAR_THE_MESSAGE_TILES_ENTRY = 0xCD0235 };

#endif
