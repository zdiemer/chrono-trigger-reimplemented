/* The shared SDL driver. A client advances once per step and supplies pixels
 * and stereo sound; the window owns devices, bindings, geometry and pacing.
 * File callbacks belong to the desktop driver, not to the game. */
#ifndef CT_FRONTEND_WINDOW_H
#define CT_FRONTEND_WINDOW_H

#include "audio_fill.h"
#include <stdio.h>

/* The picture's width in pixels (0: the original's 256; a wider view is the
 * driver's choice), the overscan rows cropped from its 224, and the displayed
 * width of a pixel relative to its height. Geometry belongs to the window
 * driver. */
typedef struct {
    int width;
    int crop_top, crop_bottom;
    double pixel_aspect;
} Display;

typedef struct {
    int scale;
    bool fullscreen;
    bool mute;
    Display display;
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

int window_run(const WindowClient *client, const WindowOptions *options);
void window_print_controls(FILE *out);

#endif
