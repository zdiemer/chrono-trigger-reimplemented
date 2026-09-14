/* Scanline renderer for the development system: draws one line at a time from
 * the PPU register layer's state into a 256x224 XRGB8888 framebuffer.
 *
 * It's a low-level reference renderer, deliberately kept apart from the PPU
 * register code so a native renderer can replace it. Output colors follow the
 * Snes9x libretro core's pipeline (see render_snes9x_color) so framebuffer
 * hashes are comparable with ct_oracle traces.
 */
#ifndef CT_SNES_RENDER_H
#define CT_SNES_RENDER_H

#include <stdint.h>

#include "ppu.h"

#define RENDER_WIDTH 256
#define RENDER_HEIGHT 224

typedef struct Renderer {
    uint32_t pixels[RENDER_WIDTH * RENDER_HEIGHT];   /* XRGB8888 */
} Renderer;

void render_init(Renderer *r);

/* Draw scanline 0-224 from the PPU's current state. Line 0 isn't displayed; it
 * resets per-frame state. Lines 1-224 go to framebuffer rows 0-223. Updates the
 * PPU's range-over and time-over flags. */
void render_line(Renderer *r, Ppu *ppu, int line);

/* Snes9x output color: a 15-bit BGR555 value (0bbbbbgggggrrrrr) scaled by master
 * brightness and packed as RGB565, then widened to XRGB8888. Exposed for tests. */
uint16_t render_snes9x_color(uint16_t bgr555, uint8_t brightness);
uint32_t render_rgb565_to_xrgb8888(uint16_t c);

#endif
