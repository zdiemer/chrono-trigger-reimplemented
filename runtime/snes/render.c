#include "render.h"

#include <stdbool.h>
#include <string.h>

enum { L_BG1, L_BG2, L_BG3, L_BG4, L_OBJ, L_BACK, L_COUNT };

typedef struct {
    uint16_t color;   /* CGRAM index, or DIRECT | palette << 8 | pixel */
    uint8_t prio;     /* BG: 0-1, OBJ: 0-3 */
    bool valid;
} Pix;

#define DIRECT 0x800

typedef struct {
    Pix layer[L_OBJ + 1][RENDER_WIDTH];
    uint16_t pal565[256];
    uint16_t fixed565;
    uint8_t brightness;
} Line;

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

static uint16_t pack565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(r << 11 | g << 6 | (g & 0x10) << 1 | b);
}

uint16_t render_snes9x_color(uint16_t bgr555, uint8_t brightness)
{
    return pack565(brightness_scale(bgr555 & 31, brightness), brightness_scale((bgr555 >> 5) & 31, brightness),
                   brightness_scale((bgr555 >> 10) & 31, brightness));
}

uint32_t render_rgb565_to_xrgb8888(uint16_t c)
{
    unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (uint32_t)((r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2));
}

static uint16_t direct565(uint8_t pixel, uint8_t palette, uint8_t brightness)
{
    uint8_t r = (uint8_t)((pixel & 7) << 2 | (palette & 1) << 1);
    uint8_t g = (uint8_t)((pixel & 0x38) >> 1 | (palette & 2));
    uint8_t b = (uint8_t)((pixel & 0xC0) >> 3 | (palette & 4));
    return pack565(brightness_scale(r, brightness), brightness_scale(g, brightness), brightness_scale(b, brightness));
}

static int min_i(int a, int b) { return a < b ? a : b; }

/* Saturating add per 5-bit channel, clamped to cap (31, or the brightness-scaled
 * maximum when brightness isn't full). */
static uint16_t math_add(uint16_t a, uint16_t b, int cap)
{
    int r = min_i(((a >> 11) & 31) + ((b >> 11) & 31), cap);
    int g = min_i(((a >> 6) & 31) + ((b >> 6) & 31), cap);
    int bl = min_i((a & 31) + (b & 31), cap);
    return pack565((uint8_t)r, (uint8_t)g, (uint8_t)bl);
}

/* Average on the packed word: low bits dropped, then restored where both have them. */
static uint16_t math_add_half(uint16_t a, uint16_t b)
{
    return (uint16_t)((((a & 0xF7DE) + (b & 0xF7DE)) >> 1) + (a & b & 0x0821));
}

/* Subtract, flooring each channel at 0. Green is subtracted at 6 bits and
 * truncated to 5. */
static uint16_t math_sub(uint16_t a, uint16_t b)
{
    int r = ((a >> 11) & 31) - ((b >> 11) & 31);
    int g6 = ((a >> 5) & 63) - ((b >> 5) & 63);
    int bl = (a & 31) - (b & 31);
    return pack565((uint8_t)(r < 0 ? 0 : r), (uint8_t)(g6 < 0 ? 0 : g6 >> 1), (uint8_t)(bl < 0 ? 0 : bl));
}

/* Halved subtract on the packed word with guard bits between channels, then each
 * channel keeps its low bits only if its guard survived (no borrow). */
static uint16_t math_sub_half(uint16_t a, uint16_t b)
{
    uint32_t t = (((uint32_t)a | 0x10820u) - (b & 0xF7DEu)) >> 1;
    unsigned r = (t >> 11) & 31, g = (t >> 5) & 63, bl = t & 31;
    r = (r & 0x10) ? r & 0x0F : 0;
    g = (g & 0x20) ? g & 0x1F : 0;
    bl = (bl & 0x10) ? bl & 0x0F : 0;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* ---- windows -------------------------------------------------------------------- */

/* sel: bit0 W1 invert, bit1 W1 enable, bit2 W2 invert, bit3 W2 enable. */
static bool window(const Ppu *p, uint8_t sel, uint8_t logic, int x)
{
    bool en1 = sel & 2, en2 = sel & 8;
    if (!en1 && !en2)
        return false;
    bool in1 = (x >= p->reg[0x26] && x <= p->reg[0x27]) != ((sel & 1) != 0);
    bool in2 = (x >= p->reg[0x28] && x <= p->reg[0x29]) != ((sel & 4) != 0);
    if (!en2)
        return in1;
    if (!en1)
        return in2;
    switch (logic & 3) {
    case 0: return in1 || in2;
    case 1: return in1 && in2;
    case 2: return in1 != in2;
    default: return in1 == in2;
    }
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

static uint8_t tile_pixel(const uint8_t *vram, uint32_t addr, int planes, int tx, int ty)
{
    uint8_t bit = (uint8_t)(0x80 >> tx);
    uint8_t v = 0;
    for (int plane = 0; plane < planes; plane++) {
        uint32_t row = addr + (uint32_t)((plane / 2) * 16) + (uint32_t)(ty * 2) + (uint32_t)(plane & 1);
        if (vram[row & 0xFFFF] & bit)
            v |= (uint8_t)(1 << plane);
    }
    return v;
}

static int mosaic_size(const Ppu *p, int bg)
{
    return (p->reg[0x06] & (1 << bg)) ? (p->reg[0x06] >> 4) + 1 : 1;
}

/* Tilemap entry for tile (row, col). Maps are laid out as 32x32 quadrants:
 * the right half of a 64-wide map starts 1024 entries in, the bottom half of a
 * 64-tall map after the top half. */
static uint16_t map_entry(const Ppu *p, uint8_t sc, uint32_t row, uint32_t col)
{
    uint32_t base = (uint32_t)(sc & 0xFC) << 9;
    uint32_t quad_x = ((col & 0x20) && (sc & 1)) ? 1024 : 0;
    uint32_t quad_y = ((row & 0x20) && (sc & 2)) ? ((sc & 1) ? 2048 : 1024) : 0;
    uint32_t a = (base + (quad_y + ((row & 0x1F) << 5) + quad_x + (col & 0x1F)) * 2) & 0xFFFF;
    return (uint16_t)(p->vram[a] | p->vram[(a + 1) & 0xFFFF] << 8);
}

/* Normal (modes 0-6) BG layer. planes: 2, 4 or 8. hires: sample at double width.
 * In modes 2, 4 and 6, BG1 and BG2 take per-column scroll offsets from BG3's
 * tilemap (offset-per-tile); the leftmost tile column is never affected. */
static void draw_bg(Line *ln, const Ppu *p, int bg, int line, int planes, bool hires)
{
    uint8_t sc = p->reg[0x07 + bg];
    uint32_t wmask = ((sc & 1) ? 512u : 256u) - 1;
    uint32_t hmask = ((sc & 2) ? 512u : 256u) - 1;
    uint8_t nba = p->reg[bg < 2 ? 0x0B : 0x0C];
    uint32_t char_base = (uint32_t)(((bg & 1) ? nba >> 4 : nba) & 7) << 13;
    uint32_t tile_bytes = (uint32_t)planes * 8;
    uint8_t mode = p->reg[0x05] & 7;
    uint16_t pal_base = (mode == 0) ? (uint16_t)(bg << 5) : 0;
    bool direct = planes == 8 && (p->reg[0x30] & 1);

    bool opt = (mode == 2 || mode == 4 || mode == 6) && bg < 2;
    uint8_t sc3 = p->reg[0x09];
    uint16_t opt_enable = (uint16_t)(0x2000 << bg);
    uint32_t hrow = (uint32_t)p->bg_vofs[2] >> 3;
    uint32_t vrow = ((uint32_t)p->bg_vofs[2] + 8) >> 3;

    int msize = mosaic_size(p, bg);
    int y = line - ((line - 1) % msize);

    for (int x = 0; x < RENDER_WIDTH; x++) {
        int sx = x - (x % msize);
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
        uint8_t px = tile_pixel(p->vram, addr, planes, tx, ty);
        if (!px)
            continue;
        uint8_t pal = (entry >> 10) & 7;
        Pix *o = &ln->layer[bg][x];
        o->valid = true;
        o->prio = (entry >> 13) & 1;
        if (direct)
            o->color = (uint16_t)(DIRECT | pal << 8 | px);
        else if (planes == 8)
            o->color = px;
        else
            o->color = (uint16_t)(pal_base + pal * (1u << planes) + px);
    }
}

static int m7_clip(int n) { return (n & 0x2000) ? (n | ~1023) : (n & 1023); }

/* Mode 7 BG1, and BG2 when EXTBG is set (bit 7 of each pixel is its priority). */
static void draw_mode7(Line *ln, const Ppu *p, int bg, int line)
{
    uint8_t sel = p->reg[0x1A];
    int msize = mosaic_size(p, bg);
    int y = line - ((line - 1) % msize);
    if (sel & 2)
        y = 255 - y;
    int a = p->m7a, b = p->m7b, c = p->m7c, d = p->m7d;
    int cx = p->m7x, cy = p->m7y;
    int hx = m7_clip(p->m7hofs - cx), vy = m7_clip(p->m7vofs - cy);
    int ox = ((a * hx) & ~63) + ((b * vy) & ~63) + ((b * y) & ~63) + (cx * 256);
    int oy = ((c * hx) & ~63) + ((d * vy) & ~63) + ((d * y) & ~63) + (cy * 256);
    bool direct = bg == L_BG1 && (p->reg[0x30] & 1);

    for (int x = 0; x < RENDER_WIDTH; x++) {
        int sx = x - (x % msize);
        if (sel & 1)
            sx = 255 - sx;
        int px = (ox + a * sx) >> 8;
        int py = (oy + c * sx) >> 8;
        uint8_t tile;
        if ((sel & 0x80) && ((px | py) & ~1023)) {
            if (!(sel & 0x40))
                continue;
            tile = 0;
        } else {
            tile = p->vram[(((py >> 3) & 127) * 128 + ((px >> 3) & 127)) * 2];
        }
        uint8_t color = p->vram[(((unsigned)tile << 6) + (unsigned)((py & 7) << 3) + (unsigned)(px & 7)) * 2 + 1];
        Pix *o = &ln->layer[bg][x];
        if (bg == L_BG2) {
            if (!(color & 0x7F))
                continue;
            o->valid = true;
            o->prio = color >> 7;
            o->color = color & 0x7F;
        } else {
            if (!color)
                continue;
            o->valid = true;
            o->prio = 0;
            o->color = direct ? (uint16_t)(DIRECT | color) : color;
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

static void draw_obj(Line *ln, Ppu *p, int line)
{
    uint8_t obsel = p->reg[0x01];
    uint8_t size = obsel >> 5;
    uint32_t base = (uint32_t)(obsel & 3) << 14;
    uint32_t name_select = (uint32_t)((obsel >> 3) & 3) << 13;
    int row = line - 1;
    uint8_t first = ppu_first_sprite(p);

    int list[32], count = 0;
    for (int i = 0; i < 128; i++) {
        int n = (first + i) & 127;
        const uint8_t *s = &p->oam[n * 4];
        uint8_t hi = (uint8_t)(p->oam[0x200 + n / 4] >> ((n % 4) * 2));
        int large = (hi >> 1) & 1;
        int w = obj_width[size][large], h = obj_height[size][large];
        int x9 = s[0] | (hi & 1) << 8;
        if (x9 > 256 && x9 + w - 1 < 512)
            continue;                                  /* entirely off the left edge */
        if (((row - s[1]) & 0xFF) >= h)
            continue;
        if (count == 32) {
            p->range_over = true;
            break;
        }
        list[count++] = n;
    }

    /* Tile limit: at most 34 8x8 tiles per line; later sprites lose theirs. */
    int tiles = 0, keep = count;
    for (int k = 0; k < count && keep == count; k++) {
        int n = list[k];
        uint8_t hi = (uint8_t)(p->oam[0x200 + n / 4] >> ((n % 4) * 2));
        int w = obj_width[size][(hi >> 1) & 1];
        int x = p->oam[n * 4] | (hi & 1) << 8;
        if (x >= 256)
            x -= 512;
        for (int t = 0; t < w; t += 8) {
            if (x + t > -8 && x + t < 256 && ++tiles > 34) {
                p->time_over = true;
                keep = k;
                break;
            }
        }
    }

    for (int k = keep - 1; k >= 0; k--) {
        int n = list[k];
        const uint8_t *s = &p->oam[n * 4];
        uint8_t hi = (uint8_t)(p->oam[0x200 + n / 4] >> ((n % 4) * 2));
        int large = (hi >> 1) & 1;
        int w = obj_width[size][large], h = obj_height[size][large];
        int x = s[0] | (hi & 1) << 8;
        if (x >= 256)
            x -= 512;
        int dy = (row - s[1]) & 0xFF;
        if (s[3] & 0x80)
            dy = h - 1 - dy;
        uint16_t name = (uint16_t)(s[2] | (s[3] & 1) << 8);
        uint8_t pal = (s[3] >> 1) & 7, prio = (s[3] >> 4) & 3;
        for (int i = 0; i < w; i++) {
            int sx = x + i;
            if (sx < 0 || sx >= RENDER_WIDTH)
                continue;
            int dx = (s[3] & 0x40) ? w - 1 - i : i;
            uint32_t tile = (uint32_t)name + (uint32_t)((dy >> 3) * 16 + (dx >> 3));
            uint32_t addr = base + ((tile & 0x3FF) << 5);
            if (tile & 0x100)
                addr += name_select;
            uint8_t px = tile_pixel(p->vram, addr & 0xFFFF, 4, dx & 7, dy & 7);
            if (!px)
                continue;
            Pix *o = &ln->layer[L_OBJ][sx];
            o->valid = true;
            o->prio = prio;
            o->color = (uint16_t)(128 + pal * 16 + px);
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

/* Front-most visible pixel on one screen. screen: TM/TS enable bits; wmask:
 * TMW/TSW window mask bits. Returns the layer, or L_BACK. */
static int resolve(const Line *ln, const uint8_t *order, uint8_t screen, uint8_t wmask, int x,
                   const bool win[L_OBJ + 1], Pix *out)
{
    for (const uint8_t *t = order; *t != END; t++) {
        int layer = *t >> 2;
        const Pix *px = &ln->layer[layer][x];
        if (!px->valid || px->prio != (*t & 3) || !(screen & (1 << layer)))
            continue;
        if ((wmask & (1 << layer)) && win[layer])
            continue;
        *out = *px;
        return layer;
    }
    return L_BACK;
}

/* ---- line ---------------------------------------------------------------------------- */

static uint16_t color565(const Line *ln, const Pix *px)
{
    if (px->color & DIRECT)
        return direct565(px->color & 0xFF, (px->color >> 8) & 7, ln->brightness);
    return ln->pal565[px->color & 0xFF];
}

void render_init(Renderer *r)
{
    memset(r, 0, sizeof *r);
}

void render_line(Renderer *r, Ppu *p, int line)
{
    if (line == 0) {
        p->range_over = p->time_over = false;
        return;
    }
    if (line < 1 || line > RENDER_HEIGHT)
        return;
    uint32_t *out = &r->pixels[(line - 1) * RENDER_WIDTH];
    if (ppu_force_blank(p)) {
        for (int x = 0; x < RENDER_WIDTH; x++)
            out[x] = 0;
        return;
    }

    static Line ln;
    memset(ln.layer, 0, sizeof ln.layer);
    ln.brightness = p->reg[0x00] & 15;
    for (int i = 0; i < 256; i++)
        ln.pal565[i] = render_snes9x_color(p->cgram[i], ln.brightness);
    uint8_t coldata_r = p->fixed_color & 31, coldata_g = (p->fixed_color >> 5) & 31, coldata_b = (p->fixed_color >> 10) & 31;
    ln.fixed565 = pack565(brightness_scale(coldata_r, ln.brightness), brightness_scale(coldata_g, ln.brightness),
                          brightness_scale(coldata_b, ln.brightness));

    uint8_t mode = p->reg[0x05] & 7;
    uint8_t used = p->reg[0x2C] | p->reg[0x2D];
    bool hires = mode == 5 || mode == 6;
    switch (mode) {
    case 0:
        for (int bg = 0; bg < 4; bg++)
            if (used & (1 << bg))
                draw_bg(&ln, p, bg, line, 2, false);
        break;
    case 1:
        if (used & 1) draw_bg(&ln, p, L_BG1, line, 4, false);
        if (used & 2) draw_bg(&ln, p, L_BG2, line, 4, false);
        if (used & 4) draw_bg(&ln, p, L_BG3, line, 2, false);
        break;
    case 2:
        if (used & 1) draw_bg(&ln, p, L_BG1, line, 4, false);
        if (used & 2) draw_bg(&ln, p, L_BG2, line, 4, false);
        break;
    case 3:
        if (used & 1) draw_bg(&ln, p, L_BG1, line, 8, false);
        if (used & 2) draw_bg(&ln, p, L_BG2, line, 4, false);
        break;
    case 4:
        if (used & 1) draw_bg(&ln, p, L_BG1, line, 8, false);
        if (used & 2) draw_bg(&ln, p, L_BG2, line, 2, false);
        break;
    case 5:
        if (used & 1) draw_bg(&ln, p, L_BG1, line, 4, hires);
        if (used & 2) draw_bg(&ln, p, L_BG2, line, 2, hires);
        break;
    case 6:
        if (used & 1) draw_bg(&ln, p, L_BG1, line, 4, hires);
        break;
    case 7:
        if (used & 1) draw_mode7(&ln, p, L_BG1, line);
        if ((used & 2) && (p->reg[0x33] & 0x40)) draw_mode7(&ln, p, L_BG2, line);
        break;
    }
    if (used & 0x10)
        draw_obj(&ln, p, line);

    const uint8_t *order = layer_order(p);
    uint8_t tm = p->reg[0x2C], ts = p->reg[0x2D], tmw = p->reg[0x2E], tsw = p->reg[0x2F];
    uint8_t cgwsel = p->reg[0x30], cgadsub = p->reg[0x31];
    bool sub_add = cgwsel & 2;
    int add_cap = ln.brightness == 15 ? 31 : brightness_scale(31, ln.brightness);

    for (int x = 0; x < RENDER_WIDTH; x++) {
        bool win[L_OBJ + 1];
        for (int l = 0; l <= L_OBJ; l++)
            win[l] = window(p, window_sel(p, l), window_logic(p, l), x);
        bool cw = window(p, window_sel(p, L_BACK), window_logic(p, L_BACK), x);

        Pix main_px;
        int main_layer = resolve(&ln, order, tm, tmw, x, win, &main_px);
        uint16_t main565 = main_layer == L_BACK ? ln.pal565[0] : color565(&ln, &main_px);

        bool clip_black;
        switch (cgwsel >> 6) {
        case 0: clip_black = false; break;
        case 1: clip_black = !cw; break;
        case 2: clip_black = cw; break;
        default: clip_black = true; break;
        }
        bool math_on;
        switch ((cgwsel >> 4) & 3) {
        case 0: math_on = true; break;
        case 1: math_on = cw; break;
        case 2: math_on = !cw; break;
        default: math_on = false; break;
        }
        if (clip_black)
            main565 = 0;

        bool layer_math = main_layer == L_BACK ? (cgadsub & 0x20)
                        : main_layer == L_OBJ  ? ((cgadsub & 0x10) && main_px.color >= 0xC0)
                                               : (cgadsub & (1 << main_layer));
        uint16_t result = main565;
        if (math_on && layer_math) {
            uint16_t sub565 = ln.fixed565;
            bool sub_real = false;
            if (sub_add) {
                Pix sub_px;
                if (resolve(&ln, order, ts, tsw, x, win, &sub_px) != L_BACK) {
                    sub565 = color565(&ln, &sub_px);
                    sub_real = true;
                }
            }
            bool halve = (cgadsub & 0x40) && !clip_black && (!sub_add || sub_real);
            if (cgadsub & 0x80)
                result = halve ? math_sub_half(main565, sub565) : math_sub(main565, sub565);
            else
                result = halve ? math_add_half(main565, sub565) : math_add(main565, sub565, add_cap);
        }
        out[x] = render_rgb565_to_xrgb8888(result);
    }
}
