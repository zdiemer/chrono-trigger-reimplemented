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

/* The entries ($C2:D065 the panel, $C2:D0DB, $C2:D0E5; a save point's save
 * screen's steps $C2:D4F8 and $C2:D506; the main menu's save screen's
 * $C2:CEDC and $C2:CEFD, and the two steps both share, $C2:CF61 and
 * $C2:CF92; saving in the slot $C2:CF0A, $C2:CF37 and $C2:CF21), and their
 * guards. */
void menu_show_save_panel(Snes *s);
void menu_put_save_panel_away(Snes *s);
void menu_lay_out_save_screen_entry(Snes *s);
void menu_open_save_screen(Snes *s);
void menu_choose_on_save_screen(Snes *s);
void menu_open_saving(Snes *s);
void menu_choose_slot_to_save_in(Snes *s);
void menu_answer_whether_to_save_over(Snes *s);
void menu_close_the_question_on_a_button(Snes *s);
void menu_save_in_the_slot(Snes *s);
void menu_save_the_game(Snes *s);
void menu_show_the_slot_saved(Snes *s);
bool menu_save_panel_shows(Snes *s);
bool menu_save_screen_lays_out(Snes *s);
bool menu_saving_opens(Snes *s);
bool menu_slot_can_be_saved_in(Snes *s);
bool menu_slot_saved_shows(Snes *s);

#endif
