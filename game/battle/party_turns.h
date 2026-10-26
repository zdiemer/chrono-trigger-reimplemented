/* Battle: what decides a party member's turn besides the player's choice
 * (battle/party_turns.c). */
#ifndef CT_GAME_BATTLE_PARTY_TURNS_H
#define CT_GAME_BATTLE_PARTY_TURNS_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "state.h"

/* A combination called off: its members' actions forgotten ($FD:A93C).
 * battle_call_off_waiting_combination (below) also closes the menu
 * of a member that waited with its action. */
void battle_call_off_combination(GameState *g);

/* A demo battle's party: ready from the start ($C1:B70E), and its actions the
 * demo's every pass ($C1:B725). */
void battle_demo_party_ready(GameState *g);
void battle_demo_next_turn(GameState *g);

/* The siblings that reach the battle's steps or a member's attack, as C,
 * each with its entry in battle/entries.h: a member's status turn ($FD:AB30),
 * its waiting action dropped ($C1:B3F9), the party's counters ($FD:AC6E), the
 * enemies knocked out ($C1:B762), the party standing down ($C1:B7F2), and a
 * combination called off while its members waited ($FD:A95F). False where the
 * program went no further. */
bool battle_member_status_turn(GameState *g, Platform *pf, uint8_t member);
bool battle_cancel_if_unable(GameState *g, Platform *pf, uint8_t member);
bool battle_party_counters(GameState *g, Platform *pf);
bool battle_see_to_knocked_out_enemies(GameState *g, Platform *pf);
bool battle_party_stands_down(GameState *g, Platform *pf);
bool battle_call_off_waiting_combination(GameState *g, Platform *pf);

#endif
