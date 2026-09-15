/* Battle: the menu window's contents and cursor sprites, and the damage
 * number sprites. */
#ifndef CT_GAME_BATTLE_MENU_WINDOW_H
#define CT_GAME_BATTLE_MENU_WINDOW_H

#include "snes/system.h"

/* battle/menu_window.c */
void battle_draw_menu_window(Snes *s);
void battle_place_menu_cursors(Snes *s);

/* battle/numbers.c */
void battle_script_build_number_sprites(Snes *s);

#endif
