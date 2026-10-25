/* Field mode: a battle's start on the field (field/battle_start.c), and the
 * field after it (field/battle_return.c). */
#ifndef CT_GAME_FIELD_BATTLE_START_H
#define CT_GAME_FIELD_BATTLE_START_H

#include <stdbool.h>

#include "snes/system.h"
#include "platform/platform.h"
#include "state.h"

/* What the battle takes from the field: its ground, the party's and the
 * enemies' places, and the screen ($C0:038F). */
void field_hand_over_to_a_battle(GameState *g);

/* The battle's start ($C0:024C): the enemies on the screen found, and with
 * none the party brought back together, a frame of its walk run; otherwise
 * the field handed over and put aside. As C, with its guard's state half
 * first; STOPPED where a step is the machine's yet, the program going no
 * further there. */
typedef enum { FIELD_BATTLE_SET, FIELD_NO_BATTLE, FIELD_BATTLE_START_STOPPED } FieldBattleStart;
FieldBattleStart field_battle_set_up(GameState *g, Platform *pf);
void field_battle_begins(Snes *s);
bool field_battle_can_begin(Snes *s);
void field_after_battle(Snes *s);
bool field_can_take_up_after_battle(Snes *s);
enum { FIELD_BATTLE_BEGINS_ENTRY = 0xC0024C, FIELD_AFTER_BATTLE_ENTRY = 0xC00283 };

#endif
