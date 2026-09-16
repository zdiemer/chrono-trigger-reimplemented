#include "platform/native/render.h"

#include <stdlib.h>
#include <string.h>

/* How a frame is drawn (docs/PLATFORM.md, P3):
 *
 * 1. The frame's spans say what state each line is in. Everything a span shares
 *    -- the palette as RGB565, the layer order's depths, the window masks, the
 *    color window's clip and math decisions -- is worked out once per span
 *    instead of once per line, which is what having the frame rather than the
 *    registers buys.
 * 2. A line's enabled layers are each drawn into a LayerBuf: per pixel a depth
 *    from the mode's front-to-back order (0 for transparent) and the RGB565
 *    color. BG layers go a tile at a time: one tilemap lookup and one bitplane
 *    decode per 8 pixels (tile_row). Mosaic, offset-per-tile and hi-res fall
 *    back to one sample per mosaic block.
 * 3. Each layer is merged into the main and sub screens it is on: the pixel with
 *    the larger depth wins unless its window masks it on that screen.
 * 4. Color math combines the two screens per pixel.
 *
 * Steps 2-4 are runtime/snes/render.c's, which this must match pixel for pixel;
 * the arithmetic is deliberately kept identical rather than tidied. What changes
 * is where the state comes from: a VidLineState per span, and the VRAM, CGRAM
 * and sprites the frame carries, never the PPU. */

enum { L_BG1, L_BG2, L_BG3, L_BG4, L_OBJ, L_BACK, L_COUNT };

#define W VID_RENDER_WIDTH
#define H VID_RENDER_HEIGHT

/* Layers are drawn up to 7 pixels past the right edge (whole tiles); only the
 * first W pixels are used. */
typedef struct {
    uint8_t z[W + 8];        /* depth in the layer order; 0: transparent */
    uint16_t color[W + 8];   /* RGB565 */
    uint8_t math[W];         /* sprites only: palettes 4-7, which color math applies to */
} LayerBuf;

typedef struct {
    uint8_t z[W];        /* depth of the front pixel; 0: backdrop */
    uint16_t color[W];
    uint8_t math[W];     /* color math enabled for the front pixel's layer */
} Screen;

/* Everything one span shares, worked out when the span starts. */
typedef struct {
    const VidLineState *st;
    const uint8_t *vram;
    const VidSprite *sprites;      /* the span's sprites, in OAM order */
    uint8_t first_sprite;          /* OAM priority rotation */
    uint16_t pal[256];             /* CGRAM as RGB565, scaled by brightness */
    uint8_t brightness;
    uint8_t depth[L_OBJ + 1][4];   /* layer, priority -> depth; 0: not in this mode's order */
    uint16_t fixed565;
    uint8_t mode;
    uint8_t main_layers, sub_layers, win_main, win_sub, used;
    bool any_math, need_sub, sub_add;
    int add_cap;
    uint8_t win[L_COUNT][W];       /* per layer: 1 inside the combined window */
    bool windowed[L_COUNT];
    uint8_t clip[W], math_on[W];   /* the color window's effect */
} SpanCtx;

/* Per-line state the layer drawers need. */
typedef struct {
    const SpanCtx *sp;
    int line;
} LineCtx;

struct VidRenderer {
    Screen main_s, sub_s;
    LayerBuf lb;
    SpanCtx span;
    /* Sprite geometry for the frame, as render.c's sprite cache holds it. */
    struct {
        int16_t x[VID_SPRITES];
        uint8_t w[VID_SPRITES], h[VID_SPRITES];
        uint64_t rows[256][2];   /* per row: bit n set when sprite n covers it */
    } sprites;
    bool range_over, time_over;
};

/* ---- tables ---------------------------------------------------------------------- */

static uint8_t scale5[16][32];   /* brightness, 5-bit channel -> scaled channel */
static uint64_t expand8[256];    /* byte -> 8 bytes, byte i = bit 7-i */
static bool tables_ready;

static void tables_init(void)
{
    if (tables_ready)
        return;
    for (int b = 0; b < 16; b++)
        for (int c = 0; c < 32; c++)
            scale5[b][c] = (uint8_t)((c * b + 7) / 15);
    for (int v = 0; v < 256; v++) {
        uint64_t e = 0;
        for (int i = 0; i < 8; i++)
            e |= (uint64_t)((v >> (7 - i)) & 1) << (8 * i);
        expand8[v] = e;
    }
    tables_ready = true;
}

static inline int min_i(int a, int b) { return a < b ? a : b; }

static inline int lowest_bit(uint64_t v)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(v);
#else
    int n = 0;
    while (!(v & 1)) {
        v >>= 1;
        n++;
    }
    return n;
#endif
}

static inline uint64_t reverse_bytes(uint64_t v)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(v);
#else
    uint64_t r = 0;
    for (int i = 0; i < 8; i++, v >>= 8)
        r = r << 8 | (v & 0xFF);
    return r;
#endif
}

/* 1 in each byte of v that isn't zero, 0 in the others. */
static inline uint64_t nonzero_bytes(uint64_t v)
{
    const uint64_t low7 = 0x7F7F7F7F7F7F7F7Full;
    return ((((v & low7) + low7) | v) >> 7) & 0x0101010101010101ull;
}

/* ---- Snes9x output color pipeline ---------------------------------------------
 *
 * Snes9x scales every palette entry by master brightness up front, packs it as
 * RGB565 with the green LSB copied from green's top bit, and runs color math on
 * those packed values (render.c says the same; the two must stay in step). */

static uint8_t brightness_scale(uint8_t channel5, uint8_t brightness)
{
    return (uint8_t)((channel5 * brightness + 7) / 15);
}

static inline uint16_t pack565(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(r << 11 | g << 6 | (g & 0x10) << 1 | b);
}

uint16_t vid_render_color(uint16_t bgr555, uint8_t brightness)
{
    return pack565(brightness_scale(bgr555 & 31, brightness), brightness_scale((bgr555 >> 5) & 31, brightness),
                   brightness_scale((bgr555 >> 10) & 31, brightness));
}

static inline uint32_t rgb565_to_xrgb8888(uint16_t c)
{
    unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (uint32_t)((r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2));
}

uint32_t vid_render_rgb565_to_xrgb8888(uint16_t c) { return rgb565_to_xrgb8888(c); }

/* Table-driven vid_render_color, for the renderer's own use. */
static inline uint16_t color565(uint16_t bgr555, uint8_t brightness)
{
    const uint8_t *s = scale5[brightness & 15];
    return pack565(s[bgr555 & 31], s[(bgr555 >> 5) & 31], s[(bgr555 >> 10) & 31]);
}

static inline uint16_t direct565(uint8_t pixel, uint8_t palette, uint8_t brightness)
{
    const uint8_t *s = scale5[brightness & 15];
    unsigned r = (pixel & 7) << 2 | (palette & 1) << 1;
    unsigned g = (pixel & 0x38) >> 1 | (palette & 2);
    unsigned b = (pixel & 0xC0) >> 3 | (palette & 4);
    return pack565(s[r], s[g], s[b]);
}

/* Saturating add per 5-bit channel, clamped to cap (31, or the brightness-scaled
 * maximum when brightness isn't full). */
static inline uint16_t math_add(uint16_t a, uint16_t b, int cap)
{
    int r = min_i(((a >> 11) & 31) + ((b >> 11) & 31), cap);
    int g = min_i(((a >> 6) & 31) + ((b >> 6) & 31), cap);
    int bl = min_i((a & 31) + (b & 31), cap);
    return pack565((unsigned)r, (unsigned)g, (unsigned)bl);
}

/* Average on the packed word: low bits dropped, then restored where both have them. */
static inline uint16_t math_add_half(uint16_t a, uint16_t b)
{
    return (uint16_t)((((a & 0xF7DE) + (b & 0xF7DE)) >> 1) + (a & b & 0x0821));
}

/* Subtract, flooring each channel at 0. Green is subtracted at 6 bits and
 * truncated to 5. */
static inline uint16_t math_sub(uint16_t a, uint16_t b)
{
    int r = ((a >> 11) & 31) - ((b >> 11) & 31);
    int g6 = ((a >> 5) & 63) - ((b >> 5) & 63);
    int bl = (a & 31) - (b & 31);
    return pack565((unsigned)(r < 0 ? 0 : r), (unsigned)(g6 < 0 ? 0 : g6 >> 1), (unsigned)(bl < 0 ? 0 : bl));
}

/* Halved subtract on the packed word with guard bits between channels, then each
 * channel keeps its low bits only if its guard survived (no borrow). */
static inline uint16_t math_sub_half(uint16_t a, uint16_t b)
{
    uint32_t t = (((uint32_t)a | 0x10820u) - (b & 0xF7DEu)) >> 1;
    unsigned r = (t >> 11) & 31, g = (t >> 5) & 63, bl = t & 31;
    r = (r & 0x10) ? r & 0x0F : 0;
    g = (g & 0x20) ? g & 0x1F : 0;
    bl = (bl & 0x10) ? bl & 0x0F : 0;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* ---- windows -------------------------------------------------------------------- */

/* Sets out[x] to 1 inside the combined window and 0 outside. Returns false,
 * leaving out alone, when neither window is enabled (nothing is inside). */
static bool window_mask(const VidWindows *ww, int layer, uint8_t *out)
{
    VidWinUse u1 = ww->w1[layer], u2 = ww->w2[layer];
    bool en1 = (u1 & 2) != 0, en2 = (u2 & 2) != 0;
    if (!en1 && !en2)
        return false;
    int l1 = ww->w1_left, r1 = ww->w1_right, l2 = ww->w2_left, r2 = ww->w2_right;
    uint8_t inv1 = (uint8_t)(u1 & 1), inv2 = (uint8_t)(u2 & 1);
    VidWinLogic logic = ww->logic[layer];
    for (int x = 0; x < W; x++) {
        uint8_t in1 = (uint8_t)((x >= l1 && x <= r1) ^ inv1);
        uint8_t in2 = (uint8_t)((x >= l2 && x <= r2) ^ inv2);
        uint8_t v;
        if (!en2)
            v = in1;
        else if (!en1)
            v = in2;
        else {
            switch (logic) {
            case VID_WIN_OR: v = in1 | in2; break;
            case VID_WIN_AND: v = in1 & in2; break;
            case VID_WIN_XOR: v = in1 ^ in2; break;
            default: v = (uint8_t)!(in1 ^ in2); break;
            }
        }
        out[x] = v;
    }
    return true;
}

/* ---- tiles ----------------------------------------------------------------------- */

/* One row (ty) of the tile whose bitplanes start at VRAM byte addr, decoded to
 * eight pixel values: byte tx of the result is the pixel at column tx. */
static inline uint64_t tile_row(const uint8_t *vram, uint32_t addr, int planes, int ty)
{
    uint32_t row = addr + (uint32_t)(ty * 2);
    uint64_t v = expand8[vram[row & 0xFFFF]] | expand8[vram[(row + 1) & 0xFFFF]] << 1;
    if (planes >= 4)
        v |= expand8[vram[(row + 16) & 0xFFFF]] << 2 | expand8[vram[(row + 17) & 0xFFFF]] << 3;
    if (planes == 8)
        v |= expand8[vram[(row + 32) & 0xFFFF]] << 4 | expand8[vram[(row + 33) & 0xFFFF]] << 5 |
             expand8[vram[(row + 48) & 0xFFFF]] << 6 | expand8[vram[(row + 49) & 0xFFFF]] << 7;
    return v;
}

static inline int bg_mosaic_size(const VidLineState *st, int bg)
{
    return st->bg[bg].mosaic ? st->mosaic_size : 1;
}

/* Tilemap entry for tile (row, col). Maps are laid out as 32x32 quadrants: the
 * right half of a 64-wide map starts 1024 entries in, the bottom half of a
 * 64-tall map after the top half. `base` is a VRAM byte address. */
static inline uint16_t map_entry(const uint8_t *vram, uint32_t base, bool wide, bool tall, uint32_t row, uint32_t col)
{
    uint32_t quad_x = ((col & 0x20) && wide) ? 1024 : 0;
    uint32_t quad_y = ((row & 0x20) && tall) ? (wide ? 2048 : 1024) : 0;
    uint32_t a = (base + (quad_y + ((row & 0x1F) << 5) + quad_x + (col & 0x1F)) * 2) & 0xFFFF;
    return (uint16_t)(vram[a] | vram[(a + 1) & 0xFFFF] << 8);
}

/* draw_bg without mosaic, offset-per-tile or hi-res: a tile at a time, writing
 * every pixel of the line. */
static inline void draw_bg_tiles(LayerBuf *lb, const LineCtx *c, int bg, int planes, unsigned pal_base, bool direct)
{
    const SpanCtx *sp = c->sp;
    const VidBg *b = &sp->st->bg[bg];
    const uint8_t *vram = sp->vram;
    bool wide = b->map_w == 64, tall = b->map_h == 64;
    uint32_t wmask = (wide ? 512u : 256u) - 1;
    uint32_t hmask = (tall ? 512u : 256u) - 1;
    uint32_t char_base = (uint32_t)b->char_base << 1;
    uint32_t tile_bytes = (uint32_t)planes * 8;
    const uint8_t *depth = sp->depth[bg];

    uint32_t by = ((uint32_t)c->line + b->vofs) & hmask;
    uint32_t bx = b->hofs & wmask;
    /* map_entry, with the row's part of the address computed once. */
    uint32_t map_row = by >> 3;
    uint32_t quad_y = ((map_row & 0x20) && tall) ? (wide ? 2048 : 1024) : 0;
    uint32_t row_base = ((uint32_t)b->map_base << 1) + (quad_y + ((map_row & 0x1F) << 5)) * 2;
    uint32_t quad_x = wide ? 1024 : 0;   /* for columns with bit 5 set */

    for (int x = 0; x < W;) {
        int tx0 = (int)(bx & 7);
        int n = min_i(8 - tx0, W - x);
        uint32_t col = bx >> 3;
        uint32_t a = (row_base + (((col & 0x20) ? quad_x : 0) + (col & 0x1F)) * 2) & 0xFFFF;
        uint16_t entry = (uint16_t)(vram[a] | vram[(a + 1) & 0xFFFF] << 8);
        x += n;
        bx = (bx + (uint32_t)n) & wmask;

        int ty = (entry & 0x8000) ? 7 - (int)(by & 7) : (int)(by & 7);
        uint32_t addr = (char_base + (entry & 0x3FFu) * tile_bytes) & 0xFFFF;
        uint64_t row = tile_row(vram, addr, planes, ty);
        uint8_t pal = (entry >> 10) & 7;
        unsigned cbase = planes == 8 ? 0 : pal_base + pal * (1u << planes);
        /* Eight pixels are written from the tile's first on screen; the next
         * tile overwrites the extra ones. */
        uint8_t *z = lb->z + x - n;
        uint16_t *color = lb->color + x - n;
        if (!row) {   /* transparent tile row, common in sparse layers */
            memset(z, 0, 8);
            continue;
        }
        if (entry & 0x4000)
            row = reverse_bytes(row);   /* horizontal flip */
        row >>= 8 * tx0;                /* byte i: the pixel at the tile's first on screen + i */
        uint64_t zs = nonzero_bytes(row) * depth[(entry >> 13) & 1];   /* depth or 0 per byte */
        for (int i = 0; i < 8; i++)
            z[i] = (uint8_t)(zs >> (8 * i));
        if (direct) {
            for (int i = 0; i < 8; i++)
                color[i] = direct565((uint8_t)(row >> (8 * i)), pal, sp->brightness);
        } else {
            /* Index at most 127 below 8bpp, 255 at 8bpp: no wrap. */
            const uint16_t *pals = sp->pal + cbase;
            for (int i = 0; i < 8; i++)
                color[i] = pals[(uint8_t)(row >> (8 * i))];
        }
    }
}

/* Normal (modes 0-6) BG layer. planes: 2, 4 or 8. hires: sample at double width.
 * In modes 2, 4 and 6, BG1 and BG2 take per-column scroll offsets from BG3's
 * tilemap (offset-per-tile); the leftmost tile column is never affected. */
static void draw_bg(LayerBuf *lb, const LineCtx *c, int bg, int planes, bool hires)
{
    const SpanCtx *sp = c->sp;
    const VidLineState *st = sp->st;
    uint8_t mode = sp->mode;
    unsigned pal_base = (mode == 0) ? (unsigned)(bg << 5) : 0;
    bool direct = planes == 8 && st->math.direct;
    bool opt = (mode == 2 || mode == 4 || mode == 6) && bg < 2;
    int msize = bg_mosaic_size(st, bg);

    if (!opt && !hires && msize == 1) {
        /* Constant plane counts let the compiler specialize the tile loop. */
        switch (planes) {
        case 2: draw_bg_tiles(lb, c, bg, 2, pal_base, false); break;
        case 4: draw_bg_tiles(lb, c, bg, 4, pal_base, false); break;
        default: draw_bg_tiles(lb, c, bg, 8, 0, direct); break;
        }
        return;
    }

    memset(lb->z, 0, sizeof lb->z);

    const uint8_t *vram = sp->vram;
    const VidBg *b = &st->bg[bg], *b3 = &st->bg[2];
    bool wide = b->map_w == 64, tall = b->map_h == 64;
    uint32_t wmask = (wide ? 512u : 256u) - 1;
    uint32_t hmask = (tall ? 512u : 256u) - 1;
    uint32_t map_base = (uint32_t)b->map_base << 1;
    uint32_t char_base = (uint32_t)b->char_base << 1;
    uint32_t tile_bytes = (uint32_t)planes * 8;
    const uint8_t *depth = sp->depth[bg];
    uint32_t map3 = (uint32_t)b3->map_base << 1;
    bool wide3 = b3->map_w == 64, tall3 = b3->map_h == 64;
    uint16_t opt_enable = (uint16_t)(0x2000 << bg);
    uint32_t hrow = (uint32_t)b3->vofs >> 3;
    uint32_t vrow = ((uint32_t)b3->vofs + 8) >> 3;
    int y = c->line - ((c->line - 1) % msize);
    uint32_t cached_addr = UINT32_MAX;
    int cached_ty = -1;
    uint64_t row = 0;

    for (int sx = 0; sx < W; sx += msize) {
        uint32_t hofs = b->hofs, vofs = b->vofs;
        if (opt && sx >= 8 - (int)(hofs & 7)) {
            int seg = sx - (int)((sx + hofs) & 7);   /* first pixel of this BG tile on screen */
            uint32_t col = ((uint32_t)(b3->hofs + seg - 1) & 0x1FF) >> 3;
            uint16_t hword = map_entry(vram, map3, wide3, tall3, hrow, col), vword;
            if (mode == 4) {
                vword = (hword & 0x8000) ? hword : 0;
                if (hword & 0x8000)
                    hword = 0;
            } else {
                vword = map_entry(vram, map3, wide3, tall3, vrow, col);
            }
            if (vword & opt_enable)
                vofs = vword & 0x3FF;
            if (hword & opt_enable)
                hofs = (uint32_t)((hword & 0x3F8) | (hofs & 7));
        }
        uint32_t bx = ((uint32_t)(hires ? sx * 2 : sx) + hofs) & wmask;
        uint32_t by = ((uint32_t)y + vofs) & hmask;
        uint16_t entry = map_entry(vram, map_base, wide, tall, by >> 3, bx >> 3);
        int tx = bx & 7, ty = by & 7;
        if (entry & 0x4000)
            tx = 7 - tx;
        if (entry & 0x8000)
            ty = 7 - ty;
        uint32_t addr = (char_base + (entry & 0x3FFu) * tile_bytes) & 0xFFFF;
        if (addr != cached_addr || ty != cached_ty) {
            row = tile_row(vram, addr, planes, ty);
            cached_addr = addr;
            cached_ty = ty;
        }
        uint8_t px = (uint8_t)(row >> (8 * tx));
        if (!px)
            continue;
        uint8_t pal = (entry >> 10) & 7;
        uint8_t z = depth[(entry >> 13) & 1];
        uint16_t color;
        if (direct)
            color = direct565(px, pal, sp->brightness);
        else if (planes == 8)
            color = sp->pal[px];
        else
            color = sp->pal[(pal_base + pal * (1u << planes) + px) & 0xFF];
        for (int x = sx, end = min_i(sx + msize, W); x < end; x++) {
            lb->z[x] = z;
            lb->color[x] = color;
        }
    }
}

static int m7_clip(int n) { return (n & 0x2000) ? (n | ~1023) : (n & 1023); }

/* Mode 7 BG1, and BG2 when EXTBG is set (bit 7 of each pixel is its priority). */
static void draw_mode7(LayerBuf *lb, const LineCtx *c, int bg)
{
    const SpanCtx *sp = c->sp;
    const VidLineState *st = sp->st;
    const VidMode7 *m7 = &st->mode7;
    int msize = bg_mosaic_size(st, bg);
    int y = c->line - ((c->line - 1) % msize);
    if (m7->vflip)
        y = 255 - y;
    int a = m7->a, b = m7->b, cc = m7->c, d = m7->d;
    int cx = m7->cx, cy = m7->cy;
    int hx = m7_clip(m7->hofs - cx), vy = m7_clip(m7->vofs - cy);
    int ox = ((a * hx) & ~63) + ((b * vy) & ~63) + ((b * y) & ~63) + (cx * 256);
    int oy = ((cc * hx) & ~63) + ((d * vy) & ~63) + ((d * y) & ~63) + (cy * 256);
    bool direct = bg == L_BG1 && st->math.direct;
    const uint8_t *depth = sp->depth[bg];
    const uint8_t *vram = sp->vram;
    bool limit = (m7->outside & 2) != 0, fill = (m7->outside & 1) != 0;

    if (msize == 1 && !direct) {
        /* Every pixel written, with no branches: the coordinates step by (a, c)
         * per pixel (the same values as ox + a * sx), and pixels outside the
         * 1024x1024 map either read tile 0 or are transparent. */
        int step = m7->hflip ? -1 : 1;
        int ax = ox + a * (m7->hflip ? 255 : 0), ay = oy + cc * (m7->hflip ? 255 : 0);
        bool extbg = bg == L_BG2;
        for (int x = 0; x < W; x++, ax += a * step, ay += cc * step) {
            int px = ax >> 8, py = ay >> 8;
            bool outside = limit & (((px | py) & ~1023) != 0);
            uint8_t tile = vram[(((py >> 3) & 127) * 128 + ((px >> 3) & 127)) * 2];
            tile = outside ? 0 : tile;
            uint8_t v = vram[(((unsigned)tile << 6) + (unsigned)((py & 7) << 3) + (unsigned)(px & 7)) * 2 + 1];
            uint8_t index = extbg ? v & 0x7F : v;
            uint8_t zt = extbg ? depth[v >> 7] : depth[0];
            lb->z[x] = (index && !(outside & !fill)) ? zt : 0;
            lb->color[x] = sp->pal[index];
        }
        return;
    }

    memset(lb->z, 0, sizeof lb->z);
    for (int s = 0; s < W; s += msize) {
        int sx = m7->hflip ? 255 - s : s;
        int px = (ox + a * sx) >> 8;
        int py = (oy + cc * sx) >> 8;
        uint8_t tile;
        if (limit && ((px | py) & ~1023)) {
            if (!fill)
                continue;
            tile = 0;
        } else {
            tile = vram[(((py >> 3) & 127) * 128 + ((px >> 3) & 127)) * 2];
        }
        uint8_t v = vram[(((unsigned)tile << 6) + (unsigned)((py & 7) << 3) + (unsigned)(px & 7)) * 2 + 1];
        uint8_t z;
        uint16_t color;
        if (bg == L_BG2) {
            if (!(v & 0x7F))
                continue;
            z = depth[v >> 7];
            color = sp->pal[v & 0x7F];
        } else {
            if (!v)
                continue;
            z = depth[0];
            color = direct ? direct565(v, 0, sp->brightness) : sp->pal[v];
        }
        for (int x = s, end = min_i(s + msize, W); x < end; x++) {
            lb->z[x] = z;
            lb->color[x] = color;
        }
    }
}

/* ---- sprites ----------------------------------------------------------------------- */

static const uint8_t obj_width[8][2] = {
    { 8, 16 }, { 8, 32 }, { 8, 64 }, { 16, 32 }, { 16, 64 }, { 32, 64 }, { 16, 32 }, { 16, 32 },
};
static const uint8_t obj_height[8][2] = {
    { 8, 16 }, { 8, 32 }, { 8, 64 }, { 16, 32 }, { 16, 64 }, { 32, 64 }, { 32, 64 }, { 32, 32 },
};

/* The rows each sprite covers: a sprite entirely off the left edge covers none.
 * Done once per frame, again wherever the sprites or their sizes change. */
static void sprites_prepare(VidRenderer *r, const VidSprite *sprites, uint8_t size)
{
    memset(r->sprites.rows, 0, sizeof r->sprites.rows);
    for (int n = 0; n < VID_SPRITES; n++) {
        const VidSprite *s = &sprites[n];
        int large = s->large ? 1 : 0;
        int w = obj_width[size][large];
        int x9 = s->x & 0x1FF;
        r->sprites.x[n] = (int16_t)(x9 >= 256 ? x9 - 512 : x9);
        r->sprites.w[n] = (uint8_t)w;
        r->sprites.h[n] = obj_height[size][large];
        int count = (x9 > 256 && x9 + w - 1 < 512) ? 0 : obj_height[size][large];
        for (int k = 0; k < count; k++)
            r->sprites.rows[(s->y + k) & 0xFF][n >> 6] |= (uint64_t)1 << (n & 63);
    }
}

/* Sprites on this line. */
static void draw_obj(VidRenderer *r, LayerBuf *lb, const LineCtx *c)
{
    const SpanCtx *sp = c->sp;
    const VidObjConfig *obj = &sp->st->obj;
    uint32_t base = (uint32_t)obj->base << 1;
    uint32_t name_select = (uint32_t)obj->name_select << 1;
    int row = c->line - 1;
    int first = sp->first_sprite;
    const uint64_t *bits = r->sprites.rows[row & 0xFF];
    const uint8_t *vram = sp->vram;

    /* In range: up to 32 sprites in OAM order from the first sprite; a 33rd sets range over. */
    int list[32], count = 0;
    for (int seg = 0; seg < 2; seg++) {
        int lo = seg == 0 ? first : 0, hi = seg == 0 ? VID_SPRITES : first;
        for (int wi = 0; wi < 2; wi++) {
            int wbase = wi * 64;
            if (lo >= wbase + 64 || hi <= wbase)
                continue;
            uint64_t word = bits[wi];
            if (lo > wbase)
                word &= ~(uint64_t)0 << (lo - wbase);
            if (hi < wbase + 64)
                word &= ~(uint64_t)0 >> (64 - (hi - wbase));
            while (word) {
                if (count == 32) {
                    r->range_over = true;
                    goto listed;
                }
                list[count++] = wbase + lowest_bit(word);
                word &= word - 1;
            }
        }
    }
listed:;

    /* Tile limit: at most 34 8x8 tiles per line; later sprites lose theirs. */
    int tiles = 0, keep = count;
    for (int k = 0; k < count && keep == count; k++) {
        int n = list[k];
        int w = r->sprites.w[n], x = r->sprites.x[n];
        for (int t = 0; t < w; t += 8) {
            if (x + t > -8 && x + t < 256 && ++tiles > 34) {
                r->time_over = true;
                keep = k;
                break;
            }
        }
    }

    memset(lb->z, 0, sizeof lb->z);
    const uint8_t *depth = sp->depth[L_OBJ];
    /* Back to front, so earlier sprites overwrite later ones. */
    for (int k = keep - 1; k >= 0; k--) {
        int n = list[k];
        const VidSprite *s = &sp->sprites[n];
        int w = r->sprites.w[n], h = r->sprites.h[n], x = r->sprites.x[n];
        int dy = (row - s->y) & 0xFF;
        if (s->vflip)
            dy = h - 1 - dy;
        uint16_t name = s->tile;
        uint8_t pal = s->palette;
        const uint16_t *pals = sp->pal + 128 + pal * 16;
        uint8_t zt = depth[s->priority], math = pal >= 4;   /* color math only for palettes 4-7 */
        bool hflip = s->hflip;
        for (int t = 0; t < w; t += 8) {
            int x0 = x + t;
            if (x0 >= W || x0 + 8 <= 0)
                continue;
            /* With a horizontal flip, screen pixels t..t+7 show sprite columns
             * w-1-t down to w-8-t: one tile, mirrored. */
            int dxt = hflip ? w - 8 - t : t;
            uint32_t tile = (uint32_t)name + (uint32_t)((dy >> 3) * 16 + (dxt >> 3));
            uint32_t addr = base + ((tile & 0x3FF) << 5);
            if (tile & 0x100)
                addr += name_select;
            uint64_t pixels = tile_row(vram, addr & 0xFFFF, 4, dy & 7);
            if (!pixels)
                continue;
            for (int j = 0; j < 8; j++) {
                int sx = x0 + j;
                if (sx < 0 || sx >= W)
                    continue;
                uint8_t v = (uint8_t)(pixels >> (8 * (hflip ? 7 - j : j)));
                if (!v)
                    continue;
                lb->z[sx] = zt;
                lb->color[sx] = pals[v];
                lb->math[sx] = math;
            }
        }
    }
}

/* ---- priority ------------------------------------------------------------------------ */

/* Front-to-back layer order per mode; each token is layer * 4 + priority. */
#define T(layer, prio) (uint8_t)((layer) * 4 + (prio))
#define END 0xFF
static const uint8_t order_mode0[] = { T(L_OBJ, 3), T(L_BG1, 1), T(L_BG2, 1), T(L_OBJ, 2), T(L_BG1, 0), T(L_BG2, 0),
                                       T(L_OBJ, 1), T(L_BG3, 1), T(L_BG4, 1), T(L_OBJ, 0), T(L_BG3, 0), T(L_BG4, 0), END };
static const uint8_t order_mode1[] = { T(L_OBJ, 3), T(L_BG1, 1), T(L_BG2, 1), T(L_OBJ, 2), T(L_BG1, 0), T(L_BG2, 0),
                                       T(L_OBJ, 1), T(L_BG3, 1), T(L_OBJ, 0), T(L_BG3, 0), END };
static const uint8_t order_mode1_bg3[] = { T(L_BG3, 1), T(L_OBJ, 3), T(L_BG1, 1), T(L_BG2, 1), T(L_OBJ, 2), T(L_BG1, 0),
                                           T(L_BG2, 0), T(L_OBJ, 1), T(L_OBJ, 0), T(L_BG3, 0), END };
static const uint8_t order_mode2_5[] = { T(L_OBJ, 3), T(L_BG1, 1), T(L_OBJ, 2), T(L_BG2, 1), T(L_OBJ, 1), T(L_BG1, 0),
                                         T(L_OBJ, 0), T(L_BG2, 0), END };
static const uint8_t order_mode6[] = { T(L_OBJ, 3), T(L_BG1, 1), T(L_OBJ, 2), T(L_OBJ, 1), T(L_BG1, 0), T(L_OBJ, 0), END };
static const uint8_t order_mode7[] = { T(L_OBJ, 3), T(L_OBJ, 2), T(L_OBJ, 1), T(L_BG1, 0), T(L_OBJ, 0), END };
static const uint8_t order_mode7_ext[] = { T(L_OBJ, 3), T(L_OBJ, 2), T(L_BG2, 1), T(L_OBJ, 1), T(L_BG1, 0), T(L_BG2, 0),
                                           T(L_OBJ, 0), END };

static const uint8_t *layer_order(const VidLineState *st)
{
    switch (st->mode.mode) {
    case 0: return order_mode0;
    case 1: return st->mode.bg3_priority ? order_mode1_bg3 : order_mode1;
    case 6: return order_mode6;
    case 7: return st->config.extbg ? order_mode7_ext : order_mode7;
    default: return order_mode2_5;
    }
}

/* Depth of each (layer, priority): the front token gets the largest. */
static void order_depths(const uint8_t *order, uint8_t depth[L_OBJ + 1][4])
{
    memset(depth, 0, sizeof(uint8_t) * (L_OBJ + 1) * 4);
    for (int i = 0; order[i] != END; i++)
        depth[order[i] >> 2][order[i] & 3] = (uint8_t)(15 - i);
}

/* ---- screens ----------------------------------------------------------------------- */

static const uint8_t no_window[W];

/* Merges a layer into a screen: where the layer's pixel is in front of what the
 * screen has and hide[x] is 0, it replaces it. Branch-free so compilers can
 * vectorize it. */
static void merge(Screen *s, const LayerBuf *lb, const uint8_t *restrict hide, uint8_t math, bool obj_math)
{
    uint8_t *restrict sz = s->z, *restrict smath = s->math;
    uint16_t *restrict scolor = s->color;
    const uint8_t *restrict lz = lb->z, *restrict lmath = lb->math;
    const uint16_t *restrict lcolor = lb->color;
    for (int x = 0; x < W; x++) {
        uint8_t take8 = (uint8_t)-(uint8_t)((lz[x] > sz[x]) & (hide[x] == 0));   /* 0 or 0xFF */
        uint16_t take16 = (uint16_t)-(uint16_t)(take8 & 1);
        uint8_t pixel_math = obj_math ? (uint8_t)(math & lmath[x]) : math;
        sz[x] = (uint8_t)((sz[x] & ~take8) | (lz[x] & take8));
        scolor[x] = (uint16_t)((scolor[x] & ~take16) | (lcolor[x] & take16));
        smath[x] = (uint8_t)((smath[x] & ~take8) | (pixel_math & take8));
    }
}

/* Final colors for a line with color math. Called with constant flags so each
 * combination gets its own branch-free loop. */
static inline void compose_math(uint32_t *restrict out, const Screen *m, const Screen *s, const uint8_t *restrict clip,
                                const uint8_t *restrict math_on, bool subtract, bool halve_on, bool sub_add,
                                int add_cap)
{
    for (int x = 0; x < W; x++) {
        uint16_t main565 = m->color[x] & (uint16_t)(clip[x] - 1);   /* clip[x] 1: black */
        uint16_t sub565 = s->color[x];
        uint16_t halve = (uint16_t)-(uint16_t)(halve_on & (clip[x] == 0) & (!sub_add | (s->z[x] != 0)));
        uint16_t apply = (uint16_t)-(uint16_t)(m->math[x] & math_on[x]);
        uint16_t full = subtract ? math_sub(main565, sub565) : math_add(main565, sub565, add_cap);
        uint16_t half = subtract ? math_sub_half(main565, sub565) : math_add_half(main565, sub565);
        uint16_t result = (full & ~halve) | (half & halve);
        out[x] = rgb565_to_xrgb8888((uint16_t)((result & apply) | (main565 & ~apply)));
    }
}

/* ---- span and line ------------------------------------------------------------------- */

/* Everything a span shares, worked out once for all its lines. */
static void span_begin(SpanCtx *sp, const VidSpan *span, const VidFrame *f)
{
    const VidLineState *st = &span->state;
    sp->st = st;
    sp->vram = span->vram ? span->vram : f->vram;
    sp->sprites = span->sprites ? span->sprites->sprites : f->sprites;
    sp->first_sprite = span->sprites ? span->sprites->first_sprite : f->first_sprite;
    const uint16_t *cgram = span->cgram ? span->cgram : f->cgram;
    sp->brightness = (uint8_t)(st->display.brightness & 15);
    for (int i = 0; i < 256; i++)
        sp->pal[i] = color565(cgram[i], sp->brightness);
    order_depths(layer_order(st), sp->depth);
    const uint8_t *s = scale5[sp->brightness];
    sp->fixed565 = pack565(s[st->fixed_color & 31], s[(st->fixed_color >> 5) & 31], s[(st->fixed_color >> 10) & 31]);
    sp->mode = st->mode.mode;
    sp->main_layers = st->main_layers;
    sp->sub_layers = st->sub_layers;
    sp->win_main = st->windows.win_main;
    sp->win_sub = st->windows.win_sub;
    sp->used = (uint8_t)(st->main_layers | st->sub_layers);
    sp->sub_add = st->math.sub_screen;
    /* Color math can apply somewhere on this line: some layer (or the backdrop)
     * has it enabled, and the color window setting isn't "never". */
    sp->any_math = (st->math.layers & 0x3F) != 0 && st->math.prevent_math != VID_REGION_ALWAYS;
    /* The sub screen's layers are only looked at for math with sub screen addition. */
    sp->need_sub = sp->any_math && sp->sub_add;
    sp->add_cap = sp->brightness == 15 ? 31 : brightness_scale(31, sp->brightness);

    for (int layer = L_BG1; layer <= L_OBJ; layer++) {
        uint8_t bit = (uint8_t)(1 << layer);
        sp->windowed[layer] = ((sp->win_main | (sp->need_sub ? sp->win_sub : 0)) & bit) != 0 &&
                              window_mask(&st->windows, layer, sp->win[layer]);
    }

    /* Color window: per value (outside, inside), whether to clip the main screen
     * to black and whether color math is on. */
    uint8_t clip_when[2], math_when[2];
    for (int v = 0; v < 2; v++) {
        switch (st->math.clip_black) {
        case VID_REGION_NEVER: clip_when[v] = 0; break;
        case VID_REGION_OUTSIDE: clip_when[v] = (uint8_t)!v; break;
        case VID_REGION_INSIDE: clip_when[v] = (uint8_t)v; break;
        default: clip_when[v] = 1; break;
        }
        switch (st->math.prevent_math) {
        case VID_REGION_NEVER: math_when[v] = 1; break;
        case VID_REGION_OUTSIDE: math_when[v] = (uint8_t)v; break;
        case VID_REGION_INSIDE: math_when[v] = (uint8_t)!v; break;
        default: math_when[v] = 0; break;
        }
    }
    if (window_mask(&st->windows, L_BACK, sp->win[L_BACK])) {
        for (int x = 0; x < W; x++) {
            sp->clip[x] = clip_when[sp->win[L_BACK][x]];
            sp->math_on[x] = math_when[sp->win[L_BACK][x]];
        }
    } else {
        memset(sp->clip, clip_when[0], sizeof sp->clip);
        memset(sp->math_on, math_when[0], sizeof sp->math_on);
    }
}

static void draw_line(VidRenderer *r, const LineCtx *c, uint32_t *out)
{
    const SpanCtx *sp = c->sp;
    const VidLineState *st = sp->st;
    Screen *main_s = &r->main_s, *sub_s = &r->sub_s;
    LayerBuf *lb = &r->lb;
    uint8_t layers_math = st->math.layers;

    memset(main_s->z, 0, sizeof main_s->z);
    for (int x = 0; x < W; x++) {
        main_s->color[x] = sp->pal[0];   /* backdrop */
        main_s->math[x] = (layers_math & VID_LAYER_BIT(VID_BACK)) != 0;
    }
    if (sp->any_math) {
        memset(sub_s->z, 0, sizeof sub_s->z);
        for (int x = 0; x < W; x++)
            sub_s->color[x] = sp->fixed565;
    }

    for (int layer = L_BG1; layer <= L_OBJ; layer++) {
        if (!(sp->used & (1 << layer)))
            continue;
        switch (layer == L_OBJ ? 8 : sp->mode) {
        case 0: draw_bg(lb, c, layer, 2, false); break;
        case 1:
            if (layer == L_BG4)
                continue;
            draw_bg(lb, c, layer, layer == L_BG3 ? 2 : 4, false);
            break;
        case 2:
        case 3:
        case 4:
        case 5:
            if (layer >= L_BG3)
                continue;
            {
                static const uint8_t planes[4][2] = { { 4, 4 }, { 8, 4 }, { 8, 2 }, { 4, 2 } };
                draw_bg(lb, c, layer, planes[sp->mode - 2][layer], sp->mode == 5);
            }
            break;
        case 6:
            if (layer != L_BG1)
                continue;
            draw_bg(lb, c, layer, 4, true);
            break;
        case 7:
            if (layer >= L_BG3 || (layer == L_BG2 && !st->config.extbg))
                continue;
            draw_mode7(lb, c, layer);
            break;
        default:
            draw_obj(r, lb, c);
            break;
        }

        uint8_t bit = (uint8_t)(1 << layer);
        bool windowed = sp->windowed[layer];
        uint8_t math = (layers_math & bit) != 0;
        if (sp->main_layers & bit)
            merge(main_s, lb, windowed && (sp->win_main & bit) ? sp->win[layer] : no_window, math, layer == L_OBJ);
        if (sp->need_sub && (sp->sub_layers & bit))
            merge(sub_s, lb, windowed && (sp->win_sub & bit) ? sp->win[layer] : no_window, math, layer == L_OBJ);
    }

    if (!sp->any_math) {
        for (int x = 0; x < W; x++)
            out[x] = rgb565_to_xrgb8888(sp->clip[x] ? 0 : main_s->color[x]);
        return;
    }
    int cap = sp->add_cap;
    switch ((st->math.subtract ? 2 : 0) | (st->math.half ? 1 : 0) | (sp->sub_add ? 4 : 0)) {
    case 0: compose_math(out, main_s, sub_s, sp->clip, sp->math_on, false, false, false, cap); break;
    case 1: compose_math(out, main_s, sub_s, sp->clip, sp->math_on, false, true, false, cap); break;
    case 2: compose_math(out, main_s, sub_s, sp->clip, sp->math_on, true, false, false, cap); break;
    case 3: compose_math(out, main_s, sub_s, sp->clip, sp->math_on, true, true, false, cap); break;
    case 4: compose_math(out, main_s, sub_s, sp->clip, sp->math_on, false, false, true, cap); break;
    case 5: compose_math(out, main_s, sub_s, sp->clip, sp->math_on, false, true, true, cap); break;
    case 6: compose_math(out, main_s, sub_s, sp->clip, sp->math_on, true, false, true, cap); break;
    default: compose_math(out, main_s, sub_s, sp->clip, sp->math_on, true, true, true, cap); break;
    }
}

/* ---- entry points ---------------------------------------------------------------------- */

VidRenderer *vid_render_create(void)
{
    tables_init();
    return calloc(1, sizeof(VidRenderer));
}

void vid_render_destroy(VidRenderer *r) { free(r); }

void vid_render_frame(VidRenderer *r, const VidFrame *f, uint32_t *out, VidRenderFlags *flags)
{
    r->range_over = r->time_over = false;
    if (f->span_count == 0) {
        memset(out, 0, sizeof *out * (size_t)W * H);
        if (flags)
            *flags = (VidRenderFlags){ false, false };
        return;
    }

    /* The sprites' geometry is rebuilt when the sprites themselves or the size
     * pair OBSEL selects change, both of which a span carries. */
    uint8_t size = 0xFF;
    const VidSprite *sprites = NULL;

    for (uint16_t i = 0; i < f->span_count; i++) {
        const VidSpan *span = &f->spans[i];
        int from = span->line < 1 ? 1 : span->line;
        int to = (i + 1 < f->span_count) ? f->spans[i + 1].line - 1 : H;
        if (to > H)
            to = H;
        if (from > to)
            continue;
        span_begin(&r->span, span, f);
        if (r->span.sprites != sprites || span->state.obj.size_select != size) {
            sprites = r->span.sprites;
            size = span->state.obj.size_select;
            sprites_prepare(r, sprites, size);
        }
        for (int line = from; line <= to; line++) {
            uint32_t *row = out + (size_t)(line - 1) * W;
            if (span->state.display.blank) {
                memset(row, 0, sizeof *row * W);
                continue;
            }
            LineCtx c = { .sp = &r->span, .line = line };
            draw_line(r, &c, row);
        }
    }
    if (flags) {
        flags->range_over = r->range_over;
        flags->time_over = r->time_over;
    }
}
