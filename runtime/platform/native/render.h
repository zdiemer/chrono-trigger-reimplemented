/* Native frame renderer (docs/PLATFORM.md, P3): draws a frame from a VidFrame
 * and nothing else.
 *
 * It never reads the PPU registers, the hardware model's VRAM, CGRAM or OAM, or
 * anything else in runtime/snes/. Its input is the retained frame state the
 * video service describes (platform/video.h): the lines' state as spans, the
 * sprites in OAM order, and the video memory each span reads. Whoever built that
 * frame -- the register decoder (platform/snes/decode.h) or the observer folding
 * service calls (platform/observe.h) -- the picture is the same.
 *
 * Output matches runtime/snes/render.c pixel for pixel at 256x224, including its
 * Snes9x color pipeline, so framebuffer hashes stay comparable with ct_oracle
 * traces. Where the two must agree on something the SNES leaves undefined, the
 * scanline renderer is the reference.
 */
#ifndef CT_PLATFORM_NATIVE_RENDER_H
#define CT_PLATFORM_NATIVE_RENDER_H

#include "platform/video.h"

#define VID_RENDER_WIDTH 256
#define VID_RENDER_HEIGHT 224

typedef struct VidRenderer VidRenderer;

/* What drawing the frame's sprites ran into: the 32-sprite range limit and the
 * 34-tile line limit ($213E bits 6 and 7, which the game reads). */
typedef struct {
    bool range_over, time_over;
} VidRenderFlags;

VidRenderer *vid_render_create(void);
void vid_render_destroy(VidRenderer *r);

/* Draw the whole frame into `out`: 256x224 XRGB8888, row 0 being line 1.
 * `flags` may be NULL. */
void vid_render_frame(VidRenderer *r, const VidFrame *f, uint32_t *out, VidRenderFlags *flags);

/* Snes9x output color: a BGR555 value scaled by master brightness and packed as
 * RGB565, then widened to XRGB8888 (the same arithmetic as render.c's). */
uint16_t vid_render_color(uint16_t bgr555, uint8_t brightness);
uint32_t vid_render_rgb565_to_xrgb8888(uint16_t c);

#endif
