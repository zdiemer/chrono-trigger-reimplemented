/* Battle: waiting for the frame (battle/wait.c). */
#ifndef CT_GAME_BATTLE_WAIT_H
#define CT_GAME_BATTLE_WAIT_H

#include "battle/bridge.h"
#include "platform/platform.h"
#include <stdbool.h>
#include <stdint.h>
#include "state.h"

/* Wait out a frame of a paused fight, until the vblank handler has finished
 * it (battle/sound.c's pause). */
void battle_sit_out_a_paused_frame(GameState *g, Platform *pf);

/* ---- The frame work's waits, from the original's calls ---------------------------------------- */

/* The frame work's waits: for its vblank handler's frame ($CD:3E7D), for the
 * next frame, with the menu's turn after ($CD:3E75), and for a number of
 * frames ($CD:3E3B). They carry A's high byte on to the battle's pause, which
 * reads it; the low byte the first two keep for their caller, and the frames'
 * wait takes its count in it, as do the loops that count in A across the
 * others. */
enum { BATTLE_HANDLER_WAIT = 0xCD3E7D, BATTLE_NEXT_FRAME_WAIT = 0xCD3E75, BATTLE_SOME_FRAMES_WAIT = 0xCD3E3B };

/* ---- The waits as C ------------------------------------------------------------------------- */

/* The battle's wait for the next frame ($C1:007E); a frame as the battle
 * hands back to the field ($CF:FCFB); the frame work's waits for its
 * handler's frame ($CD:3E7D), for the next frame with the menu's turn
 * ($CD:3E75) and for a number of frames, 0 for 256 ($CD:3E3B); and the
 * small handler's frame ($CD:044A). False where a call in them went no
 * further. */
bool battle_wait_for_frame(GameState *g, Platform *pf);
void battle_wait_for_a_last_frame(GameState *g, Platform *pf);
bool battle_wait_for_the_handler(GameState *g, Platform *pf);
bool battle_wait_for_the_next_frame(GameState *g, Platform *pf);
bool battle_wait_some_frames(GameState *g, Platform *pf, uint8_t frames);
void battle_wait_for_the_small_handler(GameState *g, Platform *pf);

#endif
