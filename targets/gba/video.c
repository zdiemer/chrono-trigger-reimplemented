/* The video service on the GBA's own picture (issue #367), in place of
 * runtime/platform/native/video.c (docs/PLATFORM.md, "The GBA build, as
 * built").
 *
 * Two halves. The SNES's video memory as the game's calls leave it: VRAM
 * (64 KiB, in work RAM), the palette and the sprite table, each written as
 * the native backend's observer writes it (runtime/platform/observe.c), so
 * that what the game reads back of VRAM is what it reads natively; and the
 * settings: the mode, the layers' maps and tiles, the sprites' tiles and
 * sizes, which layers show. And the GBA's picture made from them once a
 * frame (gba_video_present), in mode 0's four tiled layers and its sprites:
 *
 * - The palette is the same colours (both are BGR555). The 4bpp layers' eight
 *   palettes are the GBA's background palettes 0-7; a 2bpp layer's eight
 *   palettes of four are 8-15, their first four colours each; the sprites'
 *   eight are the GBA's sprite palettes 0-7.
 * - VRAM is converted in blocks of 512 bytes, the ones the game wrote and,
 *   where the settings change, all of them, each as what the settings make
 *   it: 4bpp tiles (bitplanes to the GBA's packed nibbles) at the same place
 *   in the GBA's background VRAM, a map (each entry's palette, flips and
 *   tile moved to the GBA's bits) at the same place too, 2bpp tiles widened
 *   to 4bpp in a 16 KiB block of their own, and sprite tiles into sprite
 *   VRAM, the two tables side by side in its 32 tiles a row.
 * - The SNES's per-tile priority has no GBA counterpart: each layer takes
 *   one priority, and the sprites' four map onto the GBA's around them.
 * - The GBA's 240x160 screen shows the middle of the SNES's 256x224: 8 dots
 *   in from the left, 32 lines down. A camera that follows the action is
 *   issue #366.
 *
 * The sends still take the time the native backend charges, which is the
 * game's play; the conversion is the GBA's own work, outside the game's
 * frame. */
#include <stdio.h>
#include <string.h>

#include "platform/native/host.h"
#include "platform/time_model.h"
#include "platform/timing.h"

#include "gba_video.h"

/* ---- The GBA's registers and memory ------------------------------------------------------------- */

#define REG16(at) (*(volatile uint16_t *)(at))
#define DISPCNT REG16(0x04000000)
#define BGCNT(n) REG16(0x04000008 + 2 * (n))
#define BGHOFS(n) REG16(0x04000010 + 4 * (n))
#define BGVOFS(n) REG16(0x04000012 + 4 * (n))
#define BLDCNT REG16(0x04000050)
#define BLDY REG16(0x04000054)
#define BG_PALETTE ((volatile uint16_t *)0x05000000)
#define OBJ_PALETTE ((volatile uint16_t *)0x05000200)
#define BG_VRAM ((volatile uint32_t *)0x06000000)
#define OBJ_VRAM ((volatile uint32_t *)0x06010000)
#define OAM ((volatile uint16_t *)0x07000000)

enum {
    DISPCNT_FORCED_BLANK = 0x0080, DISPCNT_BG0 = 0x0100, DISPCNT_OBJ = 0x1000, DISPCNT_OBJ_2D = 0x0000,
    BGCNT_4BPP = 0x0000,
    BLD_DARKEN = 0x00C0, BLD_ALL_FIRST = 0x003F,
    CAMERA_X = 8, CAMERA_Y = 32,
    VRAM_BYTES = 0x10000, BLOCK_BYTES = 512, BLOCKS = VRAM_BYTES / BLOCK_BYTES,
    TILES_4BPP_BYTES = 1024 * 32, TILES_2BPP_BYTES = 1024 * 16, SCREEN_BYTES = 0x800,
    OBJ_TABLE_BYTES = 256 * 32, SPRITES = 128,
};

/* ---- The SNES's video memory and settings ---------------------------------------------------- */

typedef struct {
    uint8_t vram[VRAM_BYTES];
    uint16_t cgram[256];
    uint8_t oam[544];
    VidVramPort port;
    uint16_t vram_addr;
    uint16_t oam_addr;
    uint8_t oam_latch;
    VidMode mode;
    VidBg bg[4];
    VidObjConfig obj;
    uint8_t main_layers;
    uint32_t written[BLOCKS / 32];    /* blocks the game wrote since the picture was made */
    bool palette_written, settings_changed;
    uint8_t (*read)(void *ctx, uint32_t addr);
    void *ctx;
} Snes;

static Snes snes;

static void block_written(uint32_t byte) { snes.written[byte / BLOCK_BYTES / 32] |= 1u << (byte / BLOCK_BYTES % 32); }

static uint16_t vram_mapped(void)
{
    uint16_t a = snes.vram_addr;
    switch (snes.port.remap) {
    case 1: a = (uint16_t)((a & 0xFF00) | ((a << 3) & 0x00F8) | ((a >> 5) & 0x0007)); break;
    case 2: a = (uint16_t)((a & 0xFE00) | ((a << 3) & 0x01F8) | ((a >> 6) & 0x0007)); break;
    case 3: a = (uint16_t)((a & 0xFC00) | ((a << 3) & 0x03F8) | ((a >> 7) & 0x0007)); break;
    default: break;
    }
    return (uint16_t)(a & 0x7FFF);
}

static uint8_t data_byte(PfData src, uint32_t i)
{
    const uint8_t *byte = pf_data_pointer(src, i);
    return byte ? *byte : snes.read ? snes.read(snes.ctx, pf_data_address(src, i)) : 0;
}

/* A byte through the VRAM port, as the SNES's PPU takes it. */
static void vram_put(bool high, uint8_t v)
{
    uint16_t at = (uint16_t)((vram_mapped() << 1) + (high ? 1 : 0));
    snes.vram[at] = v;
    block_written(at);
    if (snes.port.after_high == high)
        snes.vram_addr = (uint16_t)(snes.vram_addr + snes.port.step);
}

static void vram_upload(uint16_t vram_addr, PfData src, uint32_t len, bool fill, const VidVramPort *port)
{
    if (port)
        snes.port = *port;
    if (len == 0)
        return;
    snes.vram_addr = vram_addr;
    for (uint32_t i = 0; i < len; i++)
        vram_put((i & 1) != 0, data_byte(src, fill ? 0 : i));
}

static void oam_put(uint8_t v)
{
    if (snes.oam_addr & 0x200) {
        snes.oam[0x200 + (snes.oam_addr & 0x1F)] = v;
    } else if (!(snes.oam_addr & 1)) {
        snes.oam_latch = v;
    } else {
        snes.oam[snes.oam_addr - 1] = snes.oam_latch;
        snes.oam[snes.oam_addr] = v;
    }
    snes.oam_addr = (uint16_t)((snes.oam_addr + 1) & 0x3FF);
}

/* ---- The host half ------------------------------------------------------------------------------ */

void vid_host_start(Platform *pf, uint8_t (*read)(void *ctx, uint32_t addr), void *ctx)
{
    (void)pf;
    snes.read = read;
    snes.ctx = ctx;
    snes.settings_changed = snes.palette_written = true;
    DISPCNT = DISPCNT_FORCED_BLANK;
}

void vid_host_stop(Platform *pf) { (void)pf; snes.read = NULL; }

const VidFrame *vid_host_picture(Platform *pf)
{
    (void)pf;
    return NULL;
}

void vid_host_frame_begins(Platform *pf)
{
    VidHost *v = &pf->video;
    if (v->below_display)
        v->display = v->display_below;
    if (v->below_line_effects)
        v->line_effects = v->line_effects_below;
    v->display_from = v->effects_from = 0;
    v->below_display = v->below_line_effects = false;
}

size_t vid_host_observer_bytes(const Platform *pf)
{
    (void)pf;
    return sizeof snes;
}

/* ---- The view: the original's screen ------------------------------------------------------- */

void vid_host_set_view(Platform *pf, VidView view) { (void)pf, (void)view; }

VidView vid_view(const Platform *pf)
{
    (void)pf;
    return (VidView){ VID_ORIGINAL_VIEW_WIDTH, VID_ORIGINAL_VIEW_HEIGHT };
}

void vid_show_a_layer_across(Platform *pf, VidLayer bg, int16_t left, int16_t right)
{
    (void)pf, (void)bg, (void)left, (void)right;
}

void vid_lay_out_a_map_across(Platform *pf, uint16_t map_base, const VidMapLayout *layout)
{
    (void)pf, (void)map_base, (void)layout;
}

void vid_part_the_windows(Platform *pf, const VidWindowParting *parting) { (void)pf, (void)parting; }

void vid_show_a_sprite_in_the_margins(Platform *pf, const VidMarginSprite *s, const VidSpritePicture *picture)
{
    (void)pf, (void)s, (void)picture;
}

void vid_read_vram(const Platform *pf, uint16_t vram, uint16_t *words, uint16_t count)
{
    (void)pf;
    for (uint16_t i = 0; i < count; i++) {
        uint16_t at = (uint16_t)(((vram + i) & 0x7FFF) << 1);
        words[i] = (uint16_t)(snes.vram[at] | snes.vram[at + 1] << 8);
    }
}

/* Only a wider view asks what the last picture showed of a layer. */
bool vid_layer_last_shown(const Platform *pf, VidLayer bg, uint16_t line, VidBg *out)
{
    (void)pf, (void)bg, (void)line, (void)out;
    return false;
}

uint16_t vid_vram_word(Platform *pf, PfOrigin o, uint16_t vram_addr)
{
    (void)o;
    uint16_t word;
    vid_read_vram(pf, vram_addr, &word, 1);
    return word;
}

/* ---- The picture's settings ------------------------------------------------------------------- */

static uint16_t below_the_line_handler(const Platform *pf)
{
    return pf->timing.part == TM_HOST_IN_LINE ? (uint16_t)(pf->timing.line + 1) : 0;
}

static void display_from(Platform *pf, uint16_t from, const VidDisplay *d)
{
    VidHost *v = &pf->video;
    if (from) {
        v->display_from = from;
        v->below_display = true;
        v->display_below = *d;
        return;
    }
    v->display = *d;
    v->below_display = false;
}

void vid_set_display(Platform *pf, PfOrigin o, const VidDisplay *d)
{
    (void)o;
    display_from(pf, below_the_line_handler(pf), d);
}

void vid_raster_display(Platform *pf, PfOrigin o, uint16_t line, const VidDisplay *d)
{
    (void)o;
    display_from(pf, line == VID_AT_LINE_HANDLER ? below_the_line_handler(pf) : line, d);
}

void vid_enable_line_effects(Platform *pf, PfOrigin o, uint8_t slots)
{
    (void)o;
    VidHost *v = &pf->video;
    uint16_t from = below_the_line_handler(pf);
    if (from) {
        v->effects_from = from;
        v->below_line_effects = true;
        v->line_effects_below = slots;
        return;
    }
    v->line_effects = slots;
    v->below_line_effects = false;
}

void vid_set_mode(Platform *pf, PfOrigin o, const VidMode *m)
{
    (void)pf, (void)o;
    if (m->mode != snes.mode.mode)
        snes.settings_changed = true;
    snes.mode = *m;
}

void vid_set_bg_map(Platform *pf, PfOrigin o, VidLayer bg, const VidBg *b)
{
    (void)pf, (void)o;
    if (bg > VID_BG4)
        return;
    VidBg *s = &snes.bg[bg];
    if (s->map_base != b->map_base || s->map_w != b->map_w || s->map_h != b->map_h)
        snes.settings_changed = true;
    s->map_base = b->map_base;
    s->map_w = b->map_w;
    s->map_h = b->map_h;
}

void vid_set_bg_chars(Platform *pf, PfOrigin o, VidLayer first, uint16_t base0, uint16_t base1)
{
    (void)pf, (void)o;
    if (first + 1 > VID_BG4)
        return;
    if (snes.bg[first].char_base != base0 || snes.bg[first + 1].char_base != base1)
        snes.settings_changed = true;
    snes.bg[first].char_base = base0;
    snes.bg[first + 1].char_base = base1;
}

void vid_set_scroll(Platform *pf, PfOrigin o, VidLayer bg, uint16_t hofs, uint16_t vofs, uint32_t fields)
{
    (void)pf, (void)o;
    if (bg > VID_BG4)
        return;
    if (fields & VID_SCROLL_H)
        snes.bg[bg].hofs = hofs & 0x3FF;
    if (fields & VID_SCROLL_V)
        snes.bg[bg].vofs = vofs & 0x3FF;
}

void vid_set_mosaic(Platform *pf, PfOrigin o, uint8_t bgs, uint8_t size) { (void)pf, (void)o, (void)bgs, (void)size; }
void vid_set_mode7(Platform *pf, PfOrigin o, const VidMode7 *m7, uint32_t fields)
{
    (void)pf, (void)o, (void)m7, (void)fields;
}

void vid_set_obj_config(Platform *pf, PfOrigin o, const VidObjConfig *c)
{
    (void)pf, (void)o;
    if (c->base != snes.obj.base || c->name_select != snes.obj.name_select)
        snes.settings_changed = true;
    snes.obj = *c;
}

void vid_set_screens(Platform *pf, PfOrigin o, uint8_t main_layers, uint8_t sub_layers, uint32_t fields)
{
    (void)pf, (void)o, (void)sub_layers;
    if (fields & VID_SCREEN_MAIN)
        snes.main_layers = main_layers;
}

void vid_set_video_config(Platform *pf, PfOrigin o, const VidVideoConfig *c) { (void)pf, (void)o, (void)c; }
void vid_set_windows(Platform *pf, PfOrigin o, const VidWindows *w, uint32_t fields)
{
    (void)pf, (void)o, (void)w, (void)fields;
}
void vid_set_color_math(Platform *pf, PfOrigin o, const VidColorMath *m, uint32_t fields)
{
    (void)pf, (void)o, (void)m, (void)fields;
}
void vid_set_fixed_color(Platform *pf, PfOrigin o, uint8_t channels, uint8_t intensity)
{
    (void)pf, (void)o, (void)channels, (void)intensity;
}
void vid_set_line_slot(Platform *pf, PfOrigin o, uint8_t slot, const VidLineEffect *e)
{
    (void)pf, (void)o, (void)slot, (void)e;
}
void vid_set_line_table(Platform *pf, PfOrigin o, uint8_t slot, uint16_t table)
{
    (void)pf, (void)o, (void)slot, (void)table;
}

/* ---- Sends: kept as the SNES keeps them, and their time as the native backend charges it ---------- */

static void charge_send(Platform *pf, uint32_t bytes)
{
    if (pf->timing.part != TM_HOST_IDLE)
        tm_host_charge(pf, time_of_transfer(bytes));
}

void vid_start_vram_uploads(Platform *pf, PfOrigin o, const VidVramPort *port)
{
    (void)pf, (void)o;
    if (port)
        snes.port = *port;
}

void vid_send_vram(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len, const VidVramPort *port)
{
    (void)o;
    charge_send(pf, len);
    vram_upload(vram_addr, src, len, false, port);
}

/* A wider view's own: never sent here. */
void vid_send_view_vram(Platform *pf, uint16_t vram_addr, PfData src, uint32_t len, const VidVramPort *port)
{
    (void)pf, (void)vram_addr, (void)src, (void)len, (void)port;
}

void vid_send_vram_and_wait(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len)
{
    (void)o;
    charge_send(pf, len);
    vram_upload(vram_addr, src, len, false, &(VidVramPort){ .step = 1, .after_high = true });
}

void vid_send_vram_next_frame(Platform *pf, PfOrigin o, PfOrigin wait, uint16_t vram_addr, PfData src, uint32_t len,
                              const VidVramPort *port)
{
    tm_wait_frame(pf, wait);
    vid_send_vram(pf, o, vram_addr, src, len, port);
}

void vid_send_vram_in_mode(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len,
                           const VidVramPort *port, uint8_t transfer_mode)
{
    (void)transfer_mode;
    vid_send_vram(pf, o, vram_addr, src, len, port);
}

void vid_send_vram_half(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len, VidVramHalf half,
                        const VidVramPort *port)
{
    (void)o;
    charge_send(pf, len);
    if (port)
        snes.port = *port;
    if (len == 0)
        return;
    snes.vram_addr = vram_addr;
    for (uint32_t i = 0; i < len; i++)
        vram_put(half == VID_VRAM_HIGH_BYTES, data_byte(src, i));
}

void vid_send_vram_fill(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData value, uint32_t len,
                        const VidVramPort *port)
{
    (void)o;
    charge_send(pf, len);
    vram_upload(vram_addr, value, len, true, port);
}

void vid_send_palette(Platform *pf, PfOrigin o, uint8_t first, PfData src, uint16_t count)
{
    (void)o;
    charge_send(pf, 2u * count);
    uint8_t at = first;
    for (uint16_t i = 0; i < count; i++) {
        uint8_t lo = data_byte(src, 2u * i), hi = data_byte(src, 2u * i + 1);
        snes.cgram[at++] = (uint16_t)((hi & 0x7F) << 8 | lo);
    }
    snes.palette_written = true;
}

void vid_fill_vram_half(Platform *pf, PfOrigin o, PfData value, uint32_t len, VidVramHalf half)
{
    (void)o;
    charge_send(pf, len);
    if (len == 0)
        return;
    uint8_t byte = data_byte(value, 0);
    for (uint32_t i = 0; i < len; i++)
        vram_put(half == VID_VRAM_HIGH_BYTES, byte);
}

void vid_continue_vram(Platform *pf, PfOrigin o, PfData src, uint32_t len)
{
    (void)o;
    charge_send(pf, len);
    for (uint32_t i = 0; i < len; i++)
        vram_put((i & 1) != 0, data_byte(src, i));
}

void vid_continue_vram_half(Platform *pf, PfOrigin o, PfData src, uint32_t len, VidVramHalf half)
{
    (void)o;
    charge_send(pf, len);
    for (uint32_t i = 0; i < len; i++)
        vram_put(half == VID_VRAM_HIGH_BYTES, data_byte(src, i));
}

void vid_send_oam(Platform *pf, PfOrigin o, PfData oam, const VidSpritePlace *places)
{
    (void)pf, (void)o, (void)places;
    for (uint32_t i = 0; i < sizeof snes.oam; i++)
        snes.oam[i] = data_byte(oam, i);
}

void vid_send_oam_fill(Platform *pf, PfOrigin o, uint16_t word_addr, uint8_t value, uint16_t len)
{
    vid_set_oam_address(pf, o, word_addr, false);
    for (uint32_t i = 0; i < len; i++)
        oam_put(value);
}

static uint8_t sprite_attributes(const VidSprite *sp)
{
    return (uint8_t)((sp->vflip ? 0x80 : 0) | (sp->hflip ? 0x40 : 0) | (sp->priority & 3) << 4 | (sp->palette & 7) << 1 |
                     (sp->tile >> 8 & 1));
}

void vid_set_sprite(Platform *pf, PfOrigin o, uint8_t index, const VidSprite *sp)
{
    const uint8_t entry[VID_OAM_ENTRY_BYTES] = { vid_oam_x_low(sp->x), sp->y, (uint8_t)sp->tile, sprite_attributes(sp) };
    vid_set_oam_address(pf, o, (uint16_t)(index * VID_OAM_ENTRY_BYTES / 2), false);
    for (unsigned i = 0; i < VID_OAM_ENTRY_BYTES; i++)
        oam_put(entry[i]);
}

void vid_stop_uploads(Platform *pf, PfOrigin o) { (void)pf, (void)o; }

/* A screen sets all four layers and the settings the picture is made from. */
void vid_apply_screen(Platform *pf, PfOrigin o, const VidScreen *sc)
{
    if (sc->display)
        vid_set_display(pf, o, sc->display);
    vid_set_obj_config(pf, o, &sc->sprites);
    vid_set_oam_address(pf, o, 0x0000, false);
    vid_set_mode(pf, o, &sc->mode);
    for (unsigned bg = 0; bg < 4; bg++) {
        vid_set_bg_map(pf, o, (VidLayer)bg, &sc->bg[bg]);
        vid_set_scroll(pf, o, (VidLayer)bg, sc->bg[bg].hofs, sc->bg[bg].vofs, VID_SCROLL_H | VID_SCROLL_V);
    }
    vid_set_bg_chars(pf, o, VID_BG1, sc->bg[VID_BG1].char_base, sc->bg[VID_BG2].char_base);
    vid_set_bg_chars(pf, o, VID_BG3, sc->bg[VID_BG3].char_base, sc->bg[VID_BG4].char_base);
    vid_start_vram_uploads(pf, o, &sc->vram_port);
    vid_set_vram_address(pf, o, 0x0000);
    vid_set_screens(pf, o, sc->main_layers, sc->sub_layers, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    pf->video.screens_applied++;
}

void vid_set_vram_address(Platform *pf, PfOrigin o, uint16_t vram_addr) { (void)pf, (void)o; snes.vram_addr = vram_addr; }

void vid_write_vram(Platform *pf, PfOrigin o, uint16_t value)
{
    (void)pf, (void)o;
    vram_put(false, (uint8_t)value);
    vram_put(true, (uint8_t)(value >> 8));
}

void vid_write_vram_byte(Platform *pf, PfOrigin o, bool high, uint8_t value)
{
    (void)pf, (void)o;
    vram_put(high, value);
}

void vid_set_palette_address(Platform *pf, PfOrigin o, uint8_t first) { (void)pf, (void)o, (void)first; }

void vid_set_palette_color(Platform *pf, PfOrigin o, uint8_t index, uint16_t color)
{
    (void)pf, (void)o;
    snes.cgram[index] = color & 0x7FFF;
    snes.palette_written = true;
}

void vid_set_oam_address(Platform *pf, PfOrigin o, uint16_t word_addr, bool priority_rotation)
{
    (void)pf, (void)o, (void)priority_rotation;
    snes.oam_addr = (uint16_t)((word_addr & 0x1FF) << 1);
}

/* ---- The GBA's picture --------------------------------------------------------------------------- */

/* What the settings make each part of VRAM. */
typedef enum { NONE, TILES_4BPP = 1, TILES_2BPP = 2, MAP_4BPP = 4, MAP_2BPP = 8, SPRITE_TILES = 16 } Role;

/* A layer as the GBA shows it: how deep its tiles are, where they and its
 * map are, how far its 4bpp tiles are from the GBA's 16 KiB char block (in
 * tiles), and which of the 2bpp tile caches is its. */
typedef struct {
    bool shown;
    uint8_t bpp, cache;
    uint16_t char_byte, map_byte, map_bytes;
    uint16_t tile_offset;
} Layer;

/* The 2bpp layers' tiles, widened, live where the layout leaves room in the
 * GBA's background VRAM: a window of 1,024 tiles from a 16 KiB block, of
 * whose places those that no 4bpp tile or map takes are handed out to the
 * tiles the 2bpp layers' maps name, as they name them. A tile the window
 * has no room left for shows blank; the layout's log line counts them. */
enum { CACHES = 2, CACHE_TILES = 1024, NO_SLOT = 0xFFFF };
static Layer layers[4];
static uint32_t wide_block;
static uint8_t roles[BLOCKS];
static uint16_t slot_of[CACHES][CACHE_TILES];
static uint16_t next_slot, blank_slot, misses;

static uint16_t map_bytes(const VidBg *b) { return (uint16_t)((b->map_w == 64 ? 2 : 1) * (b->map_h == 64 ? 2 : 1) * SCREEN_BYTES); }

static bool within(uint32_t byte, uint32_t start, uint32_t length)
{
    return ((byte - start) & (VRAM_BYTES - 1)) < length;
}

/* Whether the window's tile `slot` is free of 4bpp tiles and maps. */
static bool slot_free(uint32_t slot)
{
    uint32_t byte = wide_block * 0x4000 + slot * 32;
    return byte < VRAM_BYTES && !(roles[byte / BLOCK_BYTES] & (TILES_4BPP | MAP_4BPP | MAP_2BPP));
}

static uint16_t take_slot(void)
{
    while (next_slot < CACHE_TILES && !slot_free(next_slot))
        next_slot++;
    return next_slot < CACHE_TILES ? next_slot++ : NO_SLOT;
}

/* The layers mode 1 and mode 0 have (the game's others, Mode 7's, aren't
 * shown yet), what each makes of VRAM, and the 2bpp tiles' window. */
static void lay_out(void)
{
    memset(layers, 0, sizeof layers);
    /* mode 2 is mode 1 without BG3, whose map there holds the offsets a column (not kept yet) */
    unsigned count = snes.mode.mode == 0 ? 4 : snes.mode.mode == 1 ? 3 : snes.mode.mode == 2 ? 2 : 0, caches = 0;
    for (unsigned n = 0; n < count; n++) {
        Layer *l = &layers[n];
        const VidBg *b = &snes.bg[n];
        l->bpp = snes.mode.mode != 0 && n < 2 ? 4 : 2;
        if (l->bpp == 2 && caches == CACHES)
            continue;   /* mode 0's third and fourth: not shown yet */
        l->shown = true;
        l->cache = l->bpp == 2 ? (uint8_t)caches++ : 0;
        l->char_byte = (uint16_t)(b->char_base << 1);
        l->map_byte = (uint16_t)(b->map_base << 1);
        l->map_bytes = map_bytes(b);
        l->tile_offset = l->bpp == 4 ? (uint16_t)((l->char_byte & 0x3FFF) / 32) : 0;
    }
    memset(roles, 0, sizeof roles);
    for (unsigned k = 0; k < BLOCKS; k++) {
        uint32_t byte = (uint32_t)k * BLOCK_BYTES;
        for (unsigned n = 0; n < 4; n++) {
            const Layer *l = &layers[n];
            if (!l->shown)
                continue;
            if (within(byte, l->char_byte, l->bpp == 4 ? TILES_4BPP_BYTES : TILES_2BPP_BYTES))
                roles[k] |= l->bpp == 4 ? TILES_4BPP : TILES_2BPP;
            if (within(byte, l->map_byte, l->map_bytes))
                roles[k] |= l->bpp == 4 ? MAP_4BPP : MAP_2BPP;
        }
        uint32_t obj = (uint32_t)snes.obj.base << 1, second = (uint32_t)(snes.obj.base + snes.obj.name_select) << 1;
        if (within(byte, obj, OBJ_TABLE_BYTES) || within(byte, second, OBJ_TABLE_BYTES))
            roles[k] |= SPRITE_TILES;
    }
    unsigned best = 0, most = 0;
    for (unsigned blk = 0; blk < 3; blk++) {
        wide_block = blk;
        unsigned free = 0;
        for (uint32_t slot = 0; slot < CACHE_TILES; slot++)
            free += slot_free(slot);
        if (free > most)
            most = free, best = blk;
    }
    wide_block = best;
    memset(slot_of, 0xFF, sizeof slot_of);
    next_slot = misses = 0;
    blank_slot = take_slot();
    if (blank_slot != NO_SLOT)
        for (unsigned w = 0; w < 8; w++)
            BG_VRAM[(wide_block * 0x4000 + blank_slot * 32u) / 4 + w] = 0;
    fprintf(stderr, "video: mode %u%s, BG1 map %04X %ux%u chars %04X, BG2 map %04X %ux%u chars %04X, "
                    "BG3 map %04X %ux%u chars %04X, sprites %04X+%04X size %u; 2bpp tiles from block %u, %u places\n",
            snes.mode.mode, snes.mode.bg3_priority ? " (BG3 in front)" : "", snes.bg[0].map_base, snes.bg[0].map_w,
            snes.bg[0].map_h, snes.bg[0].char_base, snes.bg[1].map_base, snes.bg[1].map_w, snes.bg[1].map_h,
            snes.bg[1].char_base, snes.bg[2].map_base, snes.bg[2].map_w, snes.bg[2].map_h, snes.bg[2].char_base,
            snes.obj.base, snes.obj.name_select, snes.obj.size_select, (unsigned)best, most);
}

/* A row of a tile's bitplanes as the GBA's packed nibbles, pixel 0 lowest. */
static uint32_t packed_row(uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3)
{
    uint32_t row = 0;
    for (unsigned x = 0; x < 8; x++) {
        unsigned bit = 7 - x;
        uint32_t pixel = (uint32_t)((p0 >> bit) & 1) | (uint32_t)((p1 >> bit) & 1) << 1 |
                         (uint32_t)((p2 >> bit) & 1) << 2 | (uint32_t)((p3 >> bit) & 1) << 3;
        row |= pixel << (4 * x);
    }
    return row;
}

static void convert_4bpp(volatile uint32_t *to, const uint8_t *tile)
{
    for (unsigned y = 0; y < 8; y++)
        to[y] = packed_row(tile[2 * y], tile[2 * y + 1], tile[16 + 2 * y], tile[16 + 2 * y + 1]);
}

/* A 2bpp layer's tile `tile`, widened into its place in the window. */
static void convert_2bpp(const Layer *l, uint16_t tile, uint16_t slot)
{
    volatile uint32_t *to = BG_VRAM + (wide_block * 0x4000 + slot * 32u) / 4;
    for (unsigned y = 0; y < 8; y++) {
        uint32_t at = (l->char_byte + tile * 16u + 2 * y) & (VRAM_BYTES - 1);
        to[y] = packed_row(snes.vram[at], snes.vram[at + 1], 0, 0);
    }
}

/* A 2bpp layer's tile's place in the window, given it the first time. */
static uint16_t slot_for(const Layer *l, uint16_t tile)
{
    uint16_t *slot = &slot_of[l->cache][tile & (CACHE_TILES - 1)];
    if (*slot == NO_SLOT) {
        *slot = take_slot();
        if (*slot == NO_SLOT) {
            misses++;
            return blank_slot;
        }
        convert_2bpp(l, tile, *slot);
    }
    return *slot;
}

/* A map entry as the GBA's: its tile (moved by the layer's offset, or its
 * place in the window), its flips, and its palette (a 2bpp layer's are
 * 8-15). */
static uint16_t map_entry(uint16_t e, const Layer *l)
{
    uint16_t tile = l->bpp == 2 ? slot_for(l, e & 0x3FF) : (uint16_t)(((e & 0x3FF) + l->tile_offset) & 0x3FF);
    uint16_t palette = (uint16_t)((e >> 10 & 7) + (l->bpp == 2 ? 8 : 0));
    return (uint16_t)(tile | (e & 0x4000 ? 0x0400 : 0) | (e & 0x8000 ? 0x0800 : 0) | palette << 12);
}

static const Layer *map_layer(uint32_t byte)
{
    for (unsigned n = 0; n < 4; n++)
        if (layers[n].shown && within(byte, layers[n].map_byte, layers[n].map_bytes))
            return &layers[n];
    return NULL;
}

/* A sprite tile's place in the GBA's sprite VRAM: the SNES's two tables of
 * 16 tiles a row side by side in its 32 a row. */
static int sprite_slot(uint32_t byte)
{
    uint32_t obj = (uint32_t)snes.obj.base << 1, second = (uint32_t)(snes.obj.base + snes.obj.name_select) << 1;
    int table = within(byte, obj, OBJ_TABLE_BYTES) ? 0 : within(byte, second, OBJ_TABLE_BYTES) ? 1 : -1;
    if (table < 0)
        return -1;
    uint32_t tile = ((byte - (table ? second : obj)) & (VRAM_BYTES - 1)) / 32;
    return (int)((tile / 16) * 32 + (unsigned)table * 16 + tile % 16);
}

static void convert_block(unsigned k)
{
    uint32_t first = (uint32_t)k * BLOCK_BYTES;
    uint8_t role = roles[k];
    if (role & (MAP_4BPP | MAP_2BPP)) {
        const Layer *l = map_layer(first);
        volatile uint16_t *to = (volatile uint16_t *)BG_VRAM + first / 2;
        for (uint32_t i = 0; i < BLOCK_BYTES; i += 2)
            to[i / 2] = map_entry((uint16_t)(snes.vram[first + i] | snes.vram[first + i + 1] << 8), l);
    } else if (role & TILES_4BPP) {
        for (uint32_t t = 0; t < BLOCK_BYTES; t += 32)
            convert_4bpp(BG_VRAM + (first + t) / 4, &snes.vram[first + t]);
    }
    if (role & TILES_2BPP)
        for (unsigned n = 0; n < 4; n++) {
            const Layer *l = &layers[n];
            if (!l->shown || l->bpp != 2 || !within(first, l->char_byte, TILES_2BPP_BYTES))
                continue;
            for (uint32_t t = 0; t < BLOCK_BYTES; t += 16) {
                uint16_t tile = (uint16_t)(((first + t - l->char_byte) & (VRAM_BYTES - 1)) / 16);
                uint16_t slot = slot_of[l->cache][tile & (CACHE_TILES - 1)];
                if (slot != NO_SLOT)
                    convert_2bpp(l, tile, slot);
            }
        }
    if (role & SPRITE_TILES)
        for (uint32_t t = 0; t < BLOCK_BYTES; t += 32) {
            int slot = sprite_slot(first + t);
            if (slot >= 0)
                convert_4bpp(OBJ_VRAM + (uint32_t)slot * 8, &snes.vram[first + t]);
        }
}

static void present_palettes(void)
{
    for (unsigned i = 0; i < 128; i++)
        BG_PALETTE[i] = snes.cgram[i];
    for (unsigned p = 0; p < 8; p++)
        for (unsigned c = 0; c < 4; c++)
            BG_PALETTE[(8 + p) * 16 + c] = snes.cgram[p * 4 + c];
    for (unsigned i = 0; i < 128; i++)
        OBJ_PALETTE[i] = snes.cgram[128 + i];
}

/* The SNES's sprite sizes, small and large, for each of its size settings. */
static const uint8_t sprite_sizes[8][2][2] = {
    { { 8, 8 }, { 16, 16 } }, { { 8, 8 }, { 32, 32 } }, { { 8, 8 }, { 64, 64 } }, { { 16, 16 }, { 32, 32 } },
    { { 16, 16 }, { 64, 64 } }, { { 32, 32 }, { 64, 64 } }, { { 16, 32 }, { 32, 64 } }, { { 16, 32 }, { 32, 32 } },
};

/* The GBA's shape and size bits for a sprite's width and height, or false. */
static bool gba_size(unsigned w, unsigned h, uint16_t *shape, uint16_t *size)
{
    static const struct { uint8_t w, h, shape, size; } sizes[] = {
        { 8, 8, 0, 0 }, { 16, 16, 0, 1 }, { 32, 32, 0, 2 }, { 64, 64, 0, 3 },
        { 16, 32, 2, 2 }, { 32, 64, 2, 3 },
    };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        if (sizes[i].w == w && sizes[i].h == h) {
            *shape = sizes[i].shape;
            *size = sizes[i].size;
            return true;
        }
    return false;
}

/* The SNES's sprite priorities among the layers, as the GBA's. */
static const uint8_t sprite_priority[4] = { 3, 2, 2, 1 };

static void present_sprites(void)
{
    for (unsigned n = 0; n < SPRITES; n++) {
        const uint8_t *e = &snes.oam[n * 4];
        uint8_t high = (uint8_t)(snes.oam[0x200 + n / 4] >> (2 * (n % 4)));
        int x = e[0] | (high & 1) << 8;
        if (x >= 256)
            x -= 512;
        unsigned w = sprite_sizes[snes.obj.size_select & 7][(high >> 1) & 1][0];
        unsigned h = sprite_sizes[snes.obj.size_select & 7][(high >> 1) & 1][1];
        uint16_t shape, size;
        volatile uint16_t *to = &OAM[n * 4];
        int gx = x - CAMERA_X, gy = (e[1] - CAMERA_Y) & 0xFF;
        if (!gba_size(w, h, &shape, &size) || gx <= -(int)w || gx >= 240) {
            to[0] = 0x0200;   /* hidden */
            continue;
        }
        uint16_t name = (uint16_t)(e[2] | (e[3] & 1) << 8);
        uint16_t tile = (uint16_t)(((name >> 4) & 15) * 32 + (name >> 8) * 16 + (name & 15));
        to[0] = (uint16_t)(gy | shape << 14);
        to[1] = (uint16_t)((gx & 0x1FF) | (e[3] & 0x40 ? 0x1000 : 0) | (e[3] & 0x80 ? 0x2000 : 0) | size << 14);
        to[2] = (uint16_t)(tile | sprite_priority[(e[3] >> 4) & 3] << 10 | ((e[3] >> 1) & 7) << 12);
    }
}

void gba_video_present(Platform *pf)
{
    const VidDisplay *d = &pf->video.display;
    static unsigned presents;
    if (++presents % 300 == 0) {
        unsigned shown = 0;
        for (unsigned n = 0; n < SPRITES; n++)
            shown += snes.oam[n * 4 + 1] < 224;
        fprintf(stderr, "video debug: main %02X, %u sprites on screen; sprite 0 %02X %02X %02X %02X\n", snes.main_layers,
                shown, snes.oam[0], snes.oam[1], snes.oam[2], snes.oam[3]);
    }
    if (snes.settings_changed) {
        lay_out();
        for (unsigned i = 0; i < BLOCKS / 32; i++)
            snes.written[i] = 0xFFFFFFFFu;
        snes.settings_changed = false;
    }
    for (unsigned k = 0; k < BLOCKS; k++)
        if (snes.written[k / 32] & 1u << (k % 32))
            convert_block(k);
    memset(snes.written, 0, sizeof snes.written);
    if (snes.palette_written) {
        present_palettes();
        snes.palette_written = false;
    }
    present_sprites();

    uint16_t control = DISPCNT_OBJ_2D;
    for (unsigned n = 0; n < 4; n++) {
        const Layer *l = &layers[n];
        if (!l->shown)
            continue;
        uint16_t char_block = l->bpp == 2 ? (uint16_t)wide_block : (uint16_t)(l->char_byte / 0x4000);
        uint16_t size = (uint16_t)((snes.bg[n].map_w == 64 ? 1 : 0) | (snes.bg[n].map_h == 64 ? 2 : 0));
        uint16_t priority = n == 2 ? (snes.mode.bg3_priority ? 0 : 3) : (uint16_t)(n + 2);
        BGCNT(n) = (uint16_t)(priority | char_block << 2 | BGCNT_4BPP | (l->map_byte / SCREEN_BYTES) << 8 | size << 14);
        BGHOFS(n) = (uint16_t)(snes.bg[n].hofs + CAMERA_X);
        BGVOFS(n) = (uint16_t)(snes.bg[n].vofs + CAMERA_Y);
        if (snes.main_layers & (1u << n))
            control |= (uint16_t)(DISPCNT_BG0 << n);
    }
    if (snes.main_layers & VID_OBJ_BIT)
        control |= DISPCNT_OBJ;
    if (d->blank)
        control |= DISPCNT_FORCED_BLANK;
    DISPCNT = control;
    /* The master brightness as the GBA's fade to black. */
    BLDCNT = d->brightness < 15 ? (uint16_t)(BLD_DARKEN | BLD_ALL_FIRST) : 0;
    BLDY = (uint16_t)((15 - (d->brightness & 15)) * 16 / 15);
}

size_t gba_video_bytes(void) { return sizeof snes; }
