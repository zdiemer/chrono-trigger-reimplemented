/* The original's stack frame under a routine whose work a port runs as C.
 *
 * A port that does, as C, the work of a routine the original reaches by a
 * call, and that waits or calls out in the middle of it, meets the original
 * in there: shadow mode compares the two at each wait and call-out, the stack
 * pointer included, and the original has the routine's frame on the stack --
 * its return address and what it pushes on the way in. These say so: the
 * machine's stack pointer goes down by the frame's bytes for the routine's
 * work and up again after it, and the bytes, which nothing reads but the
 * original's own way back, aren't compared. They are scaffolding: call them
 * through ORIGINAL_EFFECT (original_effects.h), or IN_THE_ORIGINALS_FRAME, and
 * a native build has none of them. runtime/port/state_io.c is the backing
 * (runtime/port/native.h, native_frame_enter). */
#ifndef CT_GAME_ORIGINAL_FRAME_H
#define CT_GAME_ORIGINAL_FRAME_H

#include <stdint.h>

#include "original_effects.h"
#include "state.h"

/* A frame's parts: a call's return address (a JSR's), and the flags a routine
 * saves on the way in and restores on the way out. */
enum { ORIGINAL_RETURN_BYTES = 2, ORIGINAL_SAVED_FLAGS_BYTES = 1 };

void original_frame_enter(GameState *g, uint16_t bytes);
void original_frame_leave(GameState *g, uint16_t bytes);

#define IN_THE_ORIGINALS_FRAME(g, bytes, statement)                \
    do {                                                           \
        ORIGINAL_EFFECT(original_frame_enter((g), (bytes)));       \
        statement;                                                 \
        ORIGINAL_EFFECT(original_frame_leave((g), (bytes)));       \
    } while (0)

#endif
