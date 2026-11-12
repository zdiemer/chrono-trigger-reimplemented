/* What the battle's files share beyond battle.h: whether a number names a
 * combatant, and the routines their work is timed and tagged as. */
#ifndef CT_GAME_BATTLE_BRIDGE_H
#define CT_GAME_BATTLE_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/battle.h"
#include "port/vars_ram.h"
#include "state.h"
#include "vars.h"

/* The battle's direct page, where its working cells start. */
enum { BATTLE_PAGE = 0x0000 };

/* The battle's send of a buffer of work RAM to VRAM from the main thread
 * ($CD:3ED2), which queues it for the vblank handler and waits until it has
 * gone (vid_send_vram_and_wait): the routine its uploads are tagged with. */
enum { BATTLE_SEND = 0xCD3ED2 };

/* The battle's steps' routine (BattleStep, battle.h). */
enum { BATTLE_STEP = 0xC10003 };

bool battle_names_combatant(uint16_t slot);

#endif
