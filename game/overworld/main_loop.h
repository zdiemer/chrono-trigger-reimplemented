/* The overworld's main loop (game/overworld/main_loop.c): as the program runs
 * it, and on the original's machine in the shipping mode, a frame at a time
 * as its held wait's continuation. Each pass ends waiting for the next frame,
 * or where the overworld leaves for the field, or at the halt. */
#ifndef CT_GAME_OVERWORLD_MAIN_LOOP_H
#define CT_GAME_OVERWORLD_MAIN_LOOP_H

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* The world map as the program runs it, with no machine: the sprite-object
 * mode started ($C2:0000), the map set up, and the loop's passes until the
 * party leaves for the field (the location to enter set), or a step only the
 * machine runs yet stops the program. */
typedef enum { OVERWORLD_LEFT, OVERWORLD_WENT_NO_FURTHER } OverworldEnd;
OverworldEnd overworld_runs(GameState *g, Platform *pf);

/* The loop's contract on the machine: the mode's page with the data bank on
 * low RAM, an 8-bit accumulator, 16-bit index registers and binary
 * arithmetic (overworld_main_loop_runs, the page and bank alone). */
bool overworld_loop_accepts_machine(Snes *s);
bool overworld_main_loop_runs(Snes *s);

/* The loop's wait for the next frame, a held wait (holds.h,
 * HOLD_OVERWORLD_FRAME): the port starting it stops in the wait's loop
 * ($C2:0348), and the runtime goes on with overworld_next_frame once the
 * vblank handler has counted the frame; overworld_waits_for_the_frame is a
 * saveable C loop before it. The guards say which applies, each where the
 * wait returns to the loop's pass, which the mode's other waits that share
 * its code don't. */
void overworld_wait_for_the_next_frame_on_the_machine(Snes *s);
void overworld_next_frame(Snes *s);
void overworld_back_from_the_menus(Snes *s);
void overworld_waits_for_the_frame(Snes *s);
void overworld_waits_for_the_frame_entry(Snes *s);
bool overworld_frame_counted(Snes *s);
bool overworld_frame_not_counted(Snes *s);

/* The halt ($C2:250D): the game stopped for good, a pass at a time, each
 * coming back to itself, a held wait of its own (HOLD_OVERWORLD_HALT). */
void overworld_halt_entry(Snes *s);

void overworld_exit_fade_finished(Snes *s);
bool overworld_exit_fade_can_resume(Snes *s);

/* Saveable phases around the menu services' counted waits. */
void overworld_menu_fade_finished(Snes *s);
void overworld_menu_return_fade_finished(Snes *s);
void overworld_map_view_fade_finished(Snes *s);
void overworld_back_from_the_map_view(Snes *s);
void overworld_screen_return_fade_finished(Snes *s);
void overworld_dial_fade_finished(Snes *s);
void overworld_back_from_the_time_dial(Snes *s);

#endif
