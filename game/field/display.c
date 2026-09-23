/* Field mode: what its vblank work sends to the display, and its line
 * interrupt.
 *
 * Every frame the field's vblank handler (field/nmi.c) uploads the sprite table
 * and the queued transfers of tile graphics, and the deferred uploads of the
 * layers' edge runs, which bring the rows and columns scrolling into view onto
 * the layers' tile maps (field/layers.c works out where they are). When a map
 * loads it sets the video mode up, copies its screens' settings, fills memory
 * and makes the transfers a screen transition describes. The four party
 * windows' corner tiles are kept up to date in VRAM, one of them marked as the
 * selected one.
 *
 * The line interrupt comes at line 211: the picture ends there, the rest of
 * the frame is blanked, and the time to vblank uploads the tile map's columns
 * (or, in some maps, animated tiles). */
#include "field/display.h"

#include "field/bridge.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The field's uploads use DMA channel 7: the sprite table one byte at a time,
 * VRAM in words. What the SNES backend is told for each transfer is its
 * register writes in the original's order (the service's `steps`, which
 * shadow mode compares). */
enum { FIELD_DMA = 7, DMA_ONE_REGISTER = 0x00, DMA_WORDS = 0x01, DMA_FROM_REGISTER = 0x80 };

/* Where the sprite table and the layers' tile maps are, as the transfers'
 * sources. */
enum { SPRITE_TABLE = 0x000700, TILE_MAP_BANK = 0x7E, QUEUE_BANK = 0x7F };

static const VidVramPort word_port = { .step = 1, .after_high = true };
static const VidVramPort column_port = { .step = 32, .after_high = true };

// $C0:ECA3
/* The sprite table, all of it, from the first sprite. */
static void upload_sprites(Platform *pf)
{
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI,   VID_STEP_CONTROL, VID_STEP_DEST,   VID_STEP_SOURCE_LO,
        VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI, VID_STEP_START,
        VID_STEP_END,
    };
    static const VidSnesXfer x = { FIELD_DMA, DMA_ONE_REGISTER, steps, NULL };
    vid_upload_oam(pf, PF_AT(0xC0ECA5), PF_GAME(SPRITE_TABLE), &x);
}

// $C0:B1B2
/* The queued transfers of new sprite tiles from bank $7F: each queue entry is
 * two, a VRAM address, a source and a size each (a size of 0 is 64 KiB, as
 * the hardware reads it), up to the first unused entry; then the queue is
 * emptied. */
static void upload_vram_queue(GameState *g, Platform *pf)
{
    static const uint8_t channel_steps[] = { VID_STEP_DEST, VID_STEP_CONTROL, VID_STEP_SOURCE_BANK, VID_STEP_END };
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    static const VidSnesXfer channel = { FIELD_DMA, DMA_WORDS, channel_steps, NULL };
    static const VidSnesXfer x = { FIELD_DMA, DMA_WORDS, steps, NULL };
    vid_begin_vram_uploads(pf, PF_AT(0xC0B1BC), VID_VRAM_WORD_PORT, PF_GAME((uint32_t)QUEUE_BANK << 16), &channel);
    for (unsigned i = 0; field_vram_queue_used(g, i) != 0; i++) {
        uint16_t size = field_vram_queue_size(g, i);
        vid_upload_vram(pf, PF_AT(0xC0B1D7), field_vram_queue_vram(g, i),
                        PF_GAME((uint32_t)QUEUE_BANK << 16 | field_vram_queue_source(g, i)), size ? size : 0x10000u, &x);
        size = field_vram_queue_size2(g, i);
        vid_upload_vram(pf, PF_AT(0xC0B1EB), field_vram_queue_vram2(g, i),
                        PF_GAME((uint32_t)QUEUE_BANK << 16 | field_vram_queue_source2(g, i)), size ? size : 0x10000u, &x);
    }
    set_field_vram_queue_used(g, 0, 0);
}

/* ---- The layers' edge runs ------------------------------------------------------------ */

/* The deferred uploads the vblank handler runs from each layer's upload flags:
 * twelve of one shape, each sending two or three parts of an edge run of the
 * layer's tile map, built in bank $7E, to VRAM. A part's size and VRAM address
 * are among the layer's edge runs (field_layer_edge_sizes and _vram, words);
 * a part of size 0 isn't sent; a run's parts follow one another in memory from
 * its base. The runs along a row step VRAM by a word, those down a column by
 * 32 words. */
typedef struct {
    uint8_t layer;
    uint8_t parts;
    uint8_t first_size, first_vram;   /* word indexes into the layer's edge sizes and addresses */
    uint16_t base;                    /* where the run's first part lies in bank $7E */
    bool down_a_column;
    uint32_t site[3];                 /* each part's transfer, for its origin tag */
} EdgeRun;

static uint16_t edge_word(const GameState *g, uint8_t (*byte)(const GameState *, unsigned, unsigned), unsigned layer,
                          unsigned word)
{
    uint8_t low = byte(g, layer, 2 * word);
    return (uint16_t)(byte(g, layer, 2 * word + 1) << 8 | low);
}

static void upload_edge_run(GameState *g, Platform *pf, const EdgeRun *r)
{
    static const uint8_t steps[] = {
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI, VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI, VID_STEP_SOURCE_LO,
        VID_STEP_SOURCE_HI, VID_STEP_VRAM_PORT, VID_STEP_CONTROL,   VID_STEP_DEST,      VID_STEP_SOURCE_BANK,
        VID_STEP_START,     VID_STEP_END,
    };
    const VidSnesXfer x = { FIELD_DMA, DMA_WORDS, steps, r->down_a_column ? &column_port : &word_port };
    uint16_t source = r->base;
    for (unsigned k = 0; k < r->parts; k++) {
        uint16_t size = edge_word(g, field_layer_edge_sizes_byte, r->layer, r->first_size + k);
        if (size != 0) {
            uint16_t vram = edge_word(g, field_layer_edge_vram_byte, r->layer, r->first_vram + k);
            vid_upload_vram(pf, PF_AT(r->site[k]), vram, PF_GAME((uint32_t)TILE_MAP_BANK << 16 | source), size, &x);
        }
        source = (uint16_t)(source + size);
    }
}

/* The uploads' guard: the parts are the size of an edge run, whose sources
 * can't reach round the bank to the stack page (where the original's
 * transfer would copy the return address its call left there, which the
 * port doesn't reproduce). The game's parts are a row or a column of tiles. */
enum { LARGEST_PART = 0x3000, EDGE_SIZE_WORDS = 5, LAYERS = 3 };

bool field_edge_runs_small(Snes *s)
{
    if (m_direct_page(s) != FIELD_LAYERS_PAGE || !m_bank_reaches_io(s))
        return false;
    vars_peek(true);
    bool ok = true;
    for (unsigned layer = 0; layer < LAYERS && ok; layer++)
        for (unsigned w = 0; w < EDGE_SIZE_WORDS && ok; w++)
            ok = edge_word(state_of(s), field_layer_edge_sizes_byte, layer, w) < LARGEST_PART;
    vars_peek(false);
    return ok;
}

#define EDGE_RUN_ENTRY(name, ...)                                                                                      \
    void name(Snes *s)                                                                                                 \
    {                                                                                                                  \
        static const EdgeRun run = __VA_ARGS__;                                                                        \
        upload_edge_run(state_of(s), m_platform(s), &run);                                                             \
        m_rts(s);                                                                                                      \
    }

// $C0:8445
EDGE_RUN_ENTRY(field_upload_layer1_tiles, { 0, 3, 0, 3, 0xC800, false, { 0xC0844D, 0xC08468, 0xC0848A } })
// $C0:84C1
EDGE_RUN_ENTRY(field_upload_layer2_tiles, { 1, 3, 0, 3, 0xD000, false, { 0xC084C9, 0xC084E4, 0xC08506 } })
// $C0:8523
EDGE_RUN_ENTRY(field_upload_layer3_tiles, { 2, 3, 0, 3, 0xD800, false, { 0xC0852B, 0xC08546, 0xC08568 } })
// $C0:8585
EDGE_RUN_ENTRY(field_upload_layer1_tiles_alt, { 0, 3, 0, 0, 0xC800, false, { 0xC0858D, 0xC085A8, 0xC085CA } })
// $C0:85E7
EDGE_RUN_ENTRY(field_upload_layer2_tiles_alt, { 1, 3, 0, 0, 0xD000, false, { 0xC085EF, 0xC0860A, 0xC0862C } })
// $C0:8649
EDGE_RUN_ENTRY(field_upload_layer3_tiles_alt, { 2, 3, 0, 0, 0xD800, false, { 0xC08651, 0xC0866C, 0xC0868E } })
// $C0:86AB
EDGE_RUN_ENTRY(field_upload_layer1_columns, { 0, 2, 3, 8, 0xC880, true, { 0xC086AF, 0xC086C4 } })
// $C0:86F7
EDGE_RUN_ENTRY(field_upload_layer2_columns, { 1, 2, 3, 8, 0xD080, true, { 0xC086FB, 0xC08710 } })
// $C0:8729
EDGE_RUN_ENTRY(field_upload_layer3_columns, { 2, 2, 3, 8, 0xD880, true, { 0xC0872D, 0xC08742 } })
// $C0:875B
EDGE_RUN_ENTRY(field_upload_layer1_columns_alt, { 0, 2, 3, 6, 0xC880, true, { 0xC0875F, 0xC08774 } })
// $C0:878D
EDGE_RUN_ENTRY(field_upload_layer2_columns_alt, { 1, 2, 3, 6, 0xD080, true, { 0xC08791, 0xC087A6 } })
// $C0:87BF
EDGE_RUN_ENTRY(field_upload_layer3_columns_alt, { 2, 2, 3, 6, 0xD880, true, { 0xC087C3, 0xC087D8 } })

/* ---- Setting a map's display up -------------------------------------------------------- */

// $FD:C0D7
/* The field's video mode: mode 1 with BG3 in front, the three layers' tile
 * maps 64 tiles wide at $6800, $7000 and $7800 with their tiles at $1000 and
 * $5000, no sprite or interlace settings, every scroll at 0, BG1, BG2 and the
 * sprites on the main screen, and color math adding the sub screen to a black
 * fixed color. */
static void init_video_mode(Platform *pf)
{
    vid_set_mode(pf, PF_AT(0xFDC0E2), &(VidMode){ .mode = 1, .bg3_priority = true });
    vid_set_bg_chars(pf, PF_AT(0xFDC0E6), VID_BG1, 0x1000, 0x1000);
    vid_set_bg_chars(pf, PF_AT(0xFDC0EA), VID_BG3, 0x5000, 0x0000);
    static const struct { uint16_t map; uint32_t site; } maps[3] = {
        { 0x6800, 0xFDC0EE }, { 0x7000, 0xFDC0F2 }, { 0x7800, 0xFDC0F6 },
    };
    for (int bg = 0; bg < 3; bg++)
        vid_set_bg_map(pf, PF_AT(maps[bg].site), (VidLayer)bg, &(VidBg){ .map_base = maps[bg].map, .map_w = 64, .map_h = 32 });
    vid_set_obj_config(pf, PF_AT(0xFDC0FA), &(VidObjConfig){ 0 });
    vid_set_video_config(pf, PF_AT(0xFDC0FC), &(VidVideoConfig){ 0 });
    static const uint32_t scroll_sites[3] = { 0xFDC0FE, 0xFDC106, 0xFDC10E };
    for (int bg = 0; bg < 3; bg++)
        vid_set_scroll(pf, PF_AT(scroll_sites[bg]), (VidLayer)bg, 0, 0, VID_SCROLL_H | VID_SCROLL_V);
    vid_set_screens(pf, PF_AT(0xFDC118), VID_LAYER_BIT(VID_BG1) | VID_LAYER_BIT(VID_BG2) | VID_LAYER_BIT(VID_OBJ), 0,
                    VID_SCREEN_MAIN);
    vid_set_color_math(pf, PF_AT(0xFDC11C), &(VidColorMath){ .sub_screen = true }, VID_MATH_CONTROL);
    vid_set_fixed_color(pf, PF_AT(0xFDC120), VID_RED | VID_GREEN | VID_BLUE, 0);
}

// $C0:A509
/* The map's screens: the main and sub screens' layers and the color math
 * layers as the map set them, and no mosaic. */
static void set_screens(const GameState *g, Platform *pf)
{
    vid_set_screens(pf, PF_AT(0xC0A50C), field_main_screen(g), 0, VID_SCREEN_MAIN);
    vid_set_screens(pf, PF_AT(0xC0A512), 0, field_sub_screen(g), VID_SCREEN_SUB);
    VidColorMath math = { 0 };
    vid_color_math_decode(&math, VID_MATH_LAYERS, field_color_math_layers(g));
    vid_set_color_math(pf, PF_AT(0xC0A518), &math, VID_MATH_LAYERS);
    vid_set_mosaic(pf, PF_AT(0xC0A51D), 0x00, 1);
}

// $C0:2DC8
/* The transfer a screen transition describes (field_transfer): its VRAM
 * address, mode, source and size (0 is 64 KiB). */
static void vram_transfer(const GameState *g, Platform *pf)
{
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI, VID_STEP_VRAM_PORT, VID_STEP_CONTROL, VID_STEP_DEST,
        VID_STEP_SOURCE_LO,   VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,
        VID_STEP_START,       VID_STEP_END,
    };
    uint16_t size = field_transfer_size(g);
    const VidSnesXfer x = { FIELD_DMA, field_transfer_mode(g), steps, &word_port };
    vid_upload_vram(pf, PF_AT(0xC02DCA), field_transfer_vram(g), PF_GAME(field_transfer_source(g)),
                    size ? size : 0x10000u, &x);
}

// $C0:2DF1
/* Clear the memory a screen transition describes (field_transfer's source
 * and size): the Mode 7 matrix set to 0 makes the PPU's product 0, and a
 * transfer from that register copies it over the memory. The transfer reads
 * a register rather than writing one, so it goes through the SNES-only
 * escape; only the two matrix values it leaves behind are video state. */
static void fill_memory(const GameState *g, Platform *pf)
{
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,   VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI, VID_STEP_START,     VID_STEP_END,
    };
    enum { MODE7_PRODUCT = 0x34 };
    vid_set_mode7(pf, PF_AT(0xC02DF3), &(VidMode7){ .a = 0, .b = 0 }, VID_M7_MATRIX_AB);
    const VidSnesXfer x = { FIELD_DMA, DMA_FROM_REGISTER, steps, NULL };
    const VidSnesTransfer t = { .dest = MODE7_PRODUCT, .source = field_transfer_source(g), .size = field_transfer_size(g) };
    vid_snes_transfer(pf, PF_AT(0xC02E01), &t, &x);
}

/* ---- The party windows' corners ------------------------------------------------------- */

/* The four party windows at the screen's top each have a pair of corner
 * tiles in VRAM (at $1C02 + $40 * slot and $1C22 + $40 * slot), two words
 * each; the selected window's pair is the marked one. */
enum { WINDOWS = 4, NO_WINDOW_SELECTED = 0x80 };
enum { CORNER_VRAM = 0x1C02, CORNER_TILE = 0x2902, SELECTED_TILE = 0x28FC, WINDOW_STRIDE = 0x40, SECOND_CORNER = 0x20 };

typedef struct {
    uint32_t address, first, second;   /* the origin tags of the three writes of a corner */
} CornerSites;

static void write_corner(Platform *pf, uint16_t vram, uint16_t tile, CornerSites at)
{
    vid_set_vram_address(pf, PF_AT(at.address), vram);
    vid_write_vram(pf, PF_AT(at.first), tile);
    vid_write_vram(pf, PF_AT(at.second), (uint16_t)(tile + 1));
}

/* A window's two corners with its own tiles, or the marked ones. */
static void write_window_corners(Platform *pf, unsigned window, bool selected, const CornerSites at[2])
{
    uint16_t vram = (uint16_t)(CORNER_VRAM + WINDOW_STRIDE * window);
    uint16_t own = (uint16_t)(CORNER_TILE + WINDOW_STRIDE * window);
    write_corner(pf, vram, selected ? SELECTED_TILE : own, at[0]);
    write_corner(pf, (uint16_t)(vram + SECOND_CORNER), selected ? (uint16_t)(SELECTED_TILE + 2) : (uint16_t)(own + 0x10),
                 at[1]);
}

/* Each window's corners as its own routine writes them ($C0:F159, $F142,
 * $F12B, $F110). */
static const CornerSites own_corners[WINDOWS][2] = {
    { { 0xC0F15C, 0xC0F161, 0xC0F164 }, { 0xC0F169, 0xC0F16E, 0xC0F128 } },
    { { 0xC0F145, 0xC0F14A, 0xC0F14D }, { 0xC0F152, 0xC0F157, 0xC0F128 } },
    { { 0xC0F12E, 0xC0F133, 0xC0F136 }, { 0xC0F13B, 0xC0F140, 0xC0F128 } },
    { { 0xC0F113, 0xC0F118, 0xC0F11B }, { 0xC0F120, 0xC0F125, 0xC0F128 } },
};

/* And the marked ones, as the refresh writes them for the selected window. */
static const CornerSites marked_corners[WINDOWS][2] = {
    { { 0xC0F097, 0xC0F09C, 0xC0F09F }, { 0xC0F0A4, 0xC0F0C9, 0xC0F0CC } },
    { { 0xC0F0FC, 0xC0F101, 0xC0F104 }, { 0xC0F109, 0xC0F0C9, 0xC0F0CC } },
    { { 0xC0F0DC, 0xC0F0E1, 0xC0F0E4 }, { 0xC0F0E9, 0xC0F0C9, 0xC0F0CC } },
    { { 0xC0F0B7, 0xC0F0BC, 0xC0F0BF }, { 0xC0F0C4, 0xC0F0C9, 0xC0F0CC } },
};

// $C0:F05E
/* Refresh the four windows' corners: the selected window's marked, the rest
 * their own; with no window selected (field_window_tile_slot past the four),
 * all their own, and the selection is cleared. The windows the refresh
 * writes with their own routines are written in the routines' order. */
static void refresh_window_corners(GameState *g, Platform *pf)
{
    vid_begin_vram_uploads(pf, PF_AT(0xC0F069), VID_VRAM_WORD_PORT, PF_GAME(0), NULL);
    unsigned selected = field_window_tile_slot(g);
    for (unsigned w = 0; w < WINDOWS; w++)
        if (w != selected)
            write_window_corners(pf, w, false, own_corners[w]);
    if (selected < WINDOWS)
        write_window_corners(pf, selected, true, marked_corners[selected]);
    else
        set_field_window_tile_slot(g, NO_WINDOW_SELECTED);
}

/* ---- The line interrupt ---------------------------------------------------------------- */

/* A line interrupt the field takes: it isn't turned off, and the line one is
 * what came. Acknowledging it is part of asking. */
static bool line_interrupt_for_field(const GameState *g, Platform *pf)
{
    if (field_irq_disabled(g) & 0x80)
        return false;
    return tm_ack_line_irq(pf, PF_AT(0xC0ECDB)) & TM_LINE_IRQ_PENDING;
}

enum { UPLOAD_TILE_ANIMATION = 0x01 };   /* field_display_flags */

// $C0:ECF2
/* The picture ends: the rest of the frame blanked, and the line effects off. */
static void end_picture(Platform *pf)
{
    vid_raster_display(pf, PF_AT(0xC0ECF4), VID_AT_LINE_HANDLER, &(VidDisplay){ .blank = true });
    vid_enable_line_effects(pf, PF_AT(0xC0ECF9), 0x00);
}

/* ---- Where it meets the original ------------------------------------------------------- */

bool field_display_bank_is_io(Snes *s) { return m_io_bank(s->cpu.db); }

/* The window corner writers are entered with the direct page on the video
 * registers, which they write through; on any other page they write memory,
 * which stays the original's. */
bool field_window_tiles_dp_is_ppu(Snes *s) { return s->cpu.d == M_DP_PPU; }

/* Nothing these leave in the registers is read after (the entries' don't-care
 * sets). */
void field_upload_sprites(Snes *s)
{
    upload_sprites(m_platform(s));
    m_rts(s);
}

/* Y is left the last entry's second size, which the callers read. */
void field_upload_vram_queue(Snes *s)
{
    GameState *g = state_of(s);
    unsigned entries = 0;
    while (field_vram_queue_used(g, entries) != 0)
        entries++;
    if (entries != 0)
        s->cpu.y = field_vram_queue_size2(g, entries - 1);
    upload_vram_queue(g, m_platform(s));
    m_rts(s);
}

void field_init_video_mode(Snes *s)
{
    init_video_mode(m_platform(s));
    m_rtl(s);
}

void field_set_screens(Snes *s)
{
    set_screens(state_of(s), m_platform(s));
    m_rts(s);
}

void field_vram_transfer(Snes *s)
{
    vram_transfer(state_of(s), m_platform(s));
    m_rts(s);
}

/* X is left the size, which the callers read (as it was before the clear,
 * which can clear it). */
void field_fill_memory(Snes *s)
{
    uint16_t size = field_transfer_size(state_of(s));
    fill_memory(state_of(s), m_platform(s));
    s->cpu.x = size;
    m_rts(s);
}

// $C0:F159, $C0:F142, $C0:F12B, $C0:F110
void field_write_window_tiles_0(Snes *s) { write_window_corners(m_platform(s), 0, false, own_corners[0]); m_rts(s); }
void field_write_window_tiles_1(Snes *s) { write_window_corners(m_platform(s), 1, false, own_corners[1]); m_rts(s); }
void field_write_window_tiles_2(Snes *s) { write_window_corners(m_platform(s), 2, false, own_corners[2]); m_rts(s); }
void field_write_window_tiles_3(Snes *s) { write_window_corners(m_platform(s), 3, false, own_corners[3]); m_rts(s); }

void field_patch_window_tiles(Snes *s)
{
    refresh_window_corners(state_of(s), m_platform(s));
    m_rts(s);
}

/* The registers the interrupted code had, saved on its stack and given back:
 * the handler stops to wait for hblank with them there. */
static void save_interrupted(Snes *s)
{
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, s->cpu.a);
    m_push16(s, s->cpu.x);
    m_push16(s, s->cpu.y);
    m_push16(s, s->cpu.d);
    m_push8(s, s->cpu.db);
    m_sep(s, CPU_FLAG_M);
}

static void return_to_interrupted(Snes *s)
{
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    s->cpu.db = m_pull8(s);
    s->cpu.d = m_pull16(s);
    s->cpu.y = m_pull16(s);
    s->cpu.x = m_pull16(s);
    s->cpu.a = m_pull16(s);
    m_set_nz16(s, s->cpu.a);
    m_rti(s);
}

// $C0:ECCC (installed at the $0504 line-interrupt stub by field_install_irq)
/* The field's line interrupt: where it is the field's, it leaves only the
 * vblank and controller interrupts on for the rest of the frame and waits for
 * the line's hblank, an idle entry at $C0:ECEB, where this port stops. The
 * wait is the original's, on the beam (docs/PLATFORM.md, "Timing without a
 * beam"). */
void field_irq(Snes *s)
{
    save_interrupted(s);
    Platform *pf = m_platform(s);
    if (!line_interrupt_for_field(state_of(s), pf)) {
        return_to_interrupted(s);
        return;
    }
    tm_enable_interrupts(pf, PF_AT(0xC0ECE3), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
    s->cpu.a = (uint16_t)(s->cpu.a & 0xFF00);
    s->cpu.db = 0x00;
    m_set_nz8(s, 0);
    s->cpu.pc = 0xECEB;
}

// $C0:ECEB
/* One pass of the line interrupt's wait for hblank. Idle entry, 8-bit A;
 * exits to the blanking at $C0:ECF2. */
void field_irq_wait_hblank_loop(Snes *s)
{
    uint8_t status = tm_video_status(m_platform(s), PF_AT(0xC0ECEB));
    m_set_a8(s, (uint8_t)(status & TM_STATUS_HBLANK));
    m_set_nz8(s, (uint8_t)(status & TM_STATUS_HBLANK));
    if (status & TM_STATUS_HBLANK)
        s->cpu.pc = 0xECF2;
}

/* The rest of the line interrupt, from hblank: the picture ends, then the
 * time to vblank uploads the tile map's columns ($C0:ED15), or in a map with
 * animated tiles those ($FD:FFFD), both the original's code; and back to what
 * it interrupted. */
void field_irq_after_hblank(Snes *s)
{
    GameState *g = state_of(s);
    end_picture(m_platform(s));
    uint8_t animated = field_display_flags(g) & UPLOAD_TILE_ANIMATION;
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | animated);
    m_set_nz8(s, animated);
    if (!animated)
        m_jsr(s, 0xC0ED03, 0xC0ED15);
    else
        m_jsl(s, 0xC0ED08, 0xFDFFFD);
    if (s->cpu.e || (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
        return;
    return_to_interrupted(s);
}
