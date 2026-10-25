/* Naming a character, menu screen 13 (game/menu/naming.c), and its steps
 * on the original's machine (game/menu/naming_bridge.c). */
#ifndef CT_GAME_MENU_NAMING_H
#define CT_GAME_MENU_NAMING_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/cursor.h"
#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

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
 * five letters (what the steps take, their entries' guards on the
 * machine). */
bool menu_naming_names_one(const GameState *g);
bool menu_naming_is_whole(const GameState *g);

/* ---- The screen's steps ---- */

/* The screen's steps, as its table has them (menu/screens.c), and the
 * routines of the naming they call as their own. */
typedef enum {
    NAMING_OPEN_THE_SCREEN, NAMING_NAME_THE_CHARACTER, NAMING_CHOOSE_BATTLE_MODE, NAMING_OPEN_THE_NAMING,
    NAMING_WAIT_TO_NAME,
} NamingStep;
typedef enum { NAMING_MOVE_ON_THE_GRID, NAMING_TYPE_ON_THE_GRID, NAMING_ACCEPT_THE_NAME, NAMING_CHECK_THE_NAME } NamingRoutine;

/* How a step ends where it isn't back in the menus' main task by its return:
 * the screen's fade in started, the screen faded out, or the menus left. */
typedef enum { NAMING_FADES_IN, NAMING_FADES_OUT, NAMING_LEAVES_THE_MENUS } NamingEnd;

/* Where the original's tests leave the registers a call the steps make reads
 * (scaffolding, which only the machine's steps leave). */
typedef enum {
    NAMING_NOTE_PRESSED_LOADED, NAMING_NOTE_MODE_DESCRIBED, NAMING_NOTE_BUTTONS_TESTED, NAMING_NOTE_CURSOR_COMPARED,
    NAMING_NOTE_LETTERS_LOADED, NAMING_NOTE_MOVED_ON_THE_GRID, NAMING_NOTE_NAME_QUEUED, NAMING_NOTE_NAME_FOUND,
    NAMING_NOTE_ANSWERED, NAMING_NOTE_NAME_KEPT, NAMING_NOTE_START_PRESSED,
} NamingNote;

/* What the steps do that is the menus' own, run as C (naming.c) or on the
 * machine (naming_bridge.c): the cursor read; a sound, from the call at
 * `site`; a routine of the naming run as its own call; the scaffolding at a
 * note; and the step's end. Each false where it went no further. */
typedef struct {
    void (*read_cursor)(void *ctx);
    bool (*sound)(void *ctx, uint32_t site, MenuSound sound);
    bool (*routine)(void *ctx, NamingRoutine routine);
    void (*note)(void *ctx, NamingNote note, uint8_t value);
    bool (*ends)(void *ctx, NamingEnd end);
} NamingSteps;

/* A step or a routine, through `n`; true where it came back. */
bool menu_naming_step(GameState *g, Platform *pf, NamingStep step, const NamingSteps *n, void *ctx);
bool menu_naming_routine(GameState *g, Platform *pf, NamingRoutine routine, const NamingSteps *n, void *ctx);
uint32_t menu_naming_routine_address(NamingRoutine routine);
/* A step as C, from inside the menus' main task: false where it went no
 * further (a state the port doesn't take). */
bool menu_naming_step_as_c(GameState *g, Platform *pf, NamingStep step);

/* The steps and entries ($C2:E1ED, $C2:E201, $C2:E209, $C2:E21E, $C2:E236;
 * $C2:E267, $C2:E2F4, $C2:E363, $C2:E3E0; $C2:E40E, $C2:E545, $C2:E34A,
 * $C2:E576, $C2:E5D3), and their guards. */
void menu_open_naming_screen(Snes *s);
void menu_open_the_naming(Snes *s);
void menu_wait_to_name(Snes *s);
void menu_name_the_character(Snes *s);
void menu_choose_battle_mode(Snes *s);
void menu_move_on_the_name_grid(Snes *s);
void menu_type_on_the_name_grid(Snes *s);
void menu_accept_the_name(Snes *s);
void menu_check_the_name(Snes *s);
void menu_naming_open_entry(Snes *s);
void menu_write_out_the_name_grid(Snes *s);
void menu_place_the_names_cursor(Snes *s);
void menu_open_the_battle_mode(Snes *s);
void menu_describe_the_battle_mode(Snes *s);
bool menu_naming_runs(Snes *s);
bool menu_naming_opens(Snes *s);
bool menu_naming_name_is_checkable(Snes *s);
bool menu_naming_step_runs(Snes *s);

#endif
