/* The menus' fades (menu/fades.c): the screen brought up out of black as a
 * screen opens, in a task of its own, and taken down to black as the menus
 * close, in the task that closes them. */
#ifndef CT_GAME_MENU_FADES_H
#define CT_GAME_MENU_FADES_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "state.h"

/* Start the fade in: its task runs from the next frame, while the screen
 * that started it goes on. */
void menu_start_the_fade_in(GameState *g);

/* From inside a task (menu/tasks.c's coroutines): the screen faded out to
 * black, a step a frame, and its last frame waited; and the fade in's task, by
 * its start. */
void menu_fade_to_black(GameState *g);
enum { MENU_FADE_IN_TASK_START = 0x83BD };
void menu_fade_in_task(GameState *g, Platform *pf);

/* One step of the fade under way, by the step the menus keep for it (the one
 * a fade out started over the fade in sets for both): on, or at full
 * brightness or black, where the fade ends. */
typedef enum { MENU_FADE_GOES_ON, MENU_FADE_AT_FULL, MENU_FADE_AT_BLACK } MenuFadeEnd;
MenuFadeEnd menu_fade_a_step(GameState *g);

#endif
