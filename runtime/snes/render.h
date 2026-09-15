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

#include <stdbool.h>
#include <stdint.h>

#include "ppu.h"

#define RENDER_WIDTH 256
#define RENDER_HEIGHT 224

/* Everything a line's drawing reads besides CGRAM and VRAM (tracked
 * separately, see RenderLineCache): registers, decoded scroll and Mode 7 values,
 * and the sprite generation of its row. Values that can't affect the line are
 * zeroed so they don't make it look changed. Laid out without padding so it
 * compares with memcmp. */
typedef struct RenderLineKey {
    uint8_t reg[0x34];            /* $2100-$2133 */
    uint16_t bg_hofs[4], bg_vofs[4];
    int16_t m7[8];                /* A B C D X Y HOFS VOFS */
    uint16_t fixed_color;
    uint8_t first_sprite;
    uint8_t pad;
    uint64_t sprite_row_gen;
} RenderLineKey;

/* A line as last drawn: its inputs, the CGRAM and VRAM parts it read (with the
 * generations current then), and the sprite flags drawing it set. While none of
 * that changes, the line's pixels in the framebuffer are still right. */
/* VRAM is tracked in 32-byte pages: one 4bpp tile each. render.c's dependency
 * marking relies on this size (a 2bpp or 4bpp tile lies within one page). */
#define RENDER_VRAM_PAGE_BITS 5
#define RENDER_VRAM_PAGES (0x10000 >> RENDER_VRAM_PAGE_BITS)

typedef struct RenderLineCache {
    RenderLineKey key;
    uint64_t palette_gen, vram_gen;
    uint64_t palette_entries[4];                 /* bit i % 64 of word i / 64: CGRAM entry i */
    uint64_t vram_pages[RENDER_VRAM_PAGES / 64]; /* likewise per VRAM page */
    bool valid, range_over, time_over;
} RenderLineCache;

typedef struct Renderer {
    uint32_t pixels[RENDER_WIDTH * RENDER_HEIGHT];   /* XRGB8888 */

    /* Caches private to render.c. Each keeps a copy of the PPU state it was
     * built from and compares it with the live state, so nothing outside the
     * renderer has to invalidate them (register writes, DMA, state loads).
     * Generations count changes; a part's generation is the count when it last
     * changed. */
    struct {
        bool valid;
        uint8_t brightness;
        uint64_t gen;
        uint64_t entry_gen[256];
        uint64_t last_changed[4]; /* entries that changed at generation gen */
        uint16_t cgram[256];
        uint16_t rgb565[256];     /* CGRAM scaled by brightness, packed as Snes9x does */
    } palette;
    struct {
        bool valid;
        uint8_t size;             /* OBSEL size bits */
        uint8_t oam[0x220];
        int16_t x[128];           /* signed screen X */
        uint8_t w[128], h[128];
        uint64_t rows[256][2];    /* per row: bit n set when sprite n covers it */
        uint64_t row_gen[256];    /* per row: changes when a sprite covering it (before or after) changes */
    } sprites;
    struct {
        bool valid;
        uint32_t writes_seen;     /* Ppu.vram_writes when last compared */
        uint64_t gen;
        uint64_t page_gen[RENDER_VRAM_PAGES];
        uint64_t last_changed[RENDER_VRAM_PAGES / 64];   /* pages that changed at generation gen */
        uint8_t copy[0x10000];
    } vram;
    RenderLineCache lines[RENDER_HEIGHT + 1];
} Renderer;

void render_init(Renderer *r);

/* Draw scanline 0-224 from the PPU's current state. Line 0 isn't displayed; it
 * resets per-frame state. Lines 1-224 go to framebuffer rows 0-223. Updates the
 * PPU's range-over and time-over flags. The framebuffer must not be changed
 * outside the renderer: unchanged lines aren't redrawn. */
void render_line(Renderer *r, Ppu *ppu, int line);

/* Snes9x output color: a 15-bit BGR555 value (0bbbbbgggggrrrrr) scaled by master
 * brightness and packed as RGB565, then widened to XRGB8888. Exposed for tests. */
uint16_t render_snes9x_color(uint16_t bgr555, uint8_t brightness);
uint32_t render_rgb565_to_xrgb8888(uint16_t c);

#endif
