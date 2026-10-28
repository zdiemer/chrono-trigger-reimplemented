/* The End of Time's gate screen (game/menu/gate_screen.c): its swirl drawn,
 * turned every frame, and the screen started. */
#ifndef CT_GAME_MENU_GATE_SCREEN_H
#define CT_GAME_MENU_GATE_SCREEN_H

#include <stdbool.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* The swirl's tilemap, colours and scroll, once its graphics are unpacked;
 * and its turn for a frame. */
void menu_draw_gate_swirl(GameState *g);
void menu_turn_gate_swirl(GameState *g);

/* As C: the swirl's graphics unpacked and the swirl drawn ($FF:F677; false
 * where the unpacking stopped); the screen started ($C2:CE9D); its frame
 * task ($C2:CECE), which the menus' tasks run (menu/tasks.c). */
bool menu_draw_gate_screen(GameState *g);
void menu_start_gate_screen(GameState *g, Platform *pf);
enum { MENU_GATE_SCREEN_TASK_START = 0xCECE };
void menu_gate_screen_task(GameState *g, Platform *pf);
/* Whether the swirl can turn as it stands (its task's guard's state half). */
bool menu_gate_swirl_can_turn(const GameState *g);

/* The registered entries ($FF:F677, $FF:F716, $C2:CE9D) and their guard. */
void menu_draw_gate_screen_entry(Snes *s);
void menu_turn_gate_swirl_entry(Snes *s);
void menu_start_gate_screen_entry(Snes *s);
void menu_run_the_gate_screen_task(Snes *s);
bool menu_gate_screen_runs(Snes *s);
bool menu_gate_swirl_turns(Snes *s);

#endif
