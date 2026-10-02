/* Battle: what the battle puts right as it ends (battle/battle_end.c). */
#ifndef CT_GAME_BATTLE_BATTLE_END_H
#define CT_GAME_BATTLE_BATTLE_END_H

#include "state.h"

void battle_clear_party_statuses(GameState *g);
void battle_stand_party_up(GameState *g);
void battle_keep_party_records(GameState *g);
void battle_take_enemies_away(GameState *g);
void battle_keep_standing_enemies(GameState *g);
void battle_count_enemies_out(GameState *g);
void battle_note_beaten_enemies(GameState *g);

#endif
