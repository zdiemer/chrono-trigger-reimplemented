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

#endif
