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
 * - VRAM is converted 32 bytes at a time, the units whose bytes the game
 *   changed (much of what it sends, VRAM already holds) and, where the
 *   settings change, all of them, each as what the settings make it: 4bpp
 *   tiles (bitplanes to the GBA's packed nibbles) at the same place
 *   in the GBA's background VRAM, a map (each entry's palette, flips and
 *   tile moved to the GBA's bits) at the same place too, 2bpp tiles widened
 *   to 4bpp in a 16 KiB block of their own, and sprite tiles into sprite
 *   VRAM, the two tables one under the other, 16 tiles of each row of the
 *   GBA's 32 (its two-dimensional layout, so that a sprite's rows of tiles
 *   are 16 names apart on both).
 * - The SNES's per-tile priority has no GBA counterpart: each layer takes
 *   one priority, and the sprites' four map onto the GBA's around them.
 * - The GBA's 240x160 screen is a smaller view (vid_view): the game frames
 *   it, a camera on the SNES's 256x224 and bands of its lines kept in view
 *   (core/view_framing.h), and each GBA line shows the SNES line its band
 *   says, its layers scrolled and its sprites moved to match.
 *
 * The sends still take the time the native backend charges, which is the
 * game's play; the conversion is the GBA's own work, outside the game's
 * frame. */
#include <stddef.h>
#include <string.h>

#include "platform/native/host.h"
#include "platform/time_model.h"
#include "platform/timing.h"

#include "services/video.h"
#include "system/interrupts.h"

/* ---- The GBA's registers and memory ------------------------------------------------------------- */

#define REG16(at) (*(volatile uint16_t *)(at))
/* State kept in fast RAM (IWRAM), which has the room work RAM hasn't. */
#define IN_FAST_RAM __attribute__((section(".iwram_bss")))
#define DISPCNT REG16(0x04000000)
#define BGCNT(n) REG16(0x04000008 + 2 * (n))
#define BGHOFS(n) REG16(0x04000010 + 4 * (n))
#define BGVOFS(n) REG16(0x04000012 + 4 * (n))
#define BLDCNT REG16(0x04000050)
#define BLDALPHA REG16(0x04000052)
#define BLDY REG16(0x04000054)
#define BG_PALETTE ((volatile uint16_t *)0x05000000)
#define OBJ_PALETTE ((volatile uint16_t *)0x05000200)
#define BG_VRAM ((volatile uint32_t *)0x06000000)
#define OBJ_VRAM ((volatile uint32_t *)0x06010000)
#define OAM ((volatile uint16_t *)0x07000000)

enum {
    DISPCNT_FORCED_BLANK = 0x0080, DISPCNT_BG0 = 0x0100, DISPCNT_OBJ = 0x1000, DISPCNT_OBJ_2D = 0x0000,
    BGCNT_4BPP = 0x0000,
    BLD_DARKEN = 0x00C0, BLD_ALPHA = 0x0040, BLD_ALL_FIRST = 0x003F,
    VRAM_BYTES = 0x10000, BLOCK_BYTES = 512, BLOCKS = VRAM_BYTES / BLOCK_BYTES,
    UNIT_BYTES = 32, UNITS = VRAM_BYTES / UNIT_BYTES,
    TILES_4BPP_BYTES = 1024 * 32, TILES_2BPP_BYTES = 1024 * 16, SCREEN_BYTES = 0x800,
    OBJ_TABLE_BYTES = 256 * 32, SPRITES = 128,
};

/* ---- The SNES's video memory and settings ---------------------------------------------------- */

/* A map a layer shows part of the way down, from a line effect: its layer,
 * where it starts in VRAM and how long it is (bytes), and where the GBA keeps
 * it, converted for that layer (another layer may show the same map as its
 * own, as the field's dialog's two layers do). A map a band of the view shows
 * with columns cut from inside it (VidViewPanel's cut) is kept so too, with
 * the columns left out: from its column `cut` on, each row shows the
 * columns `cut_columns` further on. */
typedef struct {
    uint8_t layer;
    uint8_t cut;          /* the first column cut, and (<< 5) how many; NO_CUT: the map whole */
    uint16_t byte, bytes;
    uint16_t gba_byte;    /* NO_PLACE: no room for it */
} LineMap;
enum { NO_PLACE = 0xFFFF, NO_CUT = 0xFF, CUT_COLUMN = 0x1F, CUT_COLUMNS_SHIFT = 5 };

typedef struct {
    uint8_t vram[VRAM_BYTES] __attribute__((aligned(4)));
    uint16_t cgram[256];
    uint8_t oam[544];
    VidVramPort port;
    uint16_t vram_addr;
    uint16_t oam_addr;
    uint8_t oam_latch;
    VidMode mode;
    VidBg bg[4];
    VidObjConfig obj;
    uint8_t main_layers, sub_layers;
    VidWindows windows;
    VidColorMath math;
    uint16_t fixed_color;          /* BGR555 */
    VidLineEffect slot[8];         /* the line effects' slots as the game programmed them */
    /* the maps the line effects show part of the way down, beside the layers' own */
    LineMap line_maps[6];
    uint8_t line_map_count;
    uint32_t changed[UNITS / 32];     /* the 32-byte units the game changed since the picture was made */
    bool palette_written, settings_changed, sprites_changed;
    PfReadGame *read;
    void *ctx;
} Snes;

static Snes snes;

/* Whether the maps the lines showed since the last layout are known
 * (maps_shown, below): not where the settings changed what they show. */
static bool usage_known __attribute__((section(".iwram_bss")));

/* Settings that change what the picture is made from: the next picture lays
 * it out afresh. */
static void settings_moved(void)
{
    snes.settings_changed = true;
    usage_known = false;
}

static void unit_changed(uint32_t byte) { snes.changed[byte / UNIT_BYTES / 32] |= 1u << (byte / UNIT_BYTES % 32); }

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

static uint8_t game_byte(uint32_t addr)
{
    uint8_t byte = 0;
    if (snes.read)
        snes.read(snes.ctx, addr, &byte, 1);
    return byte;
}

/* `n` bytes of the data a send names, from its `i`th: a host's own, or the
 * game's, read a span at a time. */
static void data_bytes(PfData src, uint32_t i, uint8_t *out, uint32_t n)
{
    if (src.ptr) {
        memcpy(out, src.ptr + i, n);
    } else if (src.byte_at) {
        for (uint32_t k = 0; k < n; k++) {
            const uint8_t *byte = pf_data_pointer(src, i + k);
            out[k] = byte ? *byte : game_byte(pf_data_address(src, i + k));
        }
    } else if (snes.read) {
        snes.read(snes.ctx, src.addr + i, out, n);
    } else {
        memset(out, 0, n);
    }
}

static uint8_t data_byte(PfData src, uint32_t i)
{
    uint8_t byte;
    data_bytes(src, i, &byte, 1);
    return byte;
}

/* The sends' bytes come through here a piece at a time, kept off the fibers'
 * small stacks; word-aligned, with room to start a piece at VRAM's own
 * alignment. */
enum { PIECE = 256 };
static uint32_t piece_words[PIECE / 4 + 1];
#define piece ((uint8_t *)piece_words)

/* A byte through the VRAM port, as the SNES's PPU takes it. Only a byte that
 * changes is converted again. */
static void vram_put(bool high, uint8_t v)
{
    uint16_t at = (uint16_t)((vram_mapped() << 1) + (high ? 1 : 0));
    if (snes.vram[at] != v) {
        snes.vram[at] = v;
        unit_changed(at);
    }
    if (snes.port.after_high == high)
        snes.vram_addr = (uint16_t)(snes.vram_addr + snes.port.step);
}

typedef uint32_t __attribute__((may_alias)) Word;

/* `n` bytes into VRAM from `at` (not past its end), marking the units they
 * change: the game sends much of its VRAM again as it was. `bytes` has the
 * same alignment to a word as `at` (and `snes.vram`, a word's). */
static void vram_take(uint32_t at, const uint8_t *bytes, uint32_t n)
{
    while (n) {
        uint32_t k = UNIT_BYTES - at % UNIT_BYTES;
        if (k > n)
            k = n;
        uint8_t *to = &snes.vram[at];
        uint32_t i = 0;
        while (i < k && (at + i) % 4 && to[i] == bytes[i])
            i++;
        if ((at + i) % 4 == 0)
            while (k - i >= 4 && *(const Word *)(to + i) == *(const Word *)(bytes + i))
                i += 4;
        while (i < k && to[i] == bytes[i])
            i++;
        if (i < k) {
            memcpy(to + i, bytes + i, k - i);
            unit_changed(at);
        }
        at += k, bytes += k, n -= k;
    }
}

/* `len` bytes of `src` through the VRAM port: words, low byte first (half
 * -1), or one byte of each word (half 0 the low, 1 the high). Words through
 * a port that steps a word after the high byte, with no remapping, go
 * straight into VRAM's next bytes. */
static void vram_put_bytes(PfData src, uint32_t len, int half)
{
    uint32_t i = 0;
    if (half < 0 && snes.port.remap == 0 && snes.port.step == 1 && snes.port.after_high) {
        while (len - i >= 2) {
            uint32_t at = (uint32_t)(snes.vram_addr & 0x7FFF) << 1;
            uint32_t n = (len - i) & ~1u;
            if (n > VRAM_BYTES - at)
                n = VRAM_BYTES - at;
            if (n > PIECE)
                n = PIECE;
            data_bytes(src, i, piece + at % 4, n);
            vram_take(at, piece + at % 4, n);
            snes.vram_addr = (uint16_t)(snes.vram_addr + n / 2);
            i += n;
        }
    }
    while (i < len) {
        uint32_t n = len - i < PIECE ? len - i : PIECE;
        data_bytes(src, i, piece, n);
        for (uint32_t k = 0; k < n; k++)
            vram_put(half < 0 ? ((i + k) & 1) != 0 : half == 1, piece[k]);
        i += n;
    }
}

static void vram_upload(uint16_t vram_addr, PfData src, uint32_t len, bool fill, const VidVramPort *port)
{
    if (port)
        snes.port = *port;
    if (len == 0)
        return;
    snes.vram_addr = vram_addr;
    if (fill) {
        uint8_t byte = data_byte(src, 0);
        for (uint32_t i = 0; i < len; i++)
            vram_put((i & 1) != 0, byte);
        return;
    }
    vram_put_bytes(src, len, -1);
}

static void oam_put(uint8_t v)
{
    snes.sprites_changed = true;
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

static void fill_spread(void);

/* The view (below, "The view"): the GBA's screen, framed by the game. */
enum { GBA_VIEW_WIDTH = 240, GBA_VIEW_HEIGHT = 160 };

/* The camera, where it was last aimed (the screen's middle to start), and
 * this frame's panels kept in view (vid_keep_a_panel_in_view). */
static VidAim view_aim IN_FAST_RAM;   /* set as the service starts */
static VidViewPanel view_panels[VID_VIEW_PANELS] IN_FAST_RAM;
static uint8_t view_panel_count IN_FAST_RAM;

/* The panel the view draws this frame (vid_draw_a_panel_in_view), up to two
 * of its layers (the GBA's 0-2): its lines and their looks, and each layer's
 * map, tiles and scroll (bytes of the SNES's VRAM); then, as the sends come,
 * the run of the tiles it sends (PanelRoom). Its memory is the
 * GBA's own (below, "The drawn panel"), converted as it is sent. */
enum { PANEL_LAYERS = 2, NO_LAYER = 0xFF };
typedef struct {
    int16_t at, looks;
    uint16_t lines, looks_lines;
    uint8_t layers;                      /* the GBA's layers it draws, a bit each */
    uint8_t own_tiles;                   /* the GBA's layers whose tiles it sends */
    uint8_t layer[PANEL_LAYERS];         /* each layer it draws, or NO_LAYER */
    uint16_t map_byte[PANEL_LAYERS], char_byte[PANEL_LAYERS], hofs[PANEL_LAYERS], vofs[PANEL_LAYERS];
    /* what the lines don't depend on, or the layout made: */
    bool drawn;
    uint16_t control[PANEL_LAYERS];      /* each layer's BGCNT, made once a picture */
} DrawnPanel;
static DrawnPanel panel IN_FAST_RAM;

/* The view's own memory (vid_view_memory), cleared by each layout. */
static uint8_t view_memory[VID_VIEW_MEMORY] IN_FAST_RAM;

uint8_t *vid_view_memory(Platform *pf)
{
    (void)pf;
    return view_memory;
}

/* What a frame's lines read of the picture they were worked out from (vid_layer_last_shown):
 * the lines a mode asked of, and what the main screen showed there. */
enum { ASKED_LINES = 4, ASKED_BLANK = 0x80 };
static uint8_t asked_line[ASKED_LINES] IN_FAST_RAM;   /* 1-224; 0: none */
static uint8_t asked_shown[ASKED_LINES] IN_FAST_RAM;
static bool asked_anew IN_FAST_RAM;                   /* a line the last frame's lines didn't keep */

void vid_host_start(Platform *pf, PfReadGame *read, void *ctx)
{
    (void)pf;
    snes.read = read;
    snes.ctx = ctx;
    snes.settings_changed = snes.palette_written = snes.sprites_changed = true;
    view_aim = (VidAim){ (VID_ORIGINAL_VIEW_WIDTH - GBA_VIEW_WIDTH) / 2, (VID_ORIGINAL_VIEW_HEIGHT - GBA_VIEW_HEIGHT) / 2 };
    view_panel_count = 0;
    fill_spread();
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
    view_panel_count = 0;
    panel.drawn = false;
}

size_t vid_host_observer_bytes(const Platform *pf)
{
    (void)pf;
    return sizeof snes;
}

/* ---- The view: the GBA's screen, framed by the game ---------------------------------------- */

void vid_host_set_view(Platform *pf, VidView view) { (void)pf, (void)view; }

VidView vid_view(const Platform *pf)
{
    (void)pf;
    return (VidView){ GBA_VIEW_WIDTH, GBA_VIEW_HEIGHT };
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

void vid_aim_the_view(Platform *pf, VidAim aim) { view_aim = vid_aim_held_to_the_screen(vid_view(pf), aim); }

VidAim vid_view_aim(const Platform *pf)
{
    (void)pf;
    return view_aim;
}

void vid_keep_a_panel_in_view(Platform *pf, const VidViewPanel *panel)
{
    (void)pf;
    if (view_panel_count < VID_VIEW_PANELS)
        view_panels[view_panel_count++] = *panel;
}

/* A panel's layers as the GBA's: the first two of BG1-BG3. */
void vid_draw_a_panel_in_view(Platform *pf, const VidDrawnPanel *p)
{
    (void)pf;
    panel = (DrawnPanel){ .at = p->at, .looks = p->looks, .lines = p->lines, .looks_lines = p->looks_lines,
                          .layer = { NO_LAYER, NO_LAYER }, .drawn = true };
    unsigned s = 0;
    for (unsigned n = 0; n < 3 && s < PANEL_LAYERS; n++) {
        if (!(p->layers & VID_LAYER_BIT(n)))
            continue;
        const VidBg *b = &p->bg[n];
        panel.layer[s] = (uint8_t)n;
        panel.map_byte[s] = (uint16_t)(b->map_base << 1);
        panel.char_byte[s] = (uint16_t)(b->char_base << 1);
        panel.hofs[s] = b->hofs;
        panel.vofs[s] = b->vofs;
        panel.layers |= (uint8_t)(1u << n);
        if (p->own_tiles & VID_LAYER_BIT(n))
            panel.own_tiles |= (uint8_t)(1u << n);
        s++;
    }
}

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

/* What a mode framing a smaller view asks of the last picture: a layer's
 * tiles, and whether the main screen showed it at a line, which the frame's
 * lines keep for the lines asked of (its map and scroll are the frame's
 * start's). A line not asked of before is kept from the next frame's lines
 * on; until then the frame's start says. */
bool vid_layer_last_shown(const Platform *pf, VidLayer bg, uint16_t line, VidBg *out)
{
    (void)pf;
    if (bg > VID_BG4 || line < 1 || line > VID_ORIGINAL_VIEW_HEIGHT)
        return false;
    *out = snes.bg[bg];
    unsigned k = 0;
    while (k < ASKED_LINES && asked_line[k] && asked_line[k] != line)
        k++;
    if (k == ASKED_LINES)
        k = line % ASKED_LINES;
    if (asked_line[k] != line) {
        asked_line[k] = (uint8_t)line;
        asked_shown[k] = snes.main_layers;
        asked_anew = true;
    }
    return !(asked_shown[k] & ASKED_BLANK) && (asked_shown[k] & VID_LAYER_BIT(bg));
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
        settings_moved();
    snes.mode = *m;
}

void vid_set_bg_map(Platform *pf, PfOrigin o, VidLayer bg, const VidBg *b)
{
    (void)pf, (void)o;
    if (bg > VID_BG4)
        return;
    VidBg *s = &snes.bg[bg];
    if (s->map_base != b->map_base || s->map_w != b->map_w || s->map_h != b->map_h)
        settings_moved(), snes.line_map_count = 0;
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
        settings_moved();
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
        settings_moved();
    if (c->size_select != snes.obj.size_select)
        snes.sprites_changed = true;
    snes.obj = *c;
}

void vid_set_screens(Platform *pf, PfOrigin o, uint8_t main_layers, uint8_t sub_layers, uint32_t fields)
{
    (void)pf, (void)o;
    if (fields & VID_SCREEN_MAIN)
        snes.main_layers = main_layers;
    if (fields & VID_SCREEN_SUB)
        snes.sub_layers = sub_layers;
}

void vid_set_video_config(Platform *pf, PfOrigin o, const VidVideoConfig *c) { (void)pf, (void)o, (void)c; }
/* The members a call names, as the native backend's observer takes them. */
void vid_set_windows(Platform *pf, PfOrigin o, const VidWindows *w, uint32_t fields)
{
    (void)pf, (void)o;
    VidWindows *d = &snes.windows;
    static const uint8_t pair_layer[3][2] = { { VID_BG1, VID_BG2 }, { VID_BG3, VID_BG4 }, { VID_OBJ, VID_BACK } };
    for (int g = 0; g < 3; g++)
        if (fields & (1u << g))
            for (int k = 0; k < 2; k++) {
                d->w1[pair_layer[g][k]] = w->w1[pair_layer[g][k]];
                d->w2[pair_layer[g][k]] = w->w2[pair_layer[g][k]];
            }
    if (fields & VID_WINDOWS_W1_LEFT)
        d->w1_left = w->w1_left;
    if (fields & VID_WINDOWS_W1_RIGHT)
        d->w1_right = w->w1_right;
    if (fields & VID_WINDOWS_W2_LEFT)
        d->w2_left = w->w2_left;
    if (fields & VID_WINDOWS_W2_RIGHT)
        d->w2_right = w->w2_right;
    if (fields & VID_WINDOWS_BG_LOGIC)
        for (int l = VID_BG1; l <= VID_BG4; l++)
            d->logic[l] = w->logic[l];
    if (fields & VID_WINDOWS_OBJ_LOGIC) {
        d->logic[VID_OBJ] = w->logic[VID_OBJ];
        d->logic[VID_BACK] = w->logic[VID_BACK];
    }
    if (fields & VID_WINDOWS_MAIN)
        d->win_main = w->win_main;
    if (fields & VID_WINDOWS_SUB)
        d->win_sub = w->win_sub;
}
void vid_set_color_math(Platform *pf, PfOrigin o, const VidColorMath *m, uint32_t fields)
{
    (void)pf, (void)o;
    if (fields & VID_MATH_CONTROL) {
        snes.math.clip_black = m->clip_black;
        snes.math.prevent_math = m->prevent_math;
        snes.math.sub_screen = m->sub_screen;
        snes.math.direct = m->direct;
    }
    if (fields & VID_MATH_LAYERS) {
        snes.math.subtract = m->subtract;
        snes.math.half = m->half;
        snes.math.layers = m->layers;
    }
}

static uint16_t fixed_channels(uint16_t color, uint8_t channels, uint8_t intensity)
{
    uint16_t c = intensity & 31;
    if (channels & VID_RED)
        color = (uint16_t)((color & ~0x001F) | c);
    if (channels & VID_GREEN)
        color = (uint16_t)((color & ~0x03E0) | c << 5);
    if (channels & VID_BLUE)
        color = (uint16_t)((color & ~0x7C00) | c << 10);
    return color;
}

void vid_set_fixed_color(Platform *pf, PfOrigin o, uint8_t channels, uint8_t intensity)
{
    (void)pf, (void)o;
    snes.fixed_color = fixed_channels(snes.fixed_color, channels, intensity);
}

void vid_set_line_slot(Platform *pf, PfOrigin o, uint8_t slot, const VidLineEffect *e)
{
    (void)pf, (void)o;
    snes.slot[slot & 7] = *e;
}

void vid_set_line_table(Platform *pf, PfOrigin o, uint8_t slot, uint16_t table)
{
    (void)pf, (void)o;
    PfData *t = &snes.slot[slot & 7].table;
    t->addr = (t->addr & 0xFF0000) | table;
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
    vram_put_bytes(src, len, half == VID_VRAM_HIGH_BYTES);
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
    for (uint32_t i = 0; i < count; i += PIECE / 2) {
        uint32_t n = count - i < PIECE / 2 ? count - i : PIECE / 2;
        data_bytes(src, 2u * i, piece, 2u * n);
        for (uint32_t k = 0; k < n; k++)
            snes.cgram[at++] = (uint16_t)((piece[2 * k + 1] & 0x7F) << 8 | piece[2 * k]);
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
    vram_put_bytes(src, len, -1);
}

void vid_continue_vram_half(Platform *pf, PfOrigin o, PfData src, uint32_t len, VidVramHalf half)
{
    (void)o;
    charge_send(pf, len);
    vram_put_bytes(src, len, half == VID_VRAM_HIGH_BYTES);
}

void vid_send_oam(Platform *pf, PfOrigin o, PfData oam, const VidSpritePlace *places)
{
    (void)pf, (void)o, (void)places;
    for (uint32_t i = 0; i < sizeof snes.oam; i += PIECE) {
        uint32_t n = sizeof snes.oam - i < PIECE ? sizeof snes.oam - i : PIECE;
        data_bytes(oam, i, piece, n);
        if (memcmp(piece, &snes.oam[i], n) != 0) {
            memcpy(&snes.oam[i], piece, n);
            snes.sprites_changed = true;
        }
    }
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
typedef enum {
    NONE, TILES = 1, MAP = 2, SPRITE_TILES = 4,
    LINE_MAP = 8,         /* a line effect's map: converted to where the GBA keeps it */
} Role;

/* The layers' tiles live in caches, one for each set of tiles (layers whose
 * tiles are the same, at the same place and depth, share one): a window of
 * the GBA's background VRAM, 1,024 tiles from a 16 KiB block, whose places
 * no map takes are handed out to the tiles the layers' maps name, as they
 * name them, each converted (bitplanes to the GBA's packed nibbles, 2bpp
 * widened to 4bpp) as it is given its place and again as the game writes it.
 * A tile no place is left for shows blank; the next layout, a second on at
 * most, starts the caches afresh from what the maps name then. Where the
 * maps name more tiles than a cache has places, the layout itself misses
 * some; it is made again only once a tile misses after it, since starting
 * afresh from the same maps would miss the same tiles. */
enum { CACHES = 3, CACHE_TILES = 1024, NO_SLOT = 0xFFFF, PLACES = VRAM_BYTES / 32 };
typedef struct {
    bool used;
    uint8_t bpp;
    uint16_t char_byte;
    uint8_t block;                 /* the GBA char block its window starts at */
    uint16_t next, blank;          /* the next place to look at, a blank tile's place */
    uint16_t slot_of[CACHE_TILES]; /* each SNES tile's place in the window, or NO_SLOT */
} Cache;

/* A layer as the GBA shows it: how deep its tiles are, where its map is,
 * and which cache its tiles are in. */
typedef struct {
    bool shown;
    bool own_map;          /* whether its own map is converted (a layout may leave it out) */
    uint8_t bpp, cache;
    uint16_t char_byte, map_byte, map_bytes;
} Layer;

static Layer layers[4];
static Cache caches[CACHES];
static uint8_t roles[BLOCKS];
static uint8_t taken[PLACES / 8];   /* the 32-byte places of background VRAM a map or a tile has */
static uint32_t misses, layout_misses;
static uint16_t frames_since_layout;

static bool is_taken(uint32_t place) { return (taken[place / 8] >> (place % 8)) & 1; }
static void take(uint32_t place) { taken[place / 8] |= (uint8_t)(1u << (place % 8)); }

static uint16_t map_bytes(const VidBg *b) { return (uint16_t)((b->map_w == 64 ? 2 : 1) * (b->map_h == 64 ? 2 : 1) * SCREEN_BYTES); }

static bool within(uint32_t byte, uint32_t start, uint32_t length)
{
    return ((byte - start) & (VRAM_BYTES - 1)) < length;
}

static uint32_t tile_bytes(uint8_t bpp) { return bpp == 4 ? 32u : 16u; }

/* A place in a cache's window, for a tile: the next one free, or NO_SLOT. */
static uint16_t take_slot(Cache *c)
{
    while (c->next < CACHE_TILES && (c->block * 512u + c->next >= PLACES || is_taken(c->block * 512u + c->next)))
        c->next++;
    if (c->next == CACHE_TILES)
        return NO_SLOT;
    take(c->block * 512u + c->next);
    return c->next++;
}

/* Where a sprite tile table starts in VRAM (bytes): the second is 8 KiB on
 * from the first, and the name select's words more, as the SNES's PPU has it. */
static uint32_t sprite_table(unsigned table)
{
    return ((uint32_t)snes.obj.base << 1) + (table ? 0x2000u + ((uint32_t)snes.obj.name_select << 1) : 0);
}

/* A bitplane's byte spread out as the first bit of each of a row's eight
 * packed nibbles, its leftmost dot (the byte's top bit) in the lowest:
 * filled at the start, in fast RAM, for the tiles' conversion. */
static uint32_t spread[256] __attribute__((section(".iwram_bss")));

static void fill_spread(void)
{
    for (unsigned byte = 0; byte < 256; byte++) {
        uint32_t row = 0;
        for (unsigned x = 0; x < 8; x++)
            row |= (uint32_t)((byte >> (7 - x)) & 1) << (4 * x);
        spread[byte] = row;
    }
}

/* A row of a SNES tile's four bitplanes as the GBA's packed nibbles. */
static uint32_t packed_row(uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3)
{
    return spread[p0] | spread[p1] << 1 | spread[p2] << 2 | spread[p3] << 3;
}

static void convert_4bpp(volatile uint32_t *to, const uint8_t *tile)
{
    for (unsigned y = 0; y < 8; y++)
        to[y] = packed_row(tile[2 * y], tile[2 * y + 1], tile[16 + 2 * y], tile[16 + 2 * y + 1]);
}

static void convert_tile(const Cache *c, uint16_t tile, uint16_t slot)
{
    volatile uint32_t *to = BG_VRAM + (c->block * 512u + slot) * 8u;
    uint32_t at = c->char_byte + tile * tile_bytes(c->bpp);
    for (unsigned y = 0; y < 8; y++) {
        const uint8_t *row = &snes.vram[(at + 2 * y) & (VRAM_BYTES - 1)];
        const uint8_t *high = &snes.vram[(at + 16 + 2 * y) & (VRAM_BYTES - 1)];
        to[y] = c->bpp == 4 ? packed_row(row[0], row[1], high[0], high[1]) : packed_row(row[0], row[1], 0, 0);
    }
}

static uint16_t slot_for(Cache *c, uint16_t tile)
{
    uint16_t *slot = &c->slot_of[tile & (CACHE_TILES - 1)];
    if (*slot == NO_SLOT) {
        *slot = take_slot(c);
        if (*slot == NO_SLOT) {
            misses++;
            return c->blank;
        }
        convert_tile(c, tile, *slot);
    }
    return *slot;
}

static bool room_at(uint32_t first, uint32_t count)
{
    for (uint32_t place = first; place < first + count; place++)
        if (place >= PLACES || is_taken(place))
            return false;
    return true;
}

static void take_room(uint32_t first, uint32_t count)
{
    for (uint32_t place = first; place < first + count; place++)
        take(place);
}

/* `count` free places taken, the highest run of them that starts on a
 * multiple of `step`: its first, or -1. */
static int32_t room_from_the_top(uint32_t count, uint32_t step)
{
    for (int32_t first = (int32_t)((PLACES - count) / step * step); first >= 0; first -= (int32_t)step)
        if (room_at((uint32_t)first, count)) {
            take_room((uint32_t)first, count);
            return first;
        }
    return -1;
}

/* Room for the drawn panel, made by a layout after a mode asks whether the
 * view draws one (vid_view_draws_panels): a screen block for each of its
 * layers' maps, and a run of places for the tiles it sends, a blank one and
 * then as many as the panel has sent, at most a 240x160 screen's 600, all
 * within one char block's reach. A panel that sends more than the run holds
 * has the next layout make it longer. The layouts after keep it, but one
 * whose settings moved (`kept` false): a dialog that opens again finds it
 * made, rather than showing the screen's band for a frame, whose maps would
 * take room. While a dialog is up, its band of the screen is out of view,
 * and with it the maps the screen's dialog shows. */
enum { PANEL_TILES = 600, PANEL_TILES_AT_FIRST = 256, RUN_STEP = BLOCK_BYTES / 32 };
typedef struct {
    bool made, asked;
    bool missed;                         /* the last layout was asked for it and found no room */
    uint8_t map_block[PANEL_LAYERS];     /* the GBA's screen blocks */
    uint16_t run;                        /* the run's first place, its blank */
    uint16_t run_tiles;                  /* the tiles it holds past its blank */
    uint16_t sent;                       /* the most the panel sent since the layout */
    bool any_tile;                       /* whether a tile was sent since: first_tile is the first */
    uint16_t first_tile;
} PanelRoom;
static PanelRoom panel_room IN_FAST_RAM;

static void make_room_for_the_panel(bool kept)
{
    bool asked = panel_room.asked || (kept && panel_room.made);
    uint16_t tiles = panel_room.sent > PANEL_TILES_AT_FIRST ? panel_room.sent : PANEL_TILES_AT_FIRST;
    panel_room = (PanelRoom){ 0 };
    if (!asked)
        return;
    tiles = (uint16_t)((tiles + RUN_STEP - 1) / RUN_STEP * RUN_STEP);
    tiles = tiles > PANEL_TILES ? PANEL_TILES : tiles;
    int32_t run = -1;
    for (int32_t first = (int32_t)((PLACES - tiles - 1) / RUN_STEP * RUN_STEP); first >= 0 && run < 0; first -= RUN_STEP)
        if ((uint32_t)first % 512 + tiles + 1 <= CACHE_TILES && room_at((uint32_t)first, tiles + 1u))
            run = first;
    if (run < 0) {
        panel_room.missed = true;
        return;
    }
    take_room((uint32_t)run, tiles + 1u);
    for (unsigned s = 0; s < PANEL_LAYERS; s++) {
        int32_t block = room_from_the_top(SCREEN_BYTES / 32, SCREEN_BYTES / 32);
        if (block < 0) {
            panel_room.missed = true;
            return;   /* what it took stays taken until the next layout */
        }
        panel_room.map_block[s] = (uint8_t)(block * 32 / SCREEN_BYTES);
    }
    panel_room.run = (uint16_t)run;
    panel_room.run_tiles = tiles;
    for (unsigned w = 0; w < 8; w++)
        BG_VRAM[(uint32_t)run * 8u + w] = 0;
    panel_room.made = true;
}

/* The maps the frame's lines showed since the layout (layer_control): each
 * layer's own (bit n) and each line map (bit LINE_MAPS_SHOWN + m). */
enum { LINE_MAPS_SHOWN = 4 };
static uint16_t maps_shown IN_FAST_RAM;

/* The layers mode 1, mode 2 and mode 0 have (Mode 7's aren't shown yet), the
 * places their maps take, and their tiles' caches, each with the window the
 * most places are free in. Where `pruned`, the maps the lines haven't shown
 * since the last layout are left out, their places and the tiles they name
 * given to the rest; a map left out that a line shows again is noted, and
 * the next layout has it. */

static void lay_out(bool pruned)
{
    memset(view_memory, 0, sizeof view_memory);   /* the panel's room and the caches' places are made afresh */
    if (pruned) {
        unsigned kept = 0;
        for (unsigned m = 0; m < snes.line_map_count; m++)
            if (maps_shown >> (LINE_MAPS_SHOWN + m) & 1)
                snes.line_maps[kept++] = snes.line_maps[m];
        snes.line_map_count = (uint8_t)kept;
    }
    memset(layers, 0, sizeof layers);
    memset(caches, 0, sizeof caches);
    memset(roles, 0, sizeof roles);
    memset(taken, 0, sizeof taken);
    misses = frames_since_layout = 0;
    /* mode 2 is mode 1 without BG3, whose map there holds the offsets a column (not kept yet) */
    unsigned count = snes.mode.mode == 0 ? 4 : snes.mode.mode == 1 ? 3 : snes.mode.mode == 2 ? 2 : 0;
    for (unsigned n = 0; n < count; n++) {
        Layer *l = &layers[n];
        const VidBg *b = &snes.bg[n];
        l->bpp = snes.mode.mode != 0 && n < 2 ? 4 : 2;
        l->char_byte = (uint16_t)(b->char_base << 1);
        l->map_byte = (uint16_t)(b->map_base << 1);
        l->map_bytes = map_bytes(b);
        l->own_map = !pruned || (maps_shown >> n & 1);
        unsigned c = 0;
        while (c < CACHES && caches[c].used && !(caches[c].bpp == l->bpp && caches[c].char_byte == l->char_byte))
            c++;
        if (c == CACHES)
            continue;   /* mode 0's fourth set of tiles: not shown yet */
        caches[c].used = true;
        caches[c].bpp = l->bpp;
        caches[c].char_byte = l->char_byte;
        l->cache = (uint8_t)c;
        l->shown = true;
    }
    maps_shown = 0;
    for (unsigned k = 0; k < BLOCKS; k++) {
        uint32_t byte = (uint32_t)k * BLOCK_BYTES;
        for (unsigned n = 0; n < 4; n++) {
            const Layer *l = &layers[n];
            if (!l->shown)
                continue;
            if (within(byte, l->char_byte, CACHE_TILES * tile_bytes(l->bpp)))
                roles[k] |= TILES;
            if (l->own_map && within(byte, l->map_byte, l->map_bytes))
                roles[k] |= MAP;
        }
        if (within(byte, sprite_table(0), OBJ_TABLE_BYTES) || within(byte, sprite_table(1), OBJ_TABLE_BYTES))
            roles[k] |= SPRITE_TILES;
        if (roles[k] & MAP)
            for (uint32_t place = byte / 32; place < (byte + BLOCK_BYTES) / 32; place++)
                take(place);
    }
    /* The line effects' maps and the maps cut, each where the GBA has room
     * for it, from the top of its background VRAM down; then the drawn
     * panel's room. */
    for (unsigned m = 0; m < snes.line_map_count; m++) {
        LineMap *lm = &snes.line_maps[m];
        lm->gba_byte = NO_PLACE;
        if (!layers[lm->layer].shown)
            continue;
        for (unsigned k = 0; k < BLOCKS; k++)
            if (within((uint32_t)k * BLOCK_BYTES, lm->byte, lm->bytes))
                roles[k] |= LINE_MAP;
        int32_t at = room_from_the_top(lm->bytes / 32, SCREEN_BYTES / 32);
        if (at >= 0)
            lm->gba_byte = (uint16_t)(at * 32);
    }
    make_room_for_the_panel(pruned);
    for (unsigned c = 0; c < CACHES; c++) {
        Cache *cache = &caches[c];
        if (!cache->used)
            continue;
        unsigned most = 0;
        for (unsigned blk = 0; blk < 3; blk++) {
            unsigned free = 0;
            for (uint32_t place = blk * 512u; place < blk * 512u + CACHE_TILES && place < PLACES; place++)
                free += !is_taken(place);
            if (free > most)
                most = free, cache->block = (uint8_t)blk;
        }
        memset(cache->slot_of, 0xFF, sizeof cache->slot_of);
        cache->blank = take_slot(cache);
        if (cache->blank == NO_SLOT)
            cache->blank = 0;
        else
            for (unsigned w = 0; w < 8; w++)
                BG_VRAM[(cache->block * 512u + cache->blank) * 8u + w] = 0;
    }
}

/* A map entry as the GBA's: its tile's place in its cache, its flips, and its
 * palette (a 2bpp layer's are 8-15). */
static uint16_t map_entry(uint16_t e, const Layer *l)
{
    uint16_t tile = slot_for(&caches[l->cache], e & 0x3FF);
    uint16_t palette = (uint16_t)((e >> 10 & 7) + (l->bpp == 2 ? 8 : 0));
    return (uint16_t)(tile | (e & 0x4000 ? 0x0400 : 0) | (e & 0x8000 ? 0x0800 : 0) | palette << 12);
}

static const Layer *map_layer(uint32_t byte)
{
    for (unsigned n = 0; n < 4; n++)
        if (layers[n].shown && layers[n].own_map && within(byte, layers[n].map_byte, layers[n].map_bytes))
            return &layers[n];
    return NULL;
}

/* A unit of a map, converted for a layer into the GBA's VRAM at `to`. */
static void convert_map(uint32_t first, const Layer *l, uint32_t to)
{
    volatile uint16_t *gba = (volatile uint16_t *)BG_VRAM + to / 2;
    for (uint32_t i = 0; i < UNIT_BYTES; i += 2)
        gba[i / 2] = map_entry((uint16_t)(snes.vram[first + i] | snes.vram[first + i + 1] << 8), l);
}

/* A row of a map cut (LineMap): each of its 32 columns from the column
 * the cut puts there. */
static void convert_cut_row(const LineMap *lm, uint32_t row)
{
    unsigned cut = lm->cut & CUT_COLUMN, columns = lm->cut >> CUT_COLUMNS_SHIFT;
    const uint8_t *from = &snes.vram[(lm->byte + row * 64) & (VRAM_BYTES - 1)];
    volatile uint16_t *gba = (volatile uint16_t *)BG_VRAM + (lm->gba_byte + row * 64) / 2;
    for (unsigned column = 0; column < 32; column++) {
        unsigned c = column < cut ? column : (column + columns) % 32;
        gba[column] = map_entry((uint16_t)(from[2 * c] | from[2 * c + 1] << 8), &layers[lm->layer]);
    }
}

/* A line effect's map, or a map cut (`cut`, NO_CUT for none), for a layer. */
static LineMap *line_map(unsigned n, uint16_t byte, bool cut)
{
    for (unsigned m = 0; m < snes.line_map_count; m++) {
        LineMap *lm = &snes.line_maps[m];
        if (lm->layer == n && lm->byte == byte && (lm->cut != NO_CUT) == cut)
            return lm;
    }
    return NULL;
}

/* A map's units marked changed, so the next picture converts it again. */
static void map_changed(const LineMap *lm)
{
    for (uint32_t i = 0; i < lm->bytes; i += UNIT_BYTES)
        unit_changed((lm->byte + i) & (VRAM_BYTES - 1));
}

/* A sprite tile's place in the GBA's sprite VRAM: the SNES's two tables of
 * 16 tiles a row, one under the other, the first 16 of its 32 a row. */
static int sprite_slot(uint32_t byte)
{
    uint32_t obj = sprite_table(0), second = sprite_table(1);
    int table = within(byte, obj, OBJ_TABLE_BYTES) ? 0 : within(byte, second, OBJ_TABLE_BYTES) ? 1 : -1;
    if (table < 0)
        return -1;
    uint32_t tile = ((byte - (table ? second : obj)) & (VRAM_BYTES - 1)) / 32;
    return (int)(((unsigned)table * 16 + tile / 16) * 32 + tile % 16);
}

/* A unit of VRAM the game changed, as what the settings make it. */
static void convert_unit(unsigned u)
{
    uint32_t first = (uint32_t)u * UNIT_BYTES;
    uint8_t role = roles[first / BLOCK_BYTES];
    if (role & LINE_MAP)
        for (unsigned m = 0; m < snes.line_map_count; m++) {
            const LineMap *lm = &snes.line_maps[m];
            if (lm->gba_byte == NO_PLACE || !layers[lm->layer].shown || !within(first, lm->byte, lm->bytes))
                continue;
            uint32_t offset = (first - lm->byte) & (VRAM_BYTES - 1);
            if (lm->cut == NO_CUT)
                convert_map(first, &layers[lm->layer], lm->gba_byte + offset);
            else
                convert_cut_row(lm, offset / 64);
        }
    if (role & MAP)
        convert_map(first, map_layer(first), first);
    if (role & TILES)
        for (unsigned c = 0; c < CACHES; c++) {
            const Cache *cache = &caches[c];
            uint32_t bytes = tile_bytes(cache->bpp);
            if (!cache->used || !within(first, cache->char_byte, CACHE_TILES * bytes))
                continue;
            for (uint32_t t = 0; t < UNIT_BYTES; t += bytes) {
                uint16_t tile = (uint16_t)(((first + t - cache->char_byte) & (VRAM_BYTES - 1)) / bytes);
                uint16_t slot = cache->slot_of[tile & (CACHE_TILES - 1)];
                if (slot != NO_SLOT)
                    convert_tile(cache, tile, slot);
            }
        }
    if (role & SPRITE_TILES) {
        int slot = sprite_slot(first);
        if (slot >= 0)
            convert_4bpp(OBJ_VRAM + (uint32_t)slot * 8, &snes.vram[first]);
    }
}

/* ---- The drawn panel ------------------------------------------------------------------------------
 *
 * The panel's memory is the GBA's own: no copy of what it sends is kept (work
 * RAM has no room), but each send converted as it comes into the panel's room
 * (PanelRoom), by the panel the frame stated first: a send into a layer's
 * map converted into that layer's screen block, a send of tiles of a layer
 * whose tiles it sends converted into the run, the first tile sent since the
 * room was made the run's first. Where a map names the screen's own tiles,
 * they are the cache's. What was sent stays until a layout, which clears
 * the view's memory so the mode sends its panel afresh. A send is converted
 * as it comes, so a picture shown as the game sends shows at most a glyph
 * it types a GBA frame early. */

bool vid_view_draws_panels(const Platform *pf)
{
    (void)pf;
    if (!panel_room.made && !panel_room.missed && !panel_room.asked)
        snes.settings_changed = true;   /* lay out again, with room for one */
    panel_room.asked = true;
    return panel_room.made;
}

static Cache *cache_of(uint8_t bpp, uint16_t char_byte)
{
    for (unsigned c = 0; c < CACHES; c++)
        if (caches[c].used && caches[c].bpp == bpp && caches[c].char_byte == char_byte)
            return &caches[c];
    return NULL;
}

/* The GBA's control of a panel layer: its priority as the screen's layer's,
 * its tiles where they are, its map's screen block. */
static uint16_t panel_layer_control(unsigned s)
{
    unsigned n = panel.layer[s];
    Cache *c = cache_of(layers[n].bpp, panel.char_byte[s]);
    uint16_t char_block = panel.own_tiles & (1u << n) ? panel_room.run / 512 : c ? c->block : 0;
    uint16_t priority = n == 2 ? (snes.mode.bg3_priority ? 0 : 3) : (uint16_t)(n + 2);
    return (uint16_t)(priority | char_block << 2 | BGCNT_4BPP | panel_room.map_block[s] << 8);
}

/* `count` bytes sent into a panel layer's map, from its entry `first`, as
 * the GBA's entries: a tile it sends at its place in the run (one not sent,
 * the run's blank), the screen's at its place in its cache. Written whether
 * or not they changed: writing what VRAM holds changes nothing shown. */
static void panel_map_taken(unsigned s, uint32_t first, const uint8_t *bytes, uint32_t count)
{
    unsigned n = panel.layer[s];
    bool own = panel.own_tiles & (1u << n);
    Cache *c = own ? NULL : cache_of(layers[n].bpp, panel.char_byte[s]);
    uint16_t base = (uint16_t)(panel_room.run % 512), tiles = panel_room.any_tile ? panel_room.run_tiles : 0;
    uint16_t palettes = layers[n].bpp == 2 ? 8 : 0;
    volatile uint16_t *gba = (volatile uint16_t *)BG_VRAM + panel_room.map_block[s] * (SCREEN_BYTES / 2) + first;
    for (uint32_t i = 0; i + 1 < count && first + i / 2 < SCREEN_BYTES / 2; i += 2) {
        uint16_t e = (uint16_t)(bytes[i] | bytes[i + 1] << 8), tile = 0;
        if (own) {
            uint16_t k = (uint16_t)(((e & 0x3FF) - panel_room.first_tile) & 0x3FF);
            tile = (uint16_t)(base + (k < tiles ? 1 + k : 0));
        } else if (c) {
            tile = slot_for(c, e & 0x3FF);
        }
        gba[i / 2] = (uint16_t)(tile | (e & 0x4000 ? 0x0400 : 0) | (e & 0x8000 ? 0x0800 : 0) |
                                ((e >> 10 & 7) + palettes) << 12);
    }
}

/* A tile sent, converted into the run; one past it has the next layout
 * make the run longer. */
static void panel_tile_taken(uint8_t bpp, uint16_t tile, const uint8_t *bytes)
{
    if (!panel_room.any_tile)
        panel_room.first_tile = tile, panel_room.any_tile = true;
    uint16_t k = (uint16_t)((tile - panel_room.first_tile) & 0x3FF);
    if (k >= PANEL_TILES)
        return;
    if (k >= panel_room.sent)
        panel_room.sent = (uint16_t)(k + 1);
    if (k >= panel_room.run_tiles) {
        snes.settings_changed = true;
        return;
    }
    volatile uint32_t *to = BG_VRAM + (uint32_t)(panel_room.run + 1 + k) * 8u;
    for (unsigned y = 0; y < 8; y++) {
        uint32_t row = bpp == 4 ? packed_row(bytes[2 * y], bytes[2 * y + 1], bytes[16 + 2 * y], bytes[17 + 2 * y])
                                : packed_row(bytes[2 * y], bytes[2 * y + 1], 0, 0);
        to[y] = row;
    }
}

/* What a send to the panel's memory at `byte` is: a layer's map's entries,
 * or whole tiles of a layer whose tiles it sends. */
static void panel_bytes_taken(uint32_t byte, const uint8_t *bytes, uint32_t count)
{
    for (unsigned s = 0; s < PANEL_LAYERS; s++)
        if (panel.layer[s] != NO_LAYER && layers[panel.layer[s]].shown && within(byte, panel.map_byte[s], SCREEN_BYTES)) {
            panel_map_taken(s, ((byte - panel.map_byte[s]) & (VRAM_BYTES - 1)) / 2, bytes, count);
            return;
        }
    for (unsigned s = 0; s < PANEL_LAYERS; s++) {
        unsigned n = panel.layer[s];
        if (n == NO_LAYER || !(panel.own_tiles & (1u << n)) || !layers[n].shown)
            continue;
        uint32_t size = tile_bytes(layers[n].bpp), offset = (byte - panel.char_byte[s]) & (VRAM_BYTES - 1);
        if (offset >= CACHE_TILES * size || offset % size)
            continue;
        for (uint32_t i = 0; i + size <= count; i += size)
            panel_tile_taken(layers[n].bpp, (uint16_t)((offset + i) / size), bytes + i);
        return;
    }
}

void vid_send_panel_vram(Platform *pf, uint16_t vram_addr, PfData src, uint32_t len)
{
    (void)pf;
    if (!panel.drawn || !panel_room.made)
        return;
    uint32_t at = (uint32_t)(vram_addr & 0x7FFF) << 1;
    if (src.ptr) {
        panel_bytes_taken(at, src.ptr, len);
        return;
    }
    for (uint32_t i = 0; i < len; i += PIECE) {
        uint32_t n = len - i < PIECE ? len - i : PIECE;
        data_bytes(src, i, piece, n);
        panel_bytes_taken((at + i) & (VRAM_BYTES - 1), piece, n);
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

/* ---- The view's bands ---------------------------------------------------------------------------
 *
 * Down the GBA's 160 lines, runs of lines that each show a run of the SNES's
 * from a dot across: the camera's, and the panels' over it, a later panel
 * over an earlier one, each maybe with dots cut from inside it; the drawn
 * panel's over them, its lines looking like the screen's lines their own
 * are spread over; or black, past the SNES's lines. Worked out from the
 * framing once a frame. The lines are worked out down the SNES's picture
 * (work_out_lines), so the bands must show its lines in their order; a
 * framing that doesn't shows the camera's alone. */
typedef struct {
    int16_t row, left;
    int16_t cut_at;         /* from its dot cut_at on, the SNES's dots cut_dots further on */
    uint8_t at, lines;
    uint8_t rows;           /* the SNES's lines it shows: its own lines' count, but the drawn panel's */
    uint8_t cut_dots;
    bool black, drawn;
} Band;                     /* no padding: the bands are hashed */
enum { MOST_BANDS = 2 * (VID_VIEW_PANELS + 1) + 1 };
static Band bands[MOST_BANDS] IN_FAST_RAM;
static uint8_t band_count IN_FAST_RAM;

/* A drawn panel's line is drawn as the screen's middle across. */
enum { DRAWN_LEFT = (VID_ORIGINAL_VIEW_WIDTH - GBA_VIEW_WIDTH) / 2 };

static bool panel_draws(unsigned y)
{
    return panel.drawn && panel.layers && panel_room.made && panel.lines && (int)y >= panel.at &&
           (int)y < panel.at + (int)panel.lines;
}

/* The SNES line a drawn panel's line looks like. */
static int16_t panel_looks(unsigned y)
{
    int row = panel.looks + ((int)y - panel.at) * (int)panel.looks_lines / (int)panel.lines;
    return (int16_t)(row < 0 ? 0 : row >= VID_ORIGINAL_VIEW_HEIGHT ? VID_ORIGINAL_VIEW_HEIGHT - 1 : row);
}

static Band line_band(unsigned y)
{
    Band b = { .row = (int16_t)(view_aim.top + (int)y), .left = view_aim.left, .at = (uint8_t)y, .lines = 1, .rows = 1 };
    if (panel_draws(y)) {
        b.drawn = true;
        b.row = panel_looks(y);
        b.left = DRAWN_LEFT;
        return b;
    }
    for (unsigned n = 0; n < view_panel_count; n++) {
        const VidViewPanel *p = &view_panels[n];
        if ((int)y >= p->at && (int)y < p->at + (int)p->lines) {
            b.row = (int16_t)(p->top + ((int)y - p->at)), b.left = p->left;
            b.cut_at = p->cut_at, b.cut_dots = (uint8_t)p->cut_dots;
        }
    }
    b.black = b.row < 0 || b.row >= VID_ORIGINAL_VIEW_HEIGHT;
    return b;
}

/* Whether line `b` goes on band `last`: the same kind of band, from the same
 * dot, and the next of its lines (a drawn panel's: a line it looks like as
 * far down or further). */
static bool goes_on(const Band *last, const Band *b)
{
    if (last->black != b->black || last->drawn != b->drawn || last->left != b->left || last->cut_at != b->cut_at ||
        last->cut_dots != b->cut_dots)
        return false;
    return b->black || (b->drawn ? b->row >= last->row + last->rows - 1 : b->row == last->row + last->lines);
}

/* The first line after `y` where what a line shows may change: a panel's
 * first line or the one past its last, or the camera's or a panel's line
 * passing the SNES's first or last. */
static unsigned next_change_of_band(unsigned y, const Band *b)
{
    int next = GBA_VIEW_HEIGHT;
    for (unsigned n = 0; n < view_panel_count; n++) {
        int at = view_panels[n].at, past = view_panels[n].at + (int)view_panels[n].lines;
        next = at > (int)y && at < next ? at : next;
        next = past > (int)y && past < next ? past : next;
    }
    if (panel.drawn) {
        int at = panel.at, past = panel.at + (int)panel.lines;
        next = at > (int)y && at < next ? at : next;
        next = past > (int)y && past < next ? past : next;
    }
    if (!b->drawn) {
        int to_first = (int)y - b->row, to_past = (int)y + VID_ORIGINAL_VIEW_HEIGHT - b->row;
        next = to_first > (int)y && to_first < next ? to_first : next;
        next = to_past > (int)y && to_past < next ? to_past : next;
    }
    return (unsigned)next;
}

/* The bands, a run of lines at a time between the lines where they may change. */
static void band_up_the_view(void)
{
    band_count = 0;
    for (unsigned y = 0; y < GBA_VIEW_HEIGHT;) {
        Band b = line_band(y);
        unsigned past = next_change_of_band(y, &b);
        b.lines = (uint8_t)(past - y);
        b.rows = b.drawn ? (uint8_t)(panel_looks(past - 1) - b.row + 1) : b.lines;
        Band *last = band_count ? &bands[band_count - 1] : NULL;
        if (last && goes_on(last, &b)) {
            last->rows = (uint8_t)(b.drawn ? b.row + b.rows - last->row : last->rows + b.rows);
            last->lines = (uint8_t)(last->lines + b.lines);
        } else if (band_count < MOST_BANDS) {
            bands[band_count++] = b;
        }
        y = past;
    }
    int row = -1;
    bool in_order = band_count < MOST_BANDS;
    for (unsigned k = 0; k < band_count && in_order; k++)
        if (!bands[k].black) {
            in_order = bands[k].row > row;
            row = bands[k].row + bands[k].rows - 1;
        }
    if (!in_order) {
        bands[0] = (Band){ .row = view_aim.top, .left = view_aim.left, .lines = GBA_VIEW_HEIGHT, .rows = GBA_VIEW_HEIGHT };
        band_count = 1;
    }
}

/* The band showing the SNES's line `row`, or NULL. */
static const Band *band_showing(int row)
{
    for (unsigned k = 0; k < band_count; k++)
        if (!bands[k].black && row >= bands[k].row && row < bands[k].row + bands[k].rows)
            return &bands[k];
    return NULL;
}

/* The first of a band's lines that shows the SNES's line `row` (of those it
 * shows), or -1: a drawn panel's lines may pass over a line of the screen's
 * they are spread over. */
static int band_line(const Band *b, int row)
{
    if (!b->drawn)
        return b->at + (row - b->row);
    int r = row - panel.looks;
    int i = (r * (int)panel.lines + (int)panel.looks_lines - 1) / (int)panel.looks_lines;
    for (int y = panel.at + (i > 0 ? i : 0); y < b->at + b->lines; y++) {
        if (y < b->at)
            continue;
        int looks = panel_looks((unsigned)y);
        if (looks == row)
            return y;
        if (looks > row)
            break;
    }
    return -1;
}

/* Where the view shows the SNES's dot `x` of a band's line: across from its
 * left, past its cut less what was cut. */
static int band_x(const Band *b, int x)
{
    x -= b->left;
    if (b->cut_dots && x >= b->cut_at)
        x = x >= b->cut_at + b->cut_dots ? x - b->cut_dots : b->cut_at;
    return x;
}

/* The SNES's sprite priorities among the layers, as the GBA's. */
static const uint8_t sprite_priority[4] = { 3, 2, 2, 1 };

/* The sprites, each in the band its top line is in, or its foot's; the drawn
 * panel shows none. */
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
        int top = (e[1] + 1) & 0xFF;
        const Band *b = band_showing(top);
        if (!b)
            b = band_showing(top + (int)h - 1);
        if (b && b->drawn)
            b = NULL;
        int gx = b ? band_x(b, x) : GBA_VIEW_WIDTH, gy = b ? ((e[1] - b->row + b->at) & 0xFF) : 0;
        if (!b || !gba_size(w, h, &shape, &size) || gx <= -(int)w || gx >= GBA_VIEW_WIDTH) {
            to[0] = 0x0200;   /* hidden */
            continue;
        }
        uint16_t name = (uint16_t)(e[2] | (e[3] & 1) << 8);
        uint16_t tile = (uint16_t)((name >> 4) * 32 + (name & 15));
        to[0] = (uint16_t)(gy | shape << 14);
        to[1] = (uint16_t)((gx & 0x1FF) | (e[3] & 0x40 ? 0x1000 : 0) | (e[3] & 0x80 ? 0x2000 : 0) | size << 14);
        to[2] = (uint16_t)(tile | sprite_priority[(e[3] >> 4) & 3] << 10 | ((e[3] >> 1) & 7) << 12);
    }
}

/* ---- The picture line by line ----------------------------------------------------------------- *
 *
 * The SNES changes its settings line by line from tables in the game's
 * memory (HDMA, the video service's line effects): the field's dialog and
 * its black edges are windows and layers switched on and off part of the way
 * down. Once a frame those tables are read as the native backend's observer
 * reads them, from the frame's top, and what each of the GBA's 160 lines
 * should show is kept, as the lines where it changes. The registers are set
 * only there: the line counter's interrupt fires a line before each change
 * and the horizontal blank's at its end sets the change's registers, so a
 * frame takes two interrupts a change rather than one a line. */

#define VCOUNT REG16(0x04000006)
#define DISPSTAT REG16(0x04000004)
#define WIN0H REG16(0x04000040)
#define WIN1H REG16(0x04000042)
#define WIN0V REG16(0x04000044)
#define WIN1V REG16(0x04000046)
#define WININ REG16(0x04000048)
#define WINOUT REG16(0x0400004A)
#define REG_IE REG16(0x04000200)
#define REG_IF REG16(0x04000202)
#define REG_IME REG16(0x04000208)
#define BIOS_IF REG16(0x03007FF8)
enum {
    GBA_LINES = 160, SNES_LINES = 224, IRQ_HBLANK = 2, IRQ_VCOUNT = 4,
    DISPSTAT_HBLANK_IRQ = 0x10, DISPSTAT_VCOUNT_IRQ = 0x20,
    DISPCNT_WIN0 = 0x2000, DISPCNT_WIN1 = 0x4000, WIN_EFFECTS = 0x20,
};

/* A line's registers on the GBA. */
typedef struct {
    uint16_t dispcnt, bgcnt[3], hofs[3], vofs[3], win0h, win1h, winin, winout, bldcnt, bldy;
    uint16_t backdrop, bldalpha;
} LineRegisters;

/* The frame's changes down the picture: the first line's registers, then
 * each line whose registers differ from the line above's. */
static LineRegisters change_registers[GBA_LINES];
static uint8_t change_line[GBA_LINES];
static volatile uint8_t changes;
static uint8_t next_change;       /* the interrupts' place in the changes */
static volatile bool by_line;     /* whether the interrupts set the changes' registers */

/* The registers the line effects change, as they stand. */
typedef struct {
    VidWindows windows;
    uint8_t main_layers, sub_layers;
    uint16_t hofs[3], vofs[3];
    uint8_t map[3];       /* each layer's map, as the SNES's register byte (vid_bg_map_decode) */
    VidColorMath math;
    uint16_t fixed_color;
    uint8_t brightness;
    bool blank;
} LineState;

/* A slot's progress through its table, as HDMA's, and the line it next
 * does anything at (an entry's lines between its one transfer and the next
 * entry are passed over), with the unit it last applied. */
typedef struct {
    bool active, transfer;
    uint8_t bank, count;
    uint8_t unit;       /* the bytes a transfer takes, by its target */
    uint8_t changes;    /* what of a line's registers its transfers change (Changed) */
    bool applied;       /* whether `last` holds the unit its last transfer applied */
    uint8_t last[4];
    uint16_t cursor, indirect, line;
} Slot;

/* The tables are read 32 bytes at a time, kept for the frame's reading of
 * them (the game's memory doesn't change while the picture is made). */
enum { READ_LINES = 8, READ_LINE_BYTES = 32 };
typedef struct {
    uint32_t tag;    /* the line's address / READ_LINE_BYTES, plus one; 0: none */
    uint8_t bytes[READ_LINE_BYTES];
} ReadLine;
static ReadLine read_lines[READ_LINES];

/* Where each 32 bytes of the tables a frame's lines read came from, as they
 * read them, and a 32-bit FNV-1a hash of all their bytes in that order (a
 * word at a time), so
 * that the next frame can tell whether its tables are the same (below). In
 * fast RAM, which has the room work RAM hasn't, as the rest of what the next
 * frame keeps; a hash rather than the bytes, which it hasn't the room for
 * either. */
enum { LINES_READ_KEPT = 64 };
static uint32_t lines_read[LINES_READ_KEPT] IN_FAST_RAM;
static uint32_t lines_read_hash IN_FAST_RAM;
static uint8_t lines_read_count IN_FAST_RAM;
static bool lines_read_overflowed IN_FAST_RAM;
static bool lines_read_kept IN_FAST_RAM;   /* whether this frame keeps them */

static void read_table_line(uint32_t line, uint8_t bytes[READ_LINE_BYTES])
{
    if (snes.read)
        snes.read(snes.ctx, line * READ_LINE_BYTES, bytes, READ_LINE_BYTES);
    else
        memset(bytes, 0, READ_LINE_BYTES);
}

enum { FNV_START = 2166136261u, FNV_PRIME = 16777619u };

/* FNV-1a a word at a time, then the bytes left over. */
static uint32_t hash_on(uint32_t h, const void *bytes, size_t count)
{
    const uint8_t *b = bytes;
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        uint32_t word;
        memcpy(&word, b + i, sizeof word);
        h = (h ^ word) * FNV_PRIME;
    }
    for (; i < count; i++)
        h = (h ^ b[i]) * FNV_PRIME;
    return h;
}

static void keep_line_read(uint32_t line, const uint8_t bytes[READ_LINE_BYTES])
{
    if (lines_read_count == LINES_READ_KEPT) {
        lines_read_overflowed = true;
        return;
    }
    lines_read[lines_read_count++] = line;
    lines_read_hash = hash_on(lines_read_hash, bytes, READ_LINE_BYTES);
}

/* The 32 bytes of the tables at `line` (an address / READ_LINE_BYTES). */
static const uint8_t *line_bytes(uint32_t line)
{
    ReadLine *r = &read_lines[line % READ_LINES];
    if (r->tag != line + 1) {
        r->tag = line + 1;
        read_table_line(line, r->bytes);
        if (lines_read_kept)
            keep_line_read(line, r->bytes);
    }
    return r->bytes;
}

static uint8_t line_read(uint32_t addr)
{
    return line_bytes(addr / READ_LINE_BYTES)[addr % READ_LINE_BYTES];
}

/* A transfer's `n` bytes from `at` on in `bank`, the address wrapping in the
 * bank as HDMA's does: from one read of the tables where they lie in one 32
 * bytes (which a bank's end never splits), a byte at a time where not. */
static void table_unit(uint8_t bank, uint16_t at, uint8_t n, uint8_t out[4])
{
    uint32_t from = (uint32_t)bank << 16;
    if ((at % READ_LINE_BYTES) + n <= READ_LINE_BYTES) {
        const uint8_t *p = line_bytes((from | at) / READ_LINE_BYTES) + at % READ_LINE_BYTES;
        for (unsigned k = 0; k < n; k++)
            out[k] = p[k];
        return;
    }
    for (unsigned k = 0; k < n; k++)
        out[k] = line_read(from | (uint16_t)(at + k));
}

/* Whether the tables still hold what the last frame's lines read of them. */
static bool tables_unchanged(void)
{
    uint8_t bytes[READ_LINE_BYTES];
    uint32_t h = FNV_START;
    for (unsigned i = 0; i < lines_read_count; i++) {
        read_table_line(lines_read[i], bytes);
        h = hash_on(h, bytes, READ_LINE_BYTES);
    }
    return h == lines_read_hash;
}

static void slot_entry(Slot *sl, const VidLineEffect *e)
{
    uint32_t bank = (uint32_t)sl->bank << 16;
    sl->count = line_read(bank | sl->cursor);
    sl->cursor++;
    if (e->indirect) {
        sl->indirect = (uint16_t)(line_read(bank | sl->cursor) | line_read(bank | (uint16_t)(sl->cursor + 1)) << 8);
        sl->cursor = (uint16_t)(sl->cursor + 2);
    }
    if (sl->count == 0)
        sl->active = false;
    sl->transfer = true;
}

/* What of a line's registers a line effect's transfer may change: only
 * its layers' scroll, which a line's other registers don't depend on (a
 * battle's backgrounds wave a line at a time), or anything. */
typedef enum { CHANGED_SCROLL = 1, CHANGED_OTHER = 2 } Changed;

static uint8_t target_changes(VidLineTarget target)
{
    switch (target) {
    case VID_LINE_SCROLL_BG1: case VID_LINE_SCROLL_BG2: case VID_LINE_SCROLL_BG3:
    case VID_LINE_SCROLL_BG1_H: case VID_LINE_SCROLL_BG1_V: case VID_LINE_SCROLL_BG2_H: case VID_LINE_SCROLL_BG2_V:
    case VID_LINE_SCROLL_BG3_H: case VID_LINE_SCROLL_BG3_V:
        return CHANGED_SCROLL;
    default:
        return CHANGED_OTHER;
    }
}

/* What of a line's state a line effect's target sets (line_apply), a bit
 * a register: two slots whose targets share none can't undo each other's
 * work, so a slot's transfer of the unit it last applied changes nothing. */
enum {
    SETS_WINDOW1 = 0x3, SETS_WINDOW2 = 0xC, SETS_WINDOWS = 0xF, SETS_WIN_MAIN = 0x10, SETS_MAIN = 0x20,
    SETS_SUB = 0x40, SETS_SCROLL_H = 0x80, SETS_SCROLL_V = 0x400, SETS_MAPS = 0x2000, SETS_BRIGHTNESS = 0x10000,
    SETS_FIXED = 0x20000, SETS_MATH_CONTROL = 0x40000, SETS_MATH_LAYERS = 0x80000,
};

static uint32_t target_sets(VidLineTarget target)
{
    switch (target) {
    case VID_LINE_WINDOWS: return SETS_WINDOWS;
    case VID_LINE_WINDOW1: return SETS_WINDOW1;
    case VID_LINE_WINDOW2: return SETS_WINDOW2;
    case VID_LINE_WINDOW1_LEFT: return SETS_WINDOW1 & 1;
    case VID_LINE_WINDOW1_RIGHT: return SETS_WINDOW1 & 2;
    case VID_LINE_WINDOW2_LEFT: return SETS_WINDOW2 & 4;
    case VID_LINE_WINDOW2_RIGHT: return SETS_WINDOW2 & 8;
    case VID_LINE_SCROLL_BG1: case VID_LINE_SCROLL_BG2: case VID_LINE_SCROLL_BG3:
        return (uint32_t)(SETS_SCROLL_H | SETS_SCROLL_V) << (target - VID_LINE_SCROLL_BG1);
    case VID_LINE_SCROLL_BG1_H: return SETS_SCROLL_H;
    case VID_LINE_SCROLL_BG2_H: return SETS_SCROLL_H << 1;
    case VID_LINE_SCROLL_BG3_H: return SETS_SCROLL_H << 2;
    case VID_LINE_SCROLL_BG1_V: return SETS_SCROLL_V;
    case VID_LINE_SCROLL_BG2_V: return SETS_SCROLL_V << 1;
    case VID_LINE_SCROLL_BG3_V: return SETS_SCROLL_V << 2;
    case VID_LINE_BRIGHTNESS: return SETS_BRIGHTNESS;
    case VID_LINE_SCREENS: return SETS_MAIN | SETS_SUB | SETS_WIN_MAIN;
    case VID_LINE_MAIN_SCREEN: return SETS_MAIN;
    case VID_LINE_SUB_SCREEN: return SETS_SUB;
    case VID_LINE_BG_MAPS: return SETS_MAPS * 7;
    case VID_LINE_BG2_MAP: return SETS_MAPS * 2;
    case VID_LINE_FIXED_COLOR: return SETS_FIXED;
    case VID_LINE_COLOR_MATH: return SETS_MATH_CONTROL | SETS_MATH_LAYERS | SETS_FIXED;
    case VID_LINE_MATH_LAYERS: return SETS_MATH_LAYERS | SETS_FIXED;
    case VID_LINE_MATH_LAYERS_ONLY: return SETS_MATH_LAYERS;
    case VID_LINE_MATH_CONTROL_AND_LAYERS: return SETS_MATH_CONTROL | SETS_MATH_LAYERS;
    default: return 0;
    }
}

/* Whether the slots `slots` may run have targets that set nothing in common,
 * nor with the brightness the display takes below a line (`below_display`). */
static bool targets_apart(uint8_t slots, bool below_display)
{
    uint32_t sets = below_display ? SETS_BRIGHTNESS : 0;
    for (unsigned i = 0; i < 8; i++) {
        if (!((slots >> i) & 1))
            continue;
        uint32_t these = target_sets(snes.slot[i].target);
        if (sets & these)
            return false;
        sets |= these;
    }
    return true;
}

static uint8_t unit_bytes(VidLineTarget target)
{
    switch (target) {
    case VID_LINE_WINDOWS: case VID_LINE_SCROLL_BG1: case VID_LINE_SCROLL_BG2: case VID_LINE_SCROLL_BG3:
    case VID_LINE_BG_MAPS: case VID_LINE_SCREENS: case VID_LINE_COLOR_MATH:
    case VID_LINE_MODE7_AB: case VID_LINE_MODE7_CD: case VID_LINE_MODE7_CENTER:
        return 4;
    case VID_LINE_WINDOW1: case VID_LINE_WINDOW2: case VID_LINE_MODE: case VID_LINE_MATH_LAYERS:
    case VID_LINE_MATH_CONTROL_AND_LAYERS: case VID_LINE_SCROLL_BG1_H: case VID_LINE_SCROLL_BG1_V:
    case VID_LINE_SCROLL_BG2_V: case VID_LINE_SCROLL_BG2_H: case VID_LINE_SCROLL_BG3_H: case VID_LINE_SCROLL_BG3_V:
    case VID_LINE_MODE7_A: case VID_LINE_MODE7_B: case VID_LINE_MODE7_C: case VID_LINE_MODE7_D:
        return 2;
    default:
        return 1;
    }
}

/* A scroll register's byte, low then high, as the PPU's latch takes it. */
static void scroll_byte(uint16_t *value, unsigned k, uint8_t v)
{
    *value = k & 1 ? (uint16_t)((*value & 0xFF) | (v & 3) << 8) : (uint16_t)((*value & 0x300) | v);
}

/* What one byte of a line effect changes of what the GBA shows; the effects
 * on colour math, the mode and Mode 7 aren't shown yet. */
static void line_apply(LineState *st, VidLineTarget target, unsigned k, uint8_t v)
{
    VidWindows *w = &st->windows;
    switch (target) {
    case VID_LINE_WINDOWS:
        if (k == 0) w->w1_left = v; else if (k == 1) w->w1_right = v; else if (k == 2) w->w2_left = v; else w->w2_right = v;
        break;
    case VID_LINE_WINDOW1: if (k == 0) w->w1_left = v; else w->w1_right = v; break;
    case VID_LINE_WINDOW2: if (k == 0) w->w2_left = v; else w->w2_right = v; break;
    case VID_LINE_WINDOW1_LEFT: w->w1_left = v; break;
    case VID_LINE_WINDOW1_RIGHT: w->w1_right = v; break;
    case VID_LINE_WINDOW2_LEFT: w->w2_left = v; break;
    case VID_LINE_WINDOW2_RIGHT: w->w2_right = v; break;
    case VID_LINE_SCROLL_BG1: case VID_LINE_SCROLL_BG2: case VID_LINE_SCROLL_BG3: {
        unsigned bg = (unsigned)(target - VID_LINE_SCROLL_BG1);
        scroll_byte(k < 2 ? &st->hofs[bg] : &st->vofs[bg], k, v);
        break;
    }
    case VID_LINE_SCROLL_BG1_H: scroll_byte(&st->hofs[0], k, v); break;
    case VID_LINE_SCROLL_BG1_V: scroll_byte(&st->vofs[0], k, v); break;
    case VID_LINE_SCROLL_BG2_H: scroll_byte(&st->hofs[1], k, v); break;
    case VID_LINE_SCROLL_BG2_V: scroll_byte(&st->vofs[1], k, v); break;
    case VID_LINE_SCROLL_BG3_H: scroll_byte(&st->hofs[2], k, v); break;
    case VID_LINE_SCROLL_BG3_V: scroll_byte(&st->vofs[2], k, v); break;
    case VID_LINE_BRIGHTNESS: st->brightness = v & 15; st->blank = (v & 0x80) != 0; break;
    case VID_LINE_SCREENS:
        if (k == 0) st->main_layers = v; else if (k == 1) st->sub_layers = v; else if (k == 2) w->win_main = v;
        break;
    case VID_LINE_MAIN_SCREEN: st->main_layers = v; break;
    case VID_LINE_SUB_SCREEN: st->sub_layers = v; break;
    case VID_LINE_BG_MAPS: if (k < 3) st->map[k] = v; break;
    case VID_LINE_FIXED_COLOR: st->fixed_color = fixed_channels(st->fixed_color, (uint8_t)(v >> 5), (uint8_t)(v & 31)); break;
    case VID_LINE_COLOR_MATH:
        if (k == 0) vid_color_math_decode(&st->math, VID_MATH_CONTROL, v);
        else if (k == 1) vid_color_math_decode(&st->math, VID_MATH_LAYERS, v);
        else if (k == 2) st->fixed_color = fixed_channels(st->fixed_color, (uint8_t)(v >> 5), (uint8_t)(v & 31));
        break;
    case VID_LINE_MATH_LAYERS:
        if (k == 0) vid_color_math_decode(&st->math, VID_MATH_LAYERS, v);
        else st->fixed_color = fixed_channels(st->fixed_color, (uint8_t)(v >> 5), (uint8_t)(v & 31));
        break;
    case VID_LINE_MATH_LAYERS_ONLY: vid_color_math_decode(&st->math, VID_MATH_LAYERS, v); break;
    case VID_LINE_MATH_CONTROL_AND_LAYERS:
        vid_color_math_decode(&st->math, k == 0 ? VID_MATH_CONTROL : VID_MATH_LAYERS, v);
        break;
    case VID_LINE_BG2_MAP: st->map[1] = v; break;
    default: break;
    }
}

/* Whether a layer's windows mask it where the dot is in window 1 (`in1`)
 * and window 2 (`in2`). */
static bool masked(const VidWindows *w, unsigned layer, bool in1, bool in2)
{
    int a = w->w1[layer] == VID_WIN_IN ? in1 : w->w1[layer] == VID_WIN_OUT ? !in1 : -1;
    int b = w->w2[layer] == VID_WIN_IN ? in2 : w->w2[layer] == VID_WIN_OUT ? !in2 : -1;
    if (a < 0 && b < 0)
        return false;
    if (a < 0 || b < 0)
        return a < 0 ? b : a;
    switch (w->logic[layer]) {
    case VID_WIN_OR: return a || b;
    case VID_WIN_AND: return a && b;
    case VID_WIN_XOR: return a != b;
    default: return a == b;
    }
}

/* A window's edges (inclusive, the SNES's dots) as the GBA's on a band's
 * line (its last dot exclusive, on its screen); nothing where the left edge
 * is past the right. */
static uint16_t gba_window(uint8_t left, uint8_t right, const Band *b)
{
    if (left > right)
        return 0;
    int x1 = band_x(b, left), x2 = band_x(b, right + 1);
    x1 = x1 < 0 ? 0 : x1 > GBA_VIEW_WIDTH ? GBA_VIEW_WIDTH : x1;
    x2 = x2 < 0 ? 0 : x2 > GBA_VIEW_WIDTH ? GBA_VIEW_WIDTH : x2;
    return (uint16_t)(x1 << 8 | x2);
}

/* The layers a window region shows: in window 1 alone (WIN0), in window 2
 * alone (WIN1), in neither (outside). Where both windows are, the GBA shows
 * window 1's choice. */
static uint16_t region_layers(const LineState *st, bool in1, bool in2)
{
    uint16_t shown = WIN_EFFECTS;
    for (unsigned layer = VID_BG1; layer <= VID_OBJ; layer++) {
        if (!(st->main_layers & (1u << layer)))
            continue;
        if ((st->windows.win_main & (1u << layer)) && masked(&st->windows, layer, in1, in2))
            continue;
        shown |= (uint16_t)(1u << layer);
    }
    return shown;
}

/* A map to keep for the next layout, which converts it: one a line effect
 * shows that the layout hasn't, or one a band shows cut. */
static void note_line_map(unsigned n, uint16_t byte, uint16_t bytes, uint8_t cut)
{
    if (!layers[n].shown || line_map(n, byte, cut != NO_CUT) ||
        snes.line_map_count == sizeof snes.line_maps / sizeof snes.line_maps[0])
        return;
    snes.line_maps[snes.line_map_count++] = (LineMap){ (uint8_t)n, cut, byte, bytes, NO_PLACE };
    snes.settings_changed = true;
}

/* The GBA's control of a layer showing the map the SNES's byte names: its
 * priority and tiles as the layout set them, the map's screen block and
 * size: where a band cuts columns from inside it, the map cut where the
 * layer's scroll `hofs` puts the band's cut (a 32-column map's, and only
 * whole columns), which a layout keeps once noted. */
static uint16_t layer_control(unsigned n, uint8_t map, const Band *band, uint16_t hofs)
{
    const Layer *l = &layers[n];
    VidBg b = { 0 };
    vid_bg_map_decode(&b, map);
    uint16_t char_block = caches[l->cache].block;
    uint16_t size = (uint16_t)((b.map_w == 64 ? 1 : 0) | (b.map_h == 64 ? 2 : 0));
    uint16_t priority = n == 2 ? (snes.mode.bg3_priority ? 0 : 3) : (uint16_t)(n + 2);
    uint16_t at = (uint16_t)(b.map_base << 1), bytes = map_bytes(&b);
    bool own = at == l->map_byte && bytes == l->map_bytes;
    if (!own)
        note_line_map(n, at, bytes, NO_CUT);
    else if (!l->own_map)
        snes.settings_changed = true;   /* left out by the last layout: the next has it */
    const LineMap *lm = line_map(n, at, false);
    if (band->cut_dots && b.map_w == 32 && band->cut_dots % 8 == 0) {
        uint8_t cut = (uint8_t)(((band->cut_at + band->left + hofs) / 8 % 32) | band->cut_dots / 8 << CUT_COLUMNS_SHIFT);
        LineMap *cut_map = line_map(n, at, true);
        if (!cut_map) {
            note_line_map(n, at, bytes, cut);
            if ((cut_map = line_map(n, at, true)) != NULL)
                maps_shown |= (uint16_t)(1u << (LINE_MAPS_SHOWN + (cut_map - snes.line_maps)));
        } else if (cut_map->gba_byte != NO_PLACE) {
            if (cut_map->cut != cut) {
                cut_map->cut = cut;   /* converted again for the next picture */
                map_changed(cut_map);
            }
            lm = cut_map;
        }
    }
    if (lm)
        maps_shown |= (uint16_t)(1u << (LINE_MAPS_SHOWN + (lm - snes.line_maps)));
    else if (own)
        maps_shown |= (uint16_t)(1u << n);
    uint32_t gba_byte = lm && lm->gba_byte != NO_PLACE ? lm->gba_byte : at;
    return (uint16_t)(priority | char_block << 2 | BGCNT_4BPP | (gba_byte / SCREEN_BYTES) << 8 | size << 14);
}

static uint8_t map_byte_of(const VidBg *b)
{
    return (uint8_t)((b->map_base >> 8 & 0xFC) | (b->map_w == 64 ? 1 : 0) | (b->map_h == 64 ? 2 : 0));
}

/* The backdrop with the fixed colour's math on it, where the math takes the
 * backdrop in: the one part of the SNES's colour math the GBA is given (its
 * blending can't add a colour), which the field's dialog's gradient is. */
static uint16_t backdrop(const LineState *st)
{
    uint16_t base = snes.cgram[0];
    const VidColorMath *m = &st->math;
    if (!(m->layers & VID_BACK_BIT) || m->sub_screen || m->prevent_math == VID_REGION_ALWAYS)
        return base;
    uint16_t out = 0;
    for (unsigned shift = 0; shift < 15; shift += 5) {
        int a = base >> shift & 31, b = st->fixed_color >> shift & 31;
        int c = m->subtract ? a - b : a + b;
        if (m->half)
            c >>= 1;
        c = c < 0 ? 0 : c > 31 ? 31 : c;
        out |= (uint16_t)(c << shift);
    }
    return out;
}

/* The layers' scroll on a band's line of the GBA's screen, the SNES's line
 * shown `down` lines further down the GBA's than the GBA's own. A layer's
 * row on the SNES is its line's (the first shown is its line 1) and its
 * scroll, one further down than on the GBA. */
static void line_scroll(LineRegisters *r, const LineState *st, const Band *b, int down)
{
    for (unsigned n = 0; n < 3; n++) {
        r->hofs[n] = (uint16_t)(st->hofs[n] + b->left);
        r->vofs[n] = (uint16_t)(st->vofs[n] + down + 1);
    }
}

/* A line that shows nothing: black. The layers stay enabled, every line of
 * the frame, and a line shows the ones it does through the windows: the
 * GBA shows a layer enabled part of the way down only from three lines on,
 * where a window's layers change at once. */
static LineRegisters nothing_shown(uint16_t layers_enabled)
{
    LineRegisters r = { 0 };
    r.dispcnt = (uint16_t)(DISPCNT_OBJ_2D | DISPCNT_OBJ | DISPCNT_WIN0 | DISPCNT_WIN1 | (layers_enabled & 15) * DISPCNT_BG0);
    return r;
}

/* A line's registers: the SNES's line shown on a band's line `down` lines
 * further down the GBA's than the GBA's own. A layer's row on the SNES is
 * its line's (the first shown is its line 1) and its scroll, one further
 * down than on the GBA. A drawn panel's line takes the SNES line's looks
 * and draws the panel's own layers alone. */
static LineRegisters line_registers(const LineState *st, uint16_t layers_enabled, const Band *b, int down)
{
    LineRegisters r = nothing_shown(layers_enabled);
    if (st->blank)
        return r;
    LineState drawn;
    if (b->drawn) {
        drawn = *st;
        drawn.main_layers = (uint8_t)(panel.layers & layers_enabled);
        drawn.sub_layers &= drawn.main_layers;
        st = &drawn;
    }
    for (unsigned n = 0; n < 3; n++)
        if ((layers_enabled & (1u << n)) && !b->drawn)
            r.bgcnt[n] = layer_control(n, st->map[n], b, st->hofs[n]);
    if (st->windows.win_main & (VID_ALL_LAYERS)) {
        r.win0h = gba_window(st->windows.w1_left, st->windows.w1_right, b);
        r.win1h = gba_window(st->windows.w2_left, st->windows.w2_right, b);
        r.winin = (uint16_t)(region_layers(st, true, false) | region_layers(st, false, true) << 8);
    }
    r.winout = region_layers(st, false, false);
    line_scroll(&r, st, b, down);
    for (unsigned s = 0; b->drawn && s < PANEL_LAYERS; s++) {
        unsigned n = panel.layer[s];
        if (n == NO_LAYER || !(layers_enabled & (1u << n)))
            continue;
        r.bgcnt[n] = panel.control[s];
        r.hofs[n] = panel.hofs[s];
        r.vofs[n] = (uint16_t)(panel.vofs[s] - panel.at);
    }
    r.bldcnt = st->brightness < 15 ? (uint16_t)(BLD_DARKEN | BLD_ALL_FIRST) : 0;
    r.bldy = (uint16_t)((15 - st->brightness) * 16 / 15);
    r.backdrop = backdrop(st);
    /* Adding the sub screen to the layers colour math takes in, as the GBA's
     * blending of two targets at full weight (half at half): of the sub
     * screen, only what the main screen shows too, and the backdrop. The
     * SNES's subtraction has no GBA counterpart. */
    const VidColorMath *m = &st->math;
    uint8_t first = (uint8_t)(m->layers & (st->main_layers | VID_BACK_BIT));
    if (st->brightness == 15 && m->sub_screen && !m->subtract && first && m->prevent_math != VID_REGION_ALWAYS) {
        uint8_t second = (uint8_t)((st->sub_layers & st->main_layers & VID_ALL_LAYERS) | VID_BACK_BIT);
        r.bldcnt = (uint16_t)(BLD_ALPHA | first | second << 8);
        r.bldalpha = m->half ? 0x0808 : 0x1010;
    }
    return r;
}

/* In ARM, in fast RAM, as the interrupts are; a long call from the cartridge. */
__attribute__((target("arm"), section(".iwram"), long_call, noinline)) static void set_line(const LineRegisters *r)
{
    DISPCNT = r->dispcnt;
    for (unsigned n = 0; n < 3; n++) {
        BGCNT(n) = r->bgcnt[n];
        BGHOFS(n) = r->hofs[n];
        BGVOFS(n) = r->vofs[n];
    }
    WIN0H = r->win0h;
    WIN1H = r->win1h;
    WININ = r->winin;
    WINOUT = r->winout;
    BLDCNT = r->bldcnt;
    BLDY = r->bldy;
    BLDALPHA = r->bldalpha;
    BG_PALETTE[0] = r->backdrop;
}

/* The line counter's interrupt for the line above the next change, or for
 * the vertical blank once the frame has no more. */
__attribute__((target("arm"), section(".iwram"))) static void await_change(void)
{
    uint16_t line = next_change < changes ? (uint16_t)(change_line[next_change] - 1) : GBA_LINES;
    DISPSTAT = (uint16_t)((DISPSTAT & 0x00FF & ~DISPSTAT_HBLANK_IRQ) | line << 8);
}

/* In the vertical blank, the first line's registers; on the line above a
 * change, its horizontal blank's interrupt; at that, the change's registers,
 * keeping the horizontal blank's interrupt where the next change is the
 * next line. In ARM, in fast RAM. */
__attribute__((target("arm"), section(".iwram"), noinline)) static void on_line(void)
{
    uint16_t raised = REG_IF & (IRQ_VCOUNT | IRQ_HBLANK);
    uint16_t line = VCOUNT;
    if (by_line && (raised & IRQ_VCOUNT)) {
        if (line >= GBA_LINES) {
            set_line(&change_registers[0]);
            next_change = 1;
            await_change();
        } else {
            REG_IF = IRQ_HBLANK;
            DISPSTAT = (uint16_t)(DISPSTAT | DISPSTAT_HBLANK_IRQ);
        }
    } else if (by_line && (raised & IRQ_HBLANK) && line < GBA_LINES) {
        while (next_change < changes && change_line[next_change] <= line)
            next_change++;
        if (next_change < changes && change_line[next_change] == line + 1)
            set_line(&change_registers[next_change++]);
        if (!(next_change < changes && change_line[next_change] == line + 2))
            await_change();
    }
    REG_IF = raised;
    BIOS_IF |= raised;
}

/* Everything a frame's lines are worked out from but the tables' bytes:
 * the registers as the frame begins, the line effects as the game programmed
 * them, the changes below a line, the layers shown and what of the layout a
 * line's registers take (the backdrop's colour, the third layer's priority). */
typedef struct {
    LineState st;
    VidLineEffect slot[8];
    VidDisplay display_below;
    uint16_t display_from, effects_from, layers_enabled, backdrop_color;
    uint8_t line_effects, line_effects_below;
    bool below_display, below_line_effects, bg3_priority;
    uint32_t bands;   /* the view's bands, hashed */
    uint32_t drawn;   /* the drawn panel's lines and layers, hashed */
} LinesFrom;

static void lines_from(LinesFrom *f, const VidHost *v, uint16_t layers_enabled)
{
    memset(f, 0, sizeof *f);
    LineState *st = &f->st;
    st->windows = snes.windows;
    st->main_layers = snes.main_layers;
    st->sub_layers = snes.sub_layers;
    st->math = snes.math;
    st->fixed_color = snes.fixed_color;
    st->brightness = (uint8_t)(v->display.brightness & 15);
    st->blank = v->display.blank;
    for (unsigned n = 0; n < 3; n++)
        st->hofs[n] = snes.bg[n].hofs, st->vofs[n] = snes.bg[n].vofs, st->map[n] = map_byte_of(&snes.bg[n]);
    memcpy(f->slot, snes.slot, sizeof f->slot);
    f->display_below = v->display_below;
    f->display_from = v->display_from;
    f->effects_from = v->effects_from;
    f->layers_enabled = layers_enabled;
    f->backdrop_color = snes.cgram[0];
    f->line_effects = v->line_effects;
    f->line_effects_below = v->line_effects_below;
    f->bands = hash_on(FNV_START, bands, sizeof bands[0] * band_count);
    f->drawn = panel.drawn ? hash_on(FNV_START, &panel, offsetof(DrawnPanel, drawn)) : 0;
    f->below_display = v->below_display;
    f->below_line_effects = v->below_line_effects;
    f->bg3_priority = snes.mode.bg3_priority;
}

enum { NO_LINE = 0xFFFF };

/* The black bands above GBA line `at` not yet set, set. */
static uint8_t black_bands_above(unsigned at, uint8_t count, uint8_t *next, uint16_t layers_enabled)
{
    for (; *next < band_count && bands[*next].at < at; (*next)++) {
        if (!bands[*next].black)
            continue;
        LineRegisters black = nothing_shown(layers_enabled);
        if (count == 0 || memcmp(&black, &change_registers[count - 1], sizeof black) != 0) {
            change_registers[count] = black;
            change_line[count++] = bands[*next].at;
        }
    }
    return count;
}

/* The first line of the SNES's after `line` that a band starts showing (its
 * row's, one down: a line shows the row above it), or NO_LINE. */
static uint16_t next_band_start(uint16_t line)
{
    uint16_t soonest = NO_LINE;
    for (unsigned k = 0; k < band_count; k++) {
        int starts = bands[k].row + 1;
        if (!bands[k].black && starts > line && starts < soonest)
            soonest = (uint16_t)starts;
    }
    return soonest;
}

/* The slot's next transfer to apply, from its next line up to `stop`: its
 * line, with its unit in `last`; NO_LINE where it has none left. Where the
 * slots' targets are `apart`, a transfer of the unit the slot last applied
 * changes nothing and is passed over. */
static uint16_t next_transfer(Slot *sl, const VidLineEffect *e, uint16_t stop, bool apart)
{
    while (sl->active && sl->line < stop) {
        uint16_t line = sl->line;
        bool applies = false;
        if (sl->transfer) {
            uint8_t unit[4] = { 0 };
            if (e->indirect) {
                table_unit(e->indirect_bank, sl->indirect, sl->unit, unit);
                sl->indirect = (uint16_t)(sl->indirect + sl->unit);
            } else {
                table_unit(sl->bank, sl->cursor, sl->unit, unit);
                sl->cursor = (uint16_t)(sl->cursor + sl->unit);
            }
            bool same = apart && sl->applied;
            for (unsigned k = 0; same && k < sl->unit; k++)
                same = unit[k] == sl->last[k];
            applies = !same;
            if (applies) {
                memcpy(sl->last, unit, sizeof unit);
                sl->applied = true;
            }
        }
        sl->count--;
        sl->transfer = (sl->count & 0x80) != 0;
        sl->line = (uint16_t)(line + 1);
        if (!(sl->count & 0x7F)) {
            slot_entry(sl, e);
        } else if (!sl->transfer && (sl->count & 0x7F) > 1) {
            sl->line = (uint16_t)(line + (sl->count & 0x7F));
            sl->count = 1;
        }
        if (applies)
            return line;
    }
    return NO_LINE;
}

/* The frame's lines: the line effects read down the picture from its top,
 * the line handler's changes below its line, and the GBA's registers where
 * what a line shows changes, as changes from the first line on; `st` ends as
 * the effects leave the registers. Only the lines something happens at are
 * visited: a slot's next transfer to apply, the display's change, a band's
 * first line, a line shown after a change and a line a mode asked of
 * (vid_layer_last_shown). */
static uint8_t work_out_lines(const VidHost *v, LineState *st_out, uint16_t layers_enabled)
{
    LineState st = *st_out, shown;
    uint8_t enabled = v->line_effects;
    uint8_t below = v->below_line_effects ? v->line_effects_below : enabled;
    memset(read_lines, 0, sizeof read_lines);
    lines_read_count = 0;
    lines_read_hash = FNV_START;
    lines_read_overflowed = false;
    bool apart = targets_apart((uint8_t)(enabled | below), v->below_display);
    /* A slot the effects below their line leave out stops there. */
    uint16_t effects_from = v->below_line_effects && v->effects_from ? v->effects_from : SNES_LINES;
    uint16_t display_at = v->below_display && v->display_from ? v->display_from : NO_LINE;
    Slot slots[8];
    uint16_t stop[8], next[8];
    for (unsigned i = 0; i < 8; i++) {
        slots[i] = (Slot){ .active = (enabled >> i) & 1, .bank = (uint8_t)(snes.slot[i].table.addr >> 16),
                           .unit = unit_bytes(snes.slot[i].target), .changes = target_changes(snes.slot[i].target),
                           .cursor = (uint16_t)snes.slot[i].table.addr };
        stop[i] = (below >> i) & 1 ? SNES_LINES : effects_from;
        if (slots[i].active)
            slot_entry(&slots[i], &snes.slot[i]);
        next[i] = next_transfer(&slots[i], &snes.slot[i], stop[i], apart);
    }
    uint8_t count = 0, next_black = 0;
    uint8_t touched = CHANGED_OTHER;   /* what of the lines' registers may have changed (Changed) */
    for (uint16_t line = 0; line < SNES_LINES;) {
        if (line == display_at) {
            st.brightness = (uint8_t)(v->display_below.brightness & 15);
            st.blank = v->display_below.blank;
            touched |= CHANGED_OTHER;
        }
        /* A line's registers are worked out again only where what the
         * effects set has changed, and kept only where they differ: where
         * only the scroll may have, the last change's with its scroll (but
         * on the drawn panel, whose layers' scroll is its own, and a band
         * cut, whose maps the scroll moves the cut in). The last change's
         * registers are always those of what was `shown`. */
        for (unsigned k = 0; k < ASKED_LINES; k++)
            if (asked_line[k] == line)   /* the lines asked of, which are visited */
                asked_shown[k] = (uint8_t)(st.main_layers | (st.blank ? ASKED_BLANK : 0));
        const Band *b = line ? band_showing((int)line - 1) : NULL;
        int at = !b ? -1 : b->drawn ? band_line(b, (int)line - 1) : b->at + ((int)line - 1 - b->row);
        if (at >= 0) {
            int down = (int)(line - 1) - at;
            if (next_black < band_count && bands[next_black].at < at)
                count = black_bands_above((unsigned)at, count, &next_black, layers_enabled);
            bool band_starts = at == b->at, again = false;
            LineRegisters r;
            if (count == 0 || band_starts || ((touched & CHANGED_OTHER) && memcmp(&st, &shown, sizeof st) != 0) ||
                (touched && b->cut_dots && memcmp(&st, &shown, sizeof st) != 0)) {
                r = line_registers(&st, layers_enabled, b, down);
                memcpy(&shown, &st, sizeof st);
                again = true;
            } else if (touched && !b->drawn && (memcmp(st.hofs, shown.hofs, sizeof st.hofs) != 0 ||
                                                memcmp(st.vofs, shown.vofs, sizeof st.vofs) != 0)) {
                r = change_registers[count - 1];
                line_scroll(&r, &st, b, down);
                memcpy(shown.hofs, st.hofs, sizeof st.hofs);
                memcpy(shown.vofs, st.vofs, sizeof st.vofs);
                again = true;
            }
            if (again && (count == 0 || memcmp(&r, &change_registers[count - 1], sizeof r) != 0)) {
                change_registers[count] = r;
                change_line[count++] = (uint8_t)at;
            }
            touched = 0;
        }
        uint16_t soonest = NO_LINE;
        for (unsigned i = 0; i < 8; i++) {
            if (next[i] == line) {
                Slot *sl = &slots[i];
                const VidLineEffect *e = &snes.slot[i];
                for (unsigned k = 0; k < sl->unit; k++)
                    line_apply(&st, e->target, k, sl->last[k]);
                touched |= sl->changes;
                next[i] = next_transfer(sl, e, stop[i], apart);
            }
            if (next[i] < soonest)
                soonest = next[i];
        }
        if (display_at > line && display_at < soonest)
            soonest = display_at;
        uint16_t band_start = next_band_start(line);
        if (band_start < soonest)
            soonest = band_start;
        for (unsigned k = 0; k < ASKED_LINES; k++)
            if (asked_line[k] > line && asked_line[k] < soonest)
                soonest = asked_line[k];
        if (touched && line + 1 < soonest && band_showing(line))
            soonest = (uint16_t)(line + 1);
        line = soonest;
    }
    count = black_bands_above(GBA_VIEW_HEIGHT, count, &next_black, layers_enabled);
    *st_out = st;
    return count;
}

/* The frame's lines, and the GBA's registers set for them. A frame that
 * starts from what the last one did, its tables unchanged, makes the same
 * changes and leaves the registers as it did: those stand, unworked. */
static void present_lines(Platform *pf, uint16_t layers_enabled, bool laid_out)
{
    static uint32_t last_from IN_FAST_RAM;   /* its hash */
    static LineState last_left IN_FAST_RAM;
    static bool last_stands IN_FAST_RAM;
    /* Frames worked out one after another, and a count to try keeping them
     * again by: where the tables change every frame (a battle's, the jet
     * bike race's), keeping what each read costs more than it saves, so
     * after two such frames one in sixteen tries. */
    static uint8_t worked_out IN_FAST_RAM, tries IN_FAST_RAM;
    LinesFrom from;
    lines_from(&from, &pf->video, layers_enabled);
    LineState st;
    uint8_t count;
    bool may_stand = last_stands && !laid_out && !asked_anew;
    asked_anew = false;
    if (!may_stand)
        lines_read_kept = worked_out < 2 || ++tries % 16 == 0;
    /* the starting point's hash, where it is compared or kept */
    uint32_t from_hash = may_stand || lines_read_kept ? hash_on(FNV_START, &from, sizeof from) : 0;
    if (may_stand && from_hash == last_from && tables_unchanged()) {
        st = last_left;
        count = changes;
        worked_out = 0;
    } else {
        if (may_stand)
            lines_read_kept = worked_out < 2 || ++tries % 16 == 0;
        st = from.st;
        count = work_out_lines(&pf->video, &st, layers_enabled);
        last_from = from_hash;
        last_left = st;
        /* a layout's maps change what a line's registers name */
        last_stands = lines_read_kept && !lines_read_overflowed && !laid_out;
        if (worked_out < 2)
            worked_out++;
    }
    changes = count;
    /* What the effects leave stands, as the SNES's registers do. */
    snes.windows = st.windows;
    snes.math = st.math;
    snes.fixed_color = st.fixed_color;
    snes.main_layers = st.main_layers;
    snes.sub_layers = st.sub_layers;
    for (unsigned n = 0; n < 3; n++)
        snes.bg[n].hofs = st.hofs[n], snes.bg[n].vofs = st.vofs[n];
    WIN0V = GBA_LINES;
    WIN1V = GBA_LINES;
    if (count == 1) {
        if (by_line) {
            by_line = false;
            REG_IE = (uint16_t)(REG_IE & ~(IRQ_VCOUNT | IRQ_HBLANK));
            DISPSTAT = (uint16_t)(DISPSTAT & 0x00FF & ~(DISPSTAT_HBLANK_IRQ | DISPSTAT_VCOUNT_IRQ));
        }
        set_line(&change_registers[0]);
        return;
    }
    if (!by_line) {
        gba_interrupt_handler(IRQ_VCOUNT | IRQ_HBLANK, on_line);
        next_change = count;
        DISPSTAT = (uint16_t)((DISPSTAT & 0x00FF & ~DISPSTAT_HBLANK_IRQ) | DISPSTAT_VCOUNT_IRQ | GBA_LINES << 8);
        REG_IF = IRQ_VCOUNT | IRQ_HBLANK;
        REG_IE = (uint16_t)(REG_IE | IRQ_VCOUNT | IRQ_HBLANK);
        REG_IME = 1;
        by_line = true;
    }
}

void gba_video_present(Platform *pf)
{
    /* Tiles that found no place have the caches started afresh: a second
     * on where a tile missed since the layout, eight where only the layout's
     * own misses stand (starting afresh from the same maps misses the same
     * tiles, and a layout is about ten GBA frames' work). */
    bool restart = misses && ++frames_since_layout >= (misses > layout_misses ? 60 : 480);
    bool laid_out = snes.settings_changed || restart;
    if (laid_out) {
        lay_out(usage_known);
        memset(snes.changed, 0xFF, sizeof snes.changed);
        snes.settings_changed = false;
    }
    for (unsigned w = 0; w < UNITS / 32; w++) {
        uint32_t bits = snes.changed[w];
        for (unsigned b = 0; bits; b++, bits >>= 1)
            if (bits & 1)
                convert_unit(w * 32 + b);
        snes.changed[w] = 0;
    }
    if (laid_out)
        layout_misses = misses;
    if (snes.palette_written) {
        present_palettes();
        snes.palette_written = false;
    }
    static uint32_t last_bands IN_FAST_RAM;
    band_up_the_view();
    uint32_t banded = hash_on(FNV_START, bands, sizeof bands[0] * band_count);
    if (banded != last_bands) {
        last_bands = banded;
        snes.sprites_changed = true;
    }
    if (snes.sprites_changed) {
        present_sprites();
        snes.sprites_changed = false;
    }

    uint16_t layers_enabled = 0;
    for (unsigned n = 0; n < 4; n++) {
        const Layer *l = &layers[n];
        if (!l->shown)
            continue;
        if (n == 3)
            BGCNT(3) = layer_control(3, map_byte_of(&snes.bg[3]), &(Band){ 0 }, 0);
        layers_enabled |= (uint16_t)(1u << n);
    }
    for (unsigned s = 0; panel.drawn && panel_room.made && s < PANEL_LAYERS; s++)
        if (panel.layer[s] != NO_LAYER)
            panel.control[s] = panel_layer_control(s);
    present_lines(pf, layers_enabled, laid_out);
    usage_known = true;
}

size_t gba_video_bytes(void) { return sizeof snes; }

/* The picture is made in the vertical blank, so that no frame the GBA shows
 * has half of one picture and half of the next. */
void gba_video_await_blank(void)
{
    while (VCOUNT < GBA_LINES)
        ;
}
