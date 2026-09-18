#include "render.h"

#include <stdbool.h>
#include <string.h>

/* How a line is drawn (docs/RUNTIME.md, Renderer):
 *
 * 1. Each enabled layer is drawn into a LayerBuf: per pixel, a depth taken from
 *    the mode's front-to-back layer order (0 for transparent) and the RGB565
 *    color. BG layers are drawn a tile at a time: one tilemap lookup and one
 *    bitplane decode per 8 pixels (tile_row). Mosaic, offset-per-tile and
 *    hi-res fall back to one sample per mosaic block.
 * 2. Each layer is merged into the main and sub screens it's enabled on: the
 *    pixel with the larger depth wins, unless its window masks it on that
 *    screen. Taking the largest depth is the same as walking the order list
 *    front to back and taking the first visible pixel, because every (layer,
 *    priority) pair appears in a list at most once.
 * 3. Color math combines the two screens per pixel.
 *
 * Most lines come out the same as in the previous frame, so a line is only
 * drawn when something it depends on changed (RenderLineCache): its registers,
 * the sprites on its row, the CGRAM entries and VRAM pages it read. The
 * palette as RGB565 and the per-row sprite lists are cached across lines. */

enum { L_BG1, L_BG2, L_BG3, L_BG4, L_OBJ, L_BACK, L_COUNT };

#define W RENDER_WIDTH

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

/* Per-line state shared by the layer drawers, which also record what they read. */
typedef struct {
    Ppu *p;
    int line;
    uint8_t brightness;
    const uint16_t *pal;           /* CGRAM as RGB565 */
    uint8_t depth[L_OBJ + 1][4];   /* layer, priority -> depth; 0: not in this mode's order */
    uint64_t palette_entries[4];   /* as in RenderLineCache */
    uint64_t vram_pages[RENDER_VRAM_PAGES / 64];
} LineCtx;

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
 * RGB565 with the green LSB copied from green's top bit, and runs color math
 * on those packed values. The libretro frontend receives RGB565, and
 * docs/ORACLE.md widens it to 8 bits per channel by bit replication. The
 * helpers below reproduce that arithmetic so framebuffers hash identically. */

static uint8_t brightness_scale(uint8_t channel5, uint8_t brightness)
{
    return (uint8_t)((channel5 * brightness + 7) / 15);
}

static inline uint16_t pack565(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(r << 11 | g << 6 | (g & 0x10) << 1 | b);
}

uint16_t render_snes9x_color(uint16_t bgr555, uint8_t brightness)
{
    return pack565(brightness_scale(bgr555 & 31, brightness), brightness_scale((bgr555 >> 5) & 31, brightness),
                   brightness_scale((bgr555 >> 10) & 31, brightness));
}

static inline uint32_t rgb565_to_xrgb8888(uint16_t c)
{
    unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (uint32_t)((r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2));
}

uint32_t render_rgb565_to_xrgb8888(uint16_t c)
{
    return rgb565_to_xrgb8888(c);
}

/* Table-driven render_snes9x_color, for the renderer's own use. */
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

/* ---- cached state: palette, VRAM ---------------------------------------------------- */

static const uint16_t *palette_update(Renderer *r, const Ppu *p, uint8_t brightness)
{
    uint64_t next = r->palette.gen + 1;
    if (!r->palette.valid || r->palette.brightness != brightness) {
        for (int i = 0; i < 256; i++) {
            r->palette.rgb565[i] = color565(p->cgram[i], brightness);
            r->palette.entry_gen[i] = next;
        }
        memset(r->palette.last_changed, 0xFF, sizeof r->palette.last_changed);
    } else if (memcmp(r->palette.cgram, p->cgram, sizeof p->cgram) != 0) {
        memset(r->palette.last_changed, 0, sizeof r->palette.last_changed);
        for (int i = 0; i < 256; i++) {
            if (r->palette.cgram[i] != p->cgram[i]) {
                r->palette.rgb565[i] = color565(p->cgram[i], brightness);
                r->palette.entry_gen[i] = next;
                r->palette.last_changed[i >> 6] |= (uint64_t)1 << (i & 63);
            }
        }
    } else {
        return r->palette.rgb565;
    }
    memcpy(r->palette.cgram, p->cgram, sizeof p->cgram);
    r->palette.brightness = brightness;
    r->palette.valid = true;
    r->palette.gen = next;
    return r->palette.rgb565;
}

/* Updates the VRAM copy and page generations. VRAM is only written outside
 * active display, so comparing at the start of each frame (line 0) catches
 * writes in vblank and state loads, and the write counter catches writes in
 * forced blank during the frame. */
static void vram_check(Renderer *r, const Ppu *p, bool frame_start)
{
    if (r->vram.valid && !frame_start && p->vram_writes == r->vram.writes_seen)
        return;
    r->vram.writes_seen = p->vram_writes;
    if (r->vram.valid && !memcmp(r->vram.copy, p->vram, sizeof p->vram))
        return;
    uint64_t next = r->vram.gen + 1;
    const size_t size = (size_t)1 << RENDER_VRAM_PAGE_BITS;
    memset(r->vram.last_changed, 0, sizeof r->vram.last_changed);
    for (int page = 0; page < RENDER_VRAM_PAGES; page++) {
        uint8_t *copy = r->vram.copy + page * size;
        const uint8_t *live = p->vram + page * size;
        if (!r->vram.valid || memcmp(copy, live, size) != 0) {
            memcpy(copy, live, size);
            r->vram.page_gen[page] = next;
            r->vram.last_changed[page >> 6] |= (uint64_t)1 << (page & 63);
        }
    }
    r->vram.valid = true;
    r->vram.gen = next;
}

/* Whether none of the parts in mask (words * 64 bits) changed after generation
 * then. Lines are checked every frame, and a hit brings then up to date, so
 * usually at most one change (last_changed) has happened since. */
static bool parts_unchanged(const uint64_t *part_gen, uint64_t gen, const uint64_t *last_changed, uint64_t then,
                            const uint64_t *mask, int words)
{
    if (gen == then)
        return true;
    if (gen == then + 1) {
        uint64_t hit = 0;
        for (int w = 0; w < words; w++)
            hit |= mask[w] & last_changed[w];
        return hit == 0;
    }
    for (int w = 0; w < words; w++) {
        for (uint64_t m = mask[w]; m; m &= m - 1) {
            if (part_gen[w * 64 + lowest_bit(m)] > then)
                return false;
        }
    }
    return true;
}

static inline void mark_vram(uint64_t *pages, uint32_t addr)
{
    uint32_t page = (addr & 0xFFFF) >> RENDER_VRAM_PAGE_BITS;
    pages[page >> 6] |= (uint64_t)1 << (page & 63);
}

/* Marks the pages of the bytes bytes at addr, at most two pages' worth (wrapping at 64 KiB). */
static inline void mark_vram_span(uint64_t *pages, uint32_t addr, uint32_t bytes)
{
    mark_vram(pages, addr & 0xFFFF);
    mark_vram(pages, (addr + bytes - 1) & 0xFFFF);
}

/* ---- windows -------------------------------------------------------------------- */

/* sel: bit0 W1 invert, bit1 W1 enable, bit2 W2 invert, bit3 W2 enable. Sets
 * out[x] to 1 inside the combined window and 0 outside. Returns false, leaving
 * out alone, when neither window is enabled (nothing is inside). */
static bool window_mask(const Ppu *p, uint8_t sel, uint8_t logic, uint8_t *out)
{
    bool en1 = sel & 2, en2 = sel & 8;
    if (!en1 && !en2)
        return false;
    int l1 = p->reg[0x26], r1 = p->reg[0x27], l2 = p->reg[0x28], r2 = p->reg[0x29];
    uint8_t inv1 = sel & 1, inv2 = (sel >> 2) & 1;
    for (int x = 0; x < W; x++) {
        uint8_t in1 = (uint8_t)((x >= l1 && x <= r1) ^ inv1);
        uint8_t in2 = (uint8_t)((x >= l2 && x <= r2) ^ inv2);
        uint8_t v;
        if (!en2)
            v = in1;
        else if (!en1)
            v = in2;
        else {
            switch (logic & 3) {
            case 0: v = in1 | in2; break;
            case 1: v = in1 & in2; break;
            case 2: v = in1 ^ in2; break;
            default: v = (uint8_t)!(in1 ^ in2); break;
            }
        }
        out[x] = v;
    }
    return true;
}

static uint8_t window_sel(const Ppu *p, int layer)
{
    switch (layer) {
    case L_BG1: return p->reg[0x23] & 15;
    case L_BG2: return p->reg[0x23] >> 4;
    case L_BG3: return p->reg[0x24] & 15;
    case L_BG4: return p->reg[0x24] >> 4;
    case L_OBJ: return p->reg[0x25] & 15;
    default: return p->reg[0x25] >> 4;   /* color window */
    }
}

static uint8_t window_logic(const Ppu *p, int layer)
{
    if (layer <= L_BG4)
        return (p->reg[0x2A] >> (layer * 2)) & 3;
    return layer == L_OBJ ? (p->reg[0x2B] & 3) : ((p->reg[0x2B] >> 2) & 3);
}

/* ---- tiles ----------------------------------------------------------------------- */

/* One row (ty) of the tile whose bitplanes start at VRAM byte addr, decoded to
 * eight pixel values: byte tx of the result is the pixel at column tx. Plane n
 * of row ty is at addr + (n / 2) * 16 + ty * 2 + (n & 1), wrapping at 64 KiB,
 * so a tile's rows all lie in its planes * 8 bytes from addr. */
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

static int mosaic_size(const Ppu *p, int bg)
{
    return (p->reg[0x06] & (1 << bg)) ? (p->reg[0x06] >> 4) + 1 : 1;
}

/* Tilemap entry for tile (row, col). Maps are laid out as 32x32 quadrants:
 * the right half of a 64-wide map starts 1024 entries in, the bottom half of a
 * 64-tall map after the top half. */
static inline uint16_t map_entry(const Ppu *p, uint8_t sc, uint32_t row, uint32_t col)
{
    uint32_t base = (uint32_t)(sc & 0xFC) << 9;
    uint32_t quad_x = ((col & 0x20) && (sc & 1)) ? 1024 : 0;
    uint32_t quad_y = ((row & 0x20) && (sc & 2)) ? ((sc & 1) ? 2048 : 1024) : 0;
    uint32_t a = (base + (quad_y + ((row & 0x1F) << 5) + quad_x + (col & 0x1F)) * 2) & 0xFFFF;
    return (uint16_t)(p->vram[a] | p->vram[(a + 1) & 0xFFFF] << 8);
}

/* Marks CGRAM entry index as read when used. */
static inline void mark_entry(uint64_t entries[4], unsigned index, bool used)
{
    entries[(index >> 6) & 3] |= (uint64_t)used << (index & 63);
}

/* draw_bg without mosaic, offset-per-tile or hi-res: a tile at a time, writing
 * every pixel of the line. */
static inline void draw_bg_tiles(LayerBuf *lb, LineCtx *c, int bg, int planes, unsigned pal_base, bool direct)
{
    const Ppu *p = c->p;
    const uint8_t *vram = p->vram;
    uint8_t sc = p->reg[0x07 + bg];
    uint32_t wmask = ((sc & 1) ? 512u : 256u) - 1;
    uint32_t hmask = ((sc & 2) ? 512u : 256u) - 1;
    uint8_t nba = p->reg[bg < 2 ? 0x0B : 0x0C];
    uint32_t char_base = (uint32_t)(((bg & 1) ? nba >> 4 : nba) & 7) << 13;
    uint32_t tile_bytes = (uint32_t)planes * 8;
    const uint8_t *depth = c->depth[bg];
    /* Pages and entries read, kept in locals while drawing. */
    uint64_t pages[RENDER_VRAM_PAGES / 64] = { 0 }, entries[4] = { 0 };

    uint32_t by = ((uint32_t)c->line + p->bg_vofs[bg]) & hmask;
    uint32_t bx = p->bg_hofs[bg] & wmask;
    /* map_entry, with the row's part of the address computed once. */
    uint32_t map_row = by >> 3;
    uint32_t quad_y = ((map_row & 0x20) && (sc & 2)) ? ((sc & 1) ? 2048 : 1024) : 0;
    uint32_t row_base = ((uint32_t)(sc & 0xFC) << 9) + (quad_y + ((map_row & 0x1F) << 5)) * 2;
    uint32_t quad_x = (sc & 1) ? 1024 : 0;   /* for columns with bit 5 set */
    /* The line reads entries from this map row only: 32 entries (64 bytes,
     * row_base is a multiple of 64) and, in a 64-wide map, 32 more 2048 bytes on. */
    mark_vram_span(pages, row_base, 64);
    if (sc & 1)
        mark_vram_span(pages, row_base + 2048, 64);

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
        /* A tile's bytes start at a multiple of its size: 2bpp and 4bpp tiles
         * lie within one page, 8bpp tiles span two. */
        mark_vram(pages, addr);
        if (planes == 8)
            mark_vram(pages, addr + 63);
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
                color[i] = direct565((uint8_t)(row >> (8 * i)), pal, c->brightness);
        } else {
            /* Index at most 127 below 8bpp, 255 at 8bpp: no wrap. */
            const uint16_t *pals = c->pal + cbase;
            if (planes == 8) {
                for (int i = 0; i < 8; i++) {
                    uint8_t px = (uint8_t)(row >> (8 * i));
                    color[i] = pals[px];
                    mark_entry(entries, px, px != 0);
                }
            } else {
                /* Pixel values used, as a set; cbase is a multiple of 1 << planes,
                 * so the set fits in the entry word at cbase. */
                unsigned used = 0;
                for (int i = 0; i < 8; i++) {
                    uint8_t px = (uint8_t)(row >> (8 * i));
                    color[i] = pals[px];
                    used |= 1u << px;
                }
                entries[cbase >> 6] |= (uint64_t)(used & ~1u) << (cbase & 63);
            }
        }
    }
    for (int w = 0; w < RENDER_VRAM_PAGES / 64; w++)
        c->vram_pages[w] |= pages[w];
    for (int w = 0; w < 4; w++)
        c->palette_entries[w] |= entries[w];
}

/* Normal (modes 0-6) BG layer. planes: 2, 4 or 8. hires: sample at double width.
 * In modes 2, 4 and 6, BG1 and BG2 take per-column scroll offsets from BG3's
 * tilemap (offset-per-tile); the leftmost tile column is never affected. */
static void draw_bg(LayerBuf *lb, LineCtx *c, int bg, int planes, bool hires)
{
    const Ppu *p = c->p;
    uint8_t mode = p->reg[0x05] & 7;
    unsigned pal_base = (mode == 0) ? (unsigned)(bg << 5) : 0;
    bool direct = planes == 8 && (p->reg[0x30] & 1);
    bool opt = (mode == 2 || mode == 4 || mode == 6) && bg < 2;
    int msize = mosaic_size(p, bg);

    if (!opt && !hires && msize == 1) {
        /* Constant plane counts let the compiler specialize the tile loop. */
        switch (planes) {
        case 2: draw_bg_tiles(lb, c, bg, 2, pal_base, false); break;
        case 4: draw_bg_tiles(lb, c, bg, 4, pal_base, false); break;
        default: draw_bg_tiles(lb, c, bg, 8, 0, direct); break;
        }
        return;
    }

    /* One sample per mosaic block (per pixel without mosaic). Rare, so the line
     * is simply taken to depend on all of VRAM and CGRAM. */
    memset(c->vram_pages, 0xFF, sizeof c->vram_pages);
    memset(c->palette_entries, 0xFF, sizeof c->palette_entries);
    memset(lb->z, 0, sizeof lb->z);

    uint8_t sc = p->reg[0x07 + bg];
    uint32_t wmask = ((sc & 1) ? 512u : 256u) - 1;
    uint32_t hmask = ((sc & 2) ? 512u : 256u) - 1;
    uint8_t nba = p->reg[bg < 2 ? 0x0B : 0x0C];
    uint32_t char_base = (uint32_t)(((bg & 1) ? nba >> 4 : nba) & 7) << 13;
    uint32_t tile_bytes = (uint32_t)planes * 8;
    const uint8_t *depth = c->depth[bg];
    uint8_t sc3 = p->reg[0x09];
    uint16_t opt_enable = (uint16_t)(0x2000 << bg);
    uint32_t hrow = (uint32_t)p->bg_vofs[2] >> 3;
    uint32_t vrow = ((uint32_t)p->bg_vofs[2] + 8) >> 3;
    int y = c->line - ((c->line - 1) % msize);
    uint32_t cached_addr = UINT32_MAX;
    int cached_ty = -1;
    uint64_t row = 0;

    for (int sx = 0; sx < W; sx += msize) {
        uint32_t hofs = p->bg_hofs[bg], vofs = p->bg_vofs[bg];
        if (opt && sx >= 8 - (int)(hofs & 7)) {
            int seg = sx - (int)((sx + hofs) & 7);   /* first pixel of this BG tile on screen */
            uint32_t col = ((uint32_t)(p->bg_hofs[2] + seg - 1) & 0x1FF) >> 3;
            uint16_t hword = map_entry(p, sc3, hrow, col), vword;
            if (mode == 4) {
                vword = (hword & 0x8000) ? hword : 0;
                if (hword & 0x8000)
                    hword = 0;
            } else {
                vword = map_entry(p, sc3, vrow, col);
            }
            if (vword & opt_enable)
                vofs = vword & 0x3FF;
            if (hword & opt_enable)
                hofs = (uint32_t)((hword & 0x3F8) | (hofs & 7));
        }
        uint32_t bx = ((uint32_t)(hires ? sx * 2 : sx) + hofs) & wmask;
        uint32_t by = ((uint32_t)y + vofs) & hmask;
        uint16_t entry = map_entry(p, sc, by >> 3, bx >> 3);
        int tx = bx & 7, ty = by & 7;
        if (entry & 0x4000)
            tx = 7 - tx;
        if (entry & 0x8000)
            ty = 7 - ty;
        uint32_t addr = (char_base + (entry & 0x3FFu) * tile_bytes) & 0xFFFF;
        if (addr != cached_addr || ty != cached_ty) {
            row = tile_row(p->vram, addr, planes, ty);
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
            color = direct565(px, pal, c->brightness);
        else if (planes == 8)
            color = c->pal[px];
        else
            color = c->pal[(pal_base + pal * (1u << planes) + px) & 0xFF];
        for (int x = sx, end = min_i(sx + msize, W); x < end; x++) {
            lb->z[x] = z;
            lb->color[x] = color;
        }
    }
}

static int m7_clip(int n) { return (n & 0x2000) ? (n | ~1023) : (n & 1023); }

/* Mode 7 BG1, and BG2 when EXTBG is set (bit 7 of each pixel is its priority). */
static void draw_mode7(LayerBuf *lb, LineCtx *c, int bg)
{
    const Ppu *p = c->p;
    uint8_t sel = p->reg[0x1A];
    int msize = mosaic_size(p, bg);
    int y = c->line - ((c->line - 1) % msize);
    if (sel & 2)
        y = 255 - y;
    int a = p->m7a, b = p->m7b, cc = p->m7c, d = p->m7d;
    int cx = p->m7x, cy = p->m7y;
    int hx = m7_clip(p->m7hofs - cx), vy = m7_clip(p->m7vofs - cy);
    int ox = ((a * hx) & ~63) + ((b * vy) & ~63) + ((b * y) & ~63) + (cx * 256);
    int oy = ((cc * hx) & ~63) + ((d * vy) & ~63) + ((d * y) & ~63) + (cy * 256);
    bool direct = bg == L_BG1 && (p->reg[0x30] & 1);
    const uint8_t *depth = c->depth[bg];

    /* Mode 7 reads the whole first half of VRAM, and palette indices up to 255. */
    memset(c->vram_pages, 0xFF, sizeof c->vram_pages / 2);
    memset(c->palette_entries, 0xFF, sizeof c->palette_entries);

    if (msize == 1 && !direct) {
        /* Every pixel written, with no branches: the coordinates step by (a, c)
         * per pixel (the same values as ox + a * sx), and pixels outside the
         * 1024x1024 map either read tile 0 or are transparent (sel bits 7-6). */
        int step = (sel & 1) ? -1 : 1;
        int ax = ox + a * ((sel & 1) ? 255 : 0), ay = oy + cc * ((sel & 1) ? 255 : 0);
        bool limit = sel & 0x80, fill = sel & 0x40, extbg = bg == L_BG2;
        const uint8_t *vram = p->vram;
        for (int x = 0; x < W; x++, ax += a * step, ay += cc * step) {
            int px = ax >> 8, py = ay >> 8;
            bool outside = limit & (((px | py) & ~1023) != 0);
            uint8_t tile = vram[(((py >> 3) & 127) * 128 + ((px >> 3) & 127)) * 2];
            tile = outside ? 0 : tile;
            uint8_t v = vram[(((unsigned)tile << 6) + (unsigned)((py & 7) << 3) + (unsigned)(px & 7)) * 2 + 1];
            uint8_t index = extbg ? v & 0x7F : v;
            uint8_t zt = extbg ? depth[v >> 7] : depth[0];
            lb->z[x] = (index && !(outside & !fill)) ? zt : 0;
            lb->color[x] = c->pal[index];
        }
        return;
    }

    memset(lb->z, 0, sizeof lb->z);
    for (int s = 0; s < W; s += msize) {
        int sx = (sel & 1) ? 255 - s : s;
        int px = (ox + a * sx) >> 8;
        int py = (oy + cc * sx) >> 8;
        uint8_t tile;
        if ((sel & 0x80) && ((px | py) & ~1023)) {
            if (!(sel & 0x40))
                continue;
            tile = 0;
        } else {
            tile = p->vram[(((py >> 3) & 127) * 128 + ((px >> 3) & 127)) * 2];
        }
        uint8_t v = p->vram[(((unsigned)tile << 6) + (unsigned)((py & 7) << 3) + (unsigned)(px & 7)) * 2 + 1];
        uint8_t z;
        uint16_t color;
        if (bg == L_BG2) {
            if (!(v & 0x7F))
                continue;
            z = depth[v >> 7];
            color = c->pal[v & 0x7F];
        } else {
            if (!v)
                continue;
            z = depth[0];
            color = direct ? direct565(v, 0, c->brightness) : c->pal[v];
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

/* Rows sprite n of this OAM covers, as (first row, count): none when it's
 * entirely off the left edge. */
static int sprite_rows(const uint8_t *oam, int n, uint8_t size, int *first)
{
    const uint8_t *s = &oam[n * 4];
    uint8_t hi = (uint8_t)(oam[0x200 + n / 4] >> ((n % 4) * 2));
    int large = (hi >> 1) & 1;
    int w = obj_width[size][large];
    int x9 = s[0] | (hi & 1) << 8;
    *first = s[1];
    return (x9 > 256 && x9 + w - 1 < 512) ? 0 : obj_height[size][large];
}

/* Rebuilds the per-row sprite sets when OAM or the OBSEL size changed, bumping
 * the generation of every row a changed sprite covered or covers. */
static void sprite_cache_update(Renderer *r, const Ppu *p)
{
    uint8_t size = p->reg[0x01] >> 5;
    if (r->sprites.valid && r->sprites.size == size && !memcmp(r->sprites.oam, p->oam, sizeof p->oam))
        return;
    if (!r->sprites.valid || r->sprites.size != size) {
        for (int row = 0; row < 256; row++)
            r->sprites.row_gen[row]++;
    } else {
        for (int n = 0; n < 128; n++) {
            unsigned shift = (unsigned)(n % 4) * 2;
            if (!memcmp(&r->sprites.oam[n * 4], &p->oam[n * 4], 4) &&
                !(((r->sprites.oam[0x200 + n / 4] ^ p->oam[0x200 + n / 4]) >> shift) & 3))
                continue;
            for (int version = 0; version < 2; version++) {
                int first, count = sprite_rows(version ? p->oam : r->sprites.oam, n, size, &first);
                for (int k = 0; k < count; k++)
                    r->sprites.row_gen[(first + k) & 0xFF]++;
            }
        }
    }
    memcpy(r->sprites.oam, p->oam, sizeof p->oam);
    r->sprites.size = size;
    r->sprites.valid = true;
    memset(r->sprites.rows, 0, sizeof r->sprites.rows);
    for (int n = 0; n < 128; n++) {
        const uint8_t *s = &p->oam[n * 4];
        uint8_t hi = (uint8_t)(p->oam[0x200 + n / 4] >> ((n % 4) * 2));
        int large = (hi >> 1) & 1;
        int x9 = s[0] | (hi & 1) << 8;
        r->sprites.x[n] = (int16_t)(x9 >= 256 ? x9 - 512 : x9);
        r->sprites.w[n] = obj_width[size][large];
        r->sprites.h[n] = obj_height[size][large];
        int first, count = sprite_rows(p->oam, n, size, &first);
        for (int k = 0; k < count; k++)
            r->sprites.rows[(first + k) & 0xFF][n >> 6] |= (uint64_t)1 << (n & 63);
    }
}

/* The sprites the PPU draws on a line, in OAM order from the first sprite
 * (list), and how many of them keep their tiles (the return value); sets the
 * PPU's range-over and time-over flags. sprite_cache_update must be current. */
static int sprites_on_line(const Renderer *r, Ppu *p, int line, int list[32])
{
    int row = line - 1;
    int first = ppu_first_sprite(p);
    const uint64_t *bits = r->sprites.rows[row & 0xFF];

    /* In range: up to 32 sprites in OAM order from the first sprite; a 33rd sets range over. */
    int count = 0;
    for (int seg = 0; seg < 2; seg++) {
        int lo = seg == 0 ? first : 0, hi = seg == 0 ? 128 : first;
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
                    p->range_over = true;
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
                p->time_over = true;
                keep = k;
                break;
            }
        }
    }
    return keep;
}

/* Sprites on this line; sprite_cache_update must be current. */
static void draw_obj(Renderer *r, LayerBuf *lb, LineCtx *c)
{
    Ppu *p = c->p;
    uint8_t obsel = p->reg[0x01];
    uint32_t base = (uint32_t)(obsel & 3) << 14;
    uint32_t name_select = (uint32_t)((obsel >> 3) & 3) << 13;
    int row = c->line - 1;
    int list[32];
    int keep = sprites_on_line(r, p, c->line, list);

    memset(lb->z, 0, sizeof lb->z);
    const uint8_t *depth = c->depth[L_OBJ];
    /* Back to front, so earlier sprites overwrite later ones. */
    for (int k = keep - 1; k >= 0; k--) {
        int n = list[k];
        const uint8_t *s = &p->oam[n * 4];
        int w = r->sprites.w[n], h = r->sprites.h[n], x = r->sprites.x[n];
        int dy = (row - s[1]) & 0xFF;
        if (s[3] & 0x80)
            dy = h - 1 - dy;
        uint16_t name = (uint16_t)(s[2] | (s[3] & 1) << 8);
        uint8_t pal = (s[3] >> 1) & 7;
        const uint16_t *pals = c->pal + 128 + pal * 16;
        uint8_t zt = depth[(s[3] >> 4) & 3], math = pal >= 4;   /* color math only for palettes 4-7 */
        bool hflip = s[3] & 0x40;
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
            uint64_t pixels = tile_row(p->vram, addr & 0xFFFF, 4, dy & 7);
            mark_vram_span(c->vram_pages, addr, 32);
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
                mark_entry(c->palette_entries, 128u + pal * 16u + v, true);
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

static const uint8_t *layer_order(const Ppu *p)
{
    switch (p->reg[0x05] & 7) {
    case 0: return order_mode0;
    case 1: return (p->reg[0x05] & 0x08) ? order_mode1_bg3 : order_mode1;
    case 6: return order_mode6;
    case 7: return (p->reg[0x33] & 0x40) ? order_mode7_ext : order_mode7;
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
 * screen has and hide[x] is 0, it replaces it. math is the layer's color math
 * enable, and with obj_math the sprite palette condition applies too.
 * Branch-free so compilers can vectorize it. */
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

/* Final colors for a line with color math. The screens hold the backdrop
 * (main) and fixed color (sub) where no layer is in front, clip[x] and
 * math_on[x] the color window's effect. Called with constant flags so each
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

/* Draws one line from scratch, recording what it read in c. */
static void draw_line(Renderer *r, LineCtx *c, uint32_t *out)
{
    Ppu *p = c->p;
    order_depths(layer_order(p), c->depth);
    const uint8_t *s = scale5[c->brightness];
    uint16_t fixed565 = pack565(s[p->fixed_color & 31], s[(p->fixed_color >> 5) & 31], s[(p->fixed_color >> 10) & 31]);

    uint8_t mode = p->reg[0x05] & 7;
    uint8_t tm = p->reg[0x2C], ts = p->reg[0x2D], tmw = p->reg[0x2E], tsw = p->reg[0x2F];
    uint8_t used = tm | ts;
    uint8_t cgwsel = p->reg[0x30], cgadsub = p->reg[0x31];
    bool sub_add = cgwsel & 2;
    /* Color math can apply somewhere on this line: some layer (or the backdrop)
     * has it enabled, and the color window setting isn't "never". */
    bool any_math = (cgadsub & 0x3F) && ((cgwsel >> 4) & 3) != 3;
    /* The sub screen's layers are only looked at for math with sub screen addition. */
    bool need_sub = any_math && sub_add;

    static Screen main_s, sub_s;
    static LayerBuf lb;
    static uint8_t win[W];
    memset(main_s.z, 0, sizeof main_s.z);
    for (int x = 0; x < W; x++) {
        main_s.color[x] = c->pal[0];   /* backdrop */
        main_s.math[x] = (cgadsub & 0x20) != 0;
    }
    mark_entry(c->palette_entries, 0, true);
    if (any_math) {
        memset(sub_s.z, 0, sizeof sub_s.z);
        for (int x = 0; x < W; x++)
            sub_s.color[x] = fixed565;
    }

    for (int layer = L_BG1; layer <= L_OBJ; layer++) {
        if (!(used & (1 << layer)))
            continue;
        switch (layer == L_OBJ ? 8 : mode) {
        case 0: draw_bg(&lb, c, layer, 2, false); break;
        case 1:
            if (layer == L_BG4)
                continue;
            draw_bg(&lb, c, layer, layer == L_BG3 ? 2 : 4, false);
            break;
        case 2:
        case 3:
        case 4:
        case 5:
            if (layer >= L_BG3)
                continue;
            {
                static const uint8_t planes[4][2] = { { 4, 4 }, { 8, 4 }, { 8, 2 }, { 4, 2 } };
                draw_bg(&lb, c, layer, planes[mode - 2][layer], mode == 5);
            }
            break;
        case 6:
            if (layer != L_BG1)
                continue;
            draw_bg(&lb, c, layer, 4, true);
            break;
        case 7:
            if (layer >= L_BG3 || (layer == L_BG2 && !(p->reg[0x33] & 0x40)))
                continue;
            draw_mode7(&lb, c, layer);
            break;
        default:
            draw_obj(r, &lb, c);
            break;
        }

        uint8_t bit = (uint8_t)(1 << layer);
        bool windowed = ((tmw | (need_sub ? tsw : 0)) & bit) &&
                        window_mask(p, window_sel(p, layer), window_logic(p, layer), win);
        uint8_t math = (cgadsub & bit) != 0;
        if (tm & bit)
            merge(&main_s, &lb, windowed && (tmw & bit) ? win : no_window, math, layer == L_OBJ);
        if (need_sub && (ts & bit))
            merge(&sub_s, &lb, windowed && (tsw & bit) ? win : no_window, math, layer == L_OBJ);
    }

    /* Color window: per value (outside, inside), whether to clip the main
     * screen to black and whether color math is on. */
    uint8_t clip_when[2], math_when[2];
    for (int v = 0; v < 2; v++) {
        switch (cgwsel >> 6) {
        case 0: clip_when[v] = 0; break;
        case 1: clip_when[v] = !v; break;
        case 2: clip_when[v] = (uint8_t)v; break;
        default: clip_when[v] = 1; break;
        }
        switch ((cgwsel >> 4) & 3) {
        case 0: math_when[v] = 1; break;
        case 1: math_when[v] = (uint8_t)v; break;
        case 2: math_when[v] = !v; break;
        default: math_when[v] = 0; break;
        }
    }
    static uint8_t cwin[W], clip[W], math_on[W];
    if (window_mask(p, window_sel(p, L_BACK), window_logic(p, L_BACK), cwin)) {
        for (int x = 0; x < W; x++) {
            clip[x] = clip_when[cwin[x]];
            math_on[x] = math_when[cwin[x]];
        }
    } else {
        memset(clip, clip_when[0], sizeof clip);
        memset(math_on, math_when[0], sizeof math_on);
    }

    if (!any_math) {
        for (int x = 0; x < W; x++)
            out[x] = rgb565_to_xrgb8888(clip[x] ? 0 : main_s.color[x]);
        return;
    }
    int add_cap = c->brightness == 15 ? 31 : brightness_scale(31, c->brightness);
    switch ((cgadsub >> 6) | (sub_add ? 4 : 0)) {   /* bit 0 halve, 1 subtract, 2 sub screen */
    case 0: compose_math(out, &main_s, &sub_s, clip, math_on, false, false, false, add_cap); break;
    case 1: compose_math(out, &main_s, &sub_s, clip, math_on, false, true, false, add_cap); break;
    case 2: compose_math(out, &main_s, &sub_s, clip, math_on, true, false, false, add_cap); break;
    case 3: compose_math(out, &main_s, &sub_s, clip, math_on, true, true, false, add_cap); break;
    case 4: compose_math(out, &main_s, &sub_s, clip, math_on, false, false, true, add_cap); break;
    case 5: compose_math(out, &main_s, &sub_s, clip, math_on, false, true, true, add_cap); break;
    case 6: compose_math(out, &main_s, &sub_s, clip, math_on, true, false, true, add_cap); break;
    default: compose_math(out, &main_s, &sub_s, clip, math_on, true, true, true, add_cap); break;
    }
}

/* ---- line ---------------------------------------------------------------------------- */

void render_init(Renderer *r)
{
    tables_init();
    memset(r, 0, sizeof *r);
}

/* A line's key (see RenderLineKey). */
static void line_key(RenderLineKey *k, const Renderer *r, const Ppu *p, int line)
{
    memset(k, 0, sizeof *k);
    memcpy(k->reg, p->reg, sizeof k->reg);
    /* Ports whose raw values don't affect drawing or are decoded below. */
    k->reg[0x02] = k->reg[0x04] = 0;               /* OAM address and data */
    memset(k->reg + 0x0D, 0, 0x19 - 0x0D + 1);     /* scroll offsets, VRAM address and data */
    memset(k->reg + 0x1B, 0, 0x22 - 0x1B + 1);     /* Mode 7 matrix and center, CGRAM address and data */
    k->reg[0x32] = 0;                              /* COLDATA */
    if (!((p->reg[0x23] | p->reg[0x24] | p->reg[0x25]) & 0xAA)) {
        /* No window enabled: positions, logic and masks are unused. */
        memset(k->reg + 0x26, 0, 0x2B - 0x26 + 1);
        k->reg[0x2E] = k->reg[0x2F] = 0;
    }

    uint8_t mode = p->reg[0x05] & 7;
    uint8_t used = p->reg[0x2C] | p->reg[0x2D];
    if (mode == 7) {
        const int16_t m7[8] = { p->m7a, p->m7b, p->m7c, p->m7d, p->m7x, p->m7y, p->m7hofs, p->m7vofs };
        memcpy(k->m7, m7, sizeof m7);
    } else {
        bool opt = mode == 2 || mode == 4 || mode == 6;
        for (int bg = 0; bg < 4; bg++) {
            if ((used & (1 << bg)) || (bg == 2 && opt && (used & 3))) {
                k->bg_hofs[bg] = p->bg_hofs[bg];
                k->bg_vofs[bg] = p->bg_vofs[bg];
            }
        }
    }
    if ((p->reg[0x31] & 0x3F) && ((p->reg[0x30] >> 4) & 3) != 3)   /* color math can apply */
        k->fixed_color = p->fixed_color;
    if (used & 0x10) {
        k->first_sprite = ppu_first_sprite(p);
        k->sprite_row_gen = r->sprites.row_gen[(line - 1) & 0xFF];
    }
}

void render_line(Renderer *r, Ppu *p, int line)
{
    if (line < 0 || line > RENDER_HEIGHT)
        return;
    tables_init();
    vram_check(r, p, line == 0);
    if (line == 0) {
        p->range_over = p->time_over = false;
        return;
    }
    RenderLineCache *cache = &r->lines[line];
    uint32_t *out = &r->pixels[(line - 1) * RENDER_WIDTH];
    if (ppu_force_blank(p)) {
        memset(out, 0, sizeof *out * RENDER_WIDTH);
        cache->valid = false;
        return;
    }

    LineCtx c = { .p = p, .line = line, .brightness = p->reg[0x00] & 15 };
    c.pal = palette_update(r, p, c.brightness);
    if ((p->reg[0x2C] | p->reg[0x2D]) & 0x10)
        sprite_cache_update(r, p);
    RenderLineKey key;
    line_key(&key, r, p, line);
    if (cache->valid && !memcmp(&cache->key, &key, sizeof key) &&
        parts_unchanged(r->palette.entry_gen, r->palette.gen, r->palette.last_changed, cache->palette_gen,
                        cache->palette_entries, 4) &&
        parts_unchanged(r->vram.page_gen, r->vram.gen, r->vram.last_changed, cache->vram_gen, cache->vram_pages,
                        RENDER_VRAM_PAGES / 64)) {
        /* The pixels are still in the framebuffer, and the flags come out the same. */
        cache->palette_gen = r->palette.gen;
        cache->vram_gen = r->vram.gen;
        p->range_over |= cache->range_over;
        p->time_over |= cache->time_over;
        return;
    }

    bool range_over = p->range_over, time_over = p->time_over;
    p->range_over = p->time_over = false;
    draw_line(r, &c, out);
    cache->key = key;
    cache->palette_gen = r->palette.gen;
    cache->vram_gen = r->vram.gen;
    memcpy(cache->palette_entries, c.palette_entries, sizeof cache->palette_entries);
    memcpy(cache->vram_pages, c.vram_pages, sizeof cache->vram_pages);
    cache->valid = true;
    cache->range_over = p->range_over;
    cache->time_over = p->time_over;
    p->range_over |= range_over;
    p->time_over |= time_over;
}

void render_line_flags(Renderer *r, Ppu *p, int line)
{
    if (line < 0 || line > RENDER_HEIGHT)
        return;
    if (line == 0) {
        p->range_over = p->time_over = false;
        return;
    }
    r->lines[line].valid = false;   /* the framebuffer row isn't this line any more */
    if (ppu_force_blank(p) || !((p->reg[0x2C] | p->reg[0x2D]) & 0x10))
        return;
    sprite_cache_update(r, p);
    int list[32];
    sprites_on_line(r, p, line, list);
}
