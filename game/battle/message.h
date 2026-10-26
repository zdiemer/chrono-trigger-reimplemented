/* Battle: the battle message's text (battle/message.c). */
#ifndef CT_GAME_BATTLE_MESSAGE_H
#define CT_GAME_BATTLE_MESSAGE_H

#include "snes/system.h"
#include "platform/platform.h"
#include "state.h"

void battle_type_the_message(GameState *g);
void battle_clear_the_message_tiles(GameState *g);

void battle_type_the_message_entry(Snes *s);
/* Its guard's state half: a message being set up the menus' text code goes
 * on from. */
bool battle_message_typing_fits(const GameState *g);
enum { BATTLE_TYPE_THE_MESSAGE_ENTRY = 0xCD04DF };

/* The window naming a combatant, with its HP or without ($CD:002D,
 * $CD:0030), and a tech's or an item's description ($CD:0027), as C: false
 * where the program went no further. */
bool battle_combatant_named(GameState *g, uint8_t combatant, bool with_hp);
bool battle_described(GameState *g, uint8_t description);
/* An enemy's message shown ($CD:0033), as C. */
bool battle_show_an_enemy_message(GameState *g, Platform *pf, uint8_t message);
/* One of the battle's own messages shown, the battle held for it ($CD:0021),
 * as C. */
bool battle_show_a_battle_message(GameState *g, Platform *pf, uint8_t message);
void battle_clear_the_message_tiles_entry(Snes *s);
void battle_clear_the_message_tiles_long(Snes *s);
enum { BATTLE_CLEAR_THE_MESSAGE_TILES_ENTRY = 0xCD0235 };
bool battle_message_can_be_typed(Snes *s);

void battle_show_a_battle_message_entry(Snes *s);
void battle_show_an_enemy_message_entry(Snes *s);
void battle_name_a_combatant(Snes *s);
void battle_name_a_combatant_with_hp(Snes *s);
void battle_describe(Snes *s);
void battle_set_out_the_message(Snes *s);
void battle_pause_on_the_message(Snes *s);
void battle_open_the_message(Snes *s);
bool battle_battle_message_can_open(Snes *s);
bool battle_enemy_message_can_open(Snes *s);
bool battle_naming_can_open(Snes *s);
bool battle_description_can_open(Snes *s);
bool battle_message_string_can_open(Snes *s);
bool battle_message_can_go_on(Snes *s);

#endif
