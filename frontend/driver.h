/* ct_play's drivers (docs/PLAYING.md): each owns the loop, the clock and the
 * devices, and runs the player (player.h) one frame per call. run_window plays
 * in an SDL window with sound; run_headless runs a fixed number of frames
 * without a display, for checking a build. */
#ifndef CT_FRONTEND_DRIVER_H
#define CT_FRONTEND_DRIVER_H

#include <stdbool.h>
#include <stdio.h>

#include "files.h"
#include "player.h"
#include "window.h"

/* Headless input: buttons held over a range of frames. */
typedef struct {
    uint16_t buttons;
    long first, last;
} Hold;

typedef struct {
    const Files *files;
    long frames;
    const Hold *holds;
    int nholds;
    const char *screenshot;   /* PNG of the last frame, or NULL */
    const char *save_state;   /* state file written at the end, or NULL */
    const char *wav;          /* the sound, or NULL */
} HeadlessOptions;

int run_headless(Player *pl, const HeadlessOptions *o);

int run_window(Player *pl, const Files *files, const WindowOptions *o);

#endif
