/* Battle: waiting for the frame (battle/wait.c). */
#ifndef CT_GAME_BATTLE_WAIT_H
#define CT_GAME_BATTLE_WAIT_H

#include "battle/bridge.h"
#include "platform/platform.h"
#include "snes/system.h"
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
enum { BATTLE_WAIT_READS = DC_AL | DC_X | DC_Y | DC_FLAGS, BATTLE_COUNTING_WAIT_READS = DC_X | DC_Y | DC_FLAGS };

/* The handler's and the next frame's wait from the original's JSR at `site`,
 * with nothing the bridge keeps; for the rest, battle_wait_under and
 * battle_wait_frames_under (battle/bridge.h) with these. tools/dontcare_check.py
 * reads the two as the waits they make. */
bool battle_wait_for_the_handler_from(Snes *s, uint32_t site);
bool battle_wait_for_the_next_frame_from(Snes *s, uint32_t site);

/* A count the original keeps in A and on the stack across a wait, from its
 * first pass's carry on. */
static inline BattleFrame battle_counting(uint8_t left, BattleCarry carry)
{
    return (BattleFrame){ .kept = { left }, .kept_bytes = 1, .carry = carry };
}

void battle_wait_for_frame(Snes *s);
void battle_wait_for_a_last_frame(Snes *s);
void battle_wait_for_the_handler(Snes *s);
void battle_wait_for_the_next_frame(Snes *s);
void battle_wait_some_frames(Snes *s);
void battle_wait_for_the_small_handler(Snes *s);
void battle_wait_window_slide_loop(Snes *s);
void battle_wait_graphics_loaded_loop(Snes *s);

#endif
