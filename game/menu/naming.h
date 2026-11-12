#ifndef CT_GAME_MENU_NAMING_H
#define CT_GAME_MENU_NAMING_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/cursor.h"
#include "platform/video.h"
#include "state.h"
#include "vars.h"

/* The screen opens on the naming; or, at a new game's first, on the battle
 * mode first, the character noted among those named either way. */
bool menu_naming_asks_the_battle_mode_first(GameState *g);
void menu_naming_open(GameState *g, Platform *pf);

/* The battle mode's screen: drawn, the mode on the cursor described, and
 * the mode chosen. */
void menu_naming_open_the_battle_mode(GameState *g, Platform *pf);
void menu_naming_describe_the_battle_mode(GameState *g);
void menu_naming_choose_the_battle_mode(GameState *g);

/* Once the naming has opened, it waits for any button before it takes the
 * cursor. */
bool menu_naming_waits(const GameState *g);
void menu_naming_take_the_cursor(GameState *g);

/* A frame of the naming: the cursor moved about the grid (whether it
 * moved); a letter typed or taken back, and the name drawn again; the
 * name's cursor placed and the name shown. */
bool menu_naming_move_on_the_grid(GameState *g);
bool menu_naming_keeps_the_named_ones_record(const GameState *g);

/* The grid's cursor, its column's x and its row's y as a word; the grid's
 * first column and row stand at these. */
uint16_t menu_naming_cursor_place(const GameState *g);
enum { MENU_NAME_GRID_LEFT = 0x24, MENU_NAME_GRID_TOP = 0x53 };
typedef enum { NAMING_NOTHING_TYPED, NAMING_LETTER_TYPED, NAMING_LETTER_TAKEN_BACK } NamingTyped;
NamingTyped menu_naming_type(GameState *g);
void menu_naming_draw_the_name(GameState *g, Platform *pf);
void menu_naming_show_the_name(GameState *g);
void menu_naming_place_the_names_cursor(GameState *g);

/* The letters' grid written out as a string for the text drawer. */
void menu_naming_write_out_the_grid(GameState *g);

/* Start accepts the name: kept, checked against the others', and given
 * where nobody has it and anything is left of it. */
bool menu_naming_started(const GameState *g);
void menu_naming_keep_the_name(GameState *g);
bool menu_naming_someone_has_it(GameState *g);
bool menu_naming_give_the_name(GameState *g);
bool menu_naming_name_given(const GameState *g);

/* The naming names one of the seven characters or the Epoch; and its state
 * is whole: the grid's cursor within the grid, the name typed no longer than
 * five letters (what the steps need). */
bool menu_naming_names_one(const GameState *g);
bool menu_naming_is_whole(const GameState *g);

/* ---- The screen's steps ---- */

/* The screen's steps, as its table has them (menu/screens.c), and the
 * routines of the naming they call as their own. */
typedef enum {
    NAMING_OPEN_THE_SCREEN, NAMING_NAME_THE_CHARACTER, NAMING_CHOOSE_BATTLE_MODE, NAMING_OPEN_THE_NAMING,
    NAMING_WAIT_TO_NAME,
} NamingStep;
typedef enum {
    NAMING_MOVE_ON_THE_GRID,
    NAMING_TYPE_ON_THE_GRID,
    NAMING_ACCEPT_THE_NAME,
    NAMING_CHECK_THE_NAME,
} NamingRoutine;

/* How a step ends where it isn't back in the menus' main task by its return:
 * the screen's fade in started, the screen faded out, or the menus left. */
typedef enum { NAMING_FADES_IN, NAMING_FADES_OUT, NAMING_LEAVES_THE_MENUS } NamingEnd;

/* What the steps do that is the menus' own: the cursor read; a sound, from
 * the call at `site`; a routine of the naming run as its own call; and the
 * step's end. Each false where it went no further. */
typedef struct {
    void (*read_cursor)(void *ctx);
    bool (*sound)(void *ctx, uint32_t site, MenuSound sound);
    bool (*routine)(void *ctx, NamingRoutine routine);
    bool (*ends)(void *ctx, NamingEnd end);
} NamingSteps;

/* A step or a routine, through `n`; true where it came back. */
bool menu_naming_step_with(GameState *g, Platform *pf, NamingStep step, const NamingSteps *n, void *ctx);
bool menu_naming_routine(GameState *g, Platform *pf, NamingRoutine routine, const NamingSteps *n, void *ctx);
/* The screen's step it is on, from inside the menus' main task: false
 * where it went no further (a state the port doesn't take). */
bool menu_naming_step(GameState *g, Platform *pf);

#endif
