/* Battle: what decides a party member's turn besides the player's choice
 * (battle/party_turns.c). */
#ifndef CT_GAME_BATTLE_PARTY_TURNS_H
#define CT_GAME_BATTLE_PARTY_TURNS_H

#include "state.h"

/* A combination called off: its members' actions forgotten ($FD:A93C).
 * battle_call_off_waiting_combination (battle/entries.h) also closes the menu
 * of a member that waited with its action. */
void battle_call_off_combination(GameState *g);

/* A demo battle's party: ready from the start ($C1:B70E), and its actions the
 * demo's every pass ($C1:B725). */
void battle_demo_party_ready(GameState *g);
void battle_demo_next_turn(GameState *g);

/* The siblings that reach the battle's steps or a member's attack are entries
 * (battle/entries.h): a member's status turn, its waiting action dropped, the
 * party's counters, the enemies knocked out, the party standing down. */

#endif
