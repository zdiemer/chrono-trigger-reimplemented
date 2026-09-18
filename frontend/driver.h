/* ct_play's drivers (docs/PLAYING.md): each owns the loop, the clock and the
 * devices, and runs the player (player.h) one frame per call. run_window plays
 * in an SDL window with sound; run_headless runs a fixed number of frames
 * without a display, for checking a build. */
#ifndef CT_FRONTEND_DRIVER_H
#define CT_FRONTEND_DRIVER_H

#include <stdbool.h>
#include <stdio.h>

#include "player.h"

/* Headless input: buttons held over a range of frames. */
typedef struct {
    uint16_t buttons;
    long first, last;
} Hold;

typedef struct {
    long frames;
    const Hold *holds;
    int nholds;
    const char *screenshot;   /* PNG of the last frame, or NULL */
    const char *save_state;   /* state file written at the end, or NULL */
    const char *wav;          /* the sound, or NULL */
} HeadlessOptions;

int run_headless(Player *pl, const HeadlessOptions *o);

/* How the 256x224 picture is shown: rows cropped at the top and bottom (TVs
 * hid some of them as overscan; Chrono Trigger leaves its bottom rows black),
 * and the width a row is stretched to relative to its height. */
typedef struct {
    int crop_top, crop_bottom;
    double pixel_aspect;   /* displayed pixel width / height */
} Display;

typedef struct {
    int scale;
    bool fullscreen;
    bool mute;
    Display display;
} WindowOptions;

int run_window(Player *pl, const WindowOptions *o);

/* The window's controls, for --help. */
void window_print_controls(FILE *out);

#endif
