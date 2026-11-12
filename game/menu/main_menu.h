#ifndef CT_GAME_MENU_MAIN_MENU_H
#define CT_GAME_MENU_MAIN_MENU_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/menu.h"
#include "platform/video.h"
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

/* The option's panel put away and the main menu drawn over the screen
 * again ($C2:9B71). */
void menu_put_main_menu_panel_away(GameState *g, Platform *pf);

/* The option chosen: its screen opens. */
void menu_main_menu_open_option(GameState *g, Platform *pf);

/* Back from an option's screen to choosing, its tasks stopped. */
void menu_main_menu_come_back(GameState *g);

/* ---- The steps --------------------------------------------------------------------------------- */

/* The options' panels, each its screen's (menu/<screen>.c), each false
 * where it went no further, and what each needs of the state. */
bool menu_show_equipment_panel(GameState *g, Platform *pf);
bool menu_show_items_panel(GameState *g, Platform *pf);
bool menu_show_techs_panel(GameState *g, Platform *pf);
bool menu_show_settings_panel(GameState *g, Platform *pf);
bool menu_equipment_panel_can_show(const GameState *g);
bool menu_items_panel_can_show(const GameState *g);
bool menu_techs_panel_can_show(const GameState *g);
bool menu_techs_panel_can_go(const GameState *g);
bool menu_settings_panel_can_show(const GameState *g);

/* What the steps need of the state. */
bool menu_main_menu_can_open(const GameState *g);
bool menu_main_menu_can_choose(const GameState *g);
bool menu_main_menu_can_come_back(const GameState *g);
bool menu_main_menu_panel_can_show(const GameState *g);

/* From the menus' main task: the steps, and the way back to the main
 * menu a screen's step takes; false where the program went no further. */
bool menu_open_main_menu(GameState *g, Platform *pf);
bool menu_choose_on_main_menu(GameState *g, Platform *pf);
bool menu_back_to_the_main_menu(GameState *g, Platform *pf);

#endif
