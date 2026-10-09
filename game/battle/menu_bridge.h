/* Battle: the menu's clicks on the machine (battle/menu_bridge.c). The menu's
 * entries hand their logic this host, which makes each click the call the
 * original makes, from the frames it has there. */
#ifndef CT_GAME_BATTLE_MENU_BRIDGE_H
#define CT_GAME_BATTLE_MENU_BRIDGE_H

#include <stdbool.h>

#include "battle/choosing.h"
#include "snes/system.h"

typedef struct {
    MenuHost host;
    Snes *s;
    uint32_t choosing_call; /* where the menu's frame calls the choosing from; none in the choosing's own entry */
    bool stopped;         /* a click's call didn't come back: the port has stopped there */
} BattleMenuMachine;

BattleMenuMachine battle_menu_machine(Snes *s, bool inside_choosing);

/* $C1:1B55, the click routine, as the original's callers call it. */
void battle_click_cursor(Snes *s);

#endif
