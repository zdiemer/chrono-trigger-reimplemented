/* The menus: bank C2's two modes, and the text the dialog and the menus
 * share.
 *
 * Bank C2 runs two modes of its own. The menus proper ($C2:800E) are the
 * main menu, the save and load screens, shops and the rest of the windowed
 * screens: they draw text and windows into tilemaps in work RAM and queue them
 * for VRAM (text.c, vram.c, save_screen.c), and their vblank handler sends the
 * frame's display state, sprites, palette and queue (display.c). The
 * sprite-object mode ($C2:0000) is the one the Epoch's screens and the world
 * maps' scenes run in: a map of three planes behind a set of sprite objects,
 * each run by a handler or a command script (map.c, objects.c,
 * object_script.c, object_commands.c, top_layer.c, wave.c, epoch.c,
 * turning_world.c).
 * Each mode's main loop waits for its vblank handler (frame.c). The music the
 * menus switch to is sound.c, and the dialog's proportional font, set a
 * glyph at a time, typeset.c and glyphs.c. What they read from the ROM is menu_data.h. */
#ifndef CT_GAME_MENU_H
#define CT_GAME_MENU_H

#include <stdbool.h>
#include <stdint.h>

#include "core/tasks.h"
#include "platform/platform.h"
#include "state.h"
#include "vars.h"

/* The menus' screens, by the number the menus keep for the one running
 * (menu_screen): the main menu, the six screens its options open (each its
 * option's number plus one, menu/main_menu.h), and the rest the menus are
 * opened on. The menus have no screen 8 to 10. menu/screens.c runs their
 * steps. */
typedef enum {
    MENU_SCREEN_MAIN_MENU, MENU_SCREEN_EQUIPMENT, MENU_SCREEN_ITEMS, MENU_SCREEN_TECHS, MENU_SCREEN_SETTINGS,
    MENU_SCREEN_EXCHANGE, MENU_SCREEN_SAVE, MENU_SCREEN_EXCHANGE_AT_THE_GATE,
    MENU_SCREEN_SAVE_POINT = 11, MENU_SCREEN_SHOP, MENU_SCREEN_NAMING, MENU_SCREEN_LOAD,
    MENU_SCREENS,
} MenuScreen;

/* The menus' tasks (menu_tasks), which their scheduler runs a turn of each
 * frame: a task started runs from `start` on its next turn ($C2:8249). Some
 * have places of their own: the description's typing, the cursor's sprite
 * (which a screen's panel holds back while it draws), the scroll bar's, the
 * records', the cursors a screen holds and the healing's rising numbers. */
enum { MENU_DESCRIPTION_TASK = 2, MENU_CURSOR_TASK = 3, MENU_SCROLL_BAR_TASK = 4, MENU_RECORDS_TASK = 5, MENU_HELD_CURSORS_TASK = 6, MENU_AMOUNTS_TASK = 7,  };
enum { MENU_TASK_STARTING = 1 };

/* Stop the tasks whose bits are set in `tasks`, bit n the n-th: none of
 * them takes another turn ($C2:8255, menu/frame.c). */
enum { MENU_TASKS = 8 };
void menu_stop_tasks(GameState *g, uint8_t tasks);
/* menu/tasks.c: a task waits a frame or some frames, or ends, and the next
 * goes on; the frame's turns from the first task; the menus' first wait; and
 * the turns of the main task and the cursor's, the tasks that run all the
 * time. */

/* The same as C, each task a coroutine (core/tasks.h): the tasks started as
 * the menus open; a frame's turns, which end where a task closes the menus or
 * is one only the machine runs yet; the running task's wait for `frames`
 * frames, from inside it; the menus closed from inside a task, which never
 * comes back; and the tasks' coroutines given back once the menus are. */
void menu_tasks_open(GameState *g, Platform *pf);
typedef enum { MENU_TURNS_TAKEN, MENU_TURNS_CLOSED, MENU_TURNS_WENT_NO_FURTHER } MenuTurns;
MenuTurns menu_tasks_take_the_turns(GameState *g);
void menu_task_waits(GameState *g, uint8_t frames);
void menu_tasks_closed(GameState *g);
void menu_tasks_end(GameState *g);

/* The menus' coroutines while they are open, whether a task has closed them
 * (the one closing them waits for good, and the turns end there), and the
 * mode running them, which the closing tells how it ended. The program's own
 * state, kept beside the game's (runtime/port/state_host.c) so that a
 * snapshot of the game holds the open menus with it. */
typedef struct MenuModeContext MenuModeContext;
typedef struct {
    Tasks tasks;
    bool closed;
    MenuModeContext *running;
} MenuCoroutines;
MenuCoroutines *menu_coroutines(GameState *g);

/* menu/screens.c: the running screen's step, as the main task's turn runs
 * it; true if it came back to the turn. And the turn's guard: the screen on a
 * step it has. */
/* The same as C, from inside the main task: false where the step is one the
 * port doesn't have as C, the program going no further there. */
bool menu_run_the_screen_step(GameState *g, Platform *pf);

/* menu/frame.c */
/* The sprite-object mode's main loop waits, as C: for the vblank handler to
 * count the next frame, and for `frames` frames (0 is 65,536), sending after
 * each the sound command a request left waiting. */
void menu_wait_for_the_next_frame(GameState *g, Platform *pf);
void menu_wait_frames_sending_the_sound(GameState *g, Platform *pf, uint16_t frames);

#include "menu/glyphs.h"

/* A signed division as the original makes it: the size divided and the sign
 * put back after, so it rounds toward zero, and a divisor of 0 gives the CPU's
 * divider's all ones (-1, or 1 with the sign put back). The Epoch's map view
 * and the Day of Lavos's turning world work their Mode 7 matrices out so, and
 * the time dial its gate's edges' slopes. */
static inline int16_t menu_signed_quotient(uint16_t size, uint8_t divisor, bool negative)
{
    uint16_t q = divisor ? (uint16_t)(size / divisor) : 0xFFFF;
    return (int16_t)(negative ? (uint16_t)(0 - q) : q);
}

static inline void menu_start_task(GameState *g, unsigned task, uint16_t start)
{
    set_menu_tasks_state(g, task, MENU_TASK_STARTING);
    set_menu_tasks_countdown(g, task, 1);
    set_menu_tasks_start(g, task, start);
}

#endif
