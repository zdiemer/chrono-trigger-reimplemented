/* The video service on the GBA's own picture, in place of
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
 * - The SNES's per-tile priority (a tile in front of the sprites of a
 *   priority or behind them) has no GBA counterpart, whose priority is a
 *   layer's: a layer whose map has tiles of both is split in two, its tiles
 *   behind the sprites on one of the GBA's layers and those in front on
 *   another, as many as the GBA's four layers leave room for ("Priority",
 *   below). A sprite the game masks with one before it in the SNES's table
 *   is masked by the GBA's sprite window (present_sprites).
 * - Mode 7's plane is the GBA's own: the GBA's affine layer (BG2 in its
 *   mode 2) reads a 128x128 map of bytes and 256 tiles of 8-bit pixels,
 *   as the SNES's does, so the plane's half of VRAM is split into the two
 *   (the map's bytes and the tiles' bytes interleave on the SNES) in the
 *   GBA's first 32 KiB, and each line of the plane is given the matrix and
 *   the point of the plane at its left end ("Mode 7", below).
 * - Mode 2's columns, each of the first two layers' scrolled by its own
 *   offsets, are runs of them each shown on a GBA layer free on the line,
 *   through a window ("Mode 2's columns", below).
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
#define BG2_PLANE ((volatile uint32_t *)0x04000020)   /* PA-PD, X and Y: four words */
#define DMA0SAD (*(volatile uint32_t *)0x040000B0)
#define DMA0DAD (*(volatile uint32_t *)0x040000B4)
#define DMA0CNT (*(volatile uint32_t *)0x040000B8)
#define DMA1SAD (*(volatile uint32_t *)0x040000BC)
#define DMA1DAD (*(volatile uint32_t *)0x040000C0)
#define DMA1CNT (*(volatile uint32_t *)0x040000C4)
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
    DISPCNT_OBJ_WINDOW = 0x8000, OBJ_WINDOW = 0x0800,
    BGCNT_4BPP = 0x0000,
    BLD_DARKEN = 0x00C0, BLD_LIGHTEN = 0x0080, BLD_ALPHA = 0x0040, BLD_ALL_FIRST = 0x003F,
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
 * columns `cut_columns` further on. A map whole on a layer split by
 * priority keeps its tiles in front apart (Priority, below); one that is
 * another layer's own, converted the same, is shown from that layer's. */
typedef struct {
    uint8_t layer;
    uint8_t cut;          /* the first column cut, and (<< 5) how many; NO_CUT: the map whole */
    bool shared;          /* kept where another layer's own map is */
    uint16_t byte, bytes;
    uint16_t gba_byte;    /* NO_PLACE: no room for it */
    uint16_t front_byte;  /* its tiles in front, where its layer is split; or NO_PLACE */
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
    VidMode7 m7;
    uint8_t m7_latch;              /* the byte the Mode 7 registers' writes share */
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

/* `n` bytes through the VRAM port, as vram_put takes them: the `k`th to
 * the words' high bytes where `half` is 1, low where 0, and where -1 (words)
 * as its place `first + k` in the send is odd; each of `bytes`, or `fill`
 * where `bytes` is NULL. Through a port that doesn't remap, each byte goes
 * straight to its place, the address stepped as the port steps it. */
static void vram_put_run(const uint8_t *bytes, uint8_t fill, uint32_t n, uint32_t first, int half)
{
    if (snes.port.remap) {
        for (uint32_t k = 0; k < n; k++)
            vram_put(half < 0 ? ((first + k) & 1) != 0 : half == 1, bytes ? bytes[k] : fill);
        return;
    }
    uint16_t addr = snes.vram_addr, step = snes.port.step;
    bool after_high = snes.port.after_high;
    for (uint32_t k = 0; k < n; k++) {
        bool high = half < 0 ? ((first + k) & 1) != 0 : half == 1;
        uint32_t at = (uint32_t)(addr & 0x7FFF) << 1 | (high ? 1u : 0u);
        uint8_t v = bytes ? bytes[k] : fill;
        if (snes.vram[at] != v) {
            snes.vram[at] = v;
            unit_changed(at);
        }
        if (after_high == high)
            addr = (uint16_t)(addr + step);
    }
    snes.vram_addr = addr;
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
        vram_put_run(piece, 0, n, i, half);
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
        vram_put_run(NULL, data_byte(src, 0), len, 0, -1);
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
static bool offsets_written;                          /* mode 2's columns' offsets (Mode 2's columns, below) */

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
    /* the first layer's scroll is Mode 7's too, its 13 bits */
    if (bg == VID_BG1 && (fields & VID_SCROLL_H))
        snes.m7.hofs = vid_mode7_13bit(hofs);
    if (bg == VID_BG1 && (fields & VID_SCROLL_V))
        snes.m7.vofs = vid_mode7_13bit(vofs);
}

void vid_set_mosaic(Platform *pf, PfOrigin o, uint8_t bgs, uint8_t size) { (void)pf, (void)o, (void)bgs, (void)size; }
/* As the native observer takes them (runtime/platform/observe.c): the
 * registers' writes share a latch, so a value written a byte at a time
 * takes the byte written before it. */
void vid_set_mode7(Platform *pf, PfOrigin o, const VidMode7 *m7, uint32_t fields)
{
    (void)pf, (void)o;
    VidMode7 *d = &snes.m7;
    if (fields & VID_M7_SELECT)
        d->hflip = m7->hflip, d->vflip = m7->vflip, d->outside = m7->outside;
    if (fields & VID_M7_MATRIX) {
        d->a = m7->a, d->b = m7->b, d->c = m7->c, d->d = m7->d;
        snes.m7_latch = (uint8_t)((uint16_t)m7->d >> 8);
    }
    if (fields & VID_M7_MATRIX_AB) {
        d->a = m7->a, d->b = m7->b;
        snes.m7_latch = (uint8_t)((uint16_t)m7->b >> 8);
    }
    if (fields & VID_M7_MATRIX_A) {
        d->a = m7->a;
        snes.m7_latch = (uint8_t)((uint16_t)m7->a >> 8);
    }
    if (fields & VID_M7_MATRIX_B_HIGH) {
        uint8_t high = (uint8_t)((uint16_t)m7->b >> 8);
        d->b = (int16_t)((uint16_t)(high << 8) | snes.m7_latch);
        snes.m7_latch = high;
    }
    if (fields & VID_M7_MATRIX_B) {
        d->b = m7->b;
        snes.m7_latch = (uint8_t)((uint16_t)m7->b >> 8);
    }
    if (fields & VID_M7_CENTER) {
        d->cx = vid_mode7_13bit((uint16_t)m7->cx), d->cy = vid_mode7_13bit((uint16_t)m7->cy);
        snes.m7_latch = (uint8_t)((uint16_t)m7->cy >> 8);
    }
    if (fields & VID_M7_SCROLL)
        d->hofs = vid_mode7_13bit((uint16_t)m7->hofs), d->vofs = vid_mode7_13bit((uint16_t)m7->vofs);
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
    vram_put_run(NULL, data_byte(value, 0), len, 0, half == VID_VRAM_HIGH_BYTES);
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
    vid_set_mode7(pf, o, &sc->mode7, VID_M7_SELECT | VID_M7_MATRIX | VID_M7_CENTER | VID_M7_SCROLL);
    vid_set_windows(pf, o, &sc->windows, VID_WINDOWS_SUB * 2 - 1);   /* all of them */
    vid_set_screens(pf, o, sc->main_layers, sc->sub_layers, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_set_color_math(pf, o, &sc->math, VID_MATH_CONTROL | VID_MATH_LAYERS);
    vid_set_fixed_color(pf, o, VID_RED, (uint8_t)(sc->fixed_color & 0x1F));
    vid_set_fixed_color(pf, o, VID_GREEN, (uint8_t)(sc->fixed_color >> 5 & 0x1F));
    vid_set_fixed_color(pf, o, VID_BLUE, (uint8_t)(sc->fixed_color >> 10 & 0x1F));
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
 * and which cache its tiles are in; whether it is split by priority, with
 * where its tiles in front are kept ("Priority", below). */
typedef struct {
    bool shown;
    bool own_map;          /* whether its own map is converted (a layout may leave it out) */
    bool split;
    uint8_t bpp, cache;
    uint16_t char_byte, map_byte, map_bytes;
    uint16_t front_byte;   /* NO_PLACE: not split */
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

/* The mode the layers' tiles and maps are laid out for: the one the
 * frame's lines show other than Mode 7 (a line effect may set the mode down
 * the picture, the jet bike race's from its top), mode 2 where a line shows
 * it (whose moved columns take a layer of their own), or the mode set. */
enum { NO_MODE = 0xFF };
static uint8_t lines_text_mode = NO_MODE;   /* the last frame's lines' */
static uint8_t laid_out_mode;               /* the layout's */
/* Whether a line other than mode 2's (or Mode 7's) has shown the third
 * layer, which mode 2's lines don't show, since the layout was made for
 * mode 2, and whether the layout has it: once a line shows it, it is kept
 * as long as mode 2 is, rather than laid out again as a menu's description
 * comes and goes. */
static bool third_on_lines, laid_out_third;

static uint8_t text_mode(void) { return lines_text_mode != NO_MODE ? lines_text_mode : snes.mode.mode; }

/* ---- Priority -----------------------------------------------------------------------------------
 *
 * Each of a SNES layer's tiles stands in front of the sprites of some
 * priorities and behind the others, as its map entry's priority bit says;
 * the GBA gives a whole layer one priority. A layer whose map has tiles of
 * both is split in two where the GBA has a layer to spare (mode 1's three
 * layers leave one, mode 2's two leave two): its map is converted twice, at
 * the SNES's place with its tiles in front left blank, and in room the GBA
 * has spare with its tiles behind left blank, each shown on a GBA layer of
 * its own at its own priority. Where more layers have both than there are
 * layers to spare, the first is split before the second, the second before
 * the others, and among those the one with the most of each first; a split
 * layer stays split while it still has both, since a split is made in a
 * layout; and a layer not split stands where most of its tiles do. The GBA shows the
 * lower-numbered of two layers of one priority in front, so its layers are
 * numbered front to back. */

/* Which of each layer's map's units, by their place in its map, have an
 * entry in front of the sprites, and which have one behind. */
enum { MAP_UNITS = 4 * SCREEN_BYTES / UNIT_BYTES, PRIORITY_BIT = 0x2000, NO_BLOCK = 0xFF };
static uint32_t units_in_front[4][MAP_UNITS / 32], units_behind[4][MAP_UNITS / 32];
static uint8_t split_wanted;   /* the layers the last layout was asked to split */
static bool priorities_noted;  /* whether a unit's have changed since the GBA's layers were assigned */

static void note_priorities(unsigned n, uint32_t first)
{
    uint32_t unit = ((first - layers[n].map_byte) & (VRAM_BYTES - 1)) / UNIT_BYTES, bit = 1u << (unit % 32);
    bool front = false, behind = false;
    for (uint32_t i = 1; i < UNIT_BYTES; i += 2) {
        if (snes.vram[first + i] & (PRIORITY_BIT >> 8))
            front = true;
        else
            behind = true;
    }
    uint32_t *in_front = &units_in_front[n][unit / 32], *in_behind = &units_behind[n][unit / 32];
    uint32_t f = front ? *in_front | bit : *in_front & ~bit, b = behind ? *in_behind | bit : *in_behind & ~bit;
    if (f != *in_front || b != *in_behind) {
        *in_front = f;
        *in_behind = b;
        priorities_noted = true;
    }
}

/* The units of a layer's map with an entry in front (`front`) and with one
 * behind (`behind`), or with only those in front or only those behind. */
static void count_priorities(unsigned n, bool only, unsigned *front, unsigned *behind)
{
    *front = *behind = 0;
    for (unsigned w = 0; w < MAP_UNITS / 32; w++) {
        uint32_t f = units_in_front[n][w], b = units_behind[n][w];
        *front += (unsigned)__builtin_popcount(only ? f & ~b : f);
        *behind += (unsigned)__builtin_popcount(only ? b & ~f : b);
    }
}

/* The layers to split, as many as the GBA's four leave room for, among
 * those with tiles both in front and behind: the first layer, then the
 * second (the first not split stands behind the sprites, and the second's
 * tiles in front alone would cover the first's in front: the stalls' wares
 * at the Fair), then the others (the sprites stand among the first two's
 * tiles; the third is the text and the panels, mostly); a layer of `split`
 * before one not split yet; then those with the most of the fewer. */
static uint8_t layers_to_split(uint8_t split)
{
    unsigned spare = 4, rank[4] = { 0 };
    for (unsigned n = 0; n < 4; n++) {
        if (!layers[n].shown)
            continue;
        spare--;
        unsigned front, behind;
        count_priorities(n, false, &front, &behind);
        unsigned fewer = front < behind ? front : behind;
        if (fewer)
            rank[n] = fewer + (split >> n & 1 ? 0x1000u : 0) + (n == 0 ? 0x4000u : n == 1 ? 0x2000u : 0);
    }
    if (laid_out_mode == 2 && layers[0].shown && spare)
        spare--;   /* kept for the first layer's moved columns (Mode 2's columns) */
    uint8_t wanted = 0;
    for (; spare; spare--) {
        unsigned best = NO_LAYER, most = 0;
        for (unsigned n = 0; n < 4; n++)
            if (!(wanted >> n & 1) && rank[n] > most)
                most = rank[n], best = n;
        if (best == NO_LAYER)
            break;
        wanted |= (uint8_t)(1u << best);
    }
    return wanted;
}

/* The SNES's order of its layers' tiles and its sprites, for a mode: each
 * layer's tiles behind and in front ([layer][0] and [1]) by how far back
 * they stand (`depth`) and the GBA priority that puts them among the
 * sprites, and each SNES sprite priority's GBA priority. Mode 1 with its
 * third layer's tiles in front puts those in front of every sprite, which
 * the GBA can't: there they stand in front of every layer, and only the
 * sprites of the lowest priority, which the SNES puts behind the third
 * layer's tiles behind, show in front of them too. */
typedef struct {
    uint8_t depth[4][2], priority[4][2], sprite[4];
} Order;
static const Order layers_order = { { { 4, 1 }, { 5, 2 }, { 10, 7 }, { 11, 8 } },
                                    { { 1, 0 }, { 1, 0 }, { 3, 2 }, { 3, 2 } }, { 3, 2, 1, 0 } };
static const Order third_in_front_order = { { { 5, 2 }, { 6, 3 }, { 8, 0 }, { 9, 9 } },
                                            { { 2, 1 }, { 2, 1 }, { 3, 0 }, { 3, 3 } }, { 3, 3, 2, 1 } };
static const Order mode2_order = { { { 5, 1 }, { 7, 3 }, { 8, 0 }, { 9, 9 } },
                                   { { 2, 0 }, { 3, 1 }, { 3, 0 }, { 3, 3 } }, { 3, 2, 1, 0 } };
static const Order *order = &layers_order;   /* the mode's, as the GBA's layers were assigned */

static const Order *order_of_mode(void)
{
    if (text_mode() == 2)
        return &mode2_order;
    return text_mode() == 1 && snes.mode.bg3_priority ? &third_in_front_order : &layers_order;
}

/* Whether a layer not split stands in front of the sprites: where more of
 * its map's units have only tiles in front than only tiles behind; mode 1's
 * third layer put in front, where it has any tiles in front or its own map
 * isn't shown (its text and panels, which the mode puts in front of
 * everything; what it has behind is mostly blank). Mode 2 shows no third
 * layer; where a mode 1 line below shows it, it is the menus' text, put in
 * front so (their description of a tech or an item). */
static bool stands_in_front(unsigned n)
{
    unsigned front, behind;
    bool third_in_front = n == 2 && (order_of_mode() == &third_in_front_order || order_of_mode() == &mode2_order);
    count_priorities(n, !third_in_front, &front, &behind);
    return third_in_front ? front > 0 || !layers[n].own_map : front > behind;
}

/* What each of the GBA's four layers shows: a SNES layer (NO_LAYER: none),
 * its tiles in front alone (`front`, a split layer's) or the rest, or in
 * mode 2 the columns its offsets move (`columns`, Mode 2's columns below),
 * and at which priority; and for the map a split layer shows its tiles
 * behind from, by its screen block, the screen block its tiles in front are
 * shown from (NO_BLOCK: it has none). And the GBA's layers that show each
 * set of the SNES's, as their bits, and the layers that show the first two's
 * columns moved, by the SNES's layer, and as bits. */
typedef struct {
    uint8_t source, priority;
    bool front, columns;
} GbaLayer;
static GbaLayer gba_layers[4];
static uint8_t front_block[32];
static uint8_t gba_bits_of[16];
static uint8_t scroll_source[4];   /* each GBA layer's SNES layer, or the first where none */
static uint8_t columns_layer[2];   /* NO_LAYER: none */
static uint8_t columns_bits;

/* A SNES layer's tiles in front (or behind) put among the GBA's layers
 * `count` so far, kept front to back by how far back they stand (`depth`);
 * `front_copy`: from the split layer's copy of its tiles in front. */
static unsigned shown_in_order(uint8_t depth[4], unsigned count, unsigned n, bool front, bool front_copy,
                               bool columns)
{
    if (count == 4)
        return count;
    uint8_t d = order->depth[n][front];
    unsigned k = count;
    for (; k && depth[k - 1] > d; k--) {
        depth[k] = depth[k - 1];
        gba_layers[k] = gba_layers[k - 1];
    }
    depth[k] = d;
    gba_layers[k] = (GbaLayer){ (uint8_t)n, order->priority[n][front], front_copy, columns };
    return count + 1;
}

/* The GBA's layers: each SNES layer's, or its two halves, in the SNES's
 * order front to back; in mode 2, then, a layer the first two not split
 * show their moved columns on, as many as there are layers spare, each
 * beside its own. */
static void assign_layers(void)
{
    uint8_t depth[4];
    unsigned count = 0;
    for (unsigned n = 0; n < 4; n++) {
        if (!layers[n].shown)
            continue;
        bool front = layers[n].split || stands_in_front(n);
        if (layers[n].split)
            count = shown_in_order(depth, count, n, false, false, false);
        count = shown_in_order(depth, count, n, front, layers[n].split, false);
    }
    for (unsigned n = 0; laid_out_mode == 2 && n < 2; n++)
        if (layers[n].shown && !layers[n].split)
            count = shown_in_order(depth, count, n, stands_in_front(n), false, true);
    for (unsigned g = count; g < 4; g++)
        gba_layers[g] = (GbaLayer){ NO_LAYER, 0, false, false };
    columns_layer[0] = columns_layer[1] = NO_LAYER;
    columns_bits = 0;
    for (unsigned g = 0; g < count; g++)
        if (gba_layers[g].columns) {
            columns_layer[gba_layers[g].source] = (uint8_t)g;
            columns_bits |= (uint8_t)(1u << g);
        }
    for (unsigned g = 0; g < 4; g++)
        scroll_source[g] = g < count ? gba_layers[g].source : 0;
    for (unsigned snes_bits = 0; snes_bits < 16; snes_bits++) {
        gba_bits_of[snes_bits] = 0;
        for (unsigned g = 0; g < count; g++)
            if (snes_bits >> gba_layers[g].source & 1)
                gba_bits_of[snes_bits] |= (uint8_t)(1u << g);
    }
}

/* SNES layer bits (with the sprites' and the backdrop's above them) as the GBA's. */
static uint16_t gba_layer_bits(uint16_t snes_bits)
{
    return (uint16_t)((snes_bits & ~0xFu) | gba_bits_of[snes_bits & 0xF]);
}

/* Whether a map has an entry in front of the sprites. */
static bool has_tiles_in_front(uint32_t byte, uint32_t bytes)
{
    for (uint32_t i = 1; i < bytes; i += 2)
        if (snes.vram[(byte + i) & (VRAM_BYTES - 1)] & (PRIORITY_BIT >> 8))
            return true;
    return false;
}

/* Room for a copy of a map `bytes` long, from the top of the GBA's
 * background VRAM down, taken; or NO_PLACE. */
static uint16_t place_copy(uint32_t bytes)
{
    int32_t at = room_from_the_top(bytes / 32, SCREEN_BYTES / 32);
    return at < 0 ? NO_PLACE : (uint16_t)(at * 32);
}

static const Layer *map_layer(uint32_t byte);

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

/* ---- Mode 7 ----------------------------------------------------------------------------------------
 *
 * The plane is the SNES's VRAM's first 32 KiB, each word a byte of its
 * 128x128 map (low) and a byte of its 256 tiles' 8-bit pixels (high). The
 * GBA's affine layer reads the same two, apart: the tiles at the start of
 * its background VRAM (char block 0) and the map after them (screen block
 * 8). A layout made while a line shows Mode 7 (or the mode is 7) keeps
 * those 32 KiB for the plane, where no layer's map lies in them, and the
 * tiles' caches take their places past it; a layout made while none does
 * gives them back. The table of the plane's lines DMA reads (PlaneLine,
 * below) takes room of background VRAM too, from its top, which work RAM
 * hasn't to spare. The plane's 256 colours are the GBA's 256 background
 * colours, its upper 128 the SNES's own where no 2bpp layer needs them for
 * its copies. */
enum { PLANE_BYTES = 0x8000, PLANE_MAP_BYTE = 0x4000, PLANE_MAP_BLOCK = PLANE_MAP_BYTE / SCREEN_BYTES };
static bool plane_room;        /* the layout keeps the GBA's first 32 KiB for the plane */
static bool plane_refused;     /* a layer's map lies where the plane would */
static bool plane_on_lines;    /* a line of the last frame showed Mode 7 */
static volatile void *plane_table;   /* the table of the plane's lines, in background VRAM; NULL: no room */
/* the GBA's 160 lines and the blank after them, 16 bytes each (checked by PlaneLine's assert) */
enum { PLANE_TABLE_BYTES = (160 + 1) * 16, PLANE_TABLE_PLACES = (PLANE_TABLE_BYTES + 31) / 32 };

static bool plane_wanted(void) { return snes.mode.mode == 7 || plane_on_lines; }

/* The plane's room, where it is wanted and free: its places taken. */
static void make_room_for_the_plane(void)
{
    bool was = plane_room;
    plane_room = false;
    plane_refused = false;
    plane_table = NULL;
    if (plane_wanted()) {
        for (unsigned k = 0; k < PLANE_BYTES / BLOCK_BYTES; k++)
            if (roles[k] & MAP)
                plane_refused = true;
        uint32_t table = PLACES - PLANE_TABLE_PLACES;
        while (!plane_refused && table >= PLANE_BYTES / 32 && !room_at(table, PLANE_TABLE_PLACES))
            table--;
        plane_refused |= table < PLANE_BYTES / 32;
        plane_room = !plane_refused;
        if (plane_room) {
            for (uint32_t place = 0; place < PLANE_BYTES / 32; place++)
                take(place);
            take_room(table, PLANE_TABLE_PLACES);
            plane_table = (volatile uint8_t *)BG_VRAM + table * 32;
        }
    }
    if (plane_room != was)
        snes.palette_written = true;
}

/* A unit of the plane's VRAM: its 16 map bytes and 16 pixels, apart. */
static void convert_plane_unit(uint32_t first)
{
    volatile uint16_t *vram = (volatile uint16_t *)BG_VRAM;
    const uint8_t *bytes = &snes.vram[first];
    uint32_t at = first / 2;   /* the first's byte of the map and of the tiles */
    for (unsigned k = 0; k < UNIT_BYTES / 4; k++) {
        vram[(PLANE_MAP_BYTE + at) / 2 + k] = (uint16_t)(bytes[4 * k] | bytes[4 * k + 2] << 8);
        vram[at / 2 + k] = (uint16_t)(bytes[4 * k + 1] | bytes[4 * k + 3] << 8);
    }
}

/* The maps the frame's lines showed since the layout (layer_control): each
 * layer's own (bit n) and each line map (bit LINE_MAPS_SHOWN + m). */
enum { LINE_MAPS_SHOWN = 4 };
static uint16_t maps_shown IN_FAST_RAM;

/* The layers mode 1, mode 2 and mode 0 have (Mode 7's aren't shown yet), the
 * places their maps take, the layers split by priority with their copies of
 * tiles in front, and their tiles' caches, each with the window the most
 * places are free in. Where `pruned`, the maps the lines haven't shown
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
    /* mode 2's lines don't show the third layer, whose map holds their
     * columns' offsets, but a mode 1 line below them may (the menus'
     * descriptions): mode 2 is laid out with it then, as mode 1 */
    uint8_t mode = laid_out_mode = text_mode();
    if (mode != 2)
        third_on_lines = false;
    laid_out_third = mode == 2 && third_on_lines;
    unsigned count = mode == 0 ? 4 : mode == 1 || laid_out_third ? 3 : mode == 2 ? 2 : 0;
    for (unsigned n = 0; n < count; n++) {
        Layer *l = &layers[n];
        const VidBg *b = &snes.bg[n];
        l->bpp = mode != 0 && n < 2 ? 4 : 2;
        l->char_byte = (uint16_t)(b->char_base << 1);
        l->map_byte = (uint16_t)(b->map_base << 1);
        l->map_bytes = map_bytes(b);
        l->own_map = !pruned || (maps_shown >> n & 1);
        /* a layer no screen shows, its map where the plane is, left out for it */
        if (plane_wanted() && l->map_byte < PLANE_BYTES && !((snes.main_layers | snes.sub_layers) >> n & 1))
            continue;
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
    make_room_for_the_plane();
    /* The layers to split by priority, from their maps as they stand. */
    memset(units_in_front, 0, sizeof units_in_front);
    memset(units_behind, 0, sizeof units_behind);
    for (unsigned n = 0; n < 4; n++)
        for (uint32_t at = 0; layers[n].shown && layers[n].own_map && at < layers[n].map_bytes; at += UNIT_BYTES)
            note_priorities(n, (layers[n].map_byte + at) & (VRAM_BYTES - 1));
    split_wanted = layers_to_split(split_wanted);
    /* The line effects' maps and the maps cut, each where the GBA has room
     * for it, from the top of its background VRAM down, but another layer's
     * own map converted the same, shown from that layer's copy (whole, or
     * both halves where both layers are split); then the drawn panel's room;
     * then the copies of the split layers' tiles in front, and of their
     * line effects' maps' where they have any. */
    for (unsigned m = 0; m < snes.line_map_count; m++) {
        LineMap *lm = &snes.line_maps[m];
        lm->gba_byte = lm->front_byte = NO_PLACE;
        lm->shared = false;
        if (!layers[lm->layer].shown)
            continue;
        for (unsigned k = 0; k < BLOCKS; k++)
            if (within((uint32_t)k * BLOCK_BYTES, lm->byte, lm->bytes))
                roles[k] |= LINE_MAP;
        const Layer *owner = map_layer(lm->byte);
        lm->shared = lm->cut == NO_CUT && owner && owner->map_byte == lm->byte && owner->map_bytes == lm->bytes &&
                     owner->cache == layers[lm->layer].cache &&
                     (!(split_wanted >> (owner - layers) & 1) || (split_wanted >> lm->layer & 1));
        lm->gba_byte = lm->shared ? lm->byte : place_copy(lm->bytes);
    }
    make_room_for_the_panel(pruned);
    memset(front_block, NO_BLOCK, sizeof front_block);
    for (unsigned n = 0; n < 4; n++) {
        Layer *l = &layers[n];
        l->front_byte = split_wanted >> n & 1 && l->own_map ? place_copy(l->map_bytes) : NO_PLACE;
        l->split = l->front_byte != NO_PLACE;
        if (l->split)
            front_block[l->map_byte / SCREEN_BYTES] = (uint8_t)(l->front_byte / SCREEN_BYTES);
    }
    for (unsigned m = 0; m < snes.line_map_count; m++) {
        LineMap *lm = &snes.line_maps[m];
        if (lm->gba_byte == NO_PLACE || !layers[lm->layer].split)
            continue;
        const Layer *owner = lm->shared ? map_layer(lm->byte) : NULL;
        if (lm->cut != NO_CUT) {
            /* a map cut has no copy of its tiles in front: all of it stands
             * where the layer would stand not split */
            if (stands_in_front(lm->layer))
                front_block[lm->gba_byte / SCREEN_BYTES] = (uint8_t)(lm->gba_byte / SCREEN_BYTES);
        } else if (owner && owner->split)
            lm->front_byte = owner->front_byte;
        else if (has_tiles_in_front(lm->byte, lm->bytes) && (lm->front_byte = place_copy(lm->bytes)) != NO_PLACE)
            front_block[lm->gba_byte / SCREEN_BYTES] = (uint8_t)(lm->front_byte / SCREEN_BYTES);
    }
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

/* A unit of a map, converted for a layer into the GBA's VRAM at `to`; where
 * `front_to` is a place, its tiles in front go there instead, each left
 * blank in the other; where `to` is NO_PLACE, those alone. */
static void convert_map(uint32_t first, const Layer *l, uint32_t to, uint32_t front_to)
{
    volatile uint16_t *gba = (volatile uint16_t *)BG_VRAM + to / 2;
    if (to == NO_PLACE) {
        volatile uint16_t *front = (volatile uint16_t *)BG_VRAM + front_to / 2;
        for (uint32_t i = 0; i < UNIT_BYTES; i += 2) {
            uint16_t e = (uint16_t)(snes.vram[first + i] | snes.vram[first + i + 1] << 8);
            front[i / 2] = e & PRIORITY_BIT ? map_entry(e, l) : caches[l->cache].blank;
        }
        return;
    }
    if (front_to == NO_PLACE) {
        for (uint32_t i = 0; i < UNIT_BYTES; i += 2)
            gba[i / 2] = map_entry((uint16_t)(snes.vram[first + i] | snes.vram[first + i + 1] << 8), l);
        return;
    }
    volatile uint16_t *front = (volatile uint16_t *)BG_VRAM + front_to / 2;
    uint16_t blank = caches[l->cache].blank;
    for (uint32_t i = 0; i < UNIT_BYTES; i += 2) {
        uint16_t e = (uint16_t)(snes.vram[first + i] | snes.vram[first + i + 1] << 8), entry = map_entry(e, l);
        bool in_front = (e & PRIORITY_BIT) != 0;
        gba[i / 2] = in_front ? blank : entry;
        front[i / 2] = in_front ? entry : blank;
    }
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
    if (laid_out_mode == 2 && within(first, (uint32_t)snes.bg[2].map_base << 1, map_bytes(&snes.bg[2])))
        offsets_written = true;
    if (plane_room && first < PLANE_BYTES)
        convert_plane_unit(first);
    uint8_t role = roles[first / BLOCK_BYTES];
    if (role & LINE_MAP)
        for (unsigned m = 0; m < snes.line_map_count; m++) {
            const LineMap *lm = &snes.line_maps[m];
            if (lm->gba_byte == NO_PLACE || !layers[lm->layer].shown || !within(first, lm->byte, lm->bytes))
                continue;
            uint32_t offset = (first - lm->byte) & (VRAM_BYTES - 1);
            if (lm->cut != NO_CUT) {
                convert_cut_row(lm, offset / 64);
                continue;
            }
            /* a shared copy is its owner's, converted as its map */
            bool front_shared = lm->shared && map_layer(first)->split;
            if (!lm->shared || (lm->front_byte != NO_PLACE && !front_shared))
                convert_map(first, &layers[lm->layer], lm->shared ? NO_PLACE : lm->gba_byte + offset,
                            lm->front_byte != NO_PLACE && !front_shared ? lm->front_byte + offset : NO_PLACE);
            /* tiles in front where its split layer had none: the next layout
             * gives them their copy */
            if (lm->front_byte == NO_PLACE && layers[lm->layer].split && has_tiles_in_front(first, UNIT_BYTES))
                snes.settings_changed = true;
        }
    if (role & MAP) {
        for (unsigned n = 0; n < 4; n++)
            if (layers[n].shown && layers[n].own_map && within(first, layers[n].map_byte, layers[n].map_bytes))
                note_priorities(n, first);
        const Layer *l = map_layer(first);
        convert_map(first, l, first,
                    l->split ? l->front_byte + ((first - l->map_byte) & (VRAM_BYTES - 1)) : NO_PLACE);
    }
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

/* The GBA's control of a panel layer: its tiles where they are, its map's
 * screen block (its priority is its GBA layer's). */
static uint16_t panel_layer_control(unsigned s)
{
    unsigned n = panel.layer[s];
    Cache *c = cache_of(layers[n].bpp, panel.char_byte[s]);
    uint16_t char_block = panel.own_tiles & (1u << n) ? panel_room.run / 512 : c ? c->block : 0;
    return (uint16_t)(char_block << 2 | BGCNT_4BPP | panel_room.map_block[s] << 8);
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
    /* the upper 128 are the 2bpp palettes' copies, but where the plane has
     * them and no 2bpp layer is shown */
    bool copies = !plane_room;
    for (unsigned c = 0; c < CACHES; c++)
        copies |= caches[c].used && caches[c].bpp == 2;
    for (unsigned i = 0; i < 128; i++)
        BG_PALETTE[i] = snes.cgram[i];
    if (plane_room && !copies)
        for (unsigned i = 128; i < 256; i++)
            BG_PALETTE[i] = snes.cgram[i];
    for (unsigned p = 0; copies && p < 8; p++)
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

/* Where sprites overlap, the SNES shows the one first in its table and
 * then sets that one's priority against the layers', where the GBA shows
 * the one of the higher priority: on the SNES a sprite of a lower priority
 * before one of a higher hides it, and shows the layers in front of it in
 * its place.
 * The game masks with that (Crono's covers in his bed are a sprite of the
 * lowest priority before his, hidden by the bed in front of it). Such a
 * sprite is the GBA's sprite window instead, inside which the line's
 * registers show no sprites: the layers there, as on the SNES where they
 * cover the mask, as they do where the game masks. */
static bool sprites_mask;   /* whether a sprite is the sprite window */

/* A sprite shown, as overlaps are told: where, and its GBA priority. */
typedef struct {
    int16_t x, y;
    uint8_t w, h, priority;
} Shown;

static bool overlap(const Shown *a, const Shown *b)
{
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

/* The GBA's screen in squares of 32 dots, each with the GBA priorities of
 * the sprites shown over it (a bit each), so that a sprite is looked for
 * among those before it in front of it only where one may be. */
enum { SQUARE = 32, SQUARES_ACROSS = GBA_VIEW_WIDTH / SQUARE, SQUARES_DOWN = GBA_VIEW_HEIGHT / SQUARE };
typedef uint8_t Squares[SQUARES_DOWN][SQUARES_ACROSS];

/* The priorities over the squares a sprite covers (`mark`: adding its own). */
static uint8_t over_squares(Squares squares, const Shown *sp, bool mark)
{
    int left = sp->x < 0 ? 0 : sp->x, right = sp->x + sp->w > GBA_VIEW_WIDTH ? GBA_VIEW_WIDTH : sp->x + sp->w;
    int top = sp->y < 0 ? 0 : sp->y, bottom = sp->y + sp->h > GBA_VIEW_HEIGHT ? GBA_VIEW_HEIGHT : sp->y + sp->h;
    uint8_t over = 0;
    for (int y = top / SQUARE; top < bottom && y <= (bottom - 1) / SQUARE; y++)
        for (int x = left / SQUARE; left < right && x <= (right - 1) / SQUARE; x++) {
            over |= squares[y][x];
            if (mark)
                squares[y][x] |= (uint8_t)(1u << sp->priority);
        }
    return over;
}

/* The band showing each SNES line a sprite's top or foot may be on (its
 * top is 0-255, its foot up to 63 below), as band_showing finds it: its
 * index plus one, or 0 where none. Made for each frame's sprites, which
 * would otherwise look through the bands twice a sprite. */
enum { SPRITE_ROWS = 256 + 64 };
static uint8_t band_of_row[SPRITE_ROWS];

static void note_band_rows(void)
{
    memset(band_of_row, 0, sizeof band_of_row);
    for (unsigned k = band_count; k-- > 0;) {
        const Band *b = &bands[k];
        for (int row = b->row; !b->black && row < b->row + b->rows; row++)
            if (row >= 0 && row < SPRITE_ROWS)
                band_of_row[row] = (uint8_t)(k + 1);
    }
}

/* The sprites, each in the band its top line is in, or its foot's; the drawn
 * panel shows none. */
static void present_sprites(void)
{
    Shown shown[SPRITES];   /* the sprites after this one in the table, shown */
    Squares squares;
    unsigned count = 0;
    memset(squares, 0, sizeof squares);
    sprites_mask = false;
    note_band_rows();
    /* the size setting's small and large sprites, as the GBA has them */
    struct {
        uint8_t w, h;
        bool fits;
        uint16_t shape, size;
    } sized[2];
    for (unsigned large = 0; large < 2; large++) {
        sized[large].w = sprite_sizes[snes.obj.size_select & 7][large][0];
        sized[large].h = sprite_sizes[snes.obj.size_select & 7][large][1];
        sized[large].fits = gba_size(sized[large].w, sized[large].h, &sized[large].shape, &sized[large].size);
    }
    for (unsigned n = SPRITES; n-- > 0;) {
        const uint8_t *e = &snes.oam[n * 4];
        uint8_t high = (uint8_t)(snes.oam[0x200 + n / 4] >> (2 * (n % 4)));
        int x = e[0] | (high & 1) << 8;
        if (x >= 256)
            x -= 512;
        unsigned large = (high >> 1) & 1;
        unsigned w = sized[large].w, h = sized[large].h;
        uint16_t shape = sized[large].shape, size = sized[large].size;
        volatile uint16_t *to = &OAM[n * 4];
        int top = (e[1] + 1) & 0xFF;
        unsigned k = band_of_row[top];
        if (!k)
            k = band_of_row[top + (int)h - 1];
        const Band *b = k && !bands[k - 1].drawn ? &bands[k - 1] : NULL;
        int gx = b ? band_x(b, x) : GBA_VIEW_WIDTH, gy = b ? ((e[1] - b->row + b->at) & 0xFF) : 0;
        if (!b || !sized[large].fits || gx <= -(int)w || gx >= GBA_VIEW_WIDTH) {
            to[0] = 0x0200;   /* hidden */
            continue;
        }
        uint8_t priority = order->sprite[(e[3] >> 4) & 3];
        Shown here = { (int16_t)gx, (int16_t)(gy >= 192 ? gy - 256 : gy), (uint8_t)w, (uint8_t)h, priority };
        bool mask = false;
        if (over_squares(squares, &here, true) & ((1u << priority) - 1))
            for (unsigned k = 0; k < count && !mask; k++)
                mask = shown[k].priority < priority && overlap(&here, &shown[k]);
        shown[count++] = here;
        sprites_mask |= mask;
        uint16_t name = (uint16_t)(e[2] | (e[3] & 1) << 8);
        uint16_t tile = (uint16_t)((name >> 4) * 32 + (name & 15));
        to[0] = (uint16_t)(gy | (mask ? OBJ_WINDOW : 0) | shape << 14);
        to[1] = (uint16_t)((gx & 0x1FF) | (e[3] & 0x40 ? 0x1000 : 0) | (e[3] & 0x80 ? 0x2000 : 0) | size << 14);
        to[2] = (uint16_t)(tile | priority << 10 | ((e[3] >> 1) & 7) << 12);
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
    DISPCNT_MODES = 7, DISPCNT_AFFINE_MODE = 2, DISPCNT_BG2 = 0x0400,
    BGCNT_WRAP = 0x2000, BGCNT_1024_DOTS = 0xC000, PLANE_PRIORITY = 2,
};

/* A line's registers on the GBA, its four layers' as they show the SNES's
 * (gba_layers). */
typedef struct {
    uint16_t dispcnt, bgcnt[4], hofs[4], vofs[4], win0h, win1h, winin, winout, bldcnt, bldy;
    uint16_t backdrop, bldalpha;
} LineRegisters;

/* Mode 7's plane on each of the GBA's lines: its matrix and the point at
 * the line's left end, the affine layer's registers in their order. The
 * race's perspective changes them every line, so rather than a change (and
 * an interrupt) a line, DMA copies a line's into the registers in the
 * horizontal blank above it, every line of a frame any line of which shows
 * Mode 7; a line no change sets has the one above's stepped by the matrix's
 * second column, as the GBA would step it. One more than the lines, for the
 * blank after the last. The table is in background VRAM (plane_table),
 * written a halfword or a word at a time, as VRAM takes them. */
typedef struct {
    int16_t pa, pb, pc, pd;
    int32_t x, y;
} PlaneLine;
_Static_assert(sizeof(PlaneLine) * (GBA_LINES + 1) == PLANE_TABLE_BYTES, "the table is the lines' and the blank's, 16 bytes each");
#define plane_lines ((volatile PlaneLine *)plane_table)
static uint8_t plane_set[(GBA_LINES + 7) / 8];   /* the lines a change set, a bit each */
static bool plane_shown;                         /* whether a line of the frame shows Mode 7 */

/* The blending's strength (BLDY) on each line, where the colour math's
 * fixed colour changes it every few lines (the dialog's shading, the race's
 * fog) and nothing else changes: DMA copies each line's in the horizontal
 * blank above it, rather than a change (and an interrupt) each step. A line
 * no change or step sets has the one above's. */
static uint16_t line_strength[GBA_LINES + 1];
static uint8_t strength_set[(GBA_LINES + 7) / 8];
static bool strength_steps;   /* whether a line's strength isn't its change's */

static void set_strength(unsigned at, uint16_t bldy)
{
    line_strength[at] = bldy;
    strength_set[at / 8] |= (uint8_t)(1u << (at % 8));
}

static void fill_the_strengths(void)
{
    for (unsigned at = 1; at <= GBA_LINES; at++)
        if (at == GBA_LINES || !(strength_set[at / 8] >> (at % 8) & 1))
            line_strength[at] = line_strength[at - 1];
}

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
    uint8_t mode;         /* the SNES's BG mode */
    uint8_t m7_latch;
    VidMode7 m7;
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
    uint8_t last[4] __attribute__((aligned(4)));
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
        /* a unit is 1, 2 or 4 bytes (unit_bytes): copied as such, not by a call */
        const uint8_t *p = line_bytes((from | at) / READ_LINE_BYTES) + at % READ_LINE_BYTES;
        out[0] = p[0];
        if (n > 1)
            out[1] = p[1];
        if (n > 2)
            out[2] = p[2], out[3] = p[3];
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
 * battle's backgrounds wave a line at a time), only Mode 7's plane (the
 * race's perspective, a line at a time), only the colour math (the dialog's
 * shading, the race's fog), or anything. */
typedef enum { CHANGED_SCROLL = 1, CHANGED_OTHER = 2, CHANGED_PLANE = 4, CHANGED_MATH = 8 } Changed;

static uint8_t target_changes(VidLineTarget target)
{
    switch (target) {
    case VID_LINE_SCROLL_BG1: case VID_LINE_SCROLL_BG2: case VID_LINE_SCROLL_BG3:
    case VID_LINE_SCROLL_BG1_H: case VID_LINE_SCROLL_BG1_V: case VID_LINE_SCROLL_BG2_H: case VID_LINE_SCROLL_BG2_V:
    case VID_LINE_SCROLL_BG3_H: case VID_LINE_SCROLL_BG3_V:
        return CHANGED_SCROLL;
    case VID_LINE_MODE7_AB: case VID_LINE_MODE7_CD: case VID_LINE_MODE7_CENTER:
    case VID_LINE_MODE7_A: case VID_LINE_MODE7_B: case VID_LINE_MODE7_C: case VID_LINE_MODE7_D:
        return CHANGED_PLANE;
    case VID_LINE_FIXED_COLOR: case VID_LINE_COLOR_MATH: case VID_LINE_MATH_LAYERS: case VID_LINE_MATH_LAYERS_ONLY:
    case VID_LINE_MATH_CONTROL_AND_LAYERS:
        return CHANGED_MATH;
    default:
        return CHANGED_OTHER;
    }
}

/* What of a line's state a line effect's target sets (unit_apply), a bit
 * a register: two slots whose targets share none can't undo each other's
 * work, so a slot's transfer of the unit it last applied changes nothing. */
enum {
    SETS_WINDOW1 = 0x3, SETS_WINDOW2 = 0xC, SETS_WINDOWS = 0xF, SETS_WIN_MAIN = 0x10, SETS_MAIN = 0x20,
    SETS_SUB = 0x40, SETS_SCROLL_H = 0x80, SETS_SCROLL_V = 0x400, SETS_MAPS = 0x2000, SETS_BRIGHTNESS = 0x10000,
    SETS_FIXED = 0x20000, SETS_MATH_CONTROL = 0x40000, SETS_MATH_LAYERS = 0x80000,
    SETS_MODE = 0x100000, SETS_M7_A = 0x200000, SETS_M7_B = 0x400000, SETS_M7_C = 0x800000,
    SETS_M7_D = 0x1000000, SETS_M7_CENTER = 0x2000000,
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
    case VID_LINE_MODE: case VID_LINE_BG_MODE: return SETS_MODE;
    case VID_LINE_MODE7_AB: return SETS_M7_A | SETS_M7_B;
    case VID_LINE_MODE7_CD: return SETS_M7_C | SETS_M7_D;
    case VID_LINE_MODE7_A: return SETS_M7_A;
    case VID_LINE_MODE7_B: return SETS_M7_B;
    case VID_LINE_MODE7_C: return SETS_M7_C;
    case VID_LINE_MODE7_D: return SETS_M7_D;
    case VID_LINE_MODE7_CENTER: return SETS_M7_CENTER;
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

/* A Mode 7 register's byte: they take a value a byte at a time through
 * their shared latch, as the native observer does. */
static void plane_byte(LineState *st, int16_t *value, uint8_t v)
{
    *value = (int16_t)((uint16_t)v << 8 | st->m7_latch);
    st->m7_latch = v;
}

/* The first layer's scroll is Mode 7's too, its 13 bits through the latch. */
static void plane_scroll_byte(LineState *st, int16_t *value, uint8_t v)
{
    plane_byte(st, value, v);
    *value = vid_mode7_13bit((uint16_t)*value);
}

static void fixed_byte(LineState *st, uint8_t v)
{
    st->fixed_color = fixed_channels(st->fixed_color, (uint8_t)(v >> 5), (uint8_t)(v & 31));
}

/* What a transfer of a line effect changes of what the GBA shows: its
 * bytes `v` in their order, as many as its target takes (unit_bytes). */
static void unit_apply(LineState *st, VidLineTarget target, const uint8_t *v)
{
    VidWindows *w = &st->windows;
    switch (target) {
    case VID_LINE_WINDOWS:
        w->w1_left = v[0], w->w1_right = v[1], w->w2_left = v[2], w->w2_right = v[3];
        break;
    case VID_LINE_WINDOW1: w->w1_left = v[0], w->w1_right = v[1]; break;
    case VID_LINE_WINDOW2: w->w2_left = v[0], w->w2_right = v[1]; break;
    case VID_LINE_WINDOW1_LEFT: w->w1_left = v[0]; break;
    case VID_LINE_WINDOW1_RIGHT: w->w1_right = v[0]; break;
    case VID_LINE_WINDOW2_LEFT: w->w2_left = v[0]; break;
    case VID_LINE_WINDOW2_RIGHT: w->w2_right = v[0]; break;
    case VID_LINE_SCROLL_BG1: case VID_LINE_SCROLL_BG2: case VID_LINE_SCROLL_BG3: {
        unsigned bg = (unsigned)(target - VID_LINE_SCROLL_BG1);
        for (unsigned k = 0; k < 4; k++) {
            scroll_byte(k < 2 ? &st->hofs[bg] : &st->vofs[bg], k, v[k]);
            if (bg == 0)
                plane_scroll_byte(st, k < 2 ? &st->m7.hofs : &st->m7.vofs, v[k]);
        }
        break;
    }
    case VID_LINE_SCROLL_BG1_H: case VID_LINE_SCROLL_BG1_V: {
        bool across = target == VID_LINE_SCROLL_BG1_H;
        for (unsigned k = 0; k < 2; k++) {
            scroll_byte(across ? &st->hofs[0] : &st->vofs[0], k, v[k]);
            plane_scroll_byte(st, across ? &st->m7.hofs : &st->m7.vofs, v[k]);
        }
        break;
    }
    case VID_LINE_SCROLL_BG2_H: scroll_byte(&st->hofs[1], 0, v[0]), scroll_byte(&st->hofs[1], 1, v[1]); break;
    case VID_LINE_SCROLL_BG2_V: scroll_byte(&st->vofs[1], 0, v[0]), scroll_byte(&st->vofs[1], 1, v[1]); break;
    case VID_LINE_SCROLL_BG3_H: scroll_byte(&st->hofs[2], 0, v[0]), scroll_byte(&st->hofs[2], 1, v[1]); break;
    case VID_LINE_SCROLL_BG3_V: scroll_byte(&st->vofs[2], 0, v[0]), scroll_byte(&st->vofs[2], 1, v[1]); break;
    case VID_LINE_BRIGHTNESS: st->brightness = v[0] & 15; st->blank = (v[0] & 0x80) != 0; break;
    case VID_LINE_SCREENS: st->main_layers = v[0], st->sub_layers = v[1], w->win_main = v[2]; break;
    case VID_LINE_MAIN_SCREEN: st->main_layers = v[0]; break;
    case VID_LINE_SUB_SCREEN: st->sub_layers = v[0]; break;
    case VID_LINE_BG_MAPS: st->map[0] = v[0], st->map[1] = v[1], st->map[2] = v[2]; break;
    case VID_LINE_FIXED_COLOR: fixed_byte(st, v[0]); break;
    case VID_LINE_COLOR_MATH:
        vid_color_math_decode(&st->math, VID_MATH_CONTROL, v[0]);
        vid_color_math_decode(&st->math, VID_MATH_LAYERS, v[1]);
        fixed_byte(st, v[2]);
        break;
    case VID_LINE_MATH_LAYERS: vid_color_math_decode(&st->math, VID_MATH_LAYERS, v[0]); fixed_byte(st, v[1]); break;
    case VID_LINE_MATH_LAYERS_ONLY: vid_color_math_decode(&st->math, VID_MATH_LAYERS, v[0]); break;
    case VID_LINE_MATH_CONTROL_AND_LAYERS:
        vid_color_math_decode(&st->math, VID_MATH_CONTROL, v[0]);
        vid_color_math_decode(&st->math, VID_MATH_LAYERS, v[1]);
        break;
    case VID_LINE_BG2_MAP: st->map[1] = v[0]; break;
    case VID_LINE_MODE: case VID_LINE_BG_MODE: st->mode = v[0] & 7; break;
    /* the Mode 7 registers' pairs: each takes its two bytes through the latch */
    case VID_LINE_MODE7_AB: case VID_LINE_MODE7_CD: {
        int16_t *first = target == VID_LINE_MODE7_AB ? &st->m7.a : &st->m7.c;
        int16_t *second = target == VID_LINE_MODE7_AB ? &st->m7.b : &st->m7.d;
        *first = (int16_t)((uint16_t)v[1] << 8 | v[0]);
        *second = (int16_t)((uint16_t)v[3] << 8 | v[2]);
        st->m7_latch = v[3];
        break;
    }
    case VID_LINE_MODE7_A: case VID_LINE_MODE7_B: case VID_LINE_MODE7_C: case VID_LINE_MODE7_D: {
        int16_t *value = target == VID_LINE_MODE7_A ? &st->m7.a : target == VID_LINE_MODE7_B ? &st->m7.b
                       : target == VID_LINE_MODE7_C ? &st->m7.c : &st->m7.d;
        *value = (int16_t)((uint16_t)v[1] << 8 | v[0]);
        st->m7_latch = v[1];
        break;
    }
    case VID_LINE_MODE7_CENTER:
        for (unsigned k = 0; k < 4; k++)
            plane_scroll_byte(st, k < 2 ? &st->m7.cx : &st->m7.cy, v[k]);
        break;
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
    snes.line_maps[snes.line_map_count++] =
        (LineMap){ .layer = (uint8_t)n, .cut = cut, .byte = byte, .bytes = bytes, .gba_byte = NO_PLACE, .front_byte = NO_PLACE };
    snes.settings_changed = true;
}

/* The GBA's control of a layer showing the map the SNES's byte names: its
 * tiles as the layout set them, the map's screen block and size (its
 * priority is its GBA layer's): where a band cuts columns from inside it, the map cut where the
 * layer's scroll `hofs` puts the band's cut (a 32-column map's, and only
 * whole columns), which a layout keeps once noted. */
static uint16_t layer_control(unsigned n, uint8_t map, const Band *band, uint16_t hofs)
{
    const Layer *l = &layers[n];
    VidBg b = { 0 };
    vid_bg_map_decode(&b, map);
    uint16_t char_block = caches[l->cache].block;
    uint16_t size = (uint16_t)((b.map_w == 64 ? 1 : 0) | (b.map_h == 64 ? 2 : 0));
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
    return (uint16_t)(char_block << 2 | BGCNT_4BPP | (gba_byte / SCREEN_BYTES) << 8 | size << 14);
}

static uint8_t map_byte_of(const VidBg *b)
{
    return (uint8_t)((b->map_base >> 8 & 0xFC) | (b->map_w == 64 ? 1 : 0) | (b->map_h == 64 ? 2 : 0));
}

/* Whether the sub screen shows a layer on a line: one of those the line's
 * mode has (Mode 7's plane alone); where it shows none, it is the fixed
 * colour. */
static bool sub_screen_shows_a_layer(const LineState *st)
{
    uint8_t layers = st->mode == 7 ? VID_BG1_BIT | VID_OBJ_BIT : VID_ALL_LAYERS;
    return st->math.sub_screen && (st->sub_layers & layers);
}

/* The backdrop with the fixed colour's math on it, where the math takes the
 * backdrop in (a sub screen with no layers on it is the fixed colour), worked
 * out exactly, since the GBA's blending can't add a colour. */
static uint16_t backdrop(const LineState *st)
{
    uint16_t base = snes.cgram[0];
    const VidColorMath *m = &st->math;
    if (!(m->layers & VID_BACK_BIT) || sub_screen_shows_a_layer(st) || m->prevent_math == VID_REGION_ALWAYS)
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

/* The fixed colour added to or subtracted from layers, where it is a grey
 * (the dialog's and the battle window's shading, a few lines at each
 * strength; the darkness a scene falls into; a sub screen with no layers on
 * it is the fixed colour too): the GBA's blending can't add a colour, but
 * it can brighten its first targets toward white or darken them toward
 * black by sixteenths. A grey of strength k added to a colour of strength c
 * raises it by k, as brightening by 16k / (31 - c) does; subtracted, it
 * lowers it by k, as darkening by 16k / c does. The strengths taken are a
 * window's dark colours (7 of 31) brightened, and a scene's brighter ones (20)
 * darkened, which keeps a scene darkened in sight as natively. Math the
 * colour window limits (the time warp's glow) isn't done, which leaves the
 * layers as they are outside it. The backdrop's own math is exact
 * (backdrop, above), so it isn't a target. */
enum { BRIGHTENED_STRENGTH = 7, DARKENED_STRENGTH = 20, BLEND_STEPS = 16 };

static uint16_t line_layer_bits(const LineState *st, uint16_t snes_bits);

static void fixed_shading(LineRegisters *r, const LineState *st)
{
    const VidColorMath *m = &st->math;
    uint8_t layers = (uint8_t)(m->layers & st->main_layers & (VID_BG1_BIT | VID_BG2_BIT | VID_BG3_BIT | VID_BG4_BIT));
    if (st->brightness < 15 || sub_screen_shows_a_layer(st) || !layers || m->prevent_math != VID_REGION_NEVER)
        return;
    unsigned red = st->fixed_color & 31, green = st->fixed_color >> 5 & 31, blue = st->fixed_color >> 10 & 31;
    if (red != green || green != blue || red == 0)
        return;
    unsigned steps = m->subtract ? red * BLEND_STEPS / DARKENED_STRENGTH : red * BLEND_STEPS / (31 - BRIGHTENED_STRENGTH);
    if (m->half)
        steps /= 2;
    r->bldcnt = (uint16_t)((m->subtract ? BLD_DARKEN : BLD_LIGHTEN) | line_layer_bits(st, layers));
    r->bldy = (uint16_t)(steps > BLEND_STEPS ? BLEND_STEPS : steps);
}

/* The layers' scroll on a band's line of the GBA's screen, the SNES's line
 * shown `down` lines further down the GBA's than the GBA's own. A layer's
 * row on the SNES is its line's (the first shown is its line 1) and its
 * scroll, one further down than on the GBA. */
static void line_scroll(LineRegisters *r, const LineState *st, const Band *b, int down)
{
    /* the fourth layer's, which no line effect changes, as it stands */
    const uint16_t hofs[4] = { st->hofs[0], st->hofs[1], st->hofs[2], snes.bg[3].hofs };
    const uint16_t vofs[4] = { st->vofs[0], st->vofs[1], st->vofs[2], snes.bg[3].vofs };
    for (unsigned g = 0; g < 4; g++) {
        r->hofs[g] = (uint16_t)(hofs[scroll_source[g]] + b->left);
        r->vofs[g] = (uint16_t)(vofs[scroll_source[g]] + down + 1);
    }
}

/* The GBA's layers' control, from the SNES's layers' (`controls`): a split
 * layer's tiles in front from their copy, at each layer's priority. A map
 * with no tiles in front, or a drawn panel's layer (`drawn`, a bit each),
 * isn't shown by the layer that shows them: through the windows, since a
 * layer enabled part of the way down shows only from three lines on. */
static void gba_controls(LineRegisters *r, const uint16_t controls[4], uint8_t drawn)
{
    for (unsigned g = 0; g < 4; g++) {
        const GbaLayer *l = &gba_layers[g];
        if (l->source == NO_LAYER)
            continue;
        uint16_t control = controls[l->source];
        if (l->front) {
            uint8_t block = drawn >> l->source & 1 ? NO_BLOCK : front_block[control >> 8 & 31];
            if (block == NO_BLOCK) {
                r->winin &= (uint16_t)~(0x0101u << g);
                r->winout &= (uint16_t)~(0x0101u << g);
                continue;
            }
            control = (uint16_t)((control & ~0x1F00) | block << 8);
        }
        r->bgcnt[g] = control | l->priority;
    }
}

/* The state a band's line shows: a drawn panel's line takes the SNES line's
 * looks with the panel's own layers alone (`drawn` holds it). */
static const LineState *line_looks(const LineState *st, const Band *b, uint16_t layers_enabled, LineState *drawn)
{
    if (!b->drawn)
        return st;
    *drawn = *st;
    drawn->main_layers = (uint8_t)(panel.layers & layers_enabled);
    drawn->sub_layers &= drawn->main_layers;
    return drawn;
}

/* A line's blending: the SNES's brightness as the GBA's fade to black, and
 * its colour math. Adding the sub screen to the layers colour math takes
 * in is the GBA's blending of two targets at full weight (half at half): of
 * the sub screen, only what the main screen shows too, and the backdrop.
 * The SNES's subtraction of a sub screen has no GBA counterpart. */
static void line_blending(LineRegisters *r, const LineState *st)
{
    r->bldcnt = st->brightness < 15 ? (uint16_t)(BLD_DARKEN | BLD_ALL_FIRST) : 0;
    r->bldy = (uint16_t)((15 - st->brightness) * 16 / 15);
    r->bldalpha = 0;
    r->backdrop = backdrop(st);
    const VidColorMath *m = &st->math;
    uint8_t first = (uint8_t)(m->layers & (st->main_layers | VID_BACK_BIT));
    if (st->brightness == 15 && sub_screen_shows_a_layer(st) && !m->subtract && first && m->prevent_math != VID_REGION_ALWAYS) {
        uint8_t second = (uint8_t)((st->sub_layers & st->main_layers & VID_ALL_LAYERS) | VID_BACK_BIT);
        r->bldcnt = (uint16_t)(BLD_ALPHA | line_layer_bits(st, first) | line_layer_bits(st, second) << 8);
        r->bldalpha = m->half ? 0x0808 : 0x1010;
    } else {
        fixed_shading(r, st);
    }
}

/* A line that shows nothing: black. The layers stay enabled, every line of
 * the frame, and a line shows the ones it does through the windows: the
 * GBA shows a layer enabled part of the way down only from three lines on,
 * where a window's layers change at once. */
static LineRegisters nothing_shown(uint16_t layers_enabled)
{
    LineRegisters r = { 0 };
    r.dispcnt = (uint16_t)(DISPCNT_OBJ_2D | DISPCNT_OBJ | DISPCNT_WIN0 | DISPCNT_WIN1 |
                           gba_layer_bits(layers_enabled & 15) * DISPCNT_BG0 | (plane_room ? DISPCNT_BG2 : 0));
    return r;
}

/* Whether a line shows Mode 7: the plane where the layout has made its room
 * (a line of a frame whose layout hasn't shows its sprites alone). */
static bool shows_mode7(const LineState *st) { return st->mode == 7; }

/* SNES layer bits as the GBA's on a line: on a Mode 7 line the plane (the
 * SNES's first layer) is the GBA's third, and the rest show nothing. */
static uint16_t line_layer_bits(const LineState *st, uint16_t snes_bits)
{
    if (!shows_mode7(st))
        return gba_layer_bits(snes_bits);
    uint16_t bits = (uint16_t)(snes_bits & ~0xFu);
    return plane_room && (snes_bits & VID_BG1_BIT) ? (uint16_t)(bits | DISPCNT_BG2 / DISPCNT_BG0) : bits;
}

/* The native renderer's wrap of the plane's offsets (runtime/platform/native/render.c). */
static int plane_wrap(int n) { return (n & 0x2000) ? (n | ~1023) : (n & 1023); }

/* A Mode 7 line's plane on the GBA's affine layer: the matrix, and the
 * point of the plane at the GBA line's left end (the band's dot `left`) on
 * the SNES's line `y` (its first is line 1), worked out as the native
 * renderer does; the GBA steps it by the matrix's second column a line, as
 * the SNES does, until the next change sets it again. What lies outside the
 * plane is the plane again, or nothing (where the SNES shows tile 0 there
 * too). It stands between the sprites of priority 1 and 0, as on the SNES. */
static void plane_line(unsigned at, const VidMode7 *m, int left, int y)
{
    if (m->vflip)
        y = 255 - y;
    int hx = plane_wrap(m->hofs - m->cx), vy = plane_wrap(m->vofs - m->cy);
    int32_t ox = ((m->a * hx) & ~63) + ((m->b * vy) & ~63) + ((m->b * y) & ~63) + m->cx * 256;
    int32_t oy = ((m->c * hx) & ~63) + ((m->d * vy) & ~63) + ((m->d * y) & ~63) + m->cy * 256;
    int first = m->hflip ? 255 - left : left, step = m->hflip ? -1 : 1, down = m->vflip ? -1 : 1;
    if (!plane_table)
        return;
    volatile PlaneLine *p = &plane_lines[at];
    p->x = ox + m->a * first;
    p->y = oy + m->c * first;
    p->pa = (int16_t)(m->a * step), p->pc = (int16_t)(m->c * step);
    p->pb = (int16_t)(m->b * down), p->pd = (int16_t)(m->d * down);
    plane_set[at / 8] |= (uint8_t)(1u << (at % 8));
    plane_shown = true;
}

/* A Mode 7 line's affine layer: the GBA's mode 2, its third layer the plane. */
static void plane_layer(LineRegisters *r, const VidMode7 *m)
{
    r->dispcnt = (uint16_t)((r->dispcnt & ~DISPCNT_MODES) | DISPCNT_AFFINE_MODE | DISPCNT_BG2);
    r->bgcnt[2] = (uint16_t)(PLANE_PRIORITY | PLANE_MAP_BLOCK << 8 | BGCNT_1024_DOTS |
                             (m->outside == VID_M7_WRAP || m->outside == VID_M7_WRAP_UNUSED ? BGCNT_WRAP : 0));
}

/* The lines no change set: the one above's, stepped. */
static void step_the_plane(void)
{
    for (unsigned at = 1; plane_table && at <= GBA_LINES; at++) {
        if (at < GBA_LINES && (plane_set[at / 8] >> (at % 8) & 1))
            continue;
        volatile PlaneLine *p = &plane_lines[at];
        const volatile PlaneLine *above = p - 1;
        p->pa = above->pa, p->pb = above->pb, p->pc = above->pc, p->pd = above->pd;
        p->x = above->x + above->pb;
        p->y = above->y + above->pd;
    }
}

/* ---- Mode 2's columns ------------------------------------------------------------------------------
 *
 * In mode 2 the SNES scrolls each column of the first two layers by its own
 * offsets, read from two rows of the third layer's map (which the mode
 * doesn't show), where an offset's entry names the layer: the menus show a
 * list scrolled a line at a time beside a column that stays (the techs, the
 * equipment, the shops), and their settings two columns of icons each
 * scrolled its own way. The GBA has no such thing. A run of a layer's
 * columns moved alike is shown again on a GBA layer of its own, the same
 * map scrolled as the run is, through a window the SNES's windows leave
 * free on the line, where the layer itself isn't shown. The layers it is
 * shown on are those spare (assign_layers) and, a mode 2 line showing no
 * third layer, the third layer's; the windows are the GBA's two. Runs
 * past those show as the layer lies. */

/* A run of a layer's columns its offsets move alike on a line: its first
 * and last dots (the SNES's) and the scroll it shows. A column takes the
 * offsets in the third layer's column a column to its left, the screen's
 * first column none (render.c's draw_bg). */
typedef struct {
    int first, last;
    uint16_t hofs, vofs;
} MovedColumns;

enum {
    OFFSET_LAYER_BIT = 0x2000, OFFSET_SCROLL = 0x3FF, OFFSET_COLUMN = 0x3F8, GBA_SCROLL = 0x1FF, MOST_RUNS = 2,
    OFFSET_COLUMNS = 34,   /* the third layer's columns a line's 33 columns take their offsets from, and one */
};

/* A line's offsets: the third layer's two rows of them, across and down,
 * from the column the screen's second column takes its offsets from (or the
 * one left of it, where a layer's fine scroll is past the third layer's). */
typedef struct {
    uint16_t across[OFFSET_COLUMNS], down[OFFSET_COLUMNS];
    uint16_t hofs;   /* the third layer's scroll across, less one */
} LineOffsets;

typedef uint16_t __attribute__((may_alias)) Halfword;

/* A row of a map's entries from column `first_col` on, wrapping round the
 * map; a screen's row never runs past VRAM's end, and its entries are
 * halfwords as the GBA reads them. */
static void offset_row(const VidBg *b, uint32_t row, uint32_t first_col, uint16_t *out)
{
    uint32_t down = (row & 0x20) && b->map_h == 64 ? (b->map_w == 64 ? 2048 : 1024) : 0;
    uint32_t start = (uint32_t)b->map_base * 2 + (down + ((row & 0x1F) << 5)) * 2;
    const Halfword *screens[2] = { (const Halfword *)&snes.vram[start & (VRAM_BYTES - 1)],
                                   (const Halfword *)&snes.vram[(start + (b->map_w == 64 ? 2048 : 0)) & (VRAM_BYTES - 1)] };
    for (unsigned j = 0; j < OFFSET_COLUMNS; j++) {
        uint32_t col = (first_col + j) & 0x3F;
        out[j] = screens[col >> 5][col & 0x1F];
    }
}

/* The line's offsets, false where none names the first two layers. */
static bool line_offsets(const LineState *st, LineOffsets *o)
{
    VidBg third = { 0 };
    vid_bg_map_decode(&third, st->map[2]);
    o->hofs = (uint16_t)((st->hofs[2] - 1) & 0x1FF);
    uint32_t first_col = o->hofs >> 3;
    offset_row(&third, st->vofs[2] >> 3, first_col, o->across);
    offset_row(&third, (st->vofs[2] + 8u) >> 3, first_col, o->down);
    uint16_t names = 0;
    for (unsigned j = 0; j < OFFSET_COLUMNS; j++)
        names |= o->across[j] | o->down[j];
    return names & (OFFSET_LAYER_BIT * 3);
}

/* The first MOST_RUNS runs of layer `n`'s moved columns on a line, as many as there are. */
static unsigned moved_columns(const LineState *st, const LineOffsets *o, unsigned n, MovedColumns runs[MOST_RUNS])
{
    uint16_t names = (uint16_t)(OFFSET_LAYER_BIT << n), hofs = st->hofs[n], vofs = st->vofs[n];
    unsigned fine = hofs & 7, count = 0;
    /* the screen's column from `dot` takes the offsets of the third layer's
     * column its dot left of it is in */
    unsigned j = (o->hofs & 7) >= fine ? 1 : 0;
    bool in_run = false;
    for (int dot = 8 - (int)fine; dot <= VID_WINDOW_LAST_DOT; dot += 8, j++) {
        uint16_t across = o->across[j], down = o->down[j];
        uint16_t h = across & names ? (uint16_t)((across & OFFSET_COLUMN) | fine) : hofs;
        uint16_t v = down & names ? (uint16_t)(down & OFFSET_SCROLL) : vofs;
        if (in_run && !(((h ^ runs[count - 1].hofs) | (v ^ runs[count - 1].vofs)) & GBA_SCROLL)) {
            runs[count - 1].last = dot + 7 > VID_WINDOW_LAST_DOT ? VID_WINDOW_LAST_DOT : dot + 7;
            continue;
        }
        in_run = ((h ^ hofs) | (v ^ vofs)) & GBA_SCROLL;
        if (!in_run)
            continue;
        if (count == MOST_RUNS)
            break;
        runs[count++] = (MovedColumns){ .first = dot, .last = dot + 7, .hofs = h, .vofs = v };
    }
    return count;
}

static uint16_t *window_edges(LineRegisters *r, unsigned w) { return w ? &r->win1h : &r->win0h; }

static bool covers_nothing(uint16_t edges) { return (edges >> 8) >= (edges & 0xFF); }

/* Whether the GBA's window `w` (0 or 1) is free on a line: it covers
 * nothing, or shows what the line shows outside the windows. */
static bool window_free(LineRegisters *r, unsigned w)
{
    return covers_nothing(*window_edges(r, w)) || (r->winin >> (8 * w) & 0xFF) == (r->winout & 0xFF);
}

/* The GBA's layer that shows a SNES layer not split, or NO_LAYER. */
static unsigned own_layer(unsigned n)
{
    for (unsigned g = 0; g < 4; g++)
        if (gba_layers[g].source == n && !gba_layers[g].columns && !gba_layers[g].front)
            return g;
    return NO_LAYER;
}

/* The first two layers' moved columns on a band's line of a frame laid out
 * for mode 2, after its layers' control and blending are set: the layers
 * spare for them left out; then on a mode 2 line the third layer left out
 * too, whose map holds the offsets there, and each run on a GBA layer free
 * (the layer's own spare, then the third layer's), shown as its layer is,
 * through a window free or the one an earlier run with the same edges took,
 * with the layer itself left out there. Kept out of the frame's lines'
 * loop: inlined there, or with the third layer left out of the line's
 * state instead, the loop ran a percent slower (the jet bike race). */
__attribute__((noinline)) static void show_moved_columns(LineRegisters *r, const LineState *st, const Band *b, int down)
{
    uint16_t hidden = columns_bits;
    if (st->mode == 2)
        hidden |= gba_bits_of[VID_BG3_BIT];
    r->winin &= (uint16_t)~(hidden | hidden << 8);
    r->winout &= (uint16_t)~(hidden | hidden << 8);
    if (st->mode != 2)
        return;
    r->bldcnt &= (uint16_t)~(gba_bits_of[VID_BG3_BIT] | gba_bits_of[VID_BG3_BIT] << 8);
    LineOffsets offsets;
    if (b->drawn || !line_offsets(st, &offsets))
        return;
    uint8_t free_layers = 0, taken = 0;   /* GBA layers, and the windows given to runs, a bit each */
    for (unsigned g = 0; g < 4; g++)
        if (gba_layers[g].source == 2)
            free_layers |= (uint8_t)(1u << g);
    for (unsigned n = 0; n < 2; n++) {
        unsigned own = own_layer(n);
        MovedColumns runs[MOST_RUNS];
        unsigned count = own == NO_LAYER || !(st->main_layers >> n & 1) ? 0 : moved_columns(st, &offsets, n, runs);
        uint8_t spare = columns_layer[n] != NO_LAYER ? (uint8_t)(1u << columns_layer[n]) : 0;
        uint8_t layers = free_layers | spare;
        for (unsigned k = 0; k < count && layers; k++) {
            uint16_t edges = gba_window((uint8_t)runs[k].first, (uint8_t)runs[k].last, b);
            if (covers_nothing(edges))
                continue;   /* out of view */
            unsigned w = 0;
            for (; w < 2; w++)
                if (taken >> w & 1 ? *window_edges(r, w) == edges : window_free(r, w))
                    break;
            if (w == 2)
                continue;
            if (!(taken >> w & 1)) {
                *window_edges(r, w) = edges;
                r->winin = (uint16_t)((r->winin & (0xFF00u >> 8 * w)) | (r->winout & 0xFF) << 8 * w);
                taken |= (uint8_t)(1u << w);
            }
            unsigned g = (unsigned)__builtin_ctz(layers & spare ? spare : layers);
            layers &= (uint8_t)~(1u << g);
            free_layers &= (uint8_t)~(1u << g);
            r->winin = (uint16_t)((r->winin & ~(gba_bits_of[1u << n] << 8 * w)) | 1u << (g + 8 * w));
            r->bgcnt[g] = r->bgcnt[own];
            r->hofs[g] = (uint16_t)(runs[k].hofs + b->left);
            r->vofs[g] = (uint16_t)(runs[k].vofs + down + 1);
            if (r->bldcnt & 1u << own)
                r->bldcnt |= (uint16_t)(1u << g);
            if (r->bldcnt & 0x100u << own)
                r->bldcnt |= (uint16_t)(0x100u << g);
        }
    }
}

/* A line's registers: the SNES's line shown on a band's line `down` lines
 * further down the GBA's than the GBA's own. A layer's row on the SNES is
 * its line's (the first shown is its line 1) and its scroll, one further
 * down than on the GBA. A drawn panel's line takes the SNES line's looks
 * and draws the panel's own layers alone. */
static LineRegisters line_registers(const LineState *st, uint16_t layers_enabled, const Band *b, int down, int at)
{
    LineRegisters r = nothing_shown(layers_enabled);
    if (st->blank)
        return r;
    bool mode7 = shows_mode7(st);
    LineState drawn;
    st = line_looks(st, b, layers_enabled, &drawn);
    uint16_t controls[4] = { 0 };
    for (unsigned n = 0; n < 3; n++)
        if ((layers_enabled & (1u << n)) && !b->drawn && !mode7)
            controls[n] = layer_control(n, st->map[n], b, st->hofs[n]);
    if ((layers_enabled & VID_BG4_BIT) && !b->drawn && !mode7)
        controls[3] = layer_control(3, map_byte_of(&snes.bg[3]), &(Band){ 0 }, 0);
    if (st->windows.win_main & (VID_ALL_LAYERS)) {
        r.win0h = gba_window(st->windows.w1_left, st->windows.w1_right, b);
        r.win1h = gba_window(st->windows.w2_left, st->windows.w2_right, b);
        r.winin = (uint16_t)(line_layer_bits(st, region_layers(st, true, false)) |
                             line_layer_bits(st, region_layers(st, false, true)) << 8);
    }
    r.winout = line_layer_bits(st, region_layers(st, false, false));
    /* Inside the sprite window, what the line shows outside the windows but
     * the sprites. */
    if (sprites_mask) {
        r.dispcnt |= DISPCNT_OBJ_WINDOW;
        r.winout = (uint16_t)(r.winout | (r.winout & 0x2F) << 8);
    }
    line_scroll(&r, st, b, down);
    uint8_t drawn_layers = 0;
    for (unsigned s = 0; b->drawn && s < PANEL_LAYERS; s++) {
        unsigned n = panel.layer[s];
        if (n == NO_LAYER || !(layers_enabled & (1u << n)))
            continue;
        controls[n] = panel.control[s];
        drawn_layers |= (uint8_t)(1u << n);
        for (unsigned g = 0; g < 4; g++)
            if (gba_layers[g].source == n) {
                r.hofs[g] = panel.hofs[s];
                r.vofs[g] = (uint16_t)(panel.vofs[s] - panel.at);
            }
    }
    if (mode7)
        plane_layer(&r, &st->m7), plane_line((unsigned)at, &st->m7, b->left, at + down + 1);
    else
        gba_controls(&r, controls, drawn_layers);
    line_blending(&r, st);
    if (laid_out_mode == 2 && !mode7)
        show_moved_columns(&r, st, b, down);
    return r;
}

/* In ARM, in fast RAM, as the interrupts are; a long call from the cartridge. */
__attribute__((target("arm"), section(".iwram"), long_call, noinline)) static void set_line(const LineRegisters *r)
{
    DISPCNT = r->dispcnt;
    for (unsigned g = 0; g < 4; g++) {
        BGCNT(g) = r->bgcnt[g];
        BGHOFS(g) = r->hofs[g];
        BGVOFS(g) = r->vofs[g];
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

/* The first line's plane and strength, and DMA armed to copy each next
 * line's in the horizontal blank above it, where the frame has them; DMA
 * stopped where it hasn't. In the vertical blank. DMA's control: on, at each
 * horizontal blank, again, to the registers again; words, or a halfword. */
static const uint32_t DMA_PLANE = 0xA6600000u | sizeof(PlaneLine) / 4;
static const uint32_t DMA_STRENGTH = 0xA2400000u | 1;
static volatile bool plane_dma, strength_dma;
__attribute__((target("arm"), section(".iwram"), noinline)) static void arm_the_lines(void)
{
    DMA0CNT = 0;
    DMA1CNT = 0;
    if (plane_dma && plane_table) {
        const volatile uint32_t *first = (const volatile uint32_t *)&plane_lines[0];
        for (unsigned w = 0; w < sizeof(PlaneLine) / 4; w++)
            BG2_PLANE[w] = first[w];
        DMA0SAD = (uint32_t)&plane_lines[1];
        DMA0DAD = (uint32_t)BG2_PLANE;
        DMA0CNT = DMA_PLANE;
    }
    if (strength_dma) {
        BLDY = line_strength[0];
        DMA1SAD = (uint32_t)&line_strength[1];
        DMA1DAD = (uint32_t)&BLDY;
        DMA1CNT = DMA_STRENGTH;
    }
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
            arm_the_lines();
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
    GbaLayer gba[4];  /* what the GBA's layers show */
    bool sprites_mask;
    uint16_t fourth[3];   /* the fourth layer's scroll and map */
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
    st->mode = snes.mode.mode;
    st->m7 = snes.m7;
    st->m7_latch = snes.m7_latch;
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
    memcpy(f->gba, gba_layers, sizeof f->gba);
    f->sprites_mask = sprites_mask;
    f->fourth[0] = snes.bg[3].hofs, f->fourth[1] = snes.bg[3].vofs, f->fourth[2] = map_byte_of(&snes.bg[3]);
}

enum { NO_LINE = 0xFFFF };

/* The black bands above GBA line `at` not yet set, set. */
static uint8_t black_bands_above(unsigned at, uint8_t count, uint8_t *next, uint16_t layers_enabled)
{
    for (; *next < band_count && bands[*next].at < at; (*next)++) {
        if (!bands[*next].black)
            continue;
        LineRegisters black = nothing_shown(layers_enabled);
        if (count < GBA_LINES && (count == 0 || memcmp(&black, &change_registers[count - 1], sizeof black) != 0)) {
            change_registers[count] = black;
            change_line[count++] = bands[*next].at;
        }
    }
    return count;
}

/* A line put among the `count` lines visited whatever the slots do, kept
 * in order; the new count. A band starts showing at its row's line, one
 * down (a line shows the row above it). */
static unsigned add_stop(uint16_t *lines, unsigned count, uint16_t line)
{
    unsigned k = count;
    for (; k > 0 && lines[k - 1] > line; k--)
        lines[k] = lines[k - 1];
    lines[k] = line;
    return count + 1;
}

/* The band showing the SNES's line `row`, or NULL, for rows asked of in
 * order down the picture: the bands show the SNES's lines in their order
 * (band_up_the_view), so the search goes on from the band `*from` it
 * reached for the row before. */
static const Band *band_down_to(unsigned *from, int row)
{
    for (; *from < band_count; (*from)++) {
        const Band *b = &bands[*from];
        if (b->black)
            continue;
        if (row < b->row)
            return NULL;
        if (row < b->row + b->rows)
            return b;
    }
    return NULL;
}

/* The slot's next transfer to apply, from its next line up to `stop`: its
 * line, with its unit in `last`; NO_LINE where it has none left. Where the
 * slots' targets are `apart`, a transfer of the unit the slot last applied
 * changes nothing and is passed over. */
static void pass_over_unchanged(Slot *sl, const VidLineEffect *e, uint16_t stop);

static uint16_t next_transfer(Slot *sl, const VidLineEffect *e, uint16_t stop, bool apart)
{
    bool passed = false;   /* whether a transfer here changed nothing */
    while (sl->active && sl->line < stop) {
        if (passed && sl->transfer && (sl->count & 0x80)) {
            pass_over_unchanged(sl, e, stop);
            if (sl->line >= stop)
                break;
        }
        uint16_t line = sl->line;
        bool applies = false;
        if (sl->transfer) {
            uint8_t unit[4] __attribute__((aligned(4))) = { 0 };
            if (e->indirect) {
                table_unit(e->indirect_bank, sl->indirect, sl->unit, unit);
                sl->indirect = (uint16_t)(sl->indirect + sl->unit);
            } else {
                table_unit(sl->bank, sl->cursor, sl->unit, unit);
                sl->cursor = (uint16_t)(sl->cursor + sl->unit);
            }
            /* compared a word at a time: the bytes past the unit are 0 in both */
            uint32_t now, was;
            memcpy(&now, unit, sizeof now);
            memcpy(&was, sl->last, sizeof was);
            applies = !(apart && sl->applied && now == was);
            passed = !applies;
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

/* A run of a repeating entry's transfers of the unit the slot last applied,
 * which change nothing (the battle's windows and scroll stand still down
 * most of their lines), passed over in one go once one of them has been
 * (where each line's differs, the race's plane, it isn't tried): up to the entry's last
 * transfer, `stop`, or a unit that differs or lies across two reads of the
 * tables, which next_transfer takes one at a time. The tables are read in
 * the same order as a transfer at a time reads them. */
static void pass_over_unchanged(Slot *sl, const VidLineEffect *e, uint16_t stop)
{
    uint16_t *at = e->indirect ? &sl->indirect : &sl->cursor;
    uint32_t bank = (uint32_t)(e->indirect ? e->indirect_bank : sl->bank) << 16;
    uint32_t held = 0;
    const uint8_t *bytes = NULL;
    while (sl->line < stop && (sl->count & 0x7F) > 1 && *at % READ_LINE_BYTES + sl->unit <= READ_LINE_BYTES) {
        uint32_t read = (bank | *at) / READ_LINE_BYTES;
        if (!bytes || read != held)
            bytes = line_bytes(read), held = read;
        const uint8_t *unit = bytes + *at % READ_LINE_BYTES;
        for (unsigned k = 0; k < sl->unit; k++)
            if (unit[k] != sl->last[k])
                return;
        *at = (uint16_t)(*at + sl->unit);
        sl->count--;
        sl->line++;
    }
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
    plane_on_lines = false;
    plane_shown = false;
    memset(plane_set, 0, sizeof plane_set);
    strength_steps = false;
    memset(strength_set, 0, sizeof strength_set);
    lines_text_mode = NO_MODE;
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
    /* the slots with transfers still to apply, in their order */
    uint8_t running[8], running_count = 0;
    for (unsigned i = 0; i < 8; i++)
        if (next[i] != NO_LINE)
            running[running_count++] = (uint8_t)i;
    /* the lines visited whatever the slots do, in order: the display's
     * change below a line, each band's first line and the lines asked of */
    uint16_t stop_line[1 + MOST_BANDS + ASKED_LINES];
    unsigned stops = 0, next_stop = 0;
    if (display_at != NO_LINE)
        stops = add_stop(stop_line, stops, display_at);
    for (unsigned k = 0; k < band_count; k++)
        if (!bands[k].black)
            stops = add_stop(stop_line, stops, (uint16_t)(bands[k].row + 1));
    for (unsigned k = 0; k < ASKED_LINES; k++)
        if (asked_line[k])
            stops = add_stop(stop_line, stops, asked_line[k]);
    unsigned band_at = 0;   /* where band_down_to has reached */
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
        while (next_stop < stops && stop_line[next_stop] < line)
            next_stop++;
        if (next_stop < stops && stop_line[next_stop] == line)
            for (unsigned k = 0; k < ASKED_LINES; k++)
                if (asked_line[k] == line)   /* the lines asked of, which are visited */
                    asked_shown[k] = (uint8_t)(st.main_layers | (st.blank ? ASKED_BLANK : 0));
        const Band *b = line ? band_down_to(&band_at, (int)line - 1) : NULL;
        int at = !b ? -1 : b->drawn ? band_line(b, (int)line - 1) : b->at + ((int)line - 1 - b->row);
        if (at >= 0 && st.mode == 7)
            plane_on_lines = true;
        else if (at >= 0 && lines_text_mode == NO_MODE)
            lines_text_mode = st.mode;
        if (at >= 0) {
            int down = (int)(line - 1) - at;
            if (next_black < band_count && bands[next_black].at < at)
                count = black_bands_above((unsigned)at, count, &next_black, layers_enabled);
            bool band_starts = at == b->at, again = false;
            /* Where only the scroll, the plane or the colour math may have
             * changed (but not a band cut's scroll, which moves the cut in
             * its maps, nor mode 2's, which moves its columns, nor between
             * Mode 7 and another mode), the last
             * change's registers with those: a line's scroll (but on the
             * drawn panel, whose layers' scroll is its own); on a Mode 7
             * line, its plane (the first layer's scroll is the plane's); the
             * blending, a step of its strength alone no change at all. */
            bool cheap = count && !band_starts && !(touched & CHANGED_OTHER) &&
                         !((b->cut_dots || st.mode == 2) && (touched & CHANGED_SCROLL)) &&
                         (st.mode == 7) == (shown.mode == 7) && !(st.mode == 7 && b->drawn);
            LineRegisters r;
            /* a Mode 7 line's plane is its own table's, no change's */
            if (touched && cheap && st.mode == 7 && (touched & (CHANGED_PLANE | CHANGED_SCROLL))) {
                plane_line((unsigned)at, &st.m7, b->left, at + 1 + down);
                shown.m7 = st.m7;
                shown.m7_latch = st.m7_latch;
                for (unsigned n = 0; n < 3; n++)
                    shown.hofs[n] = st.hofs[n], shown.vofs[n] = st.vofs[n];
                touched &= CHANGED_MATH;
            }
            if (touched && cheap) {
                bool scrolled = st.mode != 7 && (touched & CHANGED_SCROLL) && !b->drawn &&
                                (memcmp(st.hofs, shown.hofs, sizeof st.hofs) != 0 ||
                                 memcmp(st.vofs, shown.vofs, sizeof st.vofs) != 0);
                bool blended = (touched & CHANGED_MATH) && (st.fixed_color != shown.fixed_color ||
                                                            memcmp(&st.math, &shown.math, sizeof st.math) != 0);
                if (scrolled || blended) {
                    r = change_registers[count - 1];
                    again = true;
                }
                if (scrolled) {
                    line_scroll(&r, &st, b, down);
                    memcpy(shown.hofs, st.hofs, sizeof st.hofs);
                    memcpy(shown.vofs, st.vofs, sizeof st.vofs);
                }
                if (blended) {
                    LineState drawn;
                    line_blending(&r, line_looks(&st, b, layers_enabled, &drawn));
                    shown.math = st.math;
                    shown.fixed_color = st.fixed_color;
                    /* the strength alone changed: a step, not a change */
                    LineRegisters same = r;
                    same.bldy = change_registers[count - 1].bldy;
                    if (!scrolled && memcmp(&same, &change_registers[count - 1], sizeof same) == 0) {
                        set_strength((unsigned)at, r.bldy);
                        strength_steps = true;
                        again = false;
                    }
                }
            } else if (count == 0 || band_starts || (touched && memcmp(&st, &shown, sizeof st) != 0)) {
                /* the mode and the layers shown change only on lines worked out whole */
                if (st.mode == 2)
                    lines_text_mode = 2;
                else if (laid_out_mode == 2 && st.mode != 7 && ((st.main_layers | st.sub_layers) & VID_BG3_BIT))
                    third_on_lines = true;
                r = line_registers(&st, layers_enabled, b, down, at);
                memcpy(&shown, &st, sizeof st);
                again = true;
            }
            if (again && count < GBA_LINES && (count == 0 || memcmp(&r, &change_registers[count - 1], sizeof r) != 0)) {
                change_registers[count] = r;
                change_line[count++] = (uint8_t)at;
                set_strength((unsigned)at, r.bldy);
            }
            touched = 0;
        }
        /* the slots with a transfer at this line apply it, in the slots' order */
        uint16_t soonest = NO_LINE;
        unsigned still = 0;
        for (unsigned r = 0; r < running_count; r++) {
            unsigned i = running[r];
            if (next[i] == line) {
                Slot *sl = &slots[i];
                const VidLineEffect *e = &snes.slot[i];
                unit_apply(&st, e->target, sl->last);
                touched |= sl->changes;
                next[i] = next_transfer(sl, e, stop[i], apart);
                if (next[i] == NO_LINE)
                    continue;
            }
            running[still++] = (uint8_t)i;
            if (next[i] < soonest)
                soonest = next[i];
        }
        running_count = still;
        while (next_stop < stops && stop_line[next_stop] <= line)
            next_stop++;
        if (next_stop < stops && stop_line[next_stop] < soonest)
            soonest = stop_line[next_stop];
        if (touched && line + 1 < soonest && band_down_to(&band_at, line))
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
    bool may_stand = last_stands && !laid_out && !asked_anew && !offsets_written;
    asked_anew = offsets_written = false;
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
        if (plane_shown)
            step_the_plane();
        if (strength_steps)
            fill_the_strengths();
        last_from = from_hash;
        last_left = st;
        /* a layout's maps change what a line's registers name */
        last_stands = lines_read_kept && !lines_read_overflowed && !laid_out;
        if (worked_out < 2)
            worked_out++;
    }
    changes = count;
    plane_dma = plane_shown;
    strength_dma = strength_steps;
    /* What the effects leave stands, as the SNES's registers do. */
    snes.windows = st.windows;
    snes.math = st.math;
    snes.fixed_color = st.fixed_color;
    snes.main_layers = st.main_layers;
    snes.sub_layers = st.sub_layers;
    for (unsigned n = 0; n < 3; n++)
        snes.bg[n].hofs = st.hofs[n], snes.bg[n].vofs = st.vofs[n];
    snes.m7 = st.m7;
    snes.m7_latch = st.m7_latch;
    WIN0V = GBA_LINES;
    WIN1V = GBA_LINES;
    if (count == 1) {
        if (by_line) {
            by_line = false;
            REG_IE = (uint16_t)(REG_IE & ~(IRQ_VCOUNT | IRQ_HBLANK));
            DISPSTAT = (uint16_t)(DISPSTAT & 0x00FF & ~(DISPSTAT_HBLANK_IRQ | DISPSTAT_VCOUNT_IRQ));
        }
        set_line(&change_registers[0]);
        arm_the_lines();
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
    /* the plane's room made where a line shows Mode 7, and given back where none does */
    bool plane_moved = plane_wanted() ? !plane_room && !plane_refused : plane_room;
    bool laid_out = snes.settings_changed || restart || plane_moved || text_mode() != laid_out_mode ||
                    (laid_out_mode == 2 && third_on_lines && !laid_out_third);
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
    /* The GBA's layers, where the mode, the layout or the maps' priorities
     * changed; a layer to split, or a split one to join, makes the layout
     * again in the next frame. */
    if (priorities_noted && layers_to_split(split_wanted) != split_wanted)
        snes.settings_changed = true;
    const Order *was = order;
    order = order_of_mode();
    if (laid_out || priorities_noted || order != was) {
        assign_layers();
        priorities_noted = false;
    }
    if (order != was)
        snes.sprites_changed = true;
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
    for (unsigned n = 0; n < 4; n++)
        if (layers[n].shown)
            layers_enabled |= (uint16_t)(1u << n);
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
