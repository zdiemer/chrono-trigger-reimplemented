/* Attract sequence and title screen, which the game decompresses into RAM and
 * runs from there. */
#ifndef CT_GAME_ATTRACT_H
#define CT_GAME_ATTRACT_H

#include <stdbool.h>

#include "snes/system.h"

/* attract/main_loop.c */
bool attract_wait_loop_present(Snes *s);
void attract_wait_loop(Snes *s);
bool attract_frame_pass_present(Snes *s);
void attract_frame_pass(Snes *s);
bool attract_vblank_wait_present(Snes *s);
void attract_vblank_end_loop(Snes *s);
void attract_vblank_start_loop(Snes *s);

#endif
