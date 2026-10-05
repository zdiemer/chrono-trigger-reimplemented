/* Saving the game from the menus: the main menu's panel for it and the
 * save screen a save point opens (game/menu/saving.c). */
#ifndef CT_GAME_MENU_SAVING_H
#define CT_GAME_MENU_SAVING_H

#include <stdbool.h>

#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* The panel put away, and a save point's save screen laid out over it. */
void menu_put_save_panel_away_now(GameState *g);
void menu_lay_out_save_screen(GameState *g, Platform *pf);

/* The entries ($C2:D065 the panel, $C2:D0DB, $C2:D0E5; the save screen's
 * steps $C2:D4F8 and $C2:D506), and their guards. */
void menu_show_save_panel(Snes *s);
void menu_put_save_panel_away(Snes *s);
void menu_lay_out_save_screen_entry(Snes *s);
void menu_open_save_screen(Snes *s);
void menu_choose_on_save_screen(Snes *s);
bool menu_save_panel_shows(Snes *s);
bool menu_save_screen_lays_out(Snes *s);

#endif
