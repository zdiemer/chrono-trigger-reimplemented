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
};

typedef struct {
    uint8_t channel;         /* 0-7 */
    uint8_t control;         /* $43x0 as the original writes it (mode 1 for VRAM words, bit 3 fixed source, ...) */
    const uint8_t *steps;
} VidSnesXfer;

/* Master brightness and forced blank. */
PF_API void vid_set_display(Platform *pf, PfOrigin o, const VidDisplay *d);
/* The same at a known line of the visible frame, for the rare raster writes
 * outside HDMA (the battle IRQ's forced blank). VID_AT_LINE_HANDLER: the line
 * the running line handler was set up for. The SNES backend writes when called. */
enum { VID_AT_LINE_HANDLER = 0xFFFF };
PF_API void vid_raster_display(Platform *pf, PfOrigin o, uint16_t line, const VidDisplay *d);
PF_API void vid_set_mode(Platform *pf, PfOrigin o, const VidMode *m);
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

/* Vblank upload work begins: VRAM uploads write words from here on. The SNES
 * backend sets the VRAM port and, with `x`, programs the channel's steps (its
 * mode and destination) ahead of uploads that reuse them. */
PF_API void vid_begin_vram_uploads(Platform *pf, PfOrigin o, const VidSnesXfer *x);
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

#ifdef CT_PLATFORM_SNES
#include "platform/snes/video.h"
#endif

#endif
