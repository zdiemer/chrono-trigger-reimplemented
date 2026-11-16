/* The game's frames on the GBA: what the time model decides of them
 * (TmHostDecision, platform/native/host.h), which the GBA build, keeping no
 * time of the original's (CT_UNTIMED), decides itself.
 *
 * Driven with a movie, the runner puts the decisions ct_native's time model
 * made playing it after the cartridge's data (targets/gba/host/mgba_run.c
 * --decisions), and they are replayed in order: the game is then ct_native's
 * frame for frame, and the movie's checks hold the GBA to its end state.
 * Played by hand, the GBA's own time decides, as the SNES's beam did: where
 * the game's part of a frame runs past a GBA frame, what it ran over is owed
 * and the frames after sit it out a whole frame at a time; the main thread
 * stops in the middle of its work where it has run a whole GBA frame; and a
 * budget has time left while the game's time so far in the frame, as the
 * SNES's lines from its vertical blank, is inside its window. */
#ifndef CT_GBA_FRAMES_H
#define CT_GBA_FRAMES_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/native/host.h"

/* Replaying the runner's decisions where `driven` and it put some there;
 * the GBA's own time otherwise. `cycles_now`'s count starts here. */
void frames_start(bool driven);
bool frames_replayed(void);
/* The game's part of a frame begins (main.c, before tm_host_frame). */
void frames_game_begins(void);
/* The game's part of a frame took `cycles` (main.c, after tm_host_frame). */
void frames_game_took(uint32_t cycles);
/* TmHostProgram.decide. */
bool frames_decide(void *ctx, TmHostDecision what, uint16_t line);
/* The runner's decisions replayed so far, and how many it gave (the
 * report: a movie played to its end replays every one). Asked for more than
 * it gave, the program stops, saying at which frame. */
uint32_t frames_decisions_taken(void);
uint32_t frames_decisions_given(void);

#endif
