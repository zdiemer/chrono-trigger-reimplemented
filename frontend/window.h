/* The shared SDL driver. A client advances once per step and supplies pixels
 * and stereo sound; the window owns devices, bindings, geometry and pacing.
 * File callbacks belong to the desktop driver, not to the game. */
#ifndef CT_FRONTEND_WINDOW_H
#define CT_FRONTEND_WINDOW_H

#include "audio_fill.h"
#include "presentation.h"
#include <stdio.h>

/* The picture's width in pixels (0: the original's 256; a wider view is the
 * driver's choice). Which of its rows show, and the shape of its pixels, are
 * the presentation's (PresentSettings.pixels). Geometry belongs to the window
 * driver. */
typedef struct {
    int width;
} Display;

/* `present` is how the picture is shown (frontend/presentation.h); the
 * window leaves it as play ended it -- the scaling, filter, fullscreen and pixels the
 * player last chose -- for the driver to remember. */
typedef struct {
    int scale;
    bool mute;
    Display display;
    PresentSettings present;
} WindowOptions;

/* Callbacks run on the driver thread; audio.take supplies 32000 Hz stereo.
 * All callbacks are required except quick_save and quick_load. */
typedef struct {
    void *ctx;
    const char *name;
    void (*step)(void *ctx, uint16_t buttons);
    const uint32_t *(*pixels)(void *ctx);
    AudioSource audio;
    void (*title)(void *ctx, bool paused, char *out, size_t size);
    /* NULL while running, otherwise a message for the stop dialog. */
    const char *(*halt)(void *ctx);
    bool (*done)(void *ctx);
    void (*quick_save)(void *ctx);   /* optional */
    void (*quick_load)(void *ctx);   /* optional */
    int (*flush)(void *ctx);
} WindowClient;

int window_run(const WindowClient *client, WindowOptions *options);
void window_print_controls(FILE *out);

/* The picture `pixels` (VID_RENDER_HEIGHT rows of the display's width) as the
 * window would show it on an output of out_w x out_h under `present`, drawn
 * by SDL's software renderer into out (out_w * out_h pixels, 0x00RRGGBB):
 * for looking at a presentation without a window. Returns 0, or -1 after
 * printing why not. */
int window_present_picture(const uint32_t *pixels, const Display *display, const PresentSettings *present, int out_w,
                           int out_h, uint32_t *out);

#endif
