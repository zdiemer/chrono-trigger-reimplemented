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
 * - The palette is the same colours (both are BGR555), or each as the
 *   cartridge's colours' table has it where it was packed with one
 *   (gba_video_use_colours). The 4bpp layers' eight
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
 *   through a window ("Mode 2's columns", below); where they move up and
 *   down more ways than that, the layer is shown from a copy of its map
 *   with its columns moved ("A layer's columns copied").
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
/* Code that toolchain/fast_code.ld places in fast RAM, compiled as ARM:
 * fast RAM fetches its 32-bit instructions as quickly as Thumb's, and ARM
 * does a frame's line effects in fewer of them. */
#define ARM_CODE __attribute__((target("arm")))
#define DISPCNT REG16(0x04000000)
#define BGCNT(n) REG16(0x04000008 + 2 * (n))
#define BGHOFS(n) REG16(0x04000010 + 4 * (n))
#define BGVOFS(n) REG16(0x04000012 + 4 * (n))
#define BG2_PLANE ((volatile uint32_t *)0x04000020)   /* PA-PD, X and Y: four words */
#define DMA0SAD (*(volatile uint32_t *)0x040000B0)
#define DMA0DAD (*(volatile uint32_t *)0x040000B4)
#define DMA0CNT (*(volatile uint32_t *)0x040000B8)
#define DMA3SAD (*(volatile uint32_t *)0x040000D4)
#define DMA3DAD (*(volatile uint32_t *)0x040000D8)
#define DMA3CNT (*(volatile uint32_t *)0x040000DC)
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
    BLD_DARKEN = 0x00C0, BLD_LIGHTEN = 0x0080, BLD_ALPHA = 0x0040, BLD_ALL_FIRST = 0x003F, BLD_BACKDROP_SECOND = 0x2000,
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
    bool front_refused;   /* its tiles in front found no room for their copy at the layout */
    uint16_t byte, bytes;
    uint16_t gba_byte;    /* NO_PLACE: no room for it */
    uint16_t front_byte;  /* its tiles in front, where its layer is split; or NO_PLACE */
} LineMap;
enum { NO_PLACE = 0xFFFF, NO_CUT = 0xFF, CUT_COLUMN = 0x1F, CUT_COLUMNS_SHIFT = 5 };
/* The maps kept for the lines, beside the layers' own (a battle's six, and
 * the three its window cut to the view takes): a map a band shows cut takes
 * no more than CUT_LINE_MAPS of them, leaving room for a map a line shows
 * whole. */
enum { LINE_MAPS = 10, CUT_LINE_MAPS = 9 };

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
    LineMap line_maps[LINE_MAPS];
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
    if (half >= 0 && after_high == (half == 1) && step == 1) {
        /* one half of successive words, as the Mode 7 plane's pixels and
         * map are sent: a unit's bytes compared and put together, the unit
         * marked once */
        for (uint32_t k = 0; k < n;) {
            uint32_t at = (uint32_t)(addr & 0x7FFF) << 1 | (uint32_t)half;
            uint32_t m = (UNIT_BYTES - at % UNIT_BYTES + 1) / 2;
            if (m > n - k)
                m = n - k;
            bool changed = false;
            for (uint32_t j = 0; j < m; j++) {
                uint8_t v = bytes ? bytes[k + j] : fill;
                if (snes.vram[at + 2 * j] != v) {
                    snes.vram[at + 2 * j] = v;
                    changed = true;
                }
            }
            if (changed)
                unit_changed(at);
            addr = (uint16_t)(addr + m);
            k += m;
        }
        snes.vram_addr = addr;
        return;
    }
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

/* The SNES's VRAM, kept as bytes, read a word or a map entry at a time:
 * these types may alias the bytes, as `memcpy` would, without the copy. */
typedef uint32_t __attribute__((may_alias)) Word;
typedef uint16_t __attribute__((may_alias)) Halfword;

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

/* Whether words through the port go straight into VRAM's next bytes: a
 * port that steps a word after the high byte, with no remapping. */
static bool port_takes_words_in_order(void)
{
    return snes.port.remap == 0 && snes.port.step == 1 && snes.port.after_high;
}

/* `len` bytes of `src` through the VRAM port: words, low byte first (half
 * -1), or one byte of each word (half 0 the low, 1 the high). Words through
 * a port that takes them in order go straight into VRAM's next bytes. */
static void vram_put_bytes(PfData src, uint32_t len, int half)
{
    uint32_t i = 0;
    if (half < 0 && port_takes_words_in_order()) {
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

/* `len` bytes of `fill` through the VRAM port as words. Through a port that
 * takes them in order they are compared against VRAM and put a piece at a
 * time, as a send's bytes are (vram_put_bytes), rather than a byte at a
 * time: the menus clear all 64 KiB of VRAM as they open, which a byte at a
 * time took several GBA frames. */
static void vram_fill(uint8_t fill, uint32_t len)
{
    uint32_t i = 0;
    if (port_takes_words_in_order()) {
        memset(piece, fill, sizeof piece_words);
        while (len - i >= 2) {
            uint32_t at = (uint32_t)(snes.vram_addr & 0x7FFF) << 1;
            uint32_t n = (len - i) & ~1u;
            if (n > VRAM_BYTES - at)
                n = VRAM_BYTES - at;
            if (n > PIECE)
                n = PIECE;
            vram_take(at, piece + at % 4, n);
            snes.vram_addr = (uint16_t)(snes.vram_addr + n / 2);
            i += n;
        }
    }
    vram_put_run(NULL, fill, len - i, i, -1);
}

static void vram_upload(uint16_t vram_addr, PfData src, uint32_t len, bool fill, const VidVramPort *port)
{
    if (port)
        snes.port = *port;
    if (len == 0)
        return;
    snes.vram_addr = vram_addr;
    if (fill) {
        vram_fill(data_byte(src, 0), len);
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
 * this frame's panels kept in view (vid_keep_a_panel_in_view): their lines
 * and cuts in fast RAM, which the bands read a run of lines at a time, and
 * beside them the column each moves by its own lines (VidViewPanel's
 * column), which only a band that moves one reads. */
typedef struct {
    int16_t top;
    uint16_t lines;
    int16_t left, at, cut_at;
    uint16_t cut_dots;
} KeptPanel;
typedef struct {
    int16_t rows;   /* the column's lines this many further down the SNES's than the band's */
    int16_t end;    /* the view's dot short of which it is moved; 0: none */
} MovedColumn;
static VidAim view_aim IN_FAST_RAM;   /* set as the service starts */
static KeptPanel view_panels[VID_VIEW_PANELS] IN_FAST_RAM;
static MovedColumn view_panel_columns[VID_VIEW_PANELS];
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

/* The colours as the GBA shows them: each BGR555 colour's own in the
 * cartridge's table, where the pack has one (pack.py --profile), or the
 * colour itself. A colour is one read of the cartridge, cheaper than working
 * a gamma and a saturation out, and only the palette's written colours and
 * each line's backdrop pass through it. The GBA's fades and blending then
 * work on what it shows, as the SNES's brightness and colour math work on
 * what a television shows. */
static const uint16_t *shown_colours;

void gba_video_use_colours(const uint16_t *colours) { shown_colours = colours; }

static uint16_t shown(uint16_t colour) { return shown_colours ? shown_colours[colour & 0x7FFF] : colour; }

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

static bool darkened_this_frame;   /* the display darkened from the top since the frame began (display_from) */

void vid_host_frame_begins(Platform *pf)
{
    VidHost *v = &pf->video;
    if (v->below_display)
        v->display = v->display_below;
    if (v->below_line_effects)
        v->line_effects = v->line_effects_below;
    v->display_from = v->effects_from = 0;
    v->below_display = v->below_line_effects = false;
    darkened_this_frame = false;
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
    if (view_panel_count >= VID_VIEW_PANELS)
        return;
    view_panels[view_panel_count] = (KeptPanel){ panel->top, panel->lines, panel->left, panel->at, panel->cut_at, panel->cut_dots };
    view_panel_columns[view_panel_count++] = panel->column_end > 0 ?
        (MovedColumn){ (int16_t)(panel->column_top - panel->top), panel->column_end } : (MovedColumn){ 0, 0 };
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

static void black_where_nothing_shows(Platform *pf);

static bool dark(const VidDisplay *d) { return d->blank || !(d->brightness & 15); }

/* The display from a line down, or from the top. Darkened from the top a
 * second time in a frame, with no light between, the game means the screen
 * dark from here, and it is black at once rather than at the frame's
 * picture: a scene's loading frame (the title's last as A was pressed) ran
 * on for GBA frames over the last picture. The vertical blank's handler
 * darkens it each frame only to give it back at once. */

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
    if (darkened_this_frame && dark(d))
        black_where_nothing_shows(pf);
    darkened_this_frame = dark(d);
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
#define NO_SPRITE_TABLES 0xFFFFFFFFu
typedef struct {
    bool used;
    uint8_t bpp;
    uint16_t char_byte;
    uint8_t block;                 /* the GBA char block its window starts at */
    uint16_t next, blank;          /* the next place to look at, a blank tile's place */
    uint16_t placed;               /* the tiles with places */
    uint16_t slot_of[CACHE_TILES] __attribute__((aligned(4)));   /* each SNES tile's place in the window, or NO_SLOT */
} Cache;

/* A layer as the GBA shows it: how deep its tiles are, where its map is,
 * and which cache its tiles are in; whether it is split by priority, with
 * where its tiles in front are kept ("Priority", below). */
typedef struct {
    bool shown;
    bool own_map;          /* whether its own map is converted (a layout may leave it out) */
    bool split;
    bool overlaid;         /* its tiles in front shown again (layers_to_split), from front_byte */
    uint8_t bpp, cache;
    uint16_t char_byte, map_byte, map_bytes;
    uint16_t front_byte;   /* NO_PLACE: not split */
} Layer;

static Layer layers[4];
static Cache caches[CACHES];
static uint8_t roles[BLOCKS];
static uint8_t taken[PLACES / 8] __attribute__((aligned(4)));   /* the 32-byte places of background VRAM a map or a tile has */
static uint32_t misses, layout_misses;
static uint16_t frames_since_layout;

/* The bits set in a word, a few steps at once rather than the C library's
 * call a byte at a time. */
static unsigned bits_set(uint32_t w)
{
    w = w - (w >> 1 & 0x55555555u);
    w = (w & 0x33333333u) + (w >> 2 & 0x33333333u);
    w = (w + (w >> 4)) & 0x0F0F0F0Fu;
    return (w * 0x01010101u) >> 24;
}

static bool is_taken(uint32_t place) { return (taken[place / 8] >> (place % 8)) & 1; }
static void take(uint32_t place) { taken[place / 8] |= (uint8_t)(1u << (place % 8)); }

static uint16_t map_bytes(const VidBg *b) { return (uint16_t)((b->map_w == 64 ? 2 : 1) * (b->map_h == 64 ? 2 : 1) * SCREEN_BYTES); }

static bool within(uint32_t byte, uint32_t start, uint32_t length)
{
    return ((byte - start) & (VRAM_BYTES - 1)) < length;
}

static uint32_t tile_bytes(uint8_t bpp) { return bpp == 4 ? 32u : 16u; }
static unsigned tile_shift(uint8_t bpp) { return bpp == 4 ? 5u : 4u; }   /* tile_bytes' */

/* A place in a cache's window, for a tile: the next one free, or NO_SLOT.
 * Eight places a maps' screen or a room took whole are passed over at once. */
static uint16_t take_slot(Cache *c)
{
    uint32_t first = c->block * 512u, next = c->next;
    uint32_t end = first + CACHE_TILES < PLACES ? CACHE_TILES : PLACES - first;
    while (next < end) {
        uint32_t place = first + next;
        if (place % 8 == 0 && taken[place / 8] == 0xFF)
            next += 8;
        else if (is_taken(place))
            next++;
        else
            break;
    }
    if (next >= end) {
        c->next = CACHE_TILES;
        return NO_SLOT;
    }
    take(first + next);
    c->next = (uint16_t)(next + 1);
    return (uint16_t)next;
}

/* Where a sprite tile table starts in VRAM (bytes): the second is 8 KiB on
 * from the first, and the name select's words more, as the SNES's PPU has it. */
static uint32_t sprite_table(unsigned table)
{
    return ((uint32_t)snes.obj.base << 1) + (table ? 0x2000u + ((uint32_t)snes.obj.name_select << 1) : 0);
}

/* A bitplane's byte spread out as the first bit of each of a row's eight
 * packed nibbles, its leftmost dot (the byte's top bit) in the lowest:
 * filled at the start, for the sprites' tiles and the panel's, converted a
 * few at a time from the cartridge, inline. In work RAM: fast RAM's
 * kilobyte does more as the line effects' code (toolchain/fast_code.ld). */
static uint32_t spread[256];

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

/* A nibble of a bitplane's byte spread out as spread[] spreads a byte, for
 * the caches' tiles, hundreds given their places at a scene's first
 * picture: in fast RAM beside the code that reads it, where reading
 * spread[] from work RAM took longer than the rest of a tile's conversion.
 * The sprites' tiles converted so too took the five movies longer, a tile
 * at a time between the cartridge's code and fast RAM's, so they keep
 * spread[]. */
static const uint32_t nibble_dots[16] __attribute__((section(".iwram.nibble_dots"))) = {
    0x00000u, 0x01000u, 0x00100u, 0x01100u, 0x00010u, 0x01010u, 0x00110u, 0x01110u,
    0x00001u, 0x01001u, 0x00101u, 0x01101u, 0x00011u, 0x01011u, 0x00111u, 0x01111u,
};

/* A bitplane's byte as spread[] has it, from its two nibbles. */
static inline __attribute__((always_inline)) uint32_t plane_dots(uint32_t byte)
{
    return nibble_dots[byte >> 4] | nibble_dots[byte & 15] << 16;
}

/* A cache's tile, 2bpp (widened to 4bpp) or 4bpp, into its place. */
ARM_CODE __attribute__((noinline)) static void convert_tile(const Cache *c, uint16_t tile, uint16_t slot)
{
    volatile uint32_t *to = BG_VRAM + (c->block * 512u + slot) * 8u;
    const uint8_t *row = &snes.vram[(c->char_byte + tile * tile_bytes(c->bpp)) & (VRAM_BYTES - 1)];
    bool deep = c->bpp == 4;
#pragma GCC unroll 1
    for (unsigned y = 0; y < 8; y++, row += 2) {
        uint32_t dots = plane_dots(row[0]) | plane_dots(row[1]) << 1;
        if (deep)
            dots |= plane_dots(row[16]) << 2 | plane_dots(row[17]) << 3;
        to[y] = dots;
    }
}

/* A tile's place in its cache, given one (and the tile converted into it)
 * where it has none; the cache's blank where none is left. A scene's first
 * picture asks this for a few hundred tiles against its maps' thousands of
 * entries, so it stays in the cartridge while the maps' conversion runs
 * from fast RAM. */
__attribute__((noinline)) static uint16_t slot_for(Cache *c, uint16_t tile)
{
    uint16_t *slot = &c->slot_of[tile & (CACHE_TILES - 1)];
    if (*slot == NO_SLOT) {
        *slot = take_slot(c);
        if (*slot == NO_SLOT) {
            misses++;
            return c->blank;
        }
        c->placed++;
        convert_tile(c, tile, *slot);
    }
    return *slot;
}

/* Whether the `count` places from `first` are free, and taking them: a
 * byte of places at a time where they fill it (the maps' copies look from
 * the top of VRAM down for a screen's 64 places free, and a place at a
 * time that was a sixth of some layouts' work). */
static bool room_at(uint32_t first, uint32_t count)
{
    if (first + count > PLACES)
        return false;
    uint32_t place = first, end = first + count;
    for (; place < end && place % 8; place++)
        if (is_taken(place))
            return false;
    for (; place + 8 <= end; place += 8)
        if (taken[place / 8])
            return false;
    for (; place < end; place++)
        if (is_taken(place))
            return false;
    return true;
}

static void take_room(uint32_t first, uint32_t count)
{
    uint32_t place = first, end = first + count;
    for (; place < end && place % 8; place++)
        take(place);
    for (; place + 8 <= end; place += 8)
        taken[place / 8] = 0xFF;
    for (; place < end; place++)
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
 * entry in front of the sprites, and which have one behind: its own map's,
 * and those of the maps its line effects show in its place (the field's
 * layers, whose own maps no line shows), added to them. */
enum { MAP_UNITS = 4 * SCREEN_BYTES / UNIT_BYTES, PRIORITY_BIT = 0x2000, NO_BLOCK = 0xFF };
#define PRIORITY_BITS 0x20002000u   /* the priority of a word's two map entries */
static uint32_t units_in_front[4][MAP_UNITS / 32], units_behind[4][MAP_UNITS / 32];
/* The layers the last layout was asked to split, a bit each, and above
 * them (OVERLAY_SHIFT) the one asked to have its tiles in front shown again. */
enum { OVERLAY_SHIFT = 4 };
static uint8_t split_wanted;
static bool priorities_noted;  /* whether a unit's have changed since the GBA's layers were assigned */

ARM_CODE __attribute__((noinline, noclone)) static void note_priorities(unsigned n, uint32_t first, uint32_t map_byte, bool added)
{
    uint32_t unit = ((first - map_byte) & (VRAM_BYTES - 1)) / UNIT_BYTES, bit = 1u << (unit % 32);
    if (unit >= MAP_UNITS)
        return;
    /* the unit's 16 entries a word (two entries) at a time: an entry's
     * priority is its high byte's bit */
    const Word *words = (const Word *)&snes.vram[first];
    uint32_t any = 0, all = ~0u;
    for (unsigned w = 0; w < UNIT_BYTES / 4; w++)
        any |= words[w], all &= words[w];
    bool front = any & PRIORITY_BITS, behind = (all & PRIORITY_BITS) != PRIORITY_BITS;
    uint32_t *in_front = &units_in_front[n][unit / 32], *in_behind = &units_behind[n][unit / 32];
    uint32_t f = front ? *in_front | bit : added ? *in_front : *in_front & ~bit;
    uint32_t b = behind ? *in_behind | bit : added ? *in_behind : *in_behind & ~bit;
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
        *front += bits_set(only ? f & ~b : f);
        *behind += bits_set(only ? b & ~f : b);
    }
}

/* The layers to split, as many as the GBA's four leave room for, among
 * those with tiles both in front and behind: the first layer, then the
 * second (the first not split stands behind the sprites, and the second's
 * tiles in front alone would cover the first's in front: the stalls' wares
 * at the Fair), then the others (the sprites stand among the first two's
 * tiles; the third is the text and the panels, mostly); a layer of `split`
 * before one not split yet; then those with the most of the fewer. Where
 * the layer kept for mode 2's moved columns leaves none to split, the first
 * of those left has its tiles in front shown again on that layer instead,
 * on the lines no run takes it (Mode 2's columns, below): its bit above
 * the others, at OVERLAY_SHIFT. */
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
    bool kept = laid_out_mode == 2 && layers[0].shown && spare;
    if (kept)
        spare--;   /* kept for the first layer's moved columns (Mode 2's columns) */
    uint8_t wanted = 0;
    for (spare += kept; spare; spare--) {
        unsigned best = NO_LAYER, most = 0;
        for (unsigned n = 0; n < 4; n++)
            if (!(wanted >> n & 1) && rank[n] > most)
                most = rank[n], best = n;
        if (best == NO_LAYER)
            break;
        wanted |= (uint8_t)(1u << (best + (kept && spare == 1 ? OVERLAY_SHIFT : 0)));
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
 * third layer put in front, where it has any tiles in front (its text and
 * panels, which the mode puts in front of everything; what it has behind is
 * mostly blank), or where its own map isn't shown and the maps the lines
 * show in its place have no tiles behind either (a battle window's text, cut
 * to the view). A third layer of tiles behind alone stays behind, as on the
 * SNES, where the first two layers' tiles cover it: what a room keeps on it
 * out of sight (12,000 BC's cave, Lucca's house at the End of Time's gate
 * covered in light circles otherwise). Mode 2 shows no third layer; where a
 * mode 1 line below shows it, it is the menus' text, put in front so (their
 * description of a tech or an item). */
static bool stands_in_front(unsigned n)
{
    unsigned front, behind;
    const Order *o = order_of_mode();
    bool third_in_front = n == 2 && (o == &third_in_front_order || o == &mode2_order);
    count_priorities(n, !third_in_front, &front, &behind);
    if (!third_in_front)
        return front > behind;
    return front > 0 || (!layers[n].own_map && (o == &mode2_order || !behind));
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
    const Word *words = (const Word *)snes.vram;
    for (uint32_t i = 0; i < bytes; i += 4)
        if (words[((byte + i) & (VRAM_BYTES - 1)) / 4] & PRIORITY_BITS)
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

/* Room for a copy from the bottom of the GBA's background VRAM up: the
 * copies of tiles in front of a layer split in its line effects' maps
 * alone, which come and go with the scene's maps, kept out of the way of
 * the copies and the panel's room made from the top, and off the places the
 * last layout's tiles had (`held`, a bit each), so that what the last
 * frame's lines show stays where they show it while the layout's work goes
 * on; NO_PLACE where there is no such room, and the map shows its tiles in
 * front where its layer would stand not split. */
static uint16_t place_copy_low(uint32_t bytes, const uint8_t *held)
{
    for (uint32_t first = 0; first + bytes / 32 <= PLACES; first += SCREEN_BYTES / 32) {
        bool free = room_at(first, bytes / 32);
        for (uint32_t p = first; free && p < first + bytes / 32; p++)
            free = !(held[p / 8] >> (p % 8) & 1);
        if (free) {
            take_room(first, bytes / 32);
            return (uint16_t)(first * 32);
        }
    }
    return NO_PLACE;
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
    uint8_t mapped;                      /* the SNES layers whose map was sent since, a bit each */
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
    PanelRoom was = panel_room;
    panel_room = (PanelRoom){ 0 };
    if (!asked)
        return;
    tiles = (uint16_t)((tiles + RUN_STEP - 1) / RUN_STEP * RUN_STEP);
    tiles = tiles > PANEL_TILES ? PANEL_TILES : tiles;
    /* where it was, where that is still free, so that what was sent stays
     * shown: the dialog sends again only what it changes */
    bool same_run = was.made && was.run_tiles == tiles && room_at(was.run, tiles + 1u);
    int32_t run = same_run ? was.run : -1;
    for (int32_t first = (int32_t)((PLACES - tiles - 1) / RUN_STEP * RUN_STEP); first >= 0 && run < 0; first -= RUN_STEP)
        if ((uint32_t)first % 512 + tiles + 1 <= CACHE_TILES && room_at((uint32_t)first, tiles + 1u))
            run = first;
    if (run < 0) {
        panel_room.missed = true;
        return;
    }
    take_room((uint32_t)run, tiles + 1u);
    uint8_t mapped = was.made ? was.mapped : 0;
    for (unsigned s = 0; s < PANEL_LAYERS; s++) {
        uint32_t old = was.made ? was.map_block[s] * (SCREEN_BYTES / 32u) : PLACES;
        int32_t block = old < PLACES && room_at(old, SCREEN_BYTES / 32) ? (take_room(old, SCREEN_BYTES / 32), (int32_t)old)
                                                                         : room_from_the_top(SCREEN_BYTES / 32, SCREEN_BYTES / 32);
        if (block < 0) {
            panel_room.missed = true;
            return;   /* what it took stays taken until the next layout */
        }
        panel_room.map_block[s] = (uint8_t)(block * 32 / SCREEN_BYTES);
        /* a map moved, or naming tiles in a run that moved, is sent again */
        unsigned n = panel.layer[s];
        if (n != NO_LAYER && ((uint32_t)block != old || (!same_run && (panel.own_tiles >> n & 1))))
            mapped &= (uint8_t)~(1u << n);
    }
    panel_room.run = (uint16_t)run;
    panel_room.run_tiles = tiles;
    panel_room.mapped = mapped;
    if (!same_run)
        for (unsigned w = 0; w < 8; w++)
            BG_VRAM[(uint32_t)run * 8u + w] = 0;
    panel_room.made = true;
}

/* Room for a layer's columns copied (A layer's columns copied, below): two
 * copies, the one the lines show and the one made next, so that a copy is
 * never made while the GBA shows it, each a map as large as the layer's
 * and a run of places for its tiles, a blank one and COPY_TILES more,
 * within one char block's reach; and the strips both are made from. Made
 * where a line first wants it, from places nothing has, where as many
 * again as COPY_SPARE are left to the caches; a layout makes it again
 * where it was, if a line wanted it since the last, and gives it back if
 * none did. The title's logo takes up to 99 tiles a copy. */
enum { COPY_TILES = 127, COPIES = 2, STRIP_TILES = 160, COPY_SPARE = 256 };
typedef struct {
    bool made, asked;
    bool missed;                   /* no room was found since the last layout */
    uint8_t screens;               /* the screen blocks each map takes */
    uint8_t map_block[COPIES];     /* the GBA's screen blocks */
    uint16_t run[COPIES];          /* the tiles' run's first place, its blank */
    uint16_t strips;               /* the strips' first place (Strips, below) */
} CopyRoom;
static CopyRoom copy_room;

/* What a copy was made from: the GBA's control of the layer's map shown
 * (its tiles' char block, its screen block and size), the layer's scroll,
 * and how far each column the lines show is moved down its map, from the
 * left (the first, the screen's first dot's, by none). */
enum { COPY_COLUMNS = 33 };
typedef struct {
    uint16_t control, hofs, vofs;
    uint16_t moved[COPY_COLUMNS];
    uint8_t layer;
} CopyKey;
/* The copy the lines show, what it was made from, and whether it was made
 * in this frame's lines (another can't be: the other is the one shown) or
 * what it was made from has changed since. */
typedef struct {
    CopyKey key;
    uint16_t map_byte, map_bytes;   /* the SNES's map it was made from */
    uint8_t shown;
    bool made, fresh, stale;
} Copied;
static Copied copied;

/* The tiles of the columns the lines show, each run of a column's rows
 * whose tiles aren't blank flipped as the map shows them, one run after
 * another in the strips' room with a blank tile between: a column moved by
 * part of a tile then takes its new tiles' rows as they lie, eight words
 * on from where they start. Made again only where the map, its tiles or
 * the columns shown change; and for each copy, the rows each run takes in
 * it, which the next copy into it blanks. */
enum { STRIPS = 48 };
typedef struct {
    bool made;
    uint16_t control, hofs;
    uint8_t count;
    uint8_t column[STRIPS];   /* the column the lines show, from the left */
    uint8_t col[STRIPS];      /* its column of the map */
    uint8_t first[STRIPS];    /* the run's first row */
    uint8_t rows[STRIPS];
    uint16_t at[STRIPS];      /* the blank tile before it, in the strips' room */
    bool blanked[COPIES];     /* the copy's map holds blank tiles and the rows below */
    uint8_t was_first[COPIES][STRIPS], was_rows[COPIES][STRIPS];
} Strips;
static Strips strips;

static int32_t run_from_the_top(uint32_t count)
{
    for (int32_t first = (int32_t)((PLACES - count) / RUN_STEP * RUN_STEP); first >= 0; first -= RUN_STEP)
        if ((uint32_t)first % 512 + count <= CACHE_TILES && room_at((uint32_t)first, count)) {
            take_room((uint32_t)first, count);
            return first;
        }
    return -1;
}

/* The room for copies of maps `screens` screen blocks large, where it was
 * (`again`, a layout's) or anywhere free; false where there is none. */
static bool make_room_for_the_copies(unsigned screens, bool again)
{
    CopyRoom was = copy_room;
    copy_room = (CopyRoom){ 0 };
    /* made between layouts only where the caches keep room to spare */
    unsigned free = 0;
    for (uint32_t place = 0; !again && place < PLACES; place++)
        free += !is_taken(place);
    if (!again && free < COPIES * (screens * (SCREEN_BYTES / 32u) + COPY_TILES + 1) + STRIP_TILES + COPY_SPARE) {
        copy_room.missed = true;
        return false;
    }
    for (unsigned k = 0; k < COPIES; k++) {
        uint32_t map = was.map_block[k] * (SCREEN_BYTES / 32u), places = screens * (SCREEN_BYTES / 32u);
        int32_t block = again && room_at(map, places) ? (take_room(map, places), (int32_t)map)
                                                      : room_from_the_top(places, places);
        int32_t run = again && room_at(was.run[k], COPY_TILES + 1) ? (take_room(was.run[k], COPY_TILES + 1), (int32_t)was.run[k])
                                                                    : run_from_the_top(COPY_TILES + 1);
        if (block < 0 || run < 0) {
            copy_room.missed = true;
            return false;   /* what it took stays taken until the next layout */
        }
        copy_room.map_block[k] = (uint8_t)(block * 32 / SCREEN_BYTES);
        copy_room.run[k] = (uint16_t)run;
        for (unsigned w = 0; w < 8; w++)
            BG_VRAM[(uint32_t)run * 8u + w] = 0;
    }
    int32_t at = again && room_at(was.strips, STRIP_TILES) ? (take_room(was.strips, STRIP_TILES), (int32_t)was.strips)
                                                           : room_from_the_top(STRIP_TILES, RUN_STEP);
    if (at < 0) {
        copy_room.missed = true;
        return false;
    }
    copy_room.strips = (uint16_t)at;
    copy_room.screens = (uint8_t)screens;
    copy_room.made = true;
    strips.made = false;
    return true;
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
/* The table the frame shown reads its plane's lines from: the last frame's
 * stays where it is until this one is shown, though a layout gave its room
 * back (the title's pendulum, a part of the plane, fell apart through the
 * layout that left Mode 7). */
static volatile void *shown_plane_table;
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
    /* the table the frame shown reads its plane's lines from stays taken
     * while it is shown: a cache's new tiles there would be read as lines */
    if (!plane_table && shown_plane_table) {
        uint32_t at = (uint32_t)((const volatile uint8_t *)shown_plane_table - (const volatile uint8_t *)BG_VRAM) / 32u;
        if (room_at(at, PLANE_TABLE_PLACES))
            take_room(at, PLANE_TABLE_PLACES);
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

/* The places free from `first` up to `end` (both multiples of 32), counted
 * a word of the bits at a time: a layout counts a window's free places a
 * dozen times over, and a place at a time that was half the work of the
 * layouts at a scene's start. */
static unsigned free_places(uint32_t first, uint32_t end)
{
    unsigned used = 0;
    for (uint32_t k = first / 32; k < end / 32; k++)
        used += bits_set(((const Word *)taken)[k]);
    return (unsigned)(end - first) - used;
}

/* The places free in the cache window from char block `block`, and the
 * window with the most. */
static unsigned free_in_window(uint8_t block)
{
    uint32_t first = block * 512u, end = first + CACHE_TILES < PLACES ? first + CACHE_TILES : PLACES;
    return free_places(first, end);
}

static uint8_t roomiest_window(void)
{
    uint8_t best = 0;
    unsigned most = 0;
    for (uint8_t block = 0; block < 3; block++) {
        unsigned free = free_in_window(block);
        if (free > most)
            most = free, best = block;
    }
    return best;
}

/* The cache for a layer's tiles: one this layout has for the same tiles, or
 * the last layout had (`had`, a bit each), or one free, or one the last
 * layout had for others; CACHES where all three are taken. */
static unsigned cache_for(const Layer *l, uint8_t had)
{
    for (unsigned c = 0; c < CACHES; c++)
        if ((caches[c].used || had >> c & 1) && caches[c].bpp == l->bpp && caches[c].char_byte == l->char_byte)
            return c;
    for (unsigned c = 0; c < CACHES; c++)
        if (!caches[c].used && !(had >> c & 1))
            return c;
    for (unsigned c = 0; c < CACHES; c++)
        if (!caches[c].used)
            return c;
    return CACHES;
}

/* The tiles a map `bytes` long from `from` names, into `bits` (a bit each),
 * a word (two entries) at a time: maps start on a screen's 2 KiB. */
ARM_CODE __attribute__((noinline)) static void name_map_tiles(uint32_t *bits, uint32_t from, uint32_t bytes)
{
    const Word *words = (const Word *)snes.vram;
    for (uint32_t i = 0; i < bytes; i += 4) {
        uint32_t pair = words[((from + i) & (VRAM_BYTES - 1)) / 4];
        unsigned low = pair & (CACHE_TILES - 1), high = pair >> 16 & (CACHE_TILES - 1);
        bits[low / 32] |= 1u << (low % 32);
        bits[high / 32] |= 1u << (high % 32);
    }
}

/* The tiles each cache's maps name (the layers' own maps and the line
 * effects' that the layout keeps), a bit each; and the places in each
 * cache's window the drawn panel's maps name (`placed`, a bit each), whose
 * tiles the panel sent no map of the screen's need name. */
static void name_the_tiles(uint32_t named[CACHES][CACHE_TILES / 32], uint32_t placed[CACHES][CACHE_TILES / 32])
{
    memset(named, 0, sizeof(uint32_t) * CACHES * (CACHE_TILES / 32));
    memset(placed, 0, sizeof(uint32_t) * CACHES * (CACHE_TILES / 32));
    for (unsigned k = 0; panel_room.made && k < PANEL_LAYERS; k++) {
        unsigned n = panel.layer[k], c = 0;
        if (n == NO_LAYER || (panel.own_tiles >> n & 1) || !(panel_room.mapped >> n & 1))
            continue;
        while (c < CACHES && !(caches[c].used && caches[c].bpp == layers[n].bpp && caches[c].char_byte == panel.char_byte[k]))
            c++;
        const volatile uint16_t *map = (const volatile uint16_t *)BG_VRAM + panel_room.map_block[k] * (SCREEN_BYTES / 2);
        for (unsigned e = 0; c < CACHES && e < SCREEN_BYTES / 2; e++) {
            unsigned place = map[e] & (CACHE_TILES - 1);
            placed[c][place / 32] |= 1u << (place % 32);
        }
    }
    for (unsigned k = 0; k < 4u + snes.line_map_count; k++) {
        const LineMap *lm = k < 4 ? NULL : &snes.line_maps[k - 4];
        const Layer *l = &layers[lm ? lm->layer : k];
        if (!l->shown || (!lm && !l->own_map))
            continue;
        uint32_t from = lm ? lm->byte : l->map_byte, bytes = lm ? lm->bytes : l->map_bytes;
        name_map_tiles(named[l->cache], from, bytes);
    }
}

/* A cache the last layout had for the same tiles keeps its window and the
 * places of the tiles its maps still name, but those this layout's maps and
 * rooms took, so that what the GBA shows stays as it was while the
 * layout's maps are converted again, frames of work. A cache started
 * afresh handed the tiles new places the maps still on screen didn't name
 * yet, which showed the room in stray tiles as a dialog opened and each
 * time the caches filled; the tiles no map names give their places back as
 * starting afresh did. Whether a tile a map still names lost its place to
 * one of the layout's maps or rooms: until the maps are converted again,
 * what the GBA shows there is that map's bytes, not the tile. */
static bool keep_places(Cache *c, const uint32_t named[CACHE_TILES / 32], const uint32_t placed[CACHE_TILES / 32])
{
    uint32_t first = c->block * 512u;
    bool lost = false;
    c->next = c->placed = 0;
    for (unsigned t = 0; t < CACHE_TILES; t++) {
        if (t % 2 == 0 && *(const Word *)&c->slot_of[t] == 0xFFFFFFFFu) {
            t++;   /* two tiles with no place */
            continue;
        }
        uint16_t slot = c->slot_of[t];
        if (slot == NO_SLOT)
            continue;
        bool needed = (named[t / 32] >> (t % 32) & 1) || (placed[slot / 32] >> (slot % 32) & 1);
        if (!needed || first + slot >= PLACES || is_taken(first + slot)) {
            lost |= needed;
            c->slot_of[t] = NO_SLOT;
        } else {
            take(first + slot);
            c->placed++;
        }
    }
    if (first + c->blank < PLACES && !is_taken(first + c->blank)) {
        take(first + c->blank);
        return lost;
    }
    c->blank = take_slot(c);
    if (c->blank == NO_SLOT)
        c->blank = 0;
    else
        for (unsigned w = 0; w < 8; w++)
            BG_VRAM[(first + c->blank) * 8u + w] = 0;
    return lost;
}

/* The maps the frame's lines showed since the layout (layer_control): each
 * layer's own (bit n) and each line map (bit LINE_MAPS_SHOWN + m). */
enum { LINE_MAPS_SHOWN = 4 };
static uint16_t maps_shown IN_FAST_RAM;
_Static_assert(LINE_MAPS_SHOWN + LINE_MAPS <= 16, "a bit of maps_shown each");

/* Whether background VRAM has room left for the copies of tiles in front
 * that the line effects' maps of layers split by them alone would take,
 * beside a place for each tile the maps name: those copies took the tile
 * caches' room in Crono's house while Mom's dialog held the panel's, and
 * the room showed holes where tiles found no place. */
static bool line_splits_fit(uint8_t had)
{
    uint32_t named[CACHES][CACHE_TILES / 32];
    memset(named, 0, sizeof named);
    unsigned copies = 0, needed = 0, free = 0;
    for (unsigned k = 0; k < 4u + snes.line_map_count; k++) {
        const LineMap *lm = k < 4 ? NULL : &snes.line_maps[k - 4];
        const Layer *l = &layers[lm ? lm->layer : k];
        if (!l->shown || (!lm && !l->own_map) || (lm && lm->cut != NO_CUT))
            continue;
        uint32_t from = lm ? lm->byte : l->map_byte, bytes = lm ? lm->bytes : l->map_bytes;
        if (lm && lm->gba_byte != NO_PLACE && !lm->shared && (split_wanted >> lm->layer & 1) && !l->own_map &&
            has_tiles_in_front(lm->byte, lm->bytes))
            copies += bytes / 32;
        name_map_tiles(named[l->cache], from, bytes);
    }
    free = free_places(0, PLACES);
    /* each cache's tiles in its window (the one it had, or the roomiest),
     * the copies going low into the first; and all of them in what is free */
    for (unsigned c = 0; c < CACHES; c++) {
        unsigned own = 0;
        for (unsigned w = 0; w < CACHE_TILES / 32; w++)
            own += bits_set(named[c][w]);
        needed += own;
        if (!caches[c].used)
            continue;
        uint8_t block = had >> c & 1 ? caches[c].block : roomiest_window();
        unsigned in_window = free_in_window(block);
        in_window = block == 0 ? (in_window > copies ? in_window - copies : 0) : in_window;
        if (own > in_window)
            return false;
    }
    return free >= needed + copies;
}

/* Whether layers are split in their line effects' maps alone: decided by
 * the first layout of a scene (its layers' tiles and maps) and kept while
 * the scene lasts. Decided afresh at each layout, the split
 * came and went as a dialog's panel took room and gave it back, each time
 * moving what the last frame's lines showed. */
static uint32_t line_split_scene;
static bool line_split_ok;

static bool line_splits_in_this_scene(uint8_t had)
{
    uint32_t scene = 1;
    for (unsigned n = 0; n < 4; n++)
        scene = (scene * 31 + layers[n].char_byte) * 31 + (layers[n].shown ? layers[n].map_byte : 0xFFFFu);
    if (scene != line_split_scene) {
        line_split_scene = scene;
        line_split_ok = line_splits_fit(had);
    }
    return line_split_ok;
}

static void note_map_priorities(unsigned n, uint32_t map_byte, uint32_t bytes, bool added);

/* The places window `block` has free for cache `c`'s tiles once the tiles
 * the caches in `placed` (a bit each) have yet to place (`needed`) are
 * counted out of it where their windows share places with it: a window is
 * 1,024 places from its char block, and the next starts 512 on. */
static int32_t room_beside(unsigned c, uint8_t block, uint8_t placed, const unsigned needed[CACHES])
{
    int32_t room = (int32_t)free_in_window(block);
    for (unsigned d = 0; d < CACHES; d++)
        if (d != c && (placed >> d & 1) && caches[d].used && caches[d].block + 1 >= block && caches[d].block <= block + 1)
            room -= (int32_t)needed[d];
    return room;
}

/* The window for a cache started afresh beside the caches in `placed`:
 * the one with the most places free, where its tiles fit there beside
 * theirs, or else the one with the most room left beside them. Two caches
 * started afresh in the window with the most free took their places from
 * it both, and where it couldn't hold both one's tiles missed: the floor of
 * the lab before the jet bike race went black in sixteen-dot squares as
 * each of Johnny's dialogs opened, the dialog's map naming its own tiles
 * beside the floor's while its window grew (issue #589). */
static uint8_t window_for(unsigned c, uint8_t placed, const unsigned needed[CACHES])
{
    uint8_t best = roomiest_window();
    int32_t most = room_beside(c, best, placed, needed);
    if (most >= (int32_t)needed[c])
        return best;
    for (uint8_t block = 0; block < 3; block++) {
        int32_t room = room_beside(c, block, placed, needed);
        if (room >= (int32_t)needed[c] && room > most)
            most = room, best = block;
    }
    return best;
}

/* Whether cache `c`, which the last layout had, can't place its tiles in
 * its window beside those the other caches it had (`had`) have yet to
 * place there (`needed`: a cache that kept its places has them taken
 * already and counts the rest), where another window has more room. */
static bool crowded(unsigned c, uint8_t had, const unsigned needed[CACHES])
{
    uint8_t others = (uint8_t)(had & ~(1u << c));
    return room_beside(c, caches[c].block, others, needed) < (int32_t)needed[c] &&
           window_for(c, others, needed) != caches[c].block;
}

/* The caches the layout uses that keep no places (their bit clear in
 * `had`) started afresh, the one with the most tiles to place first, each
 * with its blank, in the window window_for finds beside those before it. */
static void start_the_caches_afresh(uint8_t had, const unsigned needed[CACHES])
{
    uint8_t placed = 0;
    for (unsigned c = 0; c < CACHES; c++)
        if (caches[c].used && had >> c & 1)
            placed |= (uint8_t)(1u << c);
    for (;;) {
        unsigned c = CACHES;
        for (unsigned d = 0; d < CACHES; d++)
            if (caches[d].used && !(placed >> d & 1) && (c == CACHES || needed[d] > needed[c]))
                c = d;
        if (c == CACHES)
            return;
        Cache *cache = &caches[c];
        cache->next = cache->placed = 0;
        cache->block = window_for(c, placed, needed);
        memset(cache->slot_of, 0xFF, sizeof cache->slot_of);
        cache->blank = take_slot(cache);
        if (cache->blank == NO_SLOT)
            cache->blank = 0;
        else
            for (unsigned w = 0; w < 8; w++)
                BG_VRAM[(cache->block * 512u + cache->blank) * 8u + w] = 0;
        placed |= (uint8_t)(1u << c);
    }
}

/* The layers mode 1, mode 2 and mode 0 have (Mode 7's aren't shown yet), the
 * places their maps take, the layers split by priority with their copies of
 * tiles in front, and their tiles' caches, each with the window the most
 * places are free in. Where `pruned`, the maps the lines haven't shown
 * since the last layout are left out, their places and the tiles they name
 * given to the rest; a map left out that a line shows again is noted, and
 * the next layout has it. Where it `keeps_windows`, it is laid out only for
 * tiles that found no place mid-scene, or to give the plane's room back. */

static uint8_t lay_out(bool pruned, bool keeps_windows)
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
    /* the caches the last layout had, a bit each */
    uint8_t had = 0, lost = 0;   /* and those that lost a place a map names (keep_places) */
    uint8_t held[PLACES / 8];       /* the places their tiles had, a bit each, where `low` needs them */
    uint16_t low = 0;               /* the line maps whose copies of tiles in front go low (place_copy_low) */
    for (unsigned c = 0; c < CACHES; c++) {
        if (caches[c].used)
            had |= (uint8_t)(1u << c);
        caches[c].used = false;
    }
    uint8_t had_places = had;   /* the caches whose places `held` is made from */
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
        unsigned c = cache_for(l, had);
        if (c == CACHES)
            continue;   /* mode 0's fourth set of tiles: not shown yet */
        if (!caches[c].used && !(caches[c].bpp == l->bpp && caches[c].char_byte == l->char_byte))
            had &= (uint8_t)~(1u << c);
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
        if (layers[n].shown && layers[n].own_map)
            note_map_priorities(n, layers[n].map_byte, layers[n].map_bytes, false);
    for (unsigned m = 0; m < snes.line_map_count; m++) {
        const LineMap *lm = &snes.line_maps[m];
        if (lm->cut == NO_CUT && layers[lm->layer].shown)
            note_map_priorities(lm->layer, lm->byte, lm->bytes, true);
    }
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
        lm->shared = lm->front_refused = false;
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
    if (copy_room.asked && copy_room.made)
        make_room_for_the_copies(copy_room.screens, true);
    else
        copy_room = (CopyRoom){ 0 };
    copied.made = strips.made = false;   /* the maps and tiles they were made from may have moved */
    memset(front_block, NO_BLOCK, sizeof front_block);
    bool line_splits = line_splits_in_this_scene(had);
    for (unsigned n = 0; n < 4; n++) {
        Layer *l = &layers[n];
        bool split = split_wanted >> n & 1, overlay = split_wanted >> (n + OVERLAY_SHIFT) & 1;
        l->front_byte = (split || overlay) && l->own_map ? place_copy(l->map_bytes) : NO_PLACE;
        l->overlaid = overlay && l->front_byte != NO_PLACE;
        /* a layer whose own map no line shows is split in its line effects'
         * maps alone (below), where there is room */
        l->split = split && (l->front_byte != NO_PLACE || (!l->own_map && line_splits));
        if (l->split && l->front_byte != NO_PLACE)
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
        else if (!layers[lm->layer].own_map)
            low |= (uint16_t)(1u << m);   /* placed once the caches have kept their places */
        else if (has_tiles_in_front(lm->byte, lm->bytes)) {
            if ((lm->front_byte = place_copy(lm->bytes)) != NO_PLACE)
                front_block[lm->gba_byte / SCREEN_BYTES] = (uint8_t)(lm->front_byte / SCREEN_BYTES);
            else
                lm->front_refused = true;
        }
    }
    /* The places the last layout's tiles had, before the caches keep theirs:
     * they stay in their windows until then. */
    if (low) {
        memset(held, 0, sizeof held);
        for (unsigned c = 0; c < CACHES; c++)
            for (unsigned t = 0; had_places >> c & 1 && t < CACHE_TILES; t++)
                if (caches[c].slot_of[t] != NO_SLOT && caches[c].block * 512u + caches[c].slot_of[t] < PLACES) {
                    uint32_t p = caches[c].block * 512u + caches[c].slot_of[t];
                    held[p / 8] |= (uint8_t)(1u << (p % 8));
                }
    }
    /* The caches the last layout had keep their windows and their tiles'
     * places (keep_places), but where the rooms made took so much of a
     * window that another has more places free: that cache starts afresh
     * there, as it would have. A layout that `keeps_windows` keeps a cache
     * where its maps' tiles and a blank still fit: starting afresh hides the
     * picture while its tiles are converted again (the attract sequence's
     * battle blinked black where a screen's map took 64 of its window's
     * places and another window had all 1,024, and the title as the plane's
     * room was given back), and only where a window can hold its tiles
     * beside the other caches': moved where none can, its tiles missed as
     * before and the picture was hidden as well. A cache none of whose
     * tiles has a place yet starts afresh where its window can't hold them
     * beside the other caches' and another window has more room (crowded). */
    unsigned needed[CACHES] = { 0 };   /* each cache's tiles its maps name that have no place yet */
    {
        uint32_t named[CACHES][CACHE_TILES / 32];
        uint32_t placed[CACHES][CACHE_TILES / 32];
        name_the_tiles(named, placed);
        for (unsigned c = 0; c < CACHES; c++)
            for (unsigned w = 0; caches[c].used && w < CACHE_TILES / 32; w++)
                needed[c] += bits_set(named[c][w]);
        for (unsigned c = 0; c < CACHES; c++) {
            if (!caches[c].used || !(had >> c & 1))
                continue;
            unsigned here = free_in_window(caches[c].block);
            uint8_t others = (uint8_t)(had & ~(1u << c));
            bool fits = room_beside(c, window_for(c, others, needed), others, needed) >= (int32_t)needed[c];
            if (((!keeps_windows || needed[c] >= here) && here < free_in_window(roomiest_window()) && fits) ||
                (!caches[c].placed && crowded(c, had, needed)))
                had &= (uint8_t)~(1u << c);
            else if (keep_places(&caches[c], named[c], placed[c]))
                lost |= (uint8_t)(1u << c);
            unsigned kept_tiles = had >> c & 1 ? caches[c].placed : 0;
            needed[c] = needed[c] > kept_tiles ? needed[c] - kept_tiles : 0;
        }
    }
    for (unsigned m = 0; m < snes.line_map_count; m++) {
        LineMap *lm = &snes.line_maps[m];
        if (!(low >> m & 1) || !has_tiles_in_front(lm->byte, lm->bytes))
            continue;
        if ((lm->front_byte = place_copy_low(lm->bytes, held)) != NO_PLACE)
            front_block[lm->gba_byte / SCREEN_BYTES] = (uint8_t)(lm->front_byte / SCREEN_BYTES);
        else
            lm->front_refused = true;
    }
    start_the_caches_afresh(had, needed);
    uint8_t kept = 0;
    for (unsigned c = 0; c < CACHES; c++)
        if (caches[c].used && had >> c & 1 && !(lost >> c & 1))
            kept |= (uint8_t)(1u << c);
    return kept;
}

/* Where a layout put a map (a layer's own or a line effect's), and the
 * window and blank of the cache whose tiles it names: a map whose place
 * and cache are as the last layout had them, the cache having kept its
 * tiles' places, is in the GBA's VRAM as the layout would convert it. */
enum { OWN_MAP = 1, SPLIT = 2, OVERLAID = 4, SHARED = 8, DEEP = 16, MAP_PLACES = 4 + sizeof snes.line_maps / sizeof snes.line_maps[0] };
typedef struct {
    uint16_t byte, bytes, gba_byte, front_byte;
    uint8_t layer, cut, kind;   /* kind: OWN_MAP, SPLIT, OVERLAID, SHARED and DEEP (4bpp), a bit each */
    uint8_t block;              /* its cache's window */
    uint16_t char_byte, blank;  /* its cache's tiles and blank's place */
} MapPlace;
static MapPlace map_places[MAP_PLACES];
static uint8_t map_place_count;

static unsigned places_of_the_maps(MapPlace out[MAP_PLACES])
{
    unsigned count = 0;
    for (unsigned k = 0; k < MAP_PLACES; k++) {
        const LineMap *lm = k < 4 ? NULL : k - 4 < snes.line_map_count ? &snes.line_maps[k - 4] : NULL;
        if (k >= 4 && !lm)
            break;
        const Layer *l = &layers[lm ? lm->layer : k];
        if (!l->shown || (lm ? lm->gba_byte == NO_PLACE : !l->own_map))
            continue;
        const Cache *c = &caches[l->cache];
        out[count++] = (MapPlace){
            .byte = lm ? lm->byte : l->map_byte, .bytes = lm ? lm->bytes : l->map_bytes,
            .gba_byte = lm ? lm->gba_byte : l->map_byte, .front_byte = lm ? lm->front_byte : l->front_byte,
            .layer = lm ? lm->layer : (uint8_t)k, .cut = lm ? lm->cut : NO_CUT,
            .kind = (uint8_t)((lm ? 0 : OWN_MAP) | (l->split ? SPLIT : 0) | (l->overlaid ? OVERLAID : 0) |
                              (lm && lm->shared ? SHARED : 0) | (l->bpp == 4 ? DEEP : 0)),
            .block = c->block, .char_byte = c->char_byte, .blank = c->blank };
    }
    return count;
}

/* What a layout leaves to convert: the maps it put somewhere else than the
 * last did, or whose cache started afresh or lost a place its maps name
 * (`kept`, the caches that kept theirs, a bit each), or all of them where
 * tiles had found no place (`missed`), which they may find now; the
 * sprites' tiles where their tables moved (the GBA's sprite VRAM holds them
 * as the game sent them, each change converted as it comes, while they
 * stay where they are: a scene's first picture converted all 16 KiB of them
 * again), and the plane's VRAM. The caches' tiles are converted as the maps
 * name them: those of a cache kept are where they were, and one started
 * afresh names none yet. A layout converted all of VRAM, tiles and all: ten
 * GBA frames and more of work, through which the GBA showed the last
 * frame's lines over maps half converted; then every unit of each cache
 * started afresh too, 1,024 a cache that named nothing, a sixth of a
 * scene's first picture's work (the black as Crono's room's curtain opens);
 * then every map again where a line effect's map came in beside them,
 * three GBA frames as the opening's narration and each dialog of a scene
 * opened. */
static void layout_changed(uint8_t kept, bool missed)
{
    static uint32_t sprites_from = NO_SPRITE_TABLES;   /* the tables the sprites' tiles were converted from */
    uint32_t tables = sprite_table(0) | sprite_table(1) << 16;
    uint8_t roles_converted = tables != sprites_from ? SPRITE_TILES : 0;
    sprites_from = tables;
    for (unsigned k = 0; k < BLOCKS; k++) {
        uint32_t byte = (uint32_t)k * BLOCK_BYTES;
        bool convert = (roles[k] & roles_converted) || (plane_room && byte < PLANE_BYTES);
        if (convert)   /* a block's 16 units, half a word of the bits */
            snes.changed[k / 2] |= 0xFFFFu << (k % 2 * 16);
    }
    MapPlace now[MAP_PLACES];
    unsigned count = places_of_the_maps(now);
    for (unsigned i = 0; i < count; i++) {
        bool where_it_was = false;
        for (unsigned j = 0; !missed && kept >> layers[now[i].layer].cache & 1 && j < map_place_count; j++)
            where_it_was |= memcmp(&now[i], &map_places[j], sizeof now[i]) == 0;
        for (uint32_t b = 0; !where_it_was && b < now[i].bytes; b += UNIT_BYTES)
            unit_changed((now[i].byte + b) & (VRAM_BYTES - 1));
    }
    memcpy(map_places, now, sizeof now[0] * count);
    map_place_count = (uint8_t)count;
}

/* A SNES map entry as the GBA's, its tiles in cache `c`: its tile's place
 * in the cache (given one where it has none), its flips, and its palette,
 * whose number a 2bpp layer's tiles take from 8 on (`high`, the top bit
 * of the GBA's palette number). The SNES's flips are the GBA's two bits up
 * and its palette's three bits two up. */
enum { SNES_FLIPS = 0xC000, SNES_PALETTE = 0x1C00, HIGH_PALETTES = 0x8000 };
static inline __attribute__((always_inline)) uint32_t entry_in(Cache *c, uint32_t e, uint32_t high)
{
    uint32_t slot = c->slot_of[e & (CACHE_TILES - 1)];
    if (slot == NO_SLOT)
        slot = slot_for(c, (uint16_t)(e & (CACHE_TILES - 1)));
    return slot | (e & SNES_FLIPS) >> 4 | (e & SNES_PALETTE) << 2 | high;
}

static uint32_t high_palettes(const Layer *l) { return l->bpp == 2 ? HIGH_PALETTES : 0; }

/* A map entry as the GBA's, for a layer. */
static uint16_t map_entry(uint16_t e, const Layer *l)
{
    return (uint16_t)entry_in(&caches[l->cache], e, high_palettes(l));
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
 * blank in the other. A scene's first picture converts thousands of
 * entries, most of whose tiles have their places by then: this runs from
 * fast RAM, an entry's place looked up where it is kept. */
ARM_CODE __attribute__((noinline, noclone)) static void convert_map(uint32_t first, const Layer *l, uint32_t to, uint32_t front_to)
{
    Cache *c = &caches[l->cache];
    const Halfword *entries = (const Halfword *)&snes.vram[first];
    uint32_t high = high_palettes(l), blank = c->blank;
    volatile uint16_t *back = (volatile uint16_t *)BG_VRAM + to / 2, *front = (volatile uint16_t *)BG_VRAM + front_to / 2;
    for (unsigned i = 0; i < UNIT_BYTES / 2; i++) {
        uint32_t e = entries[i], entry = entry_in(c, e, high);
        if (front_to == NO_PLACE) {
            back[i] = (uint16_t)entry;
        } else {
            bool in_front = (e & PRIORITY_BIT) != 0;
            back[i] = (uint16_t)(in_front ? blank : entry);
            front[i] = (uint16_t)(in_front ? entry : blank);
        }
    }
}

/* A unit of a map's tiles in front alone, converted for a layer into the
 * GBA's VRAM at `front_to`, the others blank and given no place: a shared
 * map's own copy of them, and a layer's shown again (overlaid). Few, from
 * the cartridge. */
__attribute__((noinline)) static void convert_front(uint32_t first, const Layer *l, uint32_t front_to)
{
    Cache *c = &caches[l->cache];
    const Halfword *entries = (const Halfword *)&snes.vram[first];
    volatile uint16_t *front = (volatile uint16_t *)BG_VRAM + front_to / 2;
    for (unsigned i = 0; i < UNIT_BYTES / 2; i++)
        front[i] = (uint16_t)(entries[i] & PRIORITY_BIT ? entry_in(c, entries[i], high_palettes(l)) : c->blank);
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

/* A sprite tile's number among the SNES's two tables' 512, or -1. */
static int sprite_tile(uint32_t byte)
{
    uint32_t obj = sprite_table(0), second = sprite_table(1);
    int table = within(byte, obj, OBJ_TABLE_BYTES) ? 0 : within(byte, second, OBJ_TABLE_BYTES) ? 1 : -1;
    if (table < 0)
        return -1;
    return table * 256 + (int)(((byte - (table ? second : obj)) & (VRAM_BYTES - 1)) / 32);
}

/* The sprites' tiles on two pages of the GBA's sprite VRAM, the shown
 * sprites naming one while the next picture's are converted onto the
 * other, so that a picture shown a GBA frame late (show_the_frame) never
 * has the last frame's sprites in the next frame's tiles: an object whose
 * frame changed showed its pieces moved about for a GBA frame (issue
 * #550). On a page the SNES's two tables of 16 tiles a row lie one under the
 * other, the first page in the first 16 of the GBA's 32 a row and the
 * second in the last 16. The tiles that changed are converted onto the page
 * not shown, with those the shown page took last (they changed while it
 * was the page not shown), and the sprites then name that page. */
enum { SPRITE_TILES_BOTH = 2 * 256, PAGE_COLUMNS = 16 };
static uint32_t tiles_changed[SPRITE_TILES_BOTH / 32];   /* since the last turn, a bit each */
static uint32_t tiles_behind[SPRITE_TILES_BOTH / 32];    /* that the page not shown lacks */
static uint8_t sprite_page;                              /* the page the sprites name */

static void sprite_tile_changed(int tile) { tiles_changed[tile / 32] |= 1u << (tile % 32); }

/* The tiles that changed onto the page not shown, which the sprites then
 * name. False where none changed. */
static bool turn_the_sprite_page(void)
{
    uint32_t any = 0;
    for (unsigned w = 0; w < SPRITE_TILES_BOTH / 32; w++)
        any |= tiles_changed[w];
    if (!any)
        return false;
    sprite_page ^= 1;
    for (unsigned w = 0; w < SPRITE_TILES_BOTH / 32; w++) {
        uint32_t bits = tiles_changed[w] | tiles_behind[w];
        tiles_behind[w] = tiles_changed[w];
        tiles_changed[w] = 0;
        for (unsigned b = 0; bits; b++, bits >>= 1) {
            if (!(bits & 1))
                continue;
            unsigned tile = w * 32 + b;
            uint32_t byte = (sprite_table(tile / 256) + (tile % 256) * 32) & (VRAM_BYTES - 1);
            unsigned place = (tile / PAGE_COLUMNS) * 32 + sprite_page * PAGE_COLUMNS + tile % PAGE_COLUMNS;
            convert_4bpp(OBJ_VRAM + place * 8, &snes.vram[byte]);
        }
    }
    return true;
}

/* A map's units in a block (`units`, a bit each, from `first`), each with
 * its priorities noted for layer `n` (note_priorities'; none where n is
 * past the fourth) and, where `l` is a layer, converted for it into `to`
 * and `front_to` as convert_map does (or its tiles in front alone, where
 * `to` is NO_PLACE: convert_front), each place stepping with the units.
 * From fast RAM, the units one after another: a call there and back from
 * the cartridge for each unit of a scene's maps cost more than its work.
 * Its loop is kept whole rather than copied for each kind of call, which
 * fast RAM has no room for. */
ARM_CODE __attribute__((noinline, noclone, optimize("no-unswitch-loops")))
static void convert_map_units(uint32_t first, uint32_t units, unsigned n, uint32_t map_byte, bool added, const Layer *l,
                              uint32_t to, uint32_t front_to)
{
#pragma GCC unroll 1
    for (; units; units >>= 1, first += UNIT_BYTES) {
        if (units & 1) {
            if (n < 4)
                note_priorities(n, first, map_byte, added);
            if (l && to == NO_PLACE)
                convert_front(first, l, front_to);
            else if (l)
                convert_map(first, l, to, front_to);
        }
        if (to != NO_PLACE)
            to += UNIT_BYTES;
        if (front_to != NO_PLACE)
            front_to += UNIT_BYTES;
    }
}

/* A map's units' priorities noted for layer `n` (its own map, or a line
 * effect's `added` to it), a kilobyte of the map at a time. A map starts on
 * a screen's 2 KiB, so no kilobyte runs past VRAM's end. */
static void note_map_priorities(unsigned n, uint32_t map_byte, uint32_t bytes, bool added)
{
    for (uint32_t at = 0; at < bytes; at += 32 * UNIT_BYTES) {
        uint32_t units = bytes - at >= 32 * UNIT_BYTES ? 0xFFFFFFFFu : (1u << (bytes - at) / UNIT_BYTES) - 1;
        convert_map_units((map_byte + at) & (VRAM_BYTES - 1), units, n, map_byte, added, NULL, NO_PLACE, NO_PLACE);
    }
}

/* The units of a block of VRAM the game changed (`units`, a bit each, the
 * first unit's lowest), each as what the settings make it. What a block is
 * for (the maps, line maps and caches it holds; every one starts on a
 * block) is worked out once for its 16 units: a scene's first picture
 * converts some thousand units, and working it out for each was a fifth of
 * that picture's work. Its maps' units are converted a map at a time,
 * naming their tiles, before its own tiles are converted again: the places
 * a map's tiles are given may follow another order than a unit at a time
 * did, and the picture is the same. */
static void convert_block(unsigned k, uint32_t units)
{
    uint32_t block = (uint32_t)k * BLOCK_BYTES;
    uint8_t role = roles[k];
    /* mode 2's columns' offsets, and a layer's columns copied from a map:
     * made again where their maps changed */
    bool offsets = laid_out_mode == 2 && within(block, (uint32_t)snes.bg[2].map_base << 1, map_bytes(&snes.bg[2]));
    bool copied_from = copied.made && within(block, copied.map_byte, copied.map_bytes);
    bool plane = plane_room && block < PLANE_BYTES;
    const LineMap *shown_maps[sizeof snes.line_maps / sizeof snes.line_maps[0]];
    unsigned line_maps = 0;
    for (unsigned m = 0; role & LINE_MAP && m < snes.line_map_count; m++) {
        const LineMap *lm = &snes.line_maps[m];
        if (lm->gba_byte != NO_PLACE && layers[lm->layer].shown && within(block, lm->byte, lm->bytes))
            shown_maps[line_maps++] = lm;
    }
    uint8_t own_maps = 0;   /* the layers whose own map it is, a bit each */
    for (unsigned n = 0; role & MAP && n < 4; n++)
        if (layers[n].shown && layers[n].own_map && within(block, layers[n].map_byte, layers[n].map_bytes))
            own_maps |= (uint8_t)(1u << n);
    const Layer *owner = role & MAP ? map_layer(block) : NULL;
    /* the caches whose tiles it holds, a bit each, but those none of whose
     * tiles have places yet (a cache started afresh, at a scene's first
     * picture): a tile given its place in this picture was converted then */
    uint8_t tiled = 0;
    uint16_t first_tile[CACHES] = { 0 };   /* and each one's first tile there */
    for (unsigned c = 0; role & TILES && c < CACHES; c++)
        if (caches[c].used && caches[c].placed && within(block, caches[c].char_byte, CACHE_TILES * tile_bytes(caches[c].bpp))) {
            tiled |= (uint8_t)(1u << c);
            first_tile[c] = (uint16_t)(((block - caches[c].char_byte) & (VRAM_BYTES - 1)) >> tile_shift(caches[c].bpp));
        }
    int sprites = role & SPRITE_TILES ? sprite_tile(block) : -1;   /* its first sprite tile */
    if (!line_maps && !owner && !tiled && sprites < 0 && !plane && !offsets && !copied_from)
        return;   /* tiles none of which has a place yet, as a scene's are as it loads */
    for (unsigned j = 0; j < line_maps; j++) {
        const LineMap *lm = shown_maps[j];
        uint32_t offset = (block - lm->byte) & (VRAM_BYTES - 1);
        if (lm->cut != NO_CUT) {
            for (unsigned i = 0; i < 16; i++)
                if (units >> i & 1)
                    convert_cut_row(lm, (offset + i * UNIT_BYTES) / 64);
            continue;
        }
        /* a shared copy is its owner's, converted as its map */
        bool front_shared = lm->shared && map_layer(block)->split;
        bool own_copy = !lm->shared || (lm->front_byte != NO_PLACE && !front_shared);
        convert_map_units(block, units, lm->layer, lm->byte, true, own_copy ? &layers[lm->layer] : NULL,
                          lm->shared ? NO_PLACE : lm->gba_byte + offset,
                          lm->front_byte != NO_PLACE && !front_shared ? lm->front_byte + offset : NO_PLACE);
        /* tiles in front where its split layer had none: the next layout
         * gives them their copy, where this one had room for it (laid out
         * again each frame where it hadn't, the battle in Gato's movie went
         * black a dozen GBA frames a frame) */
        for (unsigned i = 0; lm->front_byte == NO_PLACE && !lm->front_refused && layers[lm->layer].split && i < 16; i++)
            if (units >> i & 1 && has_tiles_in_front(block + i * UNIT_BYTES, UNIT_BYTES))
                snes.settings_changed = true;
    }
    if (owner) {
        for (unsigned n = 0; n < 4; n++)
            if (own_maps >> n & 1)
                convert_map_units(block, units, n, layers[n].map_byte, false, NULL, NO_PLACE, NO_PLACE);
        uint32_t front = owner->front_byte + ((block - owner->map_byte) & (VRAM_BYTES - 1));
        convert_map_units(block, units, 4, 0, false, owner, block, owner->split ? front : NO_PLACE);

        if (owner->overlaid)
            convert_map_units(block, units, 4, 0, false, owner, NO_PLACE, front);
    }
    for (unsigned i = 0; units; i++, units >>= 1) {
        if (!(units & 1))
            continue;
        uint32_t first = block + i * UNIT_BYTES;
        offsets_written |= offsets;
        if (plane)
            convert_plane_unit(first);
        if (copied_from)
            copied.stale = offsets_written = true;
        for (unsigned c = 0; tiled >> c; c++) {
            const Cache *cache = &caches[c];
            if (!(tiled >> c & 1))
                continue;
            unsigned per_unit = UNIT_BYTES >> tile_shift(cache->bpp);
            for (unsigned t = 0; t < per_unit; t++) {
                uint16_t tile = (uint16_t)(first_tile[c] + i * per_unit + t);
                uint16_t slot = cache->slot_of[tile & (CACHE_TILES - 1)];
                if (slot != NO_SLOT) {
                    convert_tile(cache, tile, slot);
                    /* and from tiles that changed */
                    if (copied.made && c == layers[copied.key.layer].cache)
                        copied.stale = offsets_written = true;
                }
            }
        }
        if (sprites >= 0)
            sprite_tile_changed(sprites + (int)i);
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
    volatile uint16_t *block = (volatile uint16_t *)BG_VRAM + panel_room.map_block[s] * (SCREEN_BYTES / 2);
    /* the first send since the room was made blanks the rest of the map,
     * which held whatever the room's places did: a dialog opening showed
     * those as rows of stray tiles until its rows were sent */
    if (!(panel_room.mapped >> n & 1)) {
        uint16_t blank = own ? base : c ? c->blank : 0;
        for (unsigned k = 0; k < SCREEN_BYTES / 2; k++)
            block[k] = blank;
        panel_room.mapped |= (uint8_t)(1u << n);
    }
    volatile uint16_t *gba = block + first;
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
    for (unsigned i = 1; i < 128; i++)   /* 0: the backdrop, each line's own (set_line) */
        BG_PALETTE[i] = shown(snes.cgram[i]);
    if (plane_room && !copies)
        for (unsigned i = 128; i < 256; i++)
            BG_PALETTE[i] = shown(snes.cgram[i]);
    for (unsigned p = 0; copies && p < 8; p++)
        for (unsigned c = 0; c < 4; c++)
            BG_PALETTE[(8 + p) * 16 + c] = shown(snes.cgram[p * 4 + c]);
    for (unsigned i = 0; i < 128; i++)
        OBJ_PALETTE[i] = shown(snes.cgram[128 + i]);
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
 * (work_out_lines), so the bands must show its lines in their order: a
 * band's lines whose rows a band above showed already are black
 * (black_out_rows_shown_above), and a framing that can't be put in order so
 * shows the camera's alone. */
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
/* Each band's column moved by its own lines, its panel's, and whether any
 * band has one (VidViewPanel's column): the mode 2 columns left of it that
 * the offsets move, and the sprites that start there, are moved down as far
 * as the column's lines are (show_moved_columns, present_sprites). */
static MovedColumn band_columns[MOST_BANDS];
static bool columns_moved;

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

static Band line_band(unsigned y, MovedColumn *column)
{
    Band b = { .row = (int16_t)(view_aim.top + (int)y), .left = view_aim.left, .at = (uint8_t)y, .lines = 1, .rows = 1 };
    *column = (MovedColumn){ 0, 0 };
    if (panel_draws(y)) {
        b.drawn = true;
        b.row = panel_looks(y);
        b.left = DRAWN_LEFT;
        return b;
    }
    for (unsigned n = 0; n < view_panel_count; n++) {
        const KeptPanel *p = &view_panels[n];
        if ((int)y >= p->at && (int)y < p->at + (int)p->lines) {
            b.row = (int16_t)(p->top + ((int)y - p->at)), b.left = p->left;
            b.cut_at = p->cut_at, b.cut_dots = (uint8_t)p->cut_dots;
            *column = view_panel_columns[n];
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

/* The bands made to show the SNES's lines in their order, or false where
 * they can't be: a camera's or a panel's band whose first rows a band
 * above it showed already shows black on their lines, and the rest from
 * the next row on; a drawn panel's band can't give up its rows. As a
 * dialog's window opens, the drawn panel looks like the rows the window
 * grows over, which the camera's band below it shows again: the view fell
 * back to the camera's band alone for those frames, the dialog's window
 * where the SNES has it, and the dialog's map its lines showed took the
 * tiles' room from the scene's (the lab's floor before the jet bike race
 * went black in sixteen-dot squares as each of Johnny's dialogs opened,
 * issue #589). */
static bool black_out_rows_shown_above(void)
{
    int row = -1;
    if (band_count >= MOST_BANDS)
        return false;
    for (unsigned k = 0; k < band_count; k++) {
        Band *b = &bands[k];
        if (b->black)
            continue;
        if (b->row > row) {
            row = b->row + b->rows - 1;
            continue;
        }
        unsigned shown = (unsigned)(row - b->row + 1);
        if (b->drawn)
            return false;
        if (shown >= b->lines) {
            b->black = true;
            band_columns[k] = (MovedColumn){ 0, 0 };
            continue;
        }
        if (band_count >= MOST_BANDS)
            return false;
        memmove(&bands[k + 1], &bands[k], sizeof bands[0] * (band_count - k));
        memmove(&band_columns[k + 1], &band_columns[k], sizeof band_columns[0] * (band_count - k));
        band_count++;
        bands[k] = (Band){ .row = -1, .at = b->at, .lines = (uint8_t)shown, .rows = (uint8_t)shown, .black = true };
        band_columns[k] = (MovedColumn){ 0, 0 };
        b = &bands[++k];
        b->at = (uint8_t)(b->at + shown), b->row = (int16_t)(b->row + shown);
        b->lines = (uint8_t)(b->lines - shown), b->rows = (uint8_t)(b->rows - shown);
        row = b->row + b->rows - 1;
    }
    return true;
}

/* The bands, a run of lines at a time between the lines where they may change. */
static void band_up_the_view(void)
{
    band_count = 0;
    columns_moved = false;
    for (unsigned y = 0; y < GBA_VIEW_HEIGHT;) {
        MovedColumn column;
        Band b = line_band(y, &column);
        unsigned past = next_change_of_band(y, &b);
        b.lines = (uint8_t)(past - y);
        b.rows = b.drawn ? (uint8_t)(panel_looks(past - 1) - b.row + 1) : b.lines;
        Band *last = band_count ? &bands[band_count - 1] : NULL;
        if (last && goes_on(last, &b) && band_columns[band_count - 1].rows == column.rows &&
            band_columns[band_count - 1].end == column.end) {
            last->rows = (uint8_t)(b.drawn ? b.row + b.rows - last->row : last->rows + b.rows);
            last->lines = (uint8_t)(last->lines + b.lines);
        } else if (band_count < MOST_BANDS) {
            band_columns[band_count] = column;
            columns_moved |= column.end != 0;
            bands[band_count++] = b;
        }
        y = past;
    }
    if (!black_out_rows_shown_above()) {
        bands[0] = (Band){ .row = view_aim.top, .left = view_aim.left, .lines = GBA_VIEW_HEIGHT, .rows = GBA_VIEW_HEIGHT };
        band_columns[0] = (MovedColumn){ 0, 0 };
        band_count = 1;
        columns_moved = false;
    }
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
 * top is 0-255, its foot up to 63 below): the first band not black whose
 * rows hold it, as its index plus one, or 0 where none. Made for each
 * frame's sprites, which would otherwise look through the bands twice a
 * sprite. */
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

/* A sprite that starts short of a band's column moved by its own lines: in
 * the band whose column shows its top line, or its foot's, `row` the SNES's
 * line that band's column starts at; none where the columns leave both out.
 * Any other sprite stays in the band it was found in. */
static const Band *column_band_showing(int row, int x, int *from)
{
    for (unsigned k = 0; k < band_count; k++) {
        MovedColumn c = band_columns[k];
        int first = bands[k].row + c.rows;
        if (c.end && x < c.end + bands[k].left && row >= first && row < first + bands[k].rows) {
            *from = first;
            return &bands[k];
        }
    }
    return NULL;
}

static const Band *band_in_its_column(int x, int top, unsigned h, const Band *found, int *row)
{
    if (found && (!band_columns[found - bands].end || x >= band_columns[found - bands].end + found->left))
        return found;
    const Band *b = column_band_showing(top, x, row);
    return b ? b : column_band_showing(top + (int)h - 1, x, row);
}

/* The sprites as the GBA's sprite table has them, kept aside until the
 * frame's picture is shown whole (show_the_frame, below), and whether they
 * changed since. */
static uint16_t sprites_shown[SPRITES * 3];   /* each sprite's first three attributes */
static bool sprites_aside;

static void set_sprite(uint16_t *to, unsigned attribute, uint16_t value)
{
    if (to[attribute] != value) {
        to[attribute] = value;
        sprites_aside = true;
    }
}

/* An earlier sprite behind a later one on the GBA is raised to the later
 * one's priority where that hides nothing (Sprites raised, below); else it
 * is the sprite window, at most RAISES_LOOKED_AT looked at a frame. */
enum { RAISES_LOOKED_AT = 16, RAISED_WIDEST = 32, LINES_A_LOOK = 4, LINE_LOOKED_AT = 1 };
static bool raise_shows_more(const uint8_t *e, int x, int y, unsigned w, unsigned h, uint8_t from, uint8_t to);
static bool sprites_overlap IN_FAST_RAM;   /* the sprites looked at again as the lines change under them */
/* Counts of the changes to what the looks are made from: the lines shown,
 * and video memory (the layers' maps and tiles, the sprites' tiles). */
static uint8_t looks_made_from IN_FAST_RAM, looks_last_made_from IN_FAST_RAM;

/* A look's result kept with the sprite in the GBA's sprite table, in its
 * second attribute's bits 9-11, which a sprite not rotated leaves unused:
 * that it was looked at, and the priority the table doesn't show, its own
 * where it was raised (to `front`, its third attribute's), `front` where
 * it was masked (at its own). */
enum { LOOKED = 0x0800, LOOK_PRIORITY_SHIFT = 9, LOOK_BITS = 0x0E00 };

static uint16_t look_kept(uint8_t other_priority) { return (uint16_t)(LOOKED | other_priority << LOOK_PRIORITY_SHIFT); }

/* Whether a sprite of priority `own` behind one later of priority `front`,
 * shown as it is now, was looked at when the sprites were last made with
 * the same priorities, and if so whether it was raised (`raised`): with
 * what the looks are made from unchanged since, the look stands. A sprite
 * masked without a look, past the budget, has none. */
static bool was_looked_at(const uint16_t *shown_as, const uint16_t attributes[3], uint8_t own, uint8_t front, bool *raised)
{
    if ((shown_as[0] & ~OBJ_WINDOW) != attributes[0] || (shown_as[1] & ~LOOK_BITS) != attributes[1] ||
        (shown_as[2] & ~(3u << 10)) != attributes[2] || !(shown_as[1] & LOOKED))
        return false;
    unsigned shown_priority = shown_as[2] >> 10 & 3, kept = shown_as[1] >> LOOK_PRIORITY_SHIFT & 3;
    *raised = !(shown_as[0] & OBJ_WINDOW);
    return *raised ? shown_priority == front && kept == own : shown_priority == own && kept == front;
}

/* The sprites, each in the band its top line is in, or its foot's; the drawn
 * panel shows none. */
static void present_sprites(void)
{
    Shown shown[SPRITES];   /* the sprites after this one in the table, shown */
    Squares squares;
    unsigned count = 0;
    unsigned looked = 0;
    bool looks_stand = looks_last_made_from == looks_made_from;   /* since the sprites were last looked at */
    looks_last_made_from = looks_made_from;
    memset(squares, 0, sizeof squares);
    sprites_mask = false;
    sprites_overlap = false;
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
        uint16_t *to = &sprites_shown[n * 3];
        int top = (e[1] + 1) & 0xFF;
        unsigned k = band_of_row[top];
        if (!k)
            k = band_of_row[top + (int)h - 1];
        /* one that runs past the SNES's last line goes on from its first, as
         * its feet do where its head is above the screen's top */
        if (!k && top + (int)h - 1 > 0xFF)
            k = band_of_row[(top + (int)h - 1) & 0xFF];
        const Band *b = k && !bands[k - 1].drawn ? &bands[k - 1] : NULL;
        int row = b ? b->row : 0;
        if (columns_moved)
            b = band_in_its_column(x, top, h, b, &row);
        int gx = b ? band_x(b, x) : GBA_VIEW_WIDTH, gy = b ? ((e[1] - row + b->at) & 0xFF) : 0;
        if (!b || !sized[large].fits || gx <= -(int)w || gx >= GBA_VIEW_WIDTH) {
            set_sprite(to, 0, 0x0200);   /* hidden */
            continue;
        }
        uint8_t priority = order->sprite[(e[3] >> 4) & 3];
        Shown here = { (int16_t)gx, (int16_t)(gy >= 192 ? gy - 256 : gy), (uint8_t)w, (uint8_t)h, priority };
        /* the frontmost priority of the later sprites in front over it */
        uint8_t front = priority;
        if (over_squares(squares, &here, false) & ((1u << priority) - 1))
            for (unsigned k = 0; k < count; k++)
                if (shown[k].priority < front && overlap(&here, &shown[k]))
                    front = shown[k].priority;
        uint16_t name = (uint16_t)(e[2] | (e[3] & 1) << 8);
        uint16_t tile = (uint16_t)((name >> 4) * 32 + sprite_page * PAGE_COLUMNS + (name & 15));
        uint16_t attributes[3] = { (uint16_t)(gy | shape << 14),
                                   (uint16_t)((gx & 0x1FF) | (e[3] & 0x40 ? 0x1000 : 0) | (e[3] & 0x80 ? 0x2000 : 0) | size << 14),
                                   (uint16_t)(tile | ((e[3] >> 1) & 7) << 12) };
        bool mask = false;
        uint16_t look = 0;
        if (front < priority) {
            bool raised = false;
            sprites_overlap = true;
            bool known = looks_stand && was_looked_at(to, attributes, priority, front, &raised);
            if (!known && looked < RAISES_LOOKED_AT) {
                looked++;
                raised = raise_shows_more(e, here.x, here.y, w, h, priority, front);
                known = true;
            }
            if (known)
                look = look_kept(raised ? priority : front);
            if (raised)
                priority = here.priority = front;
            mask = !raised;
        }
        over_squares(squares, &here, true);
        shown[count++] = here;
        sprites_mask |= mask;
        set_sprite(to, 0, (uint16_t)(attributes[0] | (mask ? OBJ_WINDOW : 0)));
        set_sprite(to, 1, (uint16_t)(attributes[1] | look));
        set_sprite(to, 2, (uint16_t)(attributes[2] | priority << 10));
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
 * each line whose registers differ from the line above's. One table holds
 * two frames' changes, one from each end: the lines show the last frame's
 * from one end while the next frame's are worked out from the other, and
 * the next are shown from the vertical blank after they are made (the
 * frame's picture, below). A frame whose work ran on past the vertical
 * blank then shows the last frame's changes whole rather than half of
 * each, as the naming screen's rows did on the frame the cursor moved down
 * a row. A frame with more changes than the other end has room for (the
 * jet bike race's, up to 160) takes the shown frame's places too, as
 * before. */
static LineRegisters change_registers[GBA_LINES];
static uint8_t change_line[GBA_LINES];
static volatile uint8_t changes;
static uint8_t next_change;       /* the interrupts' place in the changes */
static volatile bool by_line;     /* whether the interrupts set the changes' registers */
static uint8_t shown_end;   /* the end the shown changes are kept from: 0 the first place up, 1 the last down */
static uint8_t made_end;    /* and the end the frame being made keeps its changes from */
/* The shown change k's place, (k ^ flip) + add: k from the first place
 * up, or 159 - k from the last down (~k + 160), in a few instructions for
 * the interrupts. */
static uint32_t shown_flip, shown_add;

static inline unsigned change_place(uint8_t end, unsigned k) { return end ? GBA_LINES - 1 - k : k; }
static inline unsigned shown_place(unsigned k) { return (k ^ shown_flip) + shown_add; }
static inline LineRegisters *made_change(unsigned k) { return &change_registers[change_place(made_end, k)]; }
static inline uint8_t *made_line(unsigned k) { return &change_line[change_place(made_end, k)]; }

static void show_from_end(uint8_t end)
{
    shown_end = end;
    shown_flip = end ? ~0u : 0;
    shown_add = end ? GBA_LINES : 0;
}

/* ---- Sprites raised rather than masked ---------------------------------------------------------
 *
 * The SNES draws the earlier of two overlapping sprites over the later and
 * only then sets it against the layers by its own priority; the GBA draws
 * the sprite of the higher priority in front. An earlier sprite behind a
 * later one on the GBA is raised to the later one's priority where that
 * shows fewer of its dots wrong than masking it: where the GBA layers of a
 * priority between the two show fewer dots under the sprite's than not, a
 * line at a time, as the picture on the screen shows its layers there (its
 * changes' registers, windows and all, and the GBA's own maps and tiles: a
 * frame behind, the sprites being worked out before the frame's lines, so
 * looked at again each frame while any overlap). Otherwise, or past the
 * frame's budget of such looks, it is the sprite window (sprites_mask,
 * above), which shows the layers in its place: Crono's covers in bed.
 * Masked, Crono stepping out of the time gate's light showed as the room
 * the light's window cuts away, and Marle beside him in the hall before the
 * jet bike race was missing (issue #560). Dots are counted eight to a word,
 * a bit a dot in the low bit of its nibble, as the GBA packs a tile's row. */
enum { DISPCNT_TEXT_ONLY = 0, BGCNT_8BPP = 0x0080, DOT_BITS = 0x11111111u };
static uint32_t entry_row(const volatile uint32_t *tile, uint16_t e, unsigned y);
static unsigned entry_at(unsigned col, unsigned row, unsigned w);

/* A row of packed nibbles' dots, leftmost last. */
static uint32_t nibbles_reversed(uint32_t w)
{
    w = w >> 16 | w << 16;
    w = (w >> 8 & 0x00FF00FFu) | (w & 0x00FF00FFu) << 8;
    return (w >> 4 & 0x0F0F0F0Fu) | (w & 0x0F0F0F0Fu) << 4;
}

/* The dots of eight on line `l` of a sprite `w` across and `h` down, its
 * column `c` of them: the SNES's tiles named as it names them, a row's
 * columns and a table's rows wrapping, and flipped as it is. */
static uint32_t sprite_dots(const uint8_t *e, unsigned w, unsigned h, unsigned l, unsigned c)
{
    unsigned name = e[2] | (e[3] & 1) << 8, line = e[3] & 0x80 ? h - 1 - l : l;
    unsigned column = e[3] & 0x40 ? w / 8 - 1 - c : c;
    unsigned tile = (name & 0x100) | ((((name >> 4) + line / 8) & 15) << 4) | ((name + column) & 15);
    uint32_t at = sprite_table(tile >> 8) + (tile & 0xFF) * 32 + (line % 8) * 2;
    const uint8_t *v = snes.vram;
    uint32_t dots = spread[v[at & (VRAM_BYTES - 1)] | v[(at + 1) & (VRAM_BYTES - 1)] | v[(at + 16) & (VRAM_BYTES - 1)] |
                           v[(at + 17) & (VRAM_BYTES - 1)]];
    return e[3] & 0x40 ? nibbles_reversed(dots) : dots;
}

/* The dots a GBA tile's row (packed nibbles) doesn't leave transparent. */
static uint32_t opaque(uint32_t row) { return (row | row >> 1 | row >> 2 | row >> 3) & DOT_BITS; }

/* Whether a window's edges (WIN0H's) take dot `x`. */
static bool in_window(uint16_t edges, int x)
{
    return (edges >> 8) < (edges & 0xFF) && x >= (edges >> 8) && x < (edges & 0xFF);
}

/* The layers registers `r` show at dot `x`, through their windows. */
static unsigned layers_at(const LineRegisters *r, int x)
{
    if (r->dispcnt & DISPCNT_WIN0 && in_window(r->win0h, x))
        return r->winin;
    if (r->dispcnt & DISPCNT_WIN1 && in_window(r->win1h, x))
        return r->winin >> 8u;
    return r->winout;
}

/* Whether a window's edge lies among the eight dots from `x` but the first. */
static bool edge_within(const LineRegisters *r, int x)
{
    const uint16_t edges[4] = { (uint16_t)(r->win0h >> 8), (uint16_t)(r->win0h & 0xFF), (uint16_t)(r->win1h >> 8),
                                (uint16_t)(r->win1h & 0xFF) };
    for (unsigned i = 0; i < 4; i++)
        if ((int)edges[i] > x && (int)edges[i] < x + 8)
            return true;
    return false;
}

/* Of eight dots from `x` on the screen's line `y`, those GBA layer `g`
 * shows, as registers `r` show it through their windows. */
static uint32_t gba_layer_dots(const LineRegisters *r, unsigned g, int y, int x)
{
    uint16_t control = r->bgcnt[g];
    unsigned mw = control & 0x4000 ? 64 : 32, mh = control & 0x8000 ? 64 : 32;
    const volatile uint16_t *map = (const volatile uint16_t *)BG_VRAM + (control >> 8 & 31) * 1024u;
    const volatile uint32_t *tiles = BG_VRAM + (control >> 2 & 3) * 512u * 8u;
    unsigned my = (unsigned)(y + r->vofs[g]) & (mh * 8 - 1), mx = (unsigned)(x + r->hofs[g]) & (mw * 8 - 1);
    uint16_t e = map[entry_at(mx / 8, my / 8, mw)];
    uint32_t dots = opaque(entry_row(tiles + (e & 0x3FF) * 8u, e, my % 8)) >> 4 * (mx % 8);
    if (mx % 8) {
        e = map[entry_at((mx / 8 + 1) & (mw - 1), my / 8, mw)];
        dots |= opaque(entry_row(tiles + (e & 0x3FF) * 8u, e, my % 8)) << (32 - 4 * (mx % 8));
    }
    if (r->dispcnt & (DISPCNT_WIN0 | DISPCNT_WIN1 | DISPCNT_OBJ_WINDOW)) {
        uint32_t shown = 0;
        if (!edge_within(r, x))
            shown = layers_at(r, x) >> g & 1u ? DOT_BITS : 0;
        else
            for (unsigned i = 0; i < 8; i++)
                shown |= (layers_at(r, x + (int)i) >> g & 1u) << (4 * i);
        dots &= shown;
    }
    return dots;
}

/* Whether sprite `e`, at `x`, `y` on the screen and `w` by `h`, raised from
 * GBA priority `from` to `to`, shows fewer of its dots wrong than as the
 * sprite window: raised, a dot is wrong where a layer between the two
 * priorities has one under it, which the SNES shows; masked, where none
 * does, which the SNES shows the sprite over. */
static bool raise_shows_more(const uint8_t *e, int x, int y, unsigned w, unsigned h, uint8_t from, uint8_t to)
{
    if (w > RAISED_WIDEST || !changes)
        return false;
    unsigned k = 0, all = 0, wrong = 0;   /* the sprite's dots, and those a layer between covers */
    for (unsigned l = LINE_LOOKED_AT; l < h; l += LINES_A_LOOK) {
        int line = y + (int)l;
        if (line < 0 || line >= GBA_LINES)
            continue;
        while (k + 1 < changes && change_line[shown_place(k + 1)] <= line)
            k++;
        const LineRegisters *r = &change_registers[shown_place(k)];
        if ((r->dispcnt & DISPCNT_MODES) != DISPCNT_TEXT_ONLY)
            return false;
        uint8_t between = 0;
        for (unsigned g = 0; g < 4; g++) {
            unsigned priority = r->bgcnt[g] & 3;
            if (!(r->dispcnt & (DISPCNT_BG0 << g)) || priority < to || priority >= from)
                continue;
            if (r->bgcnt[g] & BGCNT_8BPP)
                return false;
            between |= (uint8_t)(1u << g);
        }
        for (unsigned c = 0; c < w / 8; c++) {
            uint32_t dots = sprite_dots(e, w, h, l, c), covered = 0;
            for (unsigned g = 0; dots && g < 4; g++)
                if (between >> g & 1)
                    covered |= gba_layer_dots(r, g, line, x + 8 * (int)c);
            all += bits_set(dots);
            wrong += bits_set(dots & covered);
        }
    }
    return 2 * wrong < all;
}

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
 * them (the game's memory doesn't change while the picture is made): in
 * fast RAM, since each of a frame's transfers reads them. */
enum { READ_LINES = 8, READ_LINE_BYTES = 32 };
typedef struct {
    uint32_t tag;    /* the line's address / READ_LINE_BYTES, plus one; 0: none */
    uint8_t bytes[READ_LINE_BYTES];
} ReadLine;
static ReadLine read_lines[READ_LINES] IN_FAST_RAM;

/* Where each 32 bytes of the tables a frame's lines read came from, as they
 * read them, and a 32-bit FNV-1a hash of all their bytes in that order (a
 * word at a time), so
 * that the next frame can tell whether its tables are the same (below):
 * the places in work RAM, written once for each 32 bytes read, the rest in
 * fast RAM; a hash rather than the bytes, which neither has the room for. */
enum { LINES_READ_KEPT = 64 };
static uint32_t lines_read[LINES_READ_KEPT];
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

ARM_CODE static void keep_line_read(uint32_t line, const uint8_t bytes[READ_LINE_BYTES])
{
    if (lines_read_count == LINES_READ_KEPT) {
        lines_read_overflowed = true;
        return;
    }
    lines_read[lines_read_count++] = line;
    lines_read_hash = hash_on(lines_read_hash, bytes, READ_LINE_BYTES);
}

/* The 32 bytes of the tables at `line` (an address / READ_LINE_BYTES). */
ARM_CODE static const uint8_t *line_bytes(uint32_t line)
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

ARM_CODE static uint8_t line_read(uint32_t addr)
{
    return line_bytes(addr / READ_LINE_BYTES)[addr % READ_LINE_BYTES];
}

/* A transfer's `n` bytes from `at` on in `bank`, the address wrapping in the
 * bank as HDMA's does: from one read of the tables where they lie in one 32
 * bytes (which a bank's end never splits), a byte at a time where not. */
ARM_CODE static void table_unit(uint8_t bank, uint16_t at, uint8_t n, uint8_t out[4])
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

ARM_CODE static void slot_entry(Slot *sl, const VidLineEffect *e)
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
 * shows that the layout hasn't, or one a band shows cut. Where the maps
 * kept are as many as there is room for, a map a line shows whole takes
 * the place of one kept cut: without its own copy a line shows whatever the
 * GBA's VRAM holds where the SNES's map is (the Magus castle battles' third
 * layer, whose second map holds the candles' flames, showed a window's
 * border down the whole battle), where a cut map left out shows the map
 * whole, its columns uncut. */
static void note_line_map(unsigned n, uint16_t byte, uint16_t bytes, uint8_t cut)
{
    if (!layers[n].shown || line_map(n, byte, cut != NO_CUT))
        return;
    unsigned m = snes.line_map_count;
    if (cut != NO_CUT && m >= CUT_LINE_MAPS)
        return;
    if (m < LINE_MAPS) {
        snes.line_map_count++;
    } else {
        /* full: the last kept cut gives its place up */
        while (m > 0 && snes.line_maps[m - 1].cut == NO_CUT)
            m--;
        if (m-- == 0)
            return;
    }
    snes.line_maps[m] =
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
    /* a band that cuts the map shows its cut copy, and the map whole only
     * where the copy found no room: noted whole otherwise, it would be shown
     * by no line, left out by the next layout and noted again, a layout a
     * frame (the battle's menus) */
    bool cuts = band->cut_dots && b.map_w == 32 && band->cut_dots % 8 == 0;
    const LineMap *cut_copy = cuts ? line_map(n, at, true) : NULL;
    if (!own && (!cuts || (cut_copy && cut_copy->gba_byte == NO_PLACE)))
        note_line_map(n, at, bytes, NO_CUT);
    else if (own && !l->own_map)
        snes.settings_changed = true;   /* left out by the last layout: the next has it */
    const LineMap *lm = line_map(n, at, false);
    if (cuts) {
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
 * it is the fixed colour too) and the blending can't add it exactly
 * (add_by_blending, below): the GBA's blending can't take a colour away, but
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

/* n / d for the small n (under 2^16 / d) and constant d the shading works
 * out, by a multiply by d's reciprocal scaled by 2^16 rather than a
 * call to the C library's divide: Thumb has no divide, nor a multiply that
 * keeps a product's high word, so even a / by a constant is a call. */
static inline unsigned small_quotient(unsigned n, unsigned d) { return n * ((1u << 16) / d + 1) >> 16; }

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
    unsigned steps = m->subtract ? small_quotient(red * BLEND_STEPS, DARKENED_STRENGTH)
                                 : small_quotient(red * BLEND_STEPS, 31 - BRIGHTENED_STRENGTH);
    if (m->half)
        steps /= 2;
    r->bldcnt = (uint16_t)((m->subtract ? BLD_DARKEN : BLD_LIGHTEN) | line_layer_bits(st, layers));
    r->bldy = (uint16_t)(steps > BLEND_STEPS ? BLEND_STEPS : steps);
}

/* The fixed colour added to layers with nothing behind them but the
 * backdrop (the windows' and the dialog's shading, a grey added down their
 * top half): the GBA's blending of those layers with the backdrop at full
 * weight, at half where the SNES halves, the line's backdrop the fixed
 * colour. The GBA's blending adds as the SNES's does, each channel held at
 * 31, and halves as it does, rounding down, so the sum is the SNES's exactly,
 * any colour, not only a grey. Where the SNES's backdrop shows on such a
 * line, the GBA's shows the fixed colour rather than its own; where the
 * colour window keeps the math to part of the line, it is done across it. */
static uint16_t layers_through_windows(const LineRegisters *r)
{
    return (uint16_t)((r->winin | r->winin >> 8 | r->winout) & 0xF);
}

/* Whether GBA layer `h` stands behind layer `g` on a line. */
static bool behind(const LineRegisters *r, unsigned h, unsigned g)
{
    unsigned ph = r->bgcnt[h] & 3, pg = r->bgcnt[g] & 3;
    return ph > pg || (ph == pg && h > g);
}

/* The backdrop's colour that the blending adds to (or halves with) the
 * colours shown to give the fixed colour's sum. With the cartridge's own
 * colours that is the fixed colour itself. Packed with a colour profile
 * (pack.py --profile), the GBA blends colours already shown, which a sum
 * can't follow exactly: the colour taken is what the sum adds to a window's
 * dark colours (BRIGHTENED_STRENGTH of 31, as the brightening is fitted),
 * each channel's, so the shading steps by what it shows there rather than
 * by its own colour shown, which a gamma brightens most for small
 * colours. */
static uint16_t added_colour(uint16_t fixed, bool half)
{
    if (!shown_colours)
        return fixed;
    const unsigned base = BRIGHTENED_STRENGTH;
    uint16_t grey = (uint16_t)(base * 0x0421), sum = 0, out = 0;
    for (unsigned shift = 0; shift < 15; shift += 5) {
        unsigned k = fixed >> shift & 31;
        sum |= (uint16_t)((half ? (base + k) / 2 : base + k > 31 ? 31 : base + k) << shift);
    }
    uint16_t was = shown(grey), now = shown(sum);
    for (unsigned shift = 0; shift < 15; shift += 5) {
        int w = was >> shift & 31, n = now >> shift & 31, add = half ? 2 * n - w : n - w;
        out |= (uint16_t)((add < 0 ? 0 : add > 31 ? 31 : add) << shift);
    }
    return out;
}

static bool add_by_blending(LineRegisters *r, const LineState *st)
{
    const VidColorMath *m = &st->math;
    uint8_t layers = (uint8_t)(m->layers & st->main_layers & (VID_BG1_BIT | VID_BG2_BIT | VID_BG3_BIT | VID_BG4_BIT));
    if (st->brightness < 15 || st->mode == 7 || sub_screen_shows_a_layer(st) || !layers || m->subtract ||
        m->prevent_math == VID_REGION_ALWAYS || m->clip_black != VID_REGION_NEVER)
        return false;
    uint16_t added = (uint16_t)(line_layer_bits(st, layers) & 0xF), through = layers_through_windows(r);
    if (!added)
        return false;
    for (unsigned g = 0; g < 4; g++)
        for (unsigned h = 0; (added >> g & 1) && h < 4; h++)
            if ((through >> h & 1) && !(added >> h & 1) && behind(r, h, g))
                return false;
    /* the sub screen showing nothing is the fixed colour, unhalved */
    bool half = m->half && !m->sub_screen;
    if (!st->fixed_color && !half)
        return true;
    r->bldcnt = (uint16_t)(BLD_ALPHA | added | BLD_BACKDROP_SECOND);
    r->bldalpha = half ? 0x0808 : 0x1010;
    r->backdrop = added_colour(st->fixed_color, half);
    return true;
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
 * Where the sub screen shows nothing the SNES adds the fixed colour and
 * doesn't halve, so a halved sum leaves the backdrop out: the Millennial
 * Fair's ground, halved with its sub screen's light where the GBA can't
 * show it, stays as bright as natively rather than halved toward black.
 * The SNES's subtraction of a sub screen has no GBA counterpart. */
/* The sub screen's own layers (those the main screen doesn't show: a
 * room's light or the Millennial Fair's, the layers the sub screen adds to
 * the math's layers) on a line whose math adds the sub screen: shown behind
 * every layer, at the GBA's last priority, where the math's layers show,
 * so that each lies under the math's tiles as the GBA's blending's second
 * target, as the sub screen lies under the main screen on the SNES. What
 * the math's layers show is the sub screen added to it; where another layer
 * stands between them it shows unblended, and where no layer of the main
 * screen covers it, it shows itself. Their GBA layers, a bit each; none where
 * one of the math's layers already stands at the last priority after them. */
static uint16_t sub_screens_own(const LineState *st)
{
    return (uint16_t)(st->sub_layers & ~st->main_layers & (VID_BG1_BIT | VID_BG2_BIT | VID_BG3_BIT | VID_BG4_BIT));
}

static uint16_t show_the_sub_screens_own(LineRegisters *r, const LineState *st, uint16_t first)
{
    uint16_t own = (uint16_t)(line_layer_bits(st, sub_screens_own(st)) & 0xF);
    for (unsigned g = 0; g < 4; g++)
        if ((own >> g & 1) && !r->bgcnt[g])
            own &= (uint16_t)~(1u << g);   /* a layer the line doesn't show (a split layer's front, no tiles there) */
    for (unsigned g = 0; g < 4; g++)
        for (unsigned h = g + 1; (own >> g & 1) && h < 4; h++)
            if ((first >> h & 1) && (r->bgcnt[h] & 3) == 3)
                return 0;
    if (!own)
        return 0;
    for (unsigned g = 0; g < 4; g++)
        if (own >> g & 1)
            r->bgcnt[g] |= 3;
    uint16_t *regions[] = { &r->winin, &r->winout };
    for (unsigned k = 0; k < 2; k++)
        for (unsigned shift = 0; shift < 16; shift += 8)
            if (*regions[k] >> shift & first & 0xF)
                *regions[k] |= (uint16_t)(own << shift);
    return own;
}

static void line_blending(LineRegisters *r, const LineState *st)
{
    r->bldcnt = st->brightness < 15 ? (uint16_t)(BLD_DARKEN | BLD_ALL_FIRST) : 0;
    /* the SNES's brightness b scales a colour by b/15, the GBA's darkening
     * by y to (16 - y)/16: y the nearest to 16 (15 - b)/15, which at the
     * dimmest steps of a fade is half as bright as rounding down (the stray
     * tiles of a scene loading under its fade showed through at twice the
     * brightness ct_native shows its picture) */
    r->bldy = (uint16_t)small_quotient((15u - st->brightness) * 16 + 7, 15);
    r->bldalpha = 0;
    r->backdrop = shown(backdrop(st));
    const VidColorMath *m = &st->math;
    uint8_t first = (uint8_t)(m->layers & (st->main_layers | VID_BACK_BIT));
    if (st->brightness == 15 && sub_screen_shows_a_layer(st) && !m->subtract && first && m->prevent_math != VID_REGION_ALWAYS) {
        uint8_t second = (uint8_t)((st->sub_layers & st->main_layers & VID_ALL_LAYERS) | (m->half ? 0 : VID_BACK_BIT));
        uint16_t own = show_the_sub_screens_own(r, st, line_layer_bits(st, first));
        r->bldcnt = (uint16_t)(BLD_ALPHA | line_layer_bits(st, first) | (line_layer_bits(st, second) | own) << 8);
        r->bldalpha = m->half ? 0x0808 : 0x1010;
    } else if (!add_by_blending(r, st)) {
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

/* The first MOST_RUNS runs of layer `n`'s moved columns on a line, as many
 * as there are; the columns the offsets move short of the SNES's dot
 * `column_end` moved `column_rows` lines further down (a band's column moved
 * by its own lines). */
static unsigned moved_columns(const LineState *st, const LineOffsets *o, unsigned n, int column_end, int column_rows,
                              MovedColumns runs[MOST_RUNS])
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
        if (down & names && dot + 8 <= column_end)
            v = (uint16_t)(v + column_rows);
        if (in_run && !(((h ^ runs[count - 1].hofs) | (v ^ runs[count - 1].vofs)) & GBA_SCROLL)) {
            runs[count - 1].last = dot + 7 > VID_WINDOW_LAST_DOT ? VID_WINDOW_LAST_DOT : dot + 7;
            continue;
        }
        in_run = ((h ^ hofs) | (v ^ vofs)) & GBA_SCROLL;
        if (!in_run)
            continue;
        if (count == MOST_RUNS)
            return MOST_RUNS + 1;
        runs[count++] = (MovedColumns){ .first = dot, .last = dot + 7, .hofs = h, .vofs = v };
    }
    return count;
}

/* ---- A layer's columns copied ----
 *
 * Where a layer's columns move more ways than the runs show (the title's
 * logo, each of whose columns the ripple moves up or down by its own
 * offset), the layer is shown from a copy of its map made in the frame:
 * each column the line shows moved up or down as its offset moves it, so
 * that the copy at the layer's own scroll shows what the columns show. A
 * column moved by part of a tile takes new tiles, each a tile's rows from
 * the two tiles of the column it lies across (Strips), in the copy's room
 * (CopyRoom). A tile takes one palette, the palette of the one of the two
 * that gives it the more rows. Horizontal offsets aren't copied. */

/* A tile's row as the map entry shows it, flipped as the entry says. */
static uint32_t entry_row(const volatile uint32_t *tile, uint16_t e, unsigned y)
{
    uint32_t w = tile[e & 0x0800 ? 7 - y : y];
    if (e & 0x0400) {
        w = (w >> 4 & 0x0F0F0F0F) | (w & 0x0F0F0F0F) << 4;
        w = (w >> 8 & 0x00FF00FF) | (w & 0x00FF00FF) << 8;
        w = w >> 16 | w << 16;
    }
    return w;
}

/* A map entry's halfword in a GBA map of `w` columns, from its screen block. */
static unsigned entry_at(unsigned col, unsigned row, unsigned w)
{
    return (col >= 32 ? 1024u : 0) + (row >= 32 ? (w == 64 ? 2048u : 1024u) : 0) + (row & 31) * 32 + (col & 31);
}

/* Whether the tile a map entry names is blank, remembered for each of the
 * window's places as it is found, a bit each (`known`, `blank`). */
static bool is_blank(const volatile uint32_t *tiles, uint16_t e, uint32_t *known, uint32_t *blank)
{
    unsigned place = e & 0x3FF;
    uint32_t bit = 1u << (place % 32);
    if (!(known[place / 32] & bit)) {
        const volatile uint32_t *t = tiles + place * 8u;
        known[place / 32] |= bit;
        if (!(t[0] | t[1] | t[2] | t[3] | t[4] | t[5] | t[6] | t[7]))
            blank[place / 32] |= bit;
    }
    return blank[place / 32] & bit;
}

/* The strips of the map's columns `col` the lines show (Strips), from the
 * GBA's map and tiles `control` names; false where they are more than the
 * room holds. */
static bool make_strips(uint16_t control, const uint8_t *col, unsigned columns)
{
    unsigned w = control & 0x4000 ? 64 : 32, h = control & 0x8000 ? 64 : 32;
    const volatile uint16_t *from = (const volatile uint16_t *)BG_VRAM + (control >> 8 & 31) * 1024u;
    const volatile uint32_t *tiles = BG_VRAM + (control >> 2 & 3) * 512u * 8u;
    volatile uint32_t *strip = BG_VRAM + copy_room.strips * 8u;
    uint32_t known[CACHE_TILES / 32] = { 0 }, blank[CACHE_TILES / 32] = { 0 };
    for (unsigned y = 0; y < 8; y++)
        strip[y] = 0;
    unsigned at = 1, count = 0;   /* the strips' next tile, the runs */
    strips.made = false;
    for (unsigned i = 0; i < columns; i++) {
        bool in_run = false;
        const volatile uint16_t *e = from + entry_at(col[i], 0, w);
        for (unsigned row = 0; row < h; row++, e += 32) {
            if (row == 32)
                e = from + entry_at(col[i], 32, w);
            if (is_blank(tiles, *e, known, blank)) {
                if (in_run) {   /* the blank tile after the run */
                    for (unsigned y = 0; y < 8; y++)
                        strip[at * 8 + y] = 0;
                    at++;
                }
                in_run = false;
                continue;
            }
            if (!in_run) {
                if (count == STRIPS)
                    return false;
                strips.column[count] = (uint8_t)i;
                strips.col[count] = col[i];
                strips.first[count] = (uint8_t)row;
                strips.rows[count] = 0;
                strips.at[count++] = (uint16_t)(at - 1);
                in_run = true;
            }
            if (at + 2 > STRIP_TILES)
                return false;
            const volatile uint32_t *t = tiles + (*e & 0x3FF) * 8u;
            for (unsigned y = 0; y < 8; y++)
                strip[at * 8 + y] = entry_row(t, *e, y);
            strips.rows[count - 1]++;
            at++;
        }
        if (in_run) {
            for (unsigned y = 0; y < 8; y++)
                strip[at * 8 + y] = 0;
            at++;
        }
    }
    strips.count = (uint8_t)count;
    strips.control = control;
    strips.blanked[0] = strips.blanked[1] = false;
    strips.made = true;
    return true;
}

/* Copy `c`: each column the lines show moved down its map by `moved[i]`
 * lines, its tiles' rows taken from its strips; its other rows blank.
 * False where its tiles are more than the room holds. */
static bool copy_the_columns(const uint16_t *moved, unsigned c)
{
    uint16_t control = strips.control;
    unsigned w = control & 0x4000 ? 64 : 32, h = control & 0x8000 ? 64 : 32;
    const volatile uint16_t *from = (const volatile uint16_t *)BG_VRAM + (control >> 8 & 31) * 1024u;
    const volatile uint32_t *strip = BG_VRAM + copy_room.strips * 8u;
    volatile uint16_t *to = (volatile uint16_t *)BG_VRAM + copy_room.map_block[c] * 1024u;
    volatile uint32_t *made = BG_VRAM + (copy_room.run[c] + 1u) * 8u;
    uint16_t blank = (uint16_t)(copy_room.run[c] % 512), count = 0;
    if (!strips.blanked[c]) {
        volatile uint32_t *map = (volatile uint32_t *)to;
        for (unsigned k = 0; k < copy_room.screens * SCREEN_BYTES / 4u; k++)
            map[k] = (uint32_t)blank * 0x10001u;
        memset(strips.was_rows[c], 0, sizeof strips.was_rows[c]);
        strips.blanked[c] = true;
    }
    /* the rows the last copy into it took blanked first: a run may take
     * rows another took */
    for (unsigned s = 0; s < strips.count; s++)
        for (unsigned r = 0; r < strips.was_rows[c][s]; r++)
            to[entry_at(strips.col[s], (strips.was_first[c][s] + r) & (h - 1), w)] = blank;
    for (unsigned s = 0; s < strips.count; s++) {
        unsigned m = strips.col[s], rows = strips.rows[s], mv = moved[strips.column[s]];
        /* the copy's row shows the strip's tile k (the blank before the
         * run's first is 0) and the next, from line `fine` of it */
        unsigned fine = mv & 7, down = (mv & (h * 8u - 1)) >> 3, k = fine ? 0 : 1;
        unsigned row = (strips.first[s] - 1u + k - down) & (h - 1);
        strips.was_first[c][s] = (uint8_t)row;
        strips.was_rows[c][s] = (uint8_t)(rows + 1 - k);
        if (count + rows + 1 - k > COPY_TILES) {
            strips.blanked[c] = false;
            return false;
        }
        /* the run's new tiles lie one after another in its strip, from
         * line `fine` of its tile k */
        unsigned tiles = rows + 1 - k;
        memcpy((void *)made, (const void *)(strip + (strips.at[s] + k) * 8u + fine), tiles * 32u);
        made += tiles * 8u;
        for (; k <= rows; k++, row = (row + 1) & (h - 1)) {
            /* the palette of the tile that gives it the more rows */
            unsigned source = k && (fine <= 4 || k == rows) ? k - 1 : k;
            uint16_t e = from[entry_at(m, strips.first[s] + source, w)];
            to[entry_at(m, row, w)] = (uint16_t)((blank + 1 + count++) | (e & 0xF000));
        }
    }
    return true;
}

/* Layer `n` shown on its GBA layer `own` from a copy of its map with its
 * columns moved as the line's offsets move them; false where it can't be
 * (a split layer, horizontal offsets, no room, or a copy made in this frame
 * for another line's). */
static bool show_columns_copied(LineRegisters *r, const LineState *st, unsigned n, unsigned own, const LineOffsets *o)
{
    if (layers[n].split || layers[n].overlaid)
        return false;
    uint16_t names = (uint16_t)(OFFSET_LAYER_BIT << n);
    for (unsigned j = 0; j < OFFSET_COLUMNS; j++)
        if (o->across[j] & names)
            return false;
    CopyKey key = { .control = (uint16_t)(r->bgcnt[own] & 0xDF0C), .hofs = st->hofs[n], .vofs = st->vofs[n],
                    .layer = (uint8_t)n };
    /* the columns the line shows: the first, from the screen's first dot,
     * moved by none; then each from the dot its tile starts at, by the
     * offsets of the third layer's column its dot left of it is in */
    unsigned fine = key.hofs & 7, j = (o->hofs & 7) >= fine ? 1 : 0, columns = 1;
    for (int dot = 8 - (int)fine; dot <= VID_WINDOW_LAST_DOT && columns < COPY_COLUMNS; dot += 8, j++) {
        uint16_t down = o->down[j];
        key.moved[columns++] = down & names ? (uint16_t)((down & OFFSET_SCROLL) - key.vofs) : 0;
    }
    unsigned screens = (key.control & 0x4000 ? 2u : 1u) * (key.control & 0x8000 ? 2u : 1u);
    if (!copy_room.made && (copy_room.missed || !make_room_for_the_copies(screens, false)))
        return false;
    if (copy_room.screens < screens)
        return false;
    copy_room.asked = true;
    if (!copied.made || copied.stale || memcmp(&key, &copied.key, sizeof key) != 0) {
        if (copied.fresh)
            return false;
        bool stale = copied.stale;
        copied.made = copied.stale = false;
        copied.fresh = true;   /* made, or tried, once a frame */
        if (stale || !strips.made || strips.control != key.control || strips.hofs != key.hofs) {
            unsigned w = key.control & 0x4000 ? 64 : 32;
            uint8_t col[COPY_COLUMNS];
            col[0] = (uint8_t)((key.hofs >> 3) & (w - 1));
            for (unsigned i = 1; i < columns; i++)
                col[i] = (uint8_t)(((key.hofs + 8 * i - fine) >> 3) & (w - 1));
            strips.hofs = key.hofs;
            if (!make_strips(key.control, col, columns))
                return false;
        }
        unsigned c = copied.shown ^ 1;
        if (!copy_the_columns(key.moved, c))
            return false;
        VidBg map = { 0 };
        vid_bg_map_decode(&map, st->map[n]);
        copied = (Copied){ .key = key, .map_byte = (uint16_t)(map.map_base << 1), .map_bytes = map_bytes(&map),
                           .shown = (uint8_t)c, .made = true, .fresh = true };
    }
    r->bgcnt[own] = (uint16_t)((r->bgcnt[own] & ~0x1F0Cu) | (copy_room.run[copied.shown] / 512u) << 2 |
                               copy_room.map_block[copied.shown] << 8);
    return true;
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

/* Whether a run of a layer whose tiles in front are shown again stands in
 * front, on the line `y` lines down the GBA's screen: where its map's units
 * under the run's two ends on that row have tiles in front (the menus' list
 * windows, which stand in front of what the first layer has under them). */
static bool runs_in_front(unsigned n, const MovedColumns *run, int y)
{
    const Layer *l = &layers[n];
    if (!l->overlaid)
        return false;
    VidBg map = { 0 };
    vid_bg_map_decode(&map, map_byte_of(&snes.bg[n]));
    unsigned row = (unsigned)(run->vofs + y) >> 3 & (map.map_h - 1u);
    for (int x = run->first; x <= run->last; x += run->last - run->first > 0 ? run->last - run->first : 1) {
        unsigned col = (unsigned)(run->hofs + x) >> 3 & (map.map_w - 1u);
        uint32_t byte = (col >= 32 ? SCREEN_BYTES : 0) + (row >= 32 ? (map.map_w == 64 ? 2 : 1) * SCREEN_BYTES : 0) +
                        (row & 31) * 64 + (col & 31) * 2;
        unsigned unit = byte / UNIT_BYTES;
        if (!(units_in_front[n][unit / 32] >> (unit % 32) & 1))
            return false;
    }
    return true;
}

/* A layer's tiles in front shown again on a GBA layer `g` free on a line
 * (layers_to_split): its copy of them, scrolled as the layer and shown where
 * it is, at the priority its tiles in front have; where the line shows the
 * layer's own map. */
static void show_tiles_in_front_again(LineRegisters *r, uint8_t free_layers)
{
    for (unsigned n = 0; n < 4 && free_layers; n++) {
        const Layer *l = &layers[n];
        unsigned own = l->overlaid ? own_layer(n) : NO_LAYER;
        if (own == NO_LAYER || (r->bgcnt[own] >> 8 & 31) != l->map_byte / SCREEN_BYTES)
            continue;
        unsigned g = (unsigned)__builtin_ctz(free_layers);
        r->bgcnt[g] = (uint16_t)((r->bgcnt[own] & ~0x1F03u) | (l->front_byte / SCREEN_BYTES) << 8 | order->priority[n][1]);
        r->hofs[g] = r->hofs[own];
        r->vofs[g] = r->vofs[own];
        r->winin |= (uint16_t)((r->winin & 0x0101u << own) >> own << g);
        r->winout |= (uint16_t)((r->winout & 0x0101u << own) >> own << g);
        r->bldcnt |= (uint16_t)((r->bldcnt & 0x0101u << own) >> own << g);
        return;
    }
}

/* The first two layers' moved columns on a band's line of a frame laid out
 * for mode 2, after its layers' control and blending are set: the layers
 * spare for them left out; then on a mode 2 line the third layer left out
 * too, whose map holds the offsets there, and each run on a GBA layer free
 * (the layer's own spare, then the third layer's), shown as its layer is,
 * through a window free or the one an earlier run with the same edges took,
 * with the layer itself left out there. A band's column moved by its own
 * lines moves the columns its offsets move further down with it, as runs
 * even where the offsets match the layer's own scroll. Kept out of the frame's lines'
 * loop: inlined there, or with the third layer left out of the line's
 * state instead, the loop ran a percent slower (the jet bike race). */
__attribute__((noinline)) static void show_moved_columns(LineRegisters *r, const LineState *st, const Band *b, int down, int at)
{
    uint16_t hidden = columns_bits;
    if (st->mode == 2)
        hidden |= gba_bits_of[VID_BG3_BIT];
    r->winin &= (uint16_t)~(hidden | hidden << 8);
    r->winout &= (uint16_t)~(hidden | hidden << 8);
    if (b->drawn)
        return;
    if (st->mode != 2) {
        show_tiles_in_front_again(r, columns_bits);
        return;
    }
    r->bldcnt &= (uint16_t)~(gba_bits_of[VID_BG3_BIT] | gba_bits_of[VID_BG3_BIT] << 8);
    uint8_t free_layers = 0, taken = 0;   /* GBA layers, and the windows given to runs, a bit each */
    for (unsigned g = 0; g < 4; g++)
        if (gba_layers[g].source == 2)
            free_layers |= (uint8_t)(1u << g);
    uint8_t unused = (uint8_t)(free_layers | columns_bits);   /* the layers no run takes */
    LineOffsets offsets;
    if (!line_offsets(st, &offsets)) {
        show_tiles_in_front_again(r, unused);
        return;
    }
    MovedColumn column = band_columns[b - bands];
    for (unsigned n = 0; n < 2; n++) {
        unsigned own = own_layer(n);
        MovedColumns runs[MOST_RUNS];
        unsigned count = own == NO_LAYER || !(st->main_layers >> n & 1) ? 0 :
                         moved_columns(st, &offsets, n, column.end ? column.end + b->left : 0, column.rows, runs);
        if (count > MOST_RUNS && !column.end && show_columns_copied(r, st, n, own, &offsets))
            continue;
        if (count > MOST_RUNS)
            count = MOST_RUNS;
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
            unused &= (uint8_t)~(1u << g);
            r->winin = (uint16_t)((r->winin & ~(gba_bits_of[1u << n] << 8 * w)) | 1u << (g + 8 * w));
            r->bgcnt[g] = r->bgcnt[own];
            if (runs_in_front(n, &runs[k], at + down + 1))
                r->bgcnt[g] = (uint16_t)((r->bgcnt[g] & ~3u) | order->priority[n][1]);
            r->hofs[g] = (uint16_t)(runs[k].hofs + b->left);
            r->vofs[g] = (uint16_t)(runs[k].vofs + down + 1);
            if (r->bldcnt & 1u << own)
                r->bldcnt |= (uint16_t)(1u << g);
            if (r->bldcnt & 0x100u << own)
                r->bldcnt |= (uint16_t)(0x100u << g);
        }
    }
    show_tiles_in_front_again(r, unused);
}

/* The GBA's two windows set to show the SNES's four regions of a line: in
 * window 1 alone, in window 2 alone, in both and in neither. Where both
 * windows are the GBA shows its first window's choice, so that one carries
 * whichever SNES window shows the same there; where neither does (both
 * masks inverted, so the picture shows only where the windows meet: the
 * palace's edges and its curtain in the attract demo, issue #565), the
 * first carries the windows' meeting and the second whatever of the rest
 * differs from outside them. Four regions all different can't be shown and
 * keep window 1's choice where they meet. */
static void window_regions(LineRegisters *r, const LineState *st, const Band *b, uint16_t neither)
{
    const VidWindows *w = &st->windows;
    uint16_t one = line_layer_bits(st, region_layers(st, true, false));
    uint16_t two = line_layer_bits(st, region_layers(st, false, true));
    uint16_t win1 = gba_window(w->w1_left, w->w1_right, b), win2 = gba_window(w->w2_left, w->w2_right, b);
    uint8_t meet_left = w->w1_left > w->w2_left ? w->w1_left : w->w2_left;
    uint8_t meet_right = w->w1_right < w->w2_right ? w->w1_right : w->w2_right;
    bool meet = w->w1_left <= w->w1_right && w->w2_left <= w->w2_right && meet_left <= meet_right;
    uint16_t both = meet ? line_layer_bits(st, region_layers(st, true, true)) : one;
    bool four_kinds = one != two && one != neither && two != neither;
    if (both == one || (both != two && four_kinds)) {
        r->win0h = win1;
        r->win1h = win2;
        r->winin = (uint16_t)(one | two << 8);
        return;
    }
    if (both == two) {
        r->win0h = win2;
        r->win1h = win1;
        r->winin = (uint16_t)(two | one << 8);
        return;
    }
    r->win0h = gba_window(meet_left, meet_right, b);
    if (one == neither) {
        r->win1h = win2;
        r->winin = (uint16_t)(both | two << 8);
        return;
    }
    /* Window 1 alone shows what window 2 alone does, or what neither does:
     * the second window spans both, or window 1. */
    uint8_t span_left = w->w1_left < w->w2_left ? w->w1_left : w->w2_left;
    uint8_t span_right = w->w1_right > w->w2_right ? w->w1_right : w->w2_right;
    r->win1h = two == neither ? win1 : gba_window(span_left, span_right, b);
    r->winin = (uint16_t)(both | one << 8);
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
    r.winout = line_layer_bits(st, region_layers(st, false, false));
    if (st->windows.win_main & (VID_ALL_LAYERS))
        window_regions(&r, st, b, r.winout);
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
        show_moved_columns(&r, st, b, down, at);
    return r;
}

/* ---- Lines that show what one before them did ----
 *
 * A line whose scroll alone changed, and which shows what one of the last
 * two such lines worked out whole in its band showed, takes the change made
 * for that line again rather than working its registers out: the Ocean
 * Palace in the attract demo steps its first layer's scroll back and forth
 * a few dots each line over mode 2's moved columns, and had each of its 160
 * lines worked out whole, about twenty GBA frames a picture. Not where a
 * line's registers are its own line's as well as its looks': a Mode 7
 * line's plane, a drawn panel's line, mode 2's runs of a layer whose tiles
 * in front are shown again (runs_in_front), a band that cuts its maps,
 * whose cut layer_control moves with each line's scroll (a line taken
 * again would leave the cut where an earlier line put it); nor a line
 * showing a copy of a layer's moved columns, whose place a later line's
 * copy may take (show_columns_copied). */
typedef struct {
    LineState st[2];
    const Band *band[2];
    uint8_t change[2];   /* the change each line's registers were made */
    uint8_t kept;        /* the lines kept, a bit each */
    uint8_t next;        /* the one to keep the next line in */
} LinesKept;

/* Whether a line's registers show a layer from a copy of its moved columns. */
static bool shows_a_copy(const LineRegisters *r)
{
    for (unsigned g = 0; copy_room.made && g < 4; g++)
        for (unsigned c = 0; c < COPIES; c++)
            if ((r->bgcnt[g] >> 8 & 31) == copy_room.map_block[c])
                return true;
    return false;
}

/* Whether a line's registers are its looks' alone, so that another line
 * of its band that looks the same takes them. */
static bool looks_alone(const LineState *st, const Band *b)
{
    return st->mode != 7 && !b->drawn && !b->cut_dots && !(st->mode == 2 && (layers[0].overlaid || layers[1].overlaid));
}

/* The change made for a kept line of band `b` that shows what `st` does, or
 * NO_CHANGE. */
enum { NO_CHANGE = 0xFF, NOT_WHOLE = 0xFE };
static uint8_t shown_before(const LinesKept *k, const LineState *st, const Band *b)
{
    for (unsigned i = 0; i < 2; i++)
        if ((k->kept >> i & 1) && k->band[i] == b && memcmp(&k->st[i], st, sizeof *st) == 0)
            return k->change[i];
    return NO_CHANGE;
}

static void keep_line(LinesKept *k, const LineState *st, const Band *b, uint8_t change)
{
    unsigned i = k->next;
    k->st[i] = *st;
    k->band[i] = b;
    k->change[i] = change;
    k->kept |= (uint8_t)(1u << i);
    k->next = (uint8_t)(i ^ 1);
}

/* The change made for a kept line that shows what a line whose scroll
 * alone changed does, or NO_CHANGE. Kept out of the frame's lines' loop, as
 * show_moved_columns is, with keep_the_line, below: the menus' and the
 * race's lines, few of which show what one before them did, took half a
 * percent longer with the kept lines' work in the loop. */
__attribute__((noinline)) static uint8_t made_before(const LinesKept *k, const LineState *st, const Band *b)
{
    return looks_alone(st, b) ? shown_before(k, st, b) : NO_CHANGE;
}

/* A line whose scroll alone changed, worked out whole, kept where another
 * may take its registers `r`: the last of the `count` changes made, which
 * they are where the changes had room. */
__attribute__((noinline)) static void keep_the_line(LinesKept *k, const LineState *st, const Band *b,
                                                    const LineRegisters *r, uint8_t count)
{
    if (count < GBA_LINES && looks_alone(st, b) && !shows_a_copy(r))
        keep_line(k, st, b, (uint8_t)(count - 1u));
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
 * stopped where it hasn't. In the vertical blank. DMA 0 and 3: DMA 1 and 2
 * are the sound's, the only two that feed its FIFOs. DMA's control: on, at each
 * horizontal blank, again, to the registers again; words, or a halfword. */
static const uint32_t DMA_PLANE = 0xA6600000u | sizeof(PlaneLine) / 4;
static const uint32_t DMA_STRENGTH = 0xA2400000u | 1;
static volatile bool plane_dma, strength_dma;
__attribute__((target("arm"), section(".iwram"), noinline)) static void arm_the_lines(void)
{
    DMA0CNT = 0;
    DMA3CNT = 0;
    if (plane_dma && shown_plane_table) {
        const volatile PlaneLine *lines = (const volatile PlaneLine *)shown_plane_table;
        const volatile uint32_t *first = (const volatile uint32_t *)&lines[0];
        for (unsigned w = 0; w < sizeof(PlaneLine) / 4; w++)
            BG2_PLANE[w] = first[w];
        DMA0SAD = (uint32_t)&lines[1];
        DMA0DAD = (uint32_t)BG2_PLANE;
        DMA0CNT = DMA_PLANE;
    }
    if (strength_dma) {
        BLDY = line_strength[0];
        DMA3SAD = (uint32_t)&line_strength[1];
        DMA3DAD = (uint32_t)&BLDY;
        DMA3CNT = DMA_STRENGTH;
    }
}

/* The line counter's interrupt for the line above the next change, or for
 * the vertical blank once the frame has no more. */
__attribute__((target("arm"), section(".iwram"))) static void await_change(void)
{
    uint16_t line = next_change < changes ? (uint16_t)(change_line[shown_place(next_change)] - 1) : GBA_LINES;
    DISPSTAT = (uint16_t)((DISPSTAT & 0x00FF & ~DISPSTAT_HBLANK_IRQ) | line << 8);
}

__attribute__((long_call, noinline)) static bool show_pending_at_blank(void);

/* In the vertical blank, a picture made too late for the last one shown
 * (show_the_frame), or else the first line's registers; on the line above a
 * change, its horizontal blank's interrupt; at that, the change's registers,
 * keeping the horizontal blank's interrupt where the next change is the
 * next line. In ARM, in fast RAM. */
__attribute__((target("arm"), section(".iwram"), noinline)) static void on_line(void)
{
    uint16_t raised = REG_IF & (IRQ_VCOUNT | IRQ_HBLANK);
    uint16_t line = VCOUNT;
    if (by_line && (raised & IRQ_VCOUNT)) {
        if (line >= GBA_LINES) {
            if (!show_pending_at_blank()) {
                set_line(&change_registers[shown_place(0)]);
                arm_the_lines();
                next_change = 1;
                await_change();
            }
        } else {
            REG_IF = IRQ_HBLANK;
            DISPSTAT = (uint16_t)(DISPSTAT | DISPSTAT_HBLANK_IRQ);
        }
    } else if (by_line && (raised & IRQ_HBLANK) && line < GBA_LINES) {
        /* the change for the next line; or, where the interrupt came late
         * (another handler ran on past this line's blank), the last of
         * those it passed, so that a band shows its own lines from here
         * rather than the last band's through to the next change (the
         * naming's rows a gap's six lines low, as the cursor moved) */
        unsigned passed = changes;
        while (next_change < changes && change_line[shown_place(next_change)] <= line + 1)
            passed = next_change++;
        if (passed < changes)
            set_line(&change_registers[shown_place(passed)]);
        if (!(next_change < changes && change_line[shown_place(next_change)] == line + 2))
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
    if (columns_moved)
        f->bands = hash_on(f->bands, band_columns, sizeof band_columns[0] * band_count);
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
        if (count < GBA_LINES && (count == 0 || memcmp(&black, made_change(count - 1u), sizeof black) != 0)) {
            *made_change(count) = black;
            *made_line(count++) = bands[*next].at;
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
ARM_CODE static void pass_over_unchanged(Slot *sl, const VidLineEffect *e, uint16_t stop);

ARM_CODE static uint16_t next_transfer(Slot *sl, const VidLineEffect *e, uint16_t stop, bool apart)
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
ARM_CODE static void pass_over_unchanged(Slot *sl, const VidLineEffect *e, uint16_t stop)
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
    LinesKept kept = { .kept = 0 };   /* (Lines that show what one before them did, above) */
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
            uint8_t made_as = NOT_WHOLE;   /* a line worked out whole: the change it takes again, or NO_CHANGE */
            /* Where only the scroll, the plane or the colour math may have
             * changed (but not a band cut's scroll, which moves the cut in
             * its maps, nor mode 2's, which moves its columns, nor that of a
             * layout showing a layer's tiles in front again, whose layer
             * isn't its GBA layer's scroll source, nor between
             * Mode 7 and another mode), the last
             * change's registers with those: a line's scroll (but on the
             * drawn panel, whose layers' scroll is its own); on a Mode 7
             * line, its plane (the first layer's scroll is the plane's); the
             * blending, a step of its strength alone no change at all. */
            bool cheap = count && !band_starts && !(touched & CHANGED_OTHER) &&
                         !((b->cut_dots || st.mode == 2 || split_wanted >> OVERLAY_SHIFT) && (touched & CHANGED_SCROLL)) &&
                         (st.mode == 7) == (shown.mode == 7) && !(st.mode == 7 && b->drawn) &&
                         !((touched & CHANGED_MATH) && (sub_screens_own(&st) || sub_screens_own(&shown)));
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
                    r = *made_change(count - 1u);
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
                    same.bldy = made_change(count - 1u)->bldy;
                    if (!scrolled && memcmp(&same, made_change(count - 1u), sizeof same) == 0) {
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
                /* a line whose scroll alone changed may show what one before it did */
                made_as = touched == CHANGED_SCROLL ? made_before(&kept, &st, b) : NO_CHANGE;
                if (made_as != NO_CHANGE)
                    r = *made_change(made_as);
                else
                    r = line_registers(&st, layers_enabled, b, down, at);
                memcpy(&shown, &st, sizeof st);
                again = true;
            }
            if (again && count < GBA_LINES && (count == 0 || memcmp(&r, made_change(count - 1u), sizeof r) != 0)) {
                *made_change(count) = r;
                *made_line(count++) = (uint8_t)at;
                set_strength((unsigned)at, r.bldy);
            }
            if (made_as == NO_CHANGE && touched == CHANGED_SCROLL && count)
                keep_the_line(&kept, &st, b, &r, count);
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

/* The frame's picture shown, all of it from one vertical blank: its sprites
 * (present_sprites keeps them aside until then), its changes and the first
 * line's registers, the line effects' DMA armed. A frame whose work ran on
 * past the vertical blank is shown from the one the next picture is made
 * in, the last frame's staying whole until then, so that no frame the GBA
 * shows has this frame's sprites over the last frame's lines (the naming
 * screen's hand on a row the bands didn't show yet) or the changes of both
 * at once; one that shows what the last showed, as most do, takes the
 * GBA's frame as it stands. A frame of one change sets it alone; one of
 * more has the interrupts set the rest, line by line. */
/* Whether the frame's `count` changes made differ from those shown. */
static bool lines_differ(uint8_t count)
{
    if (count != changes || count * 2u > GBA_LINES)
        return true;
    for (unsigned k = 0; k < count; k++)
        if (change_line[change_place(shown_end, k)] != *made_line(k) ||
            memcmp(&change_registers[change_place(shown_end, k)], made_change(k), sizeof(LineRegisters)) != 0)
            return true;
    return false;
}

static bool frame_differs(bool made, uint8_t count)
{
    if (sprites_aside)
        return true;
    if (plane_dma != plane_shown || strength_dma != strength_steps)
        return true;
    return made && lines_differ(count);
}

static bool show_pending, pending_made;   /* a frame's picture to show (show_the_pending_frame, below) */
static uint8_t pending_count;
static uint32_t made_frame IN_FAST_RAM, pending_frame IN_FAST_RAM;   /* the game's frames whose pictures they are */
static void (*tell_shown)(uint32_t frame) IN_FAST_RAM;   /* told the frame whose picture goes on the screen */
static bool picture_hidden;   /* black since hide_the_picture, until a frame is shown */

/* The frame's picture shown from here, with the interrupts off; or, `at_blank`,
 * from the vertical blank, the line counter's interrupt there setting its
 * first line's registers (on_line), the picture hidden until then. */
static void frame_shown(bool made, uint8_t count, bool at_blank, uint32_t frame)
{
    if (tell_shown)
        tell_shown(frame);
    if (made) {
        if (lines_differ(count))
            looks_made_from++;
        show_from_end(made_end);
    }
    changes = count;
    plane_dma = plane_shown;
    shown_plane_table = plane_table;
    strength_dma = strength_steps;
    if (sprites_aside) {
        for (unsigned n = 0; n < SPRITES; n++)
            for (unsigned a = 0; a < 3; a++)
                OAM[n * 4 + a] = sprites_shown[n * 3 + a];
        sprites_aside = false;
    }
    WIN0V = GBA_LINES;
    WIN1V = GBA_LINES;
    picture_hidden = false;
    if (at_blank) {
        if (!by_line) {
            gba_interrupt_handler(IRQ_VCOUNT | IRQ_HBLANK, on_line);
            DISPSTAT = (uint16_t)(DISPSTAT | DISPSTAT_VCOUNT_IRQ);
            REG_IF = IRQ_VCOUNT | IRQ_HBLANK;
            REG_IE = (uint16_t)(REG_IE | IRQ_VCOUNT | IRQ_HBLANK);
            by_line = true;
        }
        next_change = count;   /* none left before the vertical blank */
        await_change();
        return;
    }
    set_line(&change_registers[shown_place(0)]);
    arm_the_lines();
    /* one change and no line DMA: nothing to do at each blank. The line
     * DMA is armed again at every vertical blank, since a game frame can
     * run on for several GBA frames before the next picture: left running,
     * it read on past its table (the title's pendulum, a part of the
     * plane, fell apart through the frame that left Mode 7). */
    if (count == 1 && !plane_dma && !strength_dma) {
        by_line = false;
        REG_IE = (uint16_t)(REG_IE & ~(IRQ_VCOUNT | IRQ_HBLANK));
        DISPSTAT = (uint16_t)(DISPSTAT & 0x00FF & ~(DISPSTAT_HBLANK_IRQ | DISPSTAT_VCOUNT_IRQ));
        return;
    }
    if (!by_line) {
        gba_interrupt_handler(IRQ_VCOUNT | IRQ_HBLANK, on_line);
        DISPSTAT = (uint16_t)(DISPSTAT | DISPSTAT_VCOUNT_IRQ);
        REG_IF = IRQ_VCOUNT | IRQ_HBLANK;
        REG_IE = (uint16_t)(REG_IE | IRQ_VCOUNT | IRQ_HBLANK);
        by_line = true;
    }
    next_change = 1;
    await_change();
}

/* The line counter's interrupt at the vertical blank, where the frame shown
 * has none: its lines left as they stand, no change past those it has. */
static void await_the_blank(void)
{
    if (by_line)
        return;
    gba_interrupt_handler(IRQ_VCOUNT | IRQ_HBLANK, on_line);
    next_change = changes;
    await_change();
    DISPSTAT = (uint16_t)(DISPSTAT | DISPSTAT_VCOUNT_IRQ);
    REG_IF = IRQ_VCOUNT | IRQ_HBLANK;
    REG_IE = (uint16_t)(REG_IE | IRQ_VCOUNT | IRQ_HBLANK);
    by_line = true;
}

static void show_the_frame(bool made, uint8_t count)
{
    bool late = VCOUNT < GBA_LINES, differs = late && (picture_hidden || frame_differs(made, count));
    REG_IME = 0;
    if (!late) {
        frame_shown(made, count, false, made_frame);
    } else if (!differs) {
        if (made)
            show_from_end(made_end);
        sprites_aside = false;
        if (tell_shown)
            tell_shown(made_frame);
    } else if (picture_hidden) {
        /* black meanwhile, so nothing is shown torn: shown whole from the
         * vertical blank, rather than from the next picture's, which a
         * scene's loading frames put off by a dozen GBA frames and more */
        frame_shown(made, count, true, made_frame);
    } else {
        /* shown from the next vertical blank (show_pending_at_blank) */
        pending_made = made;
        pending_count = count;
        pending_frame = made_frame;
        show_pending = true;
        await_the_blank();
    }
    REG_IME = 1;
}

/* A picture made too late for the vertical blank it was to be shown from,
 * shown from the next: the line counter's interrupt there (on_line) shows
 * it, so that a game frame that runs on for GBA frames after it doesn't
 * keep it off the screen until the next picture is made. The world map's
 * flyover, two GBA frames a game frame, showed each picture a game frame
 * late, its layers a dot or two from the sprites drawn over them. False
 * where none waits. In the vertical blank, from the interrupt. */
__attribute__((long_call, noinline)) static bool show_pending_at_blank(void)
{
    if (!show_pending)
        return false;
    show_pending = false;
    frame_shown(pending_made, pending_count, false, pending_frame);
    return true;
}

/* A frame's picture made after the vertical blank it was to be shown from,
 * where the interrupt at the next didn't show it (show_pending_at_blank):
 * shown from the vertical blank the next picture is made in, before it. */
static void show_the_pending_frame(void)
{
    if (!show_pending)
        return;
    REG_IME = 0;
    frame_shown(pending_made, pending_count, false, pending_frame);
    show_pending = false;
    REG_IME = 1;
}

/* The frame's lines, and the GBA's registers for them, worked out but not
 * shown yet (show_the_frame); `made` is whether they were worked out rather
 * than the last frame's kept, and `left` what the effects leave set. A frame
 * that starts from what the last one did, its tables unchanged, makes the
 * same changes and leaves the registers as it did: those stand, unworked. */
static uint8_t make_lines(Platform *pf, uint16_t layers_enabled, bool laid_out, bool *made_out, LineState *left)
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
    bool made = false;   /* whether the frame's changes were worked out, rather than the last frame's kept */
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
        made_end = shown_end ^ 1;
        made = true;
        copied.fresh = false;
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
    *made_out = made;
    *left = st;
    return count;
}

/* What the frame's line effects leave stands, as the SNES's registers do:
 * the next frame's lines start from it. */
static void lines_leave(const LineState *st)
{
    snes.windows = st->windows;
    snes.math = st->math;
    snes.fixed_color = st->fixed_color;
    snes.main_layers = st->main_layers;
    snes.sub_layers = st->sub_layers;
    for (unsigned n = 0; n < 3; n++)
        snes.bg[n].hofs = st->hofs[n], snes.bg[n].vofs = st->vofs[n];
    snes.m7 = st->m7;
    snes.m7_latch = st->m7_latch;
}

/* The picture hidden, black, until this frame's lines are set: while a
 * layout whose tiles start afresh converts what they name, frames of work
 * through which the last frame's lines would show maps naming tiles not
 * converted yet (the room's tiles in stray rows as a scene was set behind
 * the opening's first words). */
static void hide_the_picture(void)
{
    if (by_line) {
        by_line = false;
        REG_IE = (uint16_t)(REG_IE & ~(IRQ_VCOUNT | IRQ_HBLANK));
        DISPSTAT = (uint16_t)(DISPSTAT & 0x00FF & ~(DISPSTAT_HBLANK_IRQ | DISPSTAT_VCOUNT_IRQ));
    }
    DMA0CNT = 0;
    DMA3CNT = 0;
    DISPCNT = (uint16_t)(DISPCNT & ~(0xF * DISPCNT_BG0 | DISPCNT_OBJ));
    BLDCNT = 0;
    BG_PALETTE[0] = 0;
    picture_hidden = true;
}

/* Whether the frame shows nothing: blanked or dark from its top line to its
 * bottom, no line effect setting the brightness. */
static bool shows_nothing(const VidHost *v)
{
    if (!dark(&v->display) || (v->below_display && v->display_from && !dark(&v->display_below)))
        return false;
    uint8_t slots = (uint8_t)(v->line_effects | (v->below_line_effects ? v->line_effects_below : 0));
    for (unsigned i = 0; i < 8; i++)
        if (slots >> i & 1 && snes.slot[i].target == VID_LINE_BRIGHTNESS)
            return false;
    return true;
}

/* Whether any of the places from `first`, `count` of them, has something in
 * the picture shown (`shown`, a bit each). */
static bool places_shown(const uint8_t shown[PLACES / 8], uint32_t first, uint32_t count)
{
    for (uint32_t p = first; p < first + count && p < PLACES; p++)
        if (shown[p / 8] >> (p % 8) & 1)
            return true;
    return false;
}

/* Whether a layout's caches that start afresh or lost a place (`kept`, those
 * that didn't, a bit each) write where the picture shown has something
 * (`shown`, the places the last layout took, a bit each): their free places,
 * which their tiles will take; their blank, cleared as the layout gives it;
 * and the maps that name their tiles, each converted again with the tiles'
 * new places while the picture shown may still read it. */
static bool lays_over_the_picture(const uint8_t shown[PLACES / 8], uint8_t kept)
{
    for (unsigned c = 0; c < CACHES; c++) {
        if (!caches[c].used || kept >> c & 1)
            continue;
        uint32_t first = caches[c].block * 512u, end = first + CACHE_TILES < PLACES ? first + CACHE_TILES : PLACES;
        for (uint32_t k = first / 8; k < end / 8; k++)
            if (shown[k] & (uint8_t)~taken[k])
                return true;
        if (places_shown(shown, first + caches[c].blank, 1))
            return true;
    }
    MapPlace now[MAP_PLACES];
    unsigned count = places_of_the_maps(now);
    for (unsigned i = 0; i < count; i++) {
        if (kept >> layers[now[i].layer].cache & 1)
            continue;
        if (places_shown(shown, now[i].gba_byte / 32u, now[i].bytes / 32u) ||
            (now[i].front_byte != NO_PLACE && places_shown(shown, now[i].front_byte / 32u, now[i].bytes / 32u)))
            return true;
    }
    return false;
}

/* The picture laid out afresh (`keep_windows`: only for tiles that found
 * no place, or to give the plane's room back); where a cache starting
 * afresh puts its tiles where the picture shown has something, the picture
 * is hidden until this frame's is shown. One whose window the picture
 * shown leaves free keeps it up: the title's logo, put where the clock's
 * plane wasn't, went black twice as it began to draw in. */
static void lay_out_again(bool keep_windows)
{
    bool missed = misses != 0;
    uint8_t shown[PLACES / 8];
    memcpy(shown, taken, sizeof shown);
    uint8_t kept = lay_out(usage_known, keep_windows);
    if (lays_over_the_picture(shown, kept))
        hide_the_picture();
    layout_changed(kept, missed);
    snes.settings_changed = false;
}

/* VRAM's changes since the last picture converted, and the palettes. */
static void convert_the_changes(bool laid_out)
{
    uint32_t written = 0;
    for (unsigned w = 0; w < UNITS / 32; w++) {
        uint32_t units = snes.changed[w];
        written |= units;
        if (units & 0xFFFF)
            convert_block(2 * w, units & 0xFFFF);
        if (units >> 16)
            convert_block(2 * w + 1, units >> 16);
        snes.changed[w] = 0;
    }
    if (written)
        looks_made_from++;   /* the layers' maps or tiles or the sprites' tiles (Sprites raised) */
    if (turn_the_sprite_page())
        snes.sprites_changed = true;
    if (laid_out)
        layout_misses = misses;
    if (snes.palette_written) {
        present_palettes();
        snes.palette_written = false;
    }
}

/* The GBA's layers, where the mode, the layout or the maps' priorities
 * changed (a layer to split, or a split one to join, makes the layout
 * again), the view's bands and the sprites; the SNES's layers shown, a bit
 * each. */
static uint16_t arrange_the_layers(bool laid_out)
{
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
    if (snes.sprites_changed || (sprites_overlap && looks_last_made_from != looks_made_from)) {
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
    return layers_enabled;
}

/* Whether the picture is laid out afresh: the settings changed what it is
 * made from, the lines show Mode 7 or stop showing it, or they show text in
 * another mode than the layout's (`restart`: tiles that found no place). */
static bool settings_ask_for_a_layout(void)
{
    return snes.settings_changed || (plane_wanted() && !plane_room && !plane_refused) ||
           text_mode() != laid_out_mode || (laid_out_mode == 2 && third_on_lines && !laid_out_third);
}

static bool layout_wanted(bool restart)
{
    /* the plane's room made where a line shows Mode 7, and given back where none does */
    return settings_ask_for_a_layout() || restart || (plane_room && !plane_wanted());
}

/* The picture made from the SNES's memory and registers as the frame left
 * them: laid out again where it must be, VRAM's changes converted, the
 * palettes, the GBA's layers and sprites, then the lines worked out. A frame
 * whose lines ask for a layout the picture doesn't have yet (a map a line
 * effect shows, the lines' mode) is laid out again and its lines worked out
 * once more before it is shown: shown first, it would show a map not yet
 * converted, and the next frame's layout would convert the scene a second
 * time under the picture shown (the dialog window's first row in stray
 * tiles as the opening's narration begins). */
/* The picture black from here where the frame shows nothing. */
static void black_where_nothing_shows(Platform *pf)
{
    if (!picture_hidden && shows_nothing(&pf->video))
        hide_the_picture();
}

void gba_video_tell_shown(void (*shown)(uint32_t frame)) { tell_shown = shown; }

void gba_video_present(Platform *pf, uint32_t frame)
{
    show_the_pending_frame();
    made_frame = frame;
    /* Tiles that found no place have the caches started afresh: a second
     * on where a tile missed since the layout, eight where only the layout's
     * own misses stand (starting afresh from the same maps misses the same
     * tiles, and a layout is about ten GBA frames' work). */
    bool restart = misses && ++frames_since_layout >= (misses > layout_misses ? 60 : 480);
    bool keep_windows = !settings_ask_for_a_layout(), laid_out = layout_wanted(restart), made;
    LineState left;
    uint8_t count;
    for (unsigned pass = 0;; pass++) {
        if (laid_out)
            lay_out_again(pass == 0 ? keep_windows : !settings_ask_for_a_layout());
        /* a layout's lines worked out first, before its maps are converted:
         * where they ask for another, that one's maps are converted alone */
        if (laid_out && pass == 0) {
            make_lines(pf, arrange_the_layers(true), true, &made, &left);
            if (layout_wanted(false))
                continue;
            copied.made = strips.made = false;   /* made from maps not converted yet */
        }
        convert_the_changes(laid_out);
        uint16_t layers_enabled = arrange_the_layers(laid_out);
        count = make_lines(pf, layers_enabled, laid_out, &made, &left);
        if (pass == 1 || !layout_wanted(false))
            break;
        laid_out = true;
    }
    lines_leave(&left);
    show_the_frame(made, count);
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
