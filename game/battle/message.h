/* Battle: the battle message's text (battle/message.c). */
#ifndef CT_GAME_BATTLE_MESSAGE_H
#define CT_GAME_BATTLE_MESSAGE_H

#include "snes/system.h"
#include "state.h"

void battle_type_the_message(GameState *g);
void battle_clear_the_message_tiles(GameState *g);

void battle_type_the_message_entry(Snes *s);
void battle_clear_the_message_tiles_entry(Snes *s);
void battle_clear_the_message_tiles_long(Snes *s);
bool battle_message_can_be_typed(Snes *s);

void battle_show_a_battle_message(Snes *s);
void battle_show_an_enemy_message(Snes *s);
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
