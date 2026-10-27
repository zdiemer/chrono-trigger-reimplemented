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

/* The option's panel put away and the main menu drawn over the screen
 * again ($C2:9B71). */
void menu_put_main_menu_panel_away(GameState *g, Platform *pf);

/* The option chosen: its screen opens. */
void menu_main_menu_open_option(GameState *g, Platform *pf);

/* Back from an option's screen to choosing, its tasks stopped. */
void menu_main_menu_come_back(GameState *g);

/* ---- The steps, one sequence for the machine and for C ---------------------------------------- */

/* The main menu's steps and the options' panels are each one sequence, which
 * the machine's entries (main_menu_bridge.c) and the program as C both run;
 * what differs is what they ask of the menus, through MainMenuSteps.
 *
 * The places the sequences call out or wait from (the machine's table gives
 * the original's call at each). */
typedef enum {
    MAIN_MENU_AT_THE_OPENING_PANEL,      /* the panel the menu opens on */
    MAIN_MENU_AT_THE_NEW_OPTION_PANEL,   /* the panel of the option the cursor moved to */
    MAIN_MENU_AT_THE_NEW_OPTION_TICK,    /* ... and its tick */
    MAIN_MENU_AT_THE_EQUIPMENT_LAID_OUT, /* the equipment's panel: its list laid out */
    MAIN_MENU_AT_THE_EQUIPMENT_DRAWN,    /* ... its members drawn */
    MAIN_MENU_AT_THE_ITEMS_LAID_OUT,     /* the items' panel: laid out */
    MAIN_MENU_AT_THE_ITEMS_DRAWN,        /* ... its rows drawn */
    MAIN_MENU_AT_THE_TECHS_LAID_OUT,     /* the techs' panel: laid out */
    MAIN_MENU_AT_THE_SETTINGS_FRAMED,    /* the settings' panel: its frame drawn */
    MAIN_MENU_PLACES
} MainMenuPlace;

/* How a step ends: on to choosing as the screen fades in, the menus left,
 * or the cursor's tick as the step's last. */
typedef enum { MAIN_MENU_FADES_IN, MAIN_MENU_LEAVES_THE_MENUS, MAIN_MENU_TICKS_AS_IT_ENDS } MainMenuEnd;

/* Where the machine leaves what the original's own tests left (scaffolding),
 * with the value the sequence had then. */
typedef enum {
    MAIN_MENU_NOTE_OPENING,             /* before the menu opens */
    MAIN_MENU_NOTE_TECHS_ROW_FOLDED,    /* the techs' row folded into its three */
    MAIN_MENU_NOTE_MOVED_FROM,          /* the cursor moved, from this option */
    MAIN_MENU_NOTE_OPTION_PICKED,       /* this option's panel picked */
    MAIN_MENU_NOTE_DRAWN,               /* the main menu drawn over its panel */
    MAIN_MENU_NOTE_PANEL_PUT_AWAY,      /* this option's panel put away */
    MAIN_MENU_NOTE_EQUIPMENT_TESTED,    /* the equipment's panel, its member row before */
    MAIN_MENU_NOTE_TECHS_MEMBER_CHOSEN, /* the techs' panel laid out, its member row before */
    MAIN_MENU_NOTE_SETTINGS_FRAMED,     /* the settings' panel framed */
} MainMenuNote;

/* What the sequences ask: the cursor read, a tick, a frame's wait, the
 * panel shown with the main menu over it, an option's own panel, the
 * equipment's member card, a note, and the step's end. Each false where it
 * went no further. */
typedef struct {
    void (*read_cursor)(void *ctx);
    bool (*tick)(void *ctx, MainMenuPlace at);
    bool (*wait)(void *ctx, MainMenuPlace at);
    bool (*show_the_panel)(void *ctx, MainMenuPlace at);
    bool (*option_panel)(void *ctx, MainMenuOption option);
    bool (*member_card)(void *ctx);
    void (*note)(void *ctx, MainMenuNote note, uint8_t value);
    bool (*ends)(void *ctx, MainMenuEnd end);
} MainMenuSteps;

/* The steps ($C2:99CE the opening, $C2:9A1B choosing, $C2:9A98 back from a
 * screen) and the panel's showing ($C2:9B54), through `n`. */
bool menu_open_main_menu_with(GameState *g, Platform *pf, const MainMenuSteps *n, void *ctx);
bool menu_choose_on_main_menu_with(GameState *g, Platform *pf, const MainMenuSteps *n, void *ctx);
bool menu_back_to_the_main_menu_with(GameState *g, Platform *pf, const MainMenuSteps *n, void *ctx);
bool menu_show_main_menu_panel_with(GameState *g, Platform *pf, const MainMenuSteps *n, void *ctx);

/* The options' panels, each its screen's (menu/<screen>.c), and what each
 * takes of the state (its entry's guard's state half). */
bool menu_show_equipment_panel_with(GameState *g, Platform *pf, const MainMenuSteps *n, void *ctx);
bool menu_show_items_panel_with(GameState *g, Platform *pf, const MainMenuSteps *n, void *ctx);
bool menu_show_techs_panel_with(GameState *g, Platform *pf, const MainMenuSteps *n, void *ctx);
bool menu_show_settings_panel_with(GameState *g, Platform *pf, const MainMenuSteps *n, void *ctx);
bool menu_equipment_panel_can_show(const GameState *g);
bool menu_items_panel_can_show(const GameState *g);
bool menu_techs_panel_can_show(const GameState *g);
bool menu_techs_panel_can_go(const GameState *g);
bool menu_settings_panel_can_show(const GameState *g);

/* What the steps take of the state, as their entries' guards do. */
bool menu_main_menu_can_open(const GameState *g);
bool menu_main_menu_can_choose(const GameState *g);
bool menu_main_menu_can_come_back(const GameState *g);
bool menu_main_menu_panel_can_show(const GameState *g);

/* As C, from the menus' main task: the steps, and the way back to the main
 * menu a screen's step takes; false where the program went no further. */
bool menu_open_main_menu(GameState *g, Platform *pf);
bool menu_choose_on_main_menu(GameState *g, Platform *pf);
bool menu_back_to_the_main_menu(GameState *g, Platform *pf);

/* The machine's side (main_menu_bridge.c): its answers to the sequences,
 * which the panels' entries run with too, on a context of this shape. */
typedef struct {
    Snes *s;
    uint8_t techs_row, techs_scroll;
} MainMenuOnTheMachine;
extern const MainMenuSteps menu_main_menu_on_the_machine;

/* The panels' scaffolding, each in its screen's bridge. */
void menu_leave_equipment_panel_tested(Snes *s, uint8_t row_before);
void menu_leave_techs_panel_member_chosen(Snes *s, uint8_t row);
void menu_leave_settings_panel_frame(Snes *s);

/* The entries ($C2:99CE, $C2:9A1B, $C2:9A98; $C2:9B54, $C2:9B71 the panels;
 * $C2:9AAD, $C2:9B8F), and their guards. */
void menu_open_main_menu_entry(Snes *s);
void menu_choose_on_main_menu_entry(Snes *s);
void menu_back_to_the_main_menu_entry(Snes *s);
void menu_show_main_menu_panel(Snes *s);
void menu_put_main_menu_panel_away_entry(Snes *s);
void menu_draw_main_menu_backdrop_entry(Snes *s);
void menu_draw_main_menu_entry(Snes *s);
bool menu_main_menu_opens(Snes *s);
bool menu_main_menu_step_runs(Snes *s);
bool menu_main_menu_comes_back(Snes *s);
bool menu_main_menu_panel_shows(Snes *s);
bool menu_main_menu_panel_goes(Snes *s);
bool menu_main_menu_draws(Snes *s);

#endif
