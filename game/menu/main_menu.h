/* The main menu, menu screen 0 (game/menu/main_menu.c), and its steps on the
 * original's machine (game/menu/main_menu_bridge.c). */
#ifndef CT_GAME_MENU_MAIN_MENU_H
#define CT_GAME_MENU_MAIN_MENU_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/menu.h"
#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* The options, in the order of their icons and of the screens they open
 * (each the option's number plus one). */
typedef enum {
    MAIN_MENU_EQUIPMENT, MAIN_MENU_ITEMS, MAIN_MENU_TECHS, MAIN_MENU_SETTINGS, MAIN_MENU_EXCHANGE, MAIN_MENU_SAVING,
} MainMenuOption;

/* The menus open on the main menu: with nobody in the party they close at
 * once (false); otherwise the screens' cursors are kept or started afresh
 * and the backdrop drawn. */
bool menu_main_menu_open(GameState *g);
void menu_draw_main_menu_backdrop(GameState *g);

/* The main menu's window and icons, the option under the cursor lit; drawn
 * after each option's panel. */
void menu_draw_main_menu(GameState *g, Platform *pf);
bool menu_main_menu_drawable(const GameState *g);

/* What a frame's input does on the main menu, once the cursor has read it:
 * nothing, the cursor moved to another option, an option chosen, or the
 * menus left. */
typedef enum { MAIN_MENU_STAYED, MAIN_MENU_MOVED, MAIN_MENU_OPTION_CHOSEN, MAIN_MENU_LEFT } MainMenuChoice;
MainMenuChoice menu_main_menu_choose(const GameState *g);

/* The cursor stayed: the equipment's panel's clock goes on. */
void menu_main_menu_keep_the_time(GameState *g);

/* The cursor moved to another option: the screens' cursors started afresh
 * where the player doesn't keep them, and the tasks a screen's panel may
 * have left running stopped (a bit a task, menu.h): the description's
 * typing, the scroll bar, the records and the cursors it holds. */
void menu_main_menu_move_to(GameState *g, MainMenuOption option);
enum {
    MENU_PANELS_TASKS = 1u << MENU_DESCRIPTION_TASK | 1u << MENU_SCROLL_BAR_TASK | 1u << MENU_RECORDS_TASK
                        | 1u << MENU_HELD_CURSORS_TASK,
};

/* The option chosen: its screen opens. */
void menu_main_menu_open_option(GameState *g, Platform *pf);

/* Back from an option's screen to choosing, its tasks stopped. */
void menu_main_menu_come_back(GameState *g);

/* The steps and entries ($C2:99CE, $C2:9A1B, $C2:9A98; $C2:9B54, $C2:9B71
 * the panels; $C2:9AAD, $C2:9B8F), and their guards. */
void menu_open_main_menu(Snes *s);
void menu_choose_on_main_menu(Snes *s);
void menu_back_to_the_main_menu(Snes *s);
void menu_show_main_menu_panel(Snes *s);
void menu_put_main_menu_panel_away(Snes *s);
void menu_draw_main_menu_backdrop_entry(Snes *s);
void menu_draw_main_menu_entry(Snes *s);
bool menu_main_menu_opens(Snes *s);
bool menu_main_menu_step_runs(Snes *s);
bool menu_main_menu_comes_back(Snes *s);
bool menu_main_menu_panel_shows(Snes *s);
bool menu_main_menu_panel_goes(Snes *s);
bool menu_main_menu_draws(Snes *s);

#endif
