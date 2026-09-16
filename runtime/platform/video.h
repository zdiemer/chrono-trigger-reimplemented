/* Video service (docs/PLATFORM.md): display, BG mode, windows, color math,
 * per-line effects and uploads into VRAM, CGRAM and OAM.
 *
 * Commands, not diffs: every setter acts when called. The SNES backend performs
 * the register writes the original made at that point; a native backend would
 * fold them into retained state and render at the end of the frame.
 *
 * Game shadows stay game state in SNES format (the field's $0BD9 window bytes,
 * the battle's $C861); the *_decode helpers turn such a byte into the structure
 * a call takes. They are exact: the SNES backend encodes the structure back to
 * the same byte, including bits the hardware ignores (the snes_extra members),
 * so shadow validation keeps comparing registers byte for byte.
 */
#ifndef CT_PLATFORM_VIDEO_H
#define CT_PLATFORM_VIDEO_H

#include "platform/platform.h"

typedef enum { VID_BG1, VID_BG2, VID_BG3, VID_BG4, VID_OBJ, VID_BACK } VidLayer;
#define VID_LAYER_BIT(layer) (1u << (layer))
enum { VID_LAYERS = 6 };

/* Master brightness and forced blank. */
typedef struct {
    uint8_t brightness;   /* 0-15 */
    bool blank;
    uint8_t snes_extra;   /* bits 4-6 of the SNES byte, which the hardware ignores */
} VidDisplay;

static inline VidDisplay vid_display_decode(uint8_t v)
{
    return (VidDisplay){ .brightness = v & 0x0F, .blank = (v & 0x80) != 0, .snes_extra = v & 0x70 };
}

/* BG mode. */
typedef struct {
    uint8_t mode;         /* 0-7 */
    bool bg3_priority;    /* mode 1 */
    uint8_t large_tiles;  /* per-BG bits 0-3 (16x16 tiles), which the hardware ignores */
} VidMode;

/* How a window affects a layer. Values are the SNES bit pairs (enable, invert),
 * so a disabled window with the invert bit set stays distinguishable. */
typedef enum { VID_WIN_OFF = 0, VID_WIN_OFF_INVERTED = 1, VID_WIN_IN = 2, VID_WIN_OUT = 3 } VidWinUse;
typedef enum { VID_WIN_OR, VID_WIN_AND, VID_WIN_XOR, VID_WIN_XNOR } VidWinLogic;

typedef struct {
    VidWinUse w1[6], w2[6];     /* per VidLayer; VID_BACK is the color window */
    VidWinLogic logic[6];       /* per VidLayer; VID_BACK is the color window */
    uint8_t w1_left, w1_right, w2_left, w2_right;
    uint8_t win_main, win_sub;  /* VID_LAYER_BIT set: the window masks the layer on that screen */
    uint8_t snes_extra_logic;   /* bits 4-7 of the OBJ/color logic byte, which the hardware ignores */
} VidWindows;

/* Members a vid_set_windows call sets, one bit per register group the original
 * writes separately. */
enum {
    VID_WINDOWS_BG1_BG2 = 1 << 0,   /* w1, w2 of BG1 and BG2 */
    VID_WINDOWS_BG3_BG4 = 1 << 1,
    VID_WINDOWS_OBJ_BACK = 1 << 2,  /* w1, w2 of OBJ and the color window */
    VID_WINDOWS_W1_LEFT = 1 << 3,
    VID_WINDOWS_W1_RIGHT = 1 << 4,
    VID_WINDOWS_W2_LEFT = 1 << 5,
    VID_WINDOWS_W2_RIGHT = 1 << 6,
    VID_WINDOWS_BG_LOGIC = 1 << 7,  /* logic of BG1-BG4 */
    VID_WINDOWS_OBJ_LOGIC = 1 << 8, /* logic of OBJ and the color window */
    VID_WINDOWS_MAIN = 1 << 9,
    VID_WINDOWS_SUB = 1 << 10,
};

static inline void vid_windows_decode(VidWindows *w, uint32_t field, uint8_t v)
{
    static const uint8_t pair_layer[3][2] = { { VID_BG1, VID_BG2 }, { VID_BG3, VID_BG4 }, { VID_OBJ, VID_BACK } };
    for (int g = 0; g < 3; g++) {
        if (field & (1u << g)) {
            for (int i = 0; i < 2; i++) {
                w->w1[pair_layer[g][i]] = (VidWinUse)((v >> (4 * i)) & 3);
                w->w2[pair_layer[g][i]] = (VidWinUse)((v >> (4 * i + 2)) & 3);
            }
        }
    }
    if (field & VID_WINDOWS_W1_LEFT) w->w1_left = v;
    if (field & VID_WINDOWS_W1_RIGHT) w->w1_right = v;
    if (field & VID_WINDOWS_W2_LEFT) w->w2_left = v;
    if (field & VID_WINDOWS_W2_RIGHT) w->w2_right = v;
    if (field & VID_WINDOWS_BG_LOGIC)
        for (int l = 0; l < 4; l++)
            w->logic[l] = (VidWinLogic)((v >> (2 * l)) & 3);
    if (field & VID_WINDOWS_OBJ_LOGIC) {
        w->logic[VID_OBJ] = (VidWinLogic)(v & 3);
        w->logic[VID_BACK] = (VidWinLogic)((v >> 2) & 3);
        w->snes_extra_logic = v & 0xF0;
    }
    if (field & VID_WINDOWS_MAIN) w->win_main = v;
    if (field & VID_WINDOWS_SUB) w->win_sub = v;
}

/* When the color window clips to black or keeps color math from applying. */
typedef enum { VID_REGION_NEVER, VID_REGION_OUTSIDE, VID_REGION_INSIDE, VID_REGION_ALWAYS } VidRegion;

typedef struct {
    VidRegion clip_black, prevent_math;
    bool sub_screen;      /* add the sub screen rather than the fixed color */
    bool direct;          /* direct color in 256-color BGs */
    bool subtract, half;
    uint8_t layers;       /* VID_LAYER_BIT: layers color math applies to */
    uint8_t snes_extra;   /* bits 2-3 of the control byte, which the hardware ignores */
} VidColorMath;

enum {
    VID_MATH_CONTROL = 1 << 0,   /* clip_black, prevent_math, sub_screen, direct */
    VID_MATH_LAYERS = 1 << 1,    /* subtract, half, layers */
};

static inline void vid_color_math_decode(VidColorMath *m, uint32_t field, uint8_t v)
{
    if (field & VID_MATH_CONTROL) {
        m->clip_black = (VidRegion)(v >> 6);
        m->prevent_math = (VidRegion)((v >> 4) & 3);
        m->snes_extra = v & 0x0C;
        m->sub_screen = (v & 0x02) != 0;
        m->direct = (v & 0x01) != 0;
    }
    if (field & VID_MATH_LAYERS) {
        m->subtract = (v & 0x80) != 0;
        m->half = (v & 0x40) != 0;
        m->layers = v & 0x3F;
    }
}

/* Channels of the fixed color a vid_set_fixed_color call sets. */
enum { VID_RED = 1, VID_GREEN = 2, VID_BLUE = 4 };

/* A background layer's placement in video memory and its scroll. */
typedef struct {
    uint16_t map_base;     /* VRAM word address of the tilemap */
    uint8_t map_w, map_h;  /* 32 or 64 tiles */
    uint16_t char_base;    /* VRAM word address of tile 0 */
    uint16_t hofs, vofs;   /* 10-bit scroll */
    bool mosaic;           /* the layer takes part in the mosaic */
    bool large_tiles;      /* 16x16 tiles */
} VidBg;

/* Which of a layer's scroll values a vid_set_scroll call sets. */
enum { VID_SCROLL_H = 1 << 0, VID_SCROLL_V = 1 << 1 };

enum { VID_M7_WRAP = 0, VID_M7_WRAP_UNUSED = 1, VID_M7_TRANSPARENT = 2, VID_M7_TILE0 = 3 };

typedef struct {
    int16_t a, b, c, d;     /* matrix, 8.8 fixed point */
    int16_t cx, cy;         /* 13-bit rotation center, sign-extended */
    int16_t hofs, vofs;     /* 13-bit, sign-extended */
    bool hflip, vflip;
    uint8_t outside;        /* VID_M7_* : what lies outside the 1024x1024 plane */
} VidMode7;

/* Members a vid_set_mode7 call sets. */
enum {
    VID_M7_SELECT = 1 << 0,   /* hflip, vflip, outside */
    VID_M7_MATRIX = 1 << 1,   /* a, b, c, d */
    VID_M7_CENTER = 1 << 2,   /* cx, cy */
    VID_M7_SCROLL = 1 << 3,   /* hofs, vofs */
    VID_M7_MATRIX_AB = 1 << 4,   /* a and b only: they double as the signed multiplier */
};

/* One OAM entry, as the game builds it. */
typedef struct {
    int16_t x;              /* 9 bits, as OAM holds it */
    uint8_t y;
    uint16_t tile;          /* 9-bit name */
    uint8_t palette;        /* 0-7 */
    uint8_t priority;       /* 0-3 */
    bool hflip, vflip, large;
} VidSprite;

typedef struct {
    uint8_t size_select;    /* which pair of sprite sizes (0-7) */
    uint16_t base;          /* VRAM word address of sprite tile 0 */
    uint16_t name_select;   /* word offset from it to the second tile page */
} VidObjConfig;

/* Interlace, overscan and hi-res settings ($2133). The game leaves them off;
 * they are kept so no register state is lost. */
typedef struct {
    bool interlace, obj_interlace, overscan, pseudo_hires;
    bool extbg;             /* Mode 7's BG2 carries priority bits */
    uint8_t snes_extra;     /* bits 4, 5 and 7, which the renderer doesn't model */
} VidVideoConfig;

/* How VRAM writes step the address (the VRAM port's mode). */
typedef struct {
    uint8_t step;           /* words the address advances: 1, 32 or 128 */
    uint8_t remap;          /* address remapping, 0-3 (0: none) */
    bool after_high;        /* step after the high byte rather than the low one */
    bool snes_step_alt;     /* the second SNES encoding of 128, which behaves the same */
} VidVramPort;

/* The setting every upload in ported code uses: a word at a time, stepping
 * after the high byte. */
#define VID_VRAM_WORD_PORT (&(const VidVramPort){ .step = 1, .after_high = true })

/* Decoders for the SNES-format bytes the game keeps as shadows, exact like the
 * ones above: the backend encodes each structure back to the byte it came from. */
static inline VidMode vid_mode_decode(uint8_t v)
{
    return (VidMode){ .mode = v & 7, .bg3_priority = (v & 8) != 0, .large_tiles = (uint8_t)(v >> 4) };
}

static inline VidObjConfig vid_obj_config_decode(uint8_t v)
{
    return (VidObjConfig){ .size_select = (uint8_t)(v >> 5), .base = (uint16_t)((v & 7) << 13),
                           .name_select = (uint16_t)(((v >> 3) & 3) << 12) };
}

static inline VidVideoConfig vid_video_config_decode(uint8_t v)
{
    return (VidVideoConfig){ .interlace = (v & 0x01) != 0, .obj_interlace = (v & 0x02) != 0,
                             .overscan = (v & 0x04) != 0, .pseudo_hires = (v & 0x08) != 0,
                             .extbg = (v & 0x40) != 0, .snes_extra = (uint8_t)(v & 0xB0) };
}

/* $2107-$210A: the tilemap's address and how many 32x32 quadrants it holds. */
static inline void vid_bg_map_decode(VidBg *b, uint8_t v)
{
    b->map_base = (uint16_t)((v & 0xFC) << 8);
    b->map_w = (uint8_t)((v & 1) ? 64 : 32);
    b->map_h = (uint8_t)((v & 2) ? 64 : 32);
}

static inline VidVramPort vid_vram_port_decode(uint8_t v)
{
    static const uint8_t steps[4] = { 1, 32, 128, 128 };
    return (VidVramPort){ .step = steps[v & 3], .remap = (uint8_t)((v >> 2) & 3), .after_high = (v & 0x80) != 0,
                          .snes_step_alt = (v & 3) == 3 };
}

/* Everything a renderer needs for one line. */
typedef struct {
    VidDisplay display;
    VidMode mode;
    uint8_t mosaic_size;    /* 1-16 */
    VidBg bg[4];
    VidMode7 mode7;
    VidObjConfig obj;
    VidWindows windows;
    VidColorMath math;
    VidVideoConfig config;
    uint16_t fixed_color;   /* BGR555 */
    uint8_t main_layers, sub_layers;   /* VID_LAYER_BIT sets: $212C, $212D */
} VidLineState;

enum { VID_SPRITES = 128, VID_MAX_SPANS = 225 };

/* The sprites of one span, where OAM changed while the frame was being drawn. */
typedef struct {
    VidSprite sprites[VID_SPRITES];
    uint8_t first_sprite;
} VidSpriteSet;

typedef struct {
    uint16_t line;          /* first line (1-224) this state applies to */
    bool palette_changed;   /* CGRAM changed since the previous span */
    VidLineState state;
    /* The video memory and sprites this span's lines read. NULL means the
     * frame's, which is what they were when the frame was finished: a builder
     * fills these in only where they changed while the frame was being drawn,
     * and then only for the spans before the change (docs/PLATFORM.md, P3). */
    const uint16_t *cgram;
    const uint8_t *vram;
    const VidSpriteSet *sprites;
} VidSpan;

/* A frame as a renderer consumes it: the lines' states as spans, the sprites in
 * OAM order (order decides overlap), and the memory they read. Built two ways
 * (docs/PLATFORM.md, P2): decoded from the PPU registers line by line, or folded
 * from the service calls by the observer; the two must agree. */
typedef struct {
    VidSprite sprites[VID_SPRITES];
    uint8_t first_sprite;   /* OAM priority rotation */
    uint16_t span_count;
    VidSpan spans[VID_MAX_SPANS];   /* spans[0].line == 1 */
    const uint8_t *vram;    /* 64 KiB, SNES layout */
    const uint16_t *cgram;  /* 256 BGR555 */
} VidFrame;

/* A per-line effect: a table the game keeps in memory, read at the start of
 * every frame, in HDMA's format (line counts with repeat bit, then values),
 * direct or indirect. The target names what it drives. */
typedef enum {
    VID_LINE_WINDOWS,           /* w1_left, w1_right, w2_left, w2_right (battle, $CD:0AEE) */
    VID_LINE_WINDOW1,           /* w1_left, w1_right */
    VID_LINE_WINDOW2,           /* w2_left, w2_right */
    VID_LINE_SCROLL_BG1, VID_LINE_SCROLL_BG2, VID_LINE_SCROLL_BG3,
    VID_LINE_FIXED_COLOR,
    VID_LINE_BRIGHTNESS,
    VID_LINE_BG_MAPS,           /* the four layers' tilemap addresses and sizes */
    VID_LINE_SCREENS,           /* main_layers, sub_layers, win_main, win_sub */
    VID_LINE_COLOR_MATH,        /* math control, math layers, fixed color, video config */
    VID_LINE_MODE,              /* BG mode and mosaic */
    VID_LINE_MATH_LAYERS,       /* math layers and the fixed color */
    VID_LINE_WINDOW2_LEFT, VID_LINE_WINDOW2_RIGHT,   /* one edge of window 2 */
} VidLineTarget;

typedef struct {
    VidLineTarget target;
    PfData table;
    bool indirect;
    uint8_t indirect_bank;
} VidLineEffect;

/* How the SNES backend programs a DMA or HDMA channel: the register writes in
 * the original's order, as a list of steps ending in VID_STEP_END. The original
 * code orders them differently at almost every site, and shadow validation
 * compares what an interrupted sequence leaves behind, so the order is part of
 * each call site. Other backends ignore it. */
enum {
    VID_STEP_END = 0,
    VID_STEP_CONTROL,        /* $43x0: transfer mode and flags (VidSnesXfer.control) */
    VID_STEP_DEST,           /* $43x1: the register the channel feeds */
    VID_STEP_SOURCE_LO,      /* $43x2: source or table address */
    VID_STEP_SOURCE_HI,      /* $43x3 */
    VID_STEP_SOURCE_BANK,    /* $43x4 */
    VID_STEP_SIZE_LO,        /* $43x5; an empty upload stops here */
    VID_STEP_SIZE_HI,        /* $43x6 */
    VID_STEP_SIZE_CLEAR,     /* $43x5, $43x6 = 0, as a 16-bit STZ before the size */
    VID_STEP_INDIRECT_BANK,  /* $43x7 */
    VID_STEP_TARGET_LO,      /* the destination's address register: VRAM, CGRAM or OAM */
    VID_STEP_TARGET_HI,      /* its high byte (VRAM) */
    VID_STEP_START,          /* $420B = the channel's bit */
    VID_STEP_VRAM_PORT,      /* $2115: how VRAM writes step, from VidSnesXfer.port */
};

typedef struct {
    uint8_t channel;         /* 0-7 */
    uint8_t control;         /* $43x0 as the original writes it (mode 1 for VRAM words, bit 3 fixed source, ...) */
    const uint8_t *steps;
    const VidVramPort *port; /* for VID_STEP_VRAM_PORT, where an upload sets it itself */
} VidSnesXfer;

/* Master brightness and forced blank. */
PF_API void vid_set_display(Platform *pf, PfOrigin o, const VidDisplay *d);
/* The same at a known line of the visible frame, for the rare raster writes
 * outside HDMA (the battle IRQ's forced blank). VID_AT_LINE_HANDLER: the line
 * the running line handler was set up for. The SNES backend writes when called. */
enum { VID_AT_LINE_HANDLER = 0xFFFF };
PF_API void vid_raster_display(Platform *pf, PfOrigin o, uint16_t line, const VidDisplay *d);
PF_API void vid_set_mode(Platform *pf, PfOrigin o, const VidMode *m);
/* Where a layer's tilemap lies and how large it is ($2107-$210A). */
PF_API void vid_set_bg_map(Platform *pf, PfOrigin o, VidLayer bg, const VidBg *b);
/* The character bases of a layer pair (BG1+BG2 or BG3+BG4), which the original
 * sets in one register. */
PF_API void vid_set_bg_chars(Platform *pf, PfOrigin o, VidLayer first, uint16_t base0, uint16_t base1);
/* `fields`: VID_SCROLL_H, VID_SCROLL_V. BG1's scroll shares its registers with
 * Mode 7's, which the backend keeps in step. */
PF_API void vid_set_scroll(Platform *pf, PfOrigin o, VidLayer bg, uint16_t hofs, uint16_t vofs, uint32_t fields);
/* Mosaic: `bgs` is a VID_LAYER_BIT set of BG1-BG4, `size` 1-16. */
PF_API void vid_set_mosaic(Platform *pf, PfOrigin o, uint8_t bgs, uint8_t size);
/* `fields`: VID_M7_* members to set. */
PF_API void vid_set_mode7(Platform *pf, PfOrigin o, const VidMode7 *m7, uint32_t fields);
PF_API void vid_set_obj_config(Platform *pf, PfOrigin o, const VidObjConfig *c);
/* Which layers are on the main and the sub screen. */
enum { VID_SCREEN_MAIN = 1 << 0, VID_SCREEN_SUB = 1 << 1 };
PF_API void vid_set_screens(Platform *pf, PfOrigin o, uint8_t main_layers, uint8_t sub_layers, uint32_t fields);
PF_API void vid_set_video_config(Platform *pf, PfOrigin o, const VidVideoConfig *c);
/* `fields`: VID_WINDOWS_* members to set, written in the order of the bits. */
PF_API void vid_set_windows(Platform *pf, PfOrigin o, const VidWindows *w, uint32_t fields);
/* `fields`: VID_MATH_* members to set. */
PF_API void vid_set_color_math(Platform *pf, PfOrigin o, const VidColorMath *m, uint32_t fields);
/* Set the fixed color's `channels` (VID_RED | VID_GREEN | VID_BLUE) to
 * `intensity` (0-31); the other channels keep their value. */
PF_API void vid_set_fixed_color(Platform *pf, PfOrigin o, uint8_t channels, uint8_t intensity);

PF_API void vid_set_line_effect(Platform *pf, PfOrigin o, uint8_t slot, const VidLineEffect *e,
                                const uint8_t *snes_steps);
/* Slots whose line effects run from the next frame on (bit per slot). */
PF_API void vid_enable_line_effects(Platform *pf, PfOrigin o, uint8_t slots);

/* Vblank upload work begins: VRAM writes step the address as `port` says from
 * here on. The SNES backend sets the VRAM port and, with `x`, programs the
 * channel's steps (its mode and destination) ahead of uploads that reuse them. */
PF_API void vid_begin_vram_uploads(Platform *pf, PfOrigin o, const VidVramPort *port, PfData source,
                                   const VidSnesXfer *x);
/* Where the next VRAM writes go (word address), and one word or byte written
 * there through the port rather than by an upload. */
PF_API void vid_set_vram_address(Platform *pf, PfOrigin o, uint16_t vram_addr);
PF_API void vid_write_vram(Platform *pf, PfOrigin o, uint16_t value);
PF_API void vid_write_vram_byte(Platform *pf, PfOrigin o, bool high, uint8_t value);
/* Copy `len` bytes (1-65536) from `src` into VRAM at word address `vram_addr`
 * (as the VRAM address register holds it). A `len` of 0 uploads nothing: the
 * SNES backend performs the steps before the first size step, as the original
 * programs the source before checking for an empty upload. */
PF_API void vid_upload_vram(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len,
                            const VidSnesXfer *x);
/* Fill `len` bytes of VRAM with the byte at `value` (the SNES backend: a
 * fixed-source transfer, so `x->control` has bit 3 set). */
PF_API void vid_fill_vram(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData value, uint32_t len,
                          const VidSnesXfer *x);
/* `count` BGR555 colors from `src` into CGRAM from entry `first`. */
PF_API void vid_upload_palette(Platform *pf, PfOrigin o, uint8_t first, PfData src, uint16_t count,
                               const VidSnesXfer *x);
/* The 544-byte OAM image (128 entries, 32-byte high table), as the game builds it. */
PF_API void vid_upload_oam(Platform *pf, PfOrigin o, PfData oam, const VidSnesXfer *x);
/* No uploads are in progress (the SNES backend: the transfer start register cleared). */
PF_API void vid_stop_uploads(Platform *pf, PfOrigin o);

/* SNES-only escape for a transfer whose meaning isn't established: the game's
 * own table supplies the channel's raw values (battle $CF:E338). Listed by the
 * lint test, which allows it only where registered. */
typedef struct {
    uint8_t dest;            /* $43x1 */
    uint32_t source;         /* $43x2-$43x4 */
    uint16_t size;           /* $43x5-$43x6, 0 meaning 65536 */
    uint8_t target_reg;      /* low byte of the address register the TARGET steps write */
    uint16_t target;
} VidSnesTransfer;
PF_API void vid_snes_transfer(Platform *pf, PfOrigin o, const VidSnesTransfer *t, const VidSnesXfer *x);

/* ---- observer (docs/PLATFORM.md, P2) ------------------------------------------
 *
 * The development system folds every call into a VidFrame, so the command-built
 * frame can be compared with the one decoded from the PPU registers. The hooks
 * are no-ops while no observer is installed; every backend call carries the
 * semantic arguments to the hook, never the registers they encode to. */
typedef struct VidObserver VidObserver;
extern VidObserver *vid_observer;   /* NULL unless a host installs one */

void vid_obs_display(VidObserver *ob, const VidDisplay *d);
void vid_obs_mode(VidObserver *ob, const VidMode *m);
void vid_obs_bg_map(VidObserver *ob, VidLayer bg, const VidBg *b);
void vid_obs_bg_chars(VidObserver *ob, VidLayer first, uint16_t base0, uint16_t base1);
void vid_obs_scroll(VidObserver *ob, VidLayer bg, uint16_t hofs, uint16_t vofs, uint32_t fields);
void vid_obs_mosaic(VidObserver *ob, uint8_t bgs, uint8_t size);
void vid_obs_mode7(VidObserver *ob, const VidMode7 *m7, uint32_t fields);
void vid_obs_obj_config(VidObserver *ob, const VidObjConfig *c);
void vid_obs_screens(VidObserver *ob, uint8_t main_layers, uint8_t sub_layers, uint32_t fields);
void vid_obs_video_config(VidObserver *ob, const VidVideoConfig *c);
void vid_obs_windows(VidObserver *ob, const VidWindows *w, uint32_t fields);
void vid_obs_color_math(VidObserver *ob, const VidColorMath *m, uint32_t fields);
void vid_obs_fixed_color(VidObserver *ob, uint8_t channels, uint8_t intensity);
void vid_obs_line_effect(VidObserver *ob, uint8_t slot, const VidLineEffect *e);
void vid_obs_enable_line_effects(VidObserver *ob, uint8_t slots);
void vid_obs_vram_port(VidObserver *ob, const VidVramPort *port);
void vid_obs_vram_address(VidObserver *ob, uint16_t vram_addr);
void vid_obs_write_vram(VidObserver *ob, uint16_t value, int half);   /* half: -1 word, 0 low, 1 high */
void vid_obs_upload_vram(VidObserver *ob, uint16_t vram_addr, PfData src, uint32_t len, bool fill,
                         const VidVramPort *port);
void vid_obs_upload_palette(VidObserver *ob, uint8_t first, PfData src, uint16_t count);
void vid_obs_upload_oam(VidObserver *ob, PfData oam);
void vid_obs_snes_transfer(VidObserver *ob, const VidSnesTransfer *t, uint8_t control);

#ifdef CT_PLATFORM_SNES
#include "platform/snes/video.h"
#endif

#endif
