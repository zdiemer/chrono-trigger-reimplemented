/* Video service (docs/PLATFORM.md): display, BG mode, windows, color math,
 * per-line effects and uploads into VRAM, CGRAM and OAM.
 *
 * Commands, not diffs: every setter acts when called. The SNES backend performs
 * the register writes the original made at that point; the native backend
 * folds them into retained state and renders at the end of the frame.
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

/* Sets of layers, as the screens, the window masks and color math take them:
 * VID_BG1_BIT | VID_OBJ_BIT is BG1 and the sprites. VID_BACK_BIT is the
 * backdrop, which only color math names. */
enum {
    VID_BG1_BIT = 1 << VID_BG1,
    VID_BG2_BIT = 1 << VID_BG2,
    VID_BG3_BIT = 1 << VID_BG3,
    VID_BG4_BIT = 1 << VID_BG4,
    VID_OBJ_BIT = 1 << VID_OBJ,
    VID_BACK_BIT = 1 << VID_BACK,
    VID_ALL_LAYERS = VID_BG1_BIT | VID_BG2_BIT | VID_BG3_BIT | VID_BG4_BIT | VID_OBJ_BIT,
};

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
enum { VID_WINDOW_FIRST_DOT = 0x00, VID_WINDOW_LAST_DOT = 0xFF };

/* The two windows. Their edges are dots of the original's screen, 0-255, as
 * the game's line tables hold them. A window can't reach past the screen, so
 * an edge on its first or last dot means "on to that side": on a wider view
 * (vid_view) it reaches on to the view's side, and a window from 0 to 255
 * covers the whole view. */
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
enum { VID_RED = 1, VID_GREEN = 2, VID_BLUE = 4, VID_ALL_CHANNELS = VID_RED | VID_GREEN | VID_BLUE };

/* A background layer's placement in video memory and its scroll. */
typedef struct {
    uint16_t map_base;     /* VRAM word address of the tilemap */
    uint8_t map_w, map_h;  /* 32 or 64 tiles */
    uint16_t char_base;    /* VRAM word address of tile 0 */
    uint16_t hofs, vofs;   /* 10-bit scroll */
    bool mosaic;           /* the layer takes part in the mosaic */
    bool large_tiles;      /* 16x16 tiles */
} VidBg;

/* A layer's tilemap in a VidBg's initializer: at `map_base` (a VRAM word
 * address), `tiles_wide` by `tiles_high` (32 or 64 each), as in
 * `{ VID_TILEMAP(0x6000, 64, 32), .char_base = 0x2000 }`. */
#define VID_TILEMAP(map_base_, tiles_wide, tiles_high) .map_base = (map_base_), .map_w = (tiles_wide), .map_h = (tiles_high)

/* Which of a layer's scroll values a vid_set_scroll call sets. */
enum { VID_SCROLL_H = 1 << 0, VID_SCROLL_V = 1 << 1 };

enum { VID_M7_WRAP = 0, VID_M7_WRAP_UNUSED = 1, VID_M7_TRANSPARENT = 2, VID_M7_TILE0 = 3 };

typedef struct {
    int16_t a, b, c, d;     /* matrix, 8.8 fixed point */
    int16_t cx, cy;         /* rotation center, as written; the hardware keeps 13 bits */
    int16_t hofs, vofs;     /* as written; the hardware keeps 13 bits */
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
    VID_M7_MATRIX_A = 1 << 5,    /* a alone: the multiplier's 16-bit operand */
    VID_M7_MATRIX_B_HIGH = 1 << 6,  /* the high byte of b alone: its 8-bit operand */
    VID_M7_MATRIX_B = 1 << 7,    /* b alone, both bytes */
};

/* How much of the picture is shown (vid_view, below): the original's screen,
 * 256 dots by 224 lines, or a wider one, which has the original's screen in
 * its middle. Screen coordinates stay the original's -- its left edge at 0 --
 * so a wider view reaches from vid_view_left, a negative X, to short of
 * vid_view_right. A view is at most VID_MAX_VIEW_WIDTH across, which the
 * scrolling maps' 64-column tile maps fill (the screen's 33 columns and 15
 * either side, less the fine scroll); the height stays the original's. */
enum { VID_ORIGINAL_VIEW_WIDTH = 256, VID_ORIGINAL_VIEW_HEIGHT = 224, VID_MAX_VIEW_WIDTH = 480 };
typedef struct {
    uint16_t width, height;
} VidView;
static inline int16_t vid_view_left(VidView v) { return (int16_t)(((int)VID_ORIGINAL_VIEW_WIDTH - (int)v.width) / 2); }
static inline int16_t vid_view_right(VidView v) { return (int16_t)(vid_view_left(v) + (int)v.width); }
static inline bool vid_view_is_wider(VidView v) { return v.width > VID_ORIGINAL_VIEW_WIDTH; }

/* One OAM entry, as the game builds it. */
typedef struct {
    int16_t x;              /* 9 bits, as OAM holds it; where `placed`, the X the game placed it at (VidSpritePlace) */
    uint8_t y;
    uint16_t tile;          /* 9-bit name */
    uint8_t palette;        /* 0-7 */
    uint8_t priority;       /* 0-3 */
    bool hflip, vflip, large;
    bool placed;            /* x is the game's whole signed X, which a wider view shows past 0-255 */
    bool hidden;            /* the game put the sprite out of sight: shown on no view */
} VidSprite;

/* ---- The sprite table, as the game builds it in its own memory ----------------
 *
 * The game keeps its sprites in the SNES's form, which vid_send_oam sends: 128
 * entries of four bytes (X's low byte, Y, tile, attributes), then two bits a
 * sprite (X's ninth bit, and large) four sprites a byte. Positions are signed
 * screen coordinates until they are written, and these alone turn them into
 * the table's form and back, so the ninth bit, the 8-bit Y and the line that
 * hides a sprite stay out of the game's code. */
enum {
    VID_OAM_ENTRY_BYTES = 4,
    VID_OAM_ENTRY_Y = 1,            /* where an entry's Y is */
    VID_OAM_SPRITES_A_HIGH_BYTE = 4,
    VID_OAM_HIGH_BITS = 2,          /* a sprite's bits in its high byte */
    VID_OAM_HIDDEN_Y = 0xE0,        /* the line an entry is put on to show nowhere */
    VID_OAM_HIDDEN_X = 0x100,       /* X's ninth bit, which puts an entry off the screen's side */
    VID_OAM_HIGH_BITS_WORD = 0x100, /* the table's word where the high bits start */
};

/* X as the table's entry holds it, and the sprite's two high bits. */
static inline uint8_t vid_oam_x_low(int16_t x) { return (uint8_t)x; }
static inline uint8_t vid_oam_high_bits(int16_t x, bool large)
{
    return (uint8_t)(((uint16_t)x >> 8 & 0x01) | (large ? 0x02 : 0));
}
/* ... and back: the X an entry's low byte and its high bits say (-256 to 255). */
static inline int16_t vid_oam_x(uint8_t low, uint8_t high_bits)
{
    return (int16_t)((high_bits & 0x01) ? low - 0x100 : low);
}
static inline bool vid_oam_large(uint8_t high_bits) { return (high_bits & 0x02) != 0; }

/* Where the game put a sprite, beside the table, for a view wider than the
 * original's screen (vid_view). The table's X is nine bits, -256 to 255, so a
 * sprite right of the screen reads as one left of it; and the game hides a
 * sprite by putting it where the original's screen shows nothing (X 256, the
 * line VID_OAM_HIDDEN_Y), which a wider or taller view would show. A place says
 * what the entry means: the whole X it stands at, or that it is out of sight.
 * It holds while the entry keeps the X and Y it was given with, so an entry a
 * builder fills without a place reads as the table says. The original's
 * screen draws from the table alone. */
typedef enum { VID_PLACE_NONE, VID_PLACE_AT, VID_PLACE_HIDDEN } VidPlaceKind;
typedef struct {
    int16_t x;              /* the whole X; for VID_PLACE_HIDDEN, the X the entry was left at */
    uint8_t y;              /* the entry's Y when the place was given */
    uint8_t kind;           /* VidPlaceKind */
} VidSpritePlace;

/* Whether a place still describes an entry of X `x9` (its nine bits) and Y `y`. */
static inline bool vid_place_holds(const VidSpritePlace *p, uint16_t x9, uint8_t y)
{
    return p->kind != VID_PLACE_NONE && ((uint16_t)p->x & 0x1FF) == (x9 & 0x1FF) && p->y == y;
}

/* A sprite a wider view (vid_view) shows in its margins, past the original's
 * screen, that the game's sprite table doesn't hold: one a mode leaves out
 * because it is off the original's screen, or that belongs to something the
 * game doesn't run there. It shows VRAM's tiles in a sprite palette as a
 * table entry does, or a picture of its own (VidSpritePicture), for something
 * whose tiles and colours the game has let go of. */
typedef struct {
    int16_t x, y;           /* the top left's screen coordinates */
    uint16_t tile;          /* 9-bit name, as a table entry's (VRAM's tiles) */
    uint8_t palette;        /* 0-7: its sprite palette, or for a picture the one whose colour math it takes */
    uint8_t priority;       /* 0-3 */
    bool hflip, vflip, large;
} VidMarginSprite;

/* A sprite's own picture: 16 dots square, four tiles in VRAM's 4bpp form
 * (top left, top right, bottom left, bottom right), in its own colours. */
enum { VID_PICTURE_TILES = 4, VID_PICTURE_TILE_BYTES = 32, VID_PICTURE_DOTS = 16, VID_PICTURE_COLOURS = 16 };
typedef struct {
    uint8_t tile[VID_PICTURE_TILES][VID_PICTURE_TILE_BYTES];
    uint16_t colour[VID_PICTURE_COLOURS];   /* BGR555; colour 0 is clear */
} VidSpritePicture;

/* A margin sprite showing what a table entry's tile and attributes would:
 * the name's ninth bit, the palette, the priority and the flips are the
 * attributes'. */
static inline VidMarginSprite vid_margin_sprite_as_entry(int16_t x, int16_t y, uint8_t tile, uint8_t attributes,
                                                         bool large)
{
    return (VidMarginSprite){ x, y, (uint16_t)(tile | (attributes & 0x01) << 8), (uint8_t)(attributes >> 1 & 7),
                              (uint8_t)(attributes >> 4 & 3), (attributes & 0x40) != 0, (attributes & 0x80) != 0, large };
}

/* A margin sprite as the frame carries it. */
typedef struct {
    VidMarginSprite sprite;
    bool own;               /* drawn from `picture` rather than VRAM and the palettes */
    VidSpritePicture picture;
} VidMarginEntry;
enum { VID_MARGIN_SPRITES = 128 };

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
    uint8_t snes_extra;     /* bits 4-6 of the SNES byte, which the hardware ignores */
} VidVramPort;

/* The half of each VRAM word an upload of one byte a word writes. */
typedef enum { VID_VRAM_LOW_BYTES, VID_VRAM_HIGH_BYTES } VidVramHalf;

/* The setting every upload in ported code uses: a word at a time, stepping
 * after the high byte. */
#define VID_VRAM_WORD_PORT (&(const VidVramPort){ .step = 1, .after_high = true })

/* What the hardware keeps of a Mode 7 center or scroll word: 13 bits, sign
 * extended. Calls pass the value as written, since the registers hold the whole
 * byte; this is what a renderer reads. */
static inline int16_t vid_mode7_13bit(uint16_t v)
{
    return (int16_t)((int)((v & 0x1FFF) ^ 0x1000) - 0x1000);
}

/* Decoders for the SNES-format bytes the game keeps as shadows, exact like the
 * ones above: the backend encodes each structure back to the byte it came from. */
static inline VidMode vid_mode_decode(uint8_t v)
{
    return (VidMode){ .mode = v & 7, .bg3_priority = (v & 8) != 0, .large_tiles = (uint8_t)(v >> 4) };
}
/* ... and the byte a game keeps for one, for a shadow it hands on. */
static inline uint8_t vid_mode_encode(const VidMode *m)
{
    return (uint8_t)((m->mode & 7) | (m->bg3_priority ? 8 : 0) | (m->large_tiles << 4));
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
                          .snes_step_alt = (v & 3) == 3, .snes_extra = (uint8_t)(v & 0x70) };
}

/* A whole screen, as a mode sets it up when it starts (vid_apply_screen): the
 * display, the sprites' sizes and tiles, the BG mode and mosaic, each layer's
 * tilemap, tiles and scroll, the Mode 7 plane, the windows, which layers are on
 * the main and sub screens, color math and its fixed color, and the video
 * settings, with every VRAM write stepping as `vram_port` says. Setting a
 * screen up also puts the VRAM, OAM and palette cursors back at their starts. */
typedef struct {
    const VidDisplay *display;      /* NULL: the display stays as it is */
    VidObjConfig sprites;
    VidMode mode;
    uint8_t mosaic_layers;          /* VID_LAYER_BIT set of BG1-BG4 */
    uint8_t mosaic_size;            /* 1-16 */
    VidBg bg[4];
    VidMode7 mode7;
    VidWindows windows;
    uint8_t main_layers, sub_layers;
    VidColorMath math;
    uint16_t fixed_color;           /* BGR555 */
    VidVideoConfig config;
    VidVramPort vram_port;
} VidScreen;

/* The fixed color as a BGR555 value: `intensity` (0-31) in each of `channels`. */
static inline uint16_t vid_fixed_color(uint8_t channels, uint8_t intensity)
{
    uint16_t i = intensity & 0x1F;
    return (uint16_t)(((channels & 1) ? i : 0) | ((channels & 2) ? i << 5 : 0) | ((channels & 4) ? i << 10 : 0));
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
    VidView view;           /* how much of the picture to draw (vid_view) */
    int16_t layer_left[4], layer_right[4];   /* the screen X each BG holds the picture across (vid_show_a_layer_across) */
    uint16_t margin_count;  /* the sprites in the view's margins (vid_show_a_sprite_in_the_margins), first on top */
    const VidMarginEntry *margin;
    bool picture_beside_panels;   /* vid_show_the_picture_beside_the_panels */
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
    /* One register or one scroll value, as the menus' window commands ask for */
    VID_LINE_BG_MODE,           /* the BG mode alone, without the mosaic */
    VID_LINE_MAIN_SCREEN,       /* main_layers alone */
    VID_LINE_MATH_LAYERS_ONLY,  /* math layers alone, without the fixed color */
    VID_LINE_SCROLL_BG1_H, VID_LINE_SCROLL_BG1_V,
    VID_LINE_SCROLL_BG2_V,
    VID_LINE_SCROLL_BG3_H, VID_LINE_SCROLL_BG3_V,
    VID_LINE_WINDOW1_LEFT,      /* window 1's left edge alone */
    VID_LINE_MODE7_AB, VID_LINE_MODE7_CD,   /* a pair of Mode 7 matrix values */
    /* One Mode 7 matrix value per line, which the Epoch's screens drive from
     * four tables at once rather than in pairs */
    VID_LINE_MODE7_A, VID_LINE_MODE7_B, VID_LINE_MODE7_C, VID_LINE_MODE7_D,
    /* BG2's tilemap address and size alone, which a scene of the attract
     * sequence changes down the screen (its table of effects, $7E:685E) */
    VID_LINE_BG2_MAP,
    /* Mode 7's centre (X, then Y), which the time warp moves down the screen
     * in halves (game/warp/) */
    VID_LINE_MODE7_CENTER,
    /* Window 1's right edge alone, which the curtain the menus' attract
     * scene sweeps drives (scene/curtain.c) */
    VID_LINE_WINDOW1_RIGHT,
    /* Color math's control and its layers, a pair a line, which the Epoch's
     * dive into Lavos changes down the screen (its setup's descriptors,
     * programmed by scene/screen.c's setup_line_effects) */
    VID_LINE_MATH_CONTROL_AND_LAYERS,
    VID_LINE_SUB_SCREEN,        /* subscreen layers alone, mode 10's horizon */
    VID_LINE_SCROLL_BG2_H,      /* horizontal scroll of the second layer alone */
    VID_LINE_TARGETS,
} VidLineTarget;

/* The values a line effect's table holds, in the form its target reads them
 * (the same as the setters' SNES bytes): games that build tables at run time
 * write them through these, so a value says what it sets. */
static inline uint8_t vid_line_bg_map(const VidBg *b)                  /* VID_LINE_BG_MAPS, a layer's */
{
    return (uint8_t)((b->map_base >> 8 & 0xFC) | (b->map_w == 64 ? 1 : 0) | (b->map_h == 64 ? 2 : 0));
}
static inline uint8_t vid_line_math_layers(const VidColorMath *m)      /* VID_LINE_MATH_LAYERS */
{
    return (uint8_t)((m->subtract ? 0x80 : 0) | (m->half ? 0x40 : 0) | (m->layers & 0x3F));
}
static inline uint8_t vid_math_control(const VidColorMath *m)          /* vid_color_math_decode's VID_MATH_CONTROL */
{
    return (uint8_t)((unsigned)m->clip_black << 6 | ((unsigned)m->prevent_math & 3) << 4 | (m->snes_extra & 0x0C) |
                     (m->sub_screen ? 0x02 : 0) | (m->direct ? 0x01 : 0));
}
/* The channels' bits of VID_LINE_FIXED_COLOR's byte, all of them set. */
enum { VID_LINE_FIXED_COLOR_ALL_CHANNELS = VID_ALL_CHANNELS << 5 };
static inline uint8_t vid_line_fixed_color(uint8_t channels, uint8_t intensity)   /* VID_LINE_FIXED_COLOR */
{
    return (uint8_t)((channels & 7) << 5 | (intensity & 0x1F));
}
/* A window's edges, left in the low byte (VID_LINE_WINDOW1, VID_LINE_WINDOW2). */
static inline uint16_t vid_line_window_edges(uint8_t left, uint8_t right) { return (uint16_t)(right << 8 | left); }

typedef struct {
    VidLineTarget target;
    PfData table;
    bool indirect;
    uint8_t indirect_bank;
    /* The scenes program a slot from a packed five-byte descriptor, whose last
     * value is read as a word: the byte after it lands in the channel's unused
     * size and table-cursor registers ($43x5, $43x8), which HDMA rewrites. It
     * is video state nowhere but in what shadow mode compares, so it is carried
     * here and written only by the steps that name those registers. */
    uint8_t snes_spill;
} VidLineEffect;

/* How the SNES drives a target: the transfer mode and the first register of the
 * group. The pair lives here rather than in the backend because game data holds
 * it instead of naming the target (the menus' window command at $C2:F337 takes
 * the channel's two control bytes from the command stream). */
static inline void vid_line_snes_encoding(VidLineTarget target, uint8_t *mode, uint8_t *reg)
{
    static const uint8_t modes[VID_LINE_TARGETS] = {
        4, 1, 1, 3, 3, 3, 0, 0, 4, 4, 4, 1, 1, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2,
        0, 3, 3, 2, 2, 2, 2, 0, 3, 0, 1, 0, 2,
    };
    static const uint8_t regs[VID_LINE_TARGETS] = {
        0x26, 0x26, 0x28, 0x0D, 0x0F, 0x11, 0x32, 0x00, 0x07, 0x2C, 0x30, 0x05,
        0x31, 0x28, 0x29, 0x05, 0x2C, 0x31, 0x0D, 0x0E, 0x10, 0x11, 0x12,
        0x26, 0x1B, 0x1D, 0x1B, 0x1C, 0x1D, 0x1E, 0x08, 0x1F, 0x27, 0x30, 0x2D, 0x0F,
    };
    *mode = modes[target];
    *reg = regs[target];
}

/* The target a channel's control and destination bytes name, for game data that
 * holds them. False where no target drives that group in that mode. */
/* Whether a cartridge channel selector names one of the eight line slots. */
static inline bool vid_line_channel_valid(uint16_t encoded)
{
    return (encoded & 0xFF8F) == 0x4300;
}

static inline bool vid_line_effect_decode(uint8_t control, uint8_t dest, VidLineEffect *e)
{
    for (int t = 0; t < VID_LINE_TARGETS; t++) {
        uint8_t mode, reg;
        vid_line_snes_encoding((VidLineTarget)t, &mode, &reg);
        if (mode == (control & 7) && reg == dest) {
            e->target = (VidLineTarget)t;
            e->indirect = (control & 0x40) != 0;
            return true;
        }
    }
    return false;
}

/* ---- The view ------------------------------------------------------------------
 *
 * How much of the picture is shown: the original's screen, 256 dots by 224
 * lines, unless the host chose another (a widescreen build; docs/PLAN.md,
 * "Layering"). The game asks it where it decides how much to show or where to
 * lay something out, never where a rule of the game's happens to use the
 * screen's size; the gate runs at the original's. */
PF_API VidView vid_view(const Platform *pf);
/* Where a background layer holds the picture across a wider view this frame:
 * from screen X `left` to short of `right`, the original's screen always
 * among it. Past that its tilemap holds
 * nothing of the picture -- a one-screen map would repeat, a ring would show
 * what it held last -- so the layer is left out there and what lies under it
 * (the backdrop at last) shows. Each frame starts with every layer holding
 * the original's screen alone, so a mode whose layers hold more (a map
 * scrolling in, built past the screen) says so every frame. Mode 7's plane
 * goes on past the screen as its settings say, and is never left out. */
PF_API void vid_show_a_layer_across(Platform *pf, VidLayer bg, int16_t left, int16_t right);
/* A sprite in a wider view's margins this frame (VidMarginSprite), from a
 * mode's own work rather than the game's: it takes none of the game's time and
 * changes nothing the game reads. It is drawn only past the original's
 * screen, behind the table's sprites, a sprite shown earlier over one shown
 * later, and counts toward no sprite limit. `picture` is its own picture, or
 * NULL for VRAM's tiles. Each frame starts with none, so a mode that shows
 * them shows them every frame; past VID_MARGIN_SPRITES, and on the
 * original's view, they are left out. */
PF_API void vid_show_a_sprite_in_the_margins(Platform *pf, const VidMarginSprite *s, const VidSpritePicture *picture);
/* This frame, a wider view's margins beside a panel -- lines whose layers
 * hold the original's screen alone, tilemaps one screen across or kept to it,
 * as a battle's windows are -- show the picture the nearest line above that
 * holds more draws there (below, where none above does), as though the panel
 * lay over it. Each frame starts without; the original's screen is drawn as
 * the lines say. */
PF_API void vid_show_the_picture_beside_the_panels(Platform *pf);

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

/* What a per-line effect slot (0-7) drives, and from which table. */
PF_API void vid_set_line_slot(Platform *pf, PfOrigin o, uint8_t slot, const VidLineEffect *e);
/* A slot's table moves inside the bank it was programmed with, its target and
 * mode unchanged: the menus swap between two tables by frame parity, and
 * rebuild one in place. */
PF_API void vid_set_line_table(Platform *pf, PfOrigin o, uint8_t slot, uint16_t table);
/* Slots whose line effects run from the next frame on (bit per slot). */
PF_API void vid_enable_line_effects(Platform *pf, PfOrigin o, uint8_t slots);

/* ---- Uploads ---------------------------------------------------------------------
 *
 * Each says what moves where. The SNES backend makes the transfer the original
 * made at the call's site, which it looks up there. */

/* Upload work begins: VRAM writes step the address as `port` says from here on
 * (NULL: as they did). Where the original programmed its channel ahead of the
 * uploads that follow, the SNES backend does that here too. */
PF_API void vid_start_vram_uploads(Platform *pf, PfOrigin o, const VidVramPort *port);
/* Copy `len` bytes (1-65536) from `src` into VRAM at word address `vram_addr`.
 * `port`: how the writes step the address, where the upload sets it itself
 * (NULL: as vid_start_vram_uploads left it). A `len` of 0 uploads nothing. */
PF_API void vid_send_vram(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len,
                          const VidVramPort *port);
/* The same for a view wider than the original's screen (vid_view), from its
 * own work rather than the game's: the margins' columns a mode builds. It is
 * shown, but takes none of the game's time, leaves the port and the address
 * where the game's sends left them, and is never read back (vid_vram_word),
 * so the game finds VRAM as at the original's view. */
PF_API void vid_send_view_vram(Platform *pf, uint16_t vram_addr, PfData src, uint32_t len,
                               const VidVramPort *port);
/* From the game's main thread rather than a handler: `len` bytes (1-65536)
 * from `src` into VRAM at word address `vram_addr`, there before the next
 * picture, the call returning once they are. A machine that can reach VRAM
 * only between pictures has its vblank handler send them, a piece a frame,
 * and the caller waits a frame for each: `o` names the call of the mode's
 * routine that queues them and waits (PF_CALL_DC), which the SNES backend
 * makes with the arguments it takes (platform/snes/encodings.c). A host
 * draws the picture after the frame's work, so it copies them now, from a
 * host pointer: a game address, which a host can't resolve until the game's
 * state is its own, stops the program with the call's site. */
PF_API void vid_send_vram_and_wait(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len);
/* Prepare an upload now, then transfer it at the next frame boundary. */
PF_API void vid_send_vram_next_frame(Platform *pf, PfOrigin o, PfOrigin wait,
                                    uint16_t vram_addr, PfData src, uint32_t len,
                                    const VidVramPort *port);
/* The same where the game's data holds the transfer's mode in the SNES's form
 * (a screen transition's, field_transfer; the battle's described transfer). */
PF_API void vid_send_vram_in_mode(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len,
                                  const VidVramPort *port, uint8_t snes_mode);
/* One byte of each VRAM word, the other left as it is: Mode 7 keeps its tile
 * numbers in the low bytes and its pixels in the high ones, which are loaded
 * apart. The port's step then advances the address once per byte. */
PF_API void vid_send_vram_half(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len,
                               VidVramHalf half, const VidVramPort *port);
/* `len` bytes of VRAM filled with the byte at `value`. */
PF_API void vid_send_vram_fill(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData value, uint32_t len,
                               const VidVramPort *port);
/* `count` BGR555 colors from `src` into CGRAM from entry `first`. */
PF_API void vid_send_palette(Platform *pf, PfOrigin o, uint8_t first, PfData src, uint16_t count);
/* The sprite table (VID_OAM_*: 128 entries and the high bits), as the game
 * built it, with where it put each sprite (VID_SPRITES of them, or NULL). */
PF_API void vid_send_oam(Platform *pf, PfOrigin o, PfData oam, const VidSpritePlace *places);
/* `len` bytes of the sprite table from its word `word_addr` (the entries from
 * 0, the high bits from VID_OAM_HIGH_BITS_WORD) all set to `value`: every
 * entry put on one line, or every sprite's high bits cleared. */
PF_API void vid_send_oam_fill(Platform *pf, PfOrigin o, uint16_t word_addr, uint8_t value, uint16_t len);
/* Sprite `index`'s entry (0-127) set in the sprite table; the sprite's high
 * bits (X's ninth, large) are the high table's, which this leaves. A screen
 * of letters set as sprites once, rather than built each frame, is written so. */
PF_API void vid_set_sprite(Platform *pf, PfOrigin o, uint8_t index, const VidSprite *sp);

/* A whole screen set up at once (VidScreen). The SNES backend writes the
 * registers in the order the original's routine at the site did. */
PF_API void vid_apply_screen(Platform *pf, PfOrigin o, const VidScreen *screen);

/* `len` bytes (1-65536) from `src` into VRAM from where the last writes left
 * off (vid_set_vram_address, and the writes since), stepping as the port
 * says: a word at a time, or one byte of each word with the other left as it
 * is. A loader that sets the address once and sends what it unpacks piece by
 * piece sends each piece so. */
PF_API void vid_continue_vram(Platform *pf, PfOrigin o, PfData src, uint32_t len);
PF_API void vid_continue_vram_half(Platform *pf, PfOrigin o, PfData src, uint32_t len, VidVramHalf half);

/* The word VRAM holds at word address `vram_addr`, as the game's uploads left
 * it (never a wider view's, vid_send_view_vram): a setup that builds a map
 * out of one already sent reads it back so. A read takes no time of its own;
 * the caller charges what its copy takes (tm_charge_transfer). */
PF_API uint16_t vid_vram_word(Platform *pf, PfOrigin o, uint16_t vram_addr);

/* The picture's settings that the game's own data names by the SNES address
 * that holds them, which only the menus' object scripts do (an ending's set
 * the sprites' sizes and their tiles' place so): which setting an address
 * is. The setting's byte then decodes as its decoder above says. */
typedef enum { VID_SETTING_NONE, VID_SETTING_SPRITES } VidSetting;
static inline VidSetting vid_setting_at(uint16_t address)
{
    enum { SPRITE_SETTINGS = 0x2101 };
    return address == SPRITE_SETTINGS ? VID_SETTING_SPRITES : VID_SETTING_NONE;
}

/* ---- Writes through the cursors -------------------------------------------------- */

/* Where the next VRAM writes go (word address), and one word or byte written
 * there through the port rather than by an upload. */
PF_API void vid_set_vram_address(Platform *pf, PfOrigin o, uint16_t vram_addr);
PF_API void vid_write_vram(Platform *pf, PfOrigin o, uint16_t value);
PF_API void vid_write_vram_byte(Platform *pf, PfOrigin o, bool high, uint8_t value);
/* Fill one half of each word from the current VRAM cursor, preserving the
 * other half. The active port supplies the stride and which half advances
 * the cursor. A zero length leaves the cursor and memory unchanged. */
PF_API void vid_fill_vram_half(Platform *pf, PfOrigin o, PfData value, uint32_t len, VidVramHalf half);
/* Where the next palette writes go (CGRAM entry), and where the next sprite
 * writes go: the OAM word address, with the priority rotation that decides which
 * sprite is drawn first. Uploads set both themselves; these are for the mode
 * setups that only put them back to 0. */
PF_API void vid_set_palette_address(Platform *pf, PfOrigin o, uint8_t first);
/* Replace one palette colour, including the backdrop at index zero. */
PF_API void vid_set_palette_color(Platform *pf, PfOrigin o, uint8_t index, uint16_t color);
PF_API void vid_set_oam_address(Platform *pf, PfOrigin o, uint16_t word_addr, bool priority_rotation);
/* No uploads are in progress (the SNES backend: the transfer start register cleared). */
PF_API void vid_stop_uploads(Platform *pf, PfOrigin o);

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
void vid_obs_line_table(VidObserver *ob, uint8_t slot, uint16_t table);
void vid_obs_enable_line_effects(VidObserver *ob, uint8_t slots);
void vid_obs_vram_port(VidObserver *ob, const VidVramPort *port);
void vid_obs_vram_address(VidObserver *ob, uint16_t vram_addr);
void vid_obs_palette_address(VidObserver *ob, uint8_t first);
void vid_obs_oam_address(VidObserver *ob, uint16_t word_addr, bool priority_rotation);
void vid_obs_write_vram(VidObserver *ob, uint16_t value, int half);   /* half: -1 word, 0 low, 1 high */
void vid_obs_upload_vram_half(VidObserver *ob, uint16_t vram_addr, PfData src, uint32_t len,
                              VidVramHalf half, const VidVramPort *port);
void vid_obs_fill_vram_half(VidObserver *ob, PfData value, uint32_t len, VidVramHalf half);
void vid_obs_upload_vram(VidObserver *ob, uint16_t vram_addr, PfData src, uint32_t len, bool fill,
                         const VidVramPort *port);
void vid_obs_continue_vram(VidObserver *ob, PfData src, uint32_t len, int half);   /* half as vid_obs_write_vram's */
void vid_obs_upload_view_vram(VidObserver *ob, uint16_t vram_addr, PfData src, uint32_t len,
                              const VidVramPort *port);
uint16_t vid_obs_vram_word(const VidObserver *ob, uint16_t vram_addr);
void vid_obs_upload_palette(VidObserver *ob, uint8_t first, PfData src, uint16_t count);
void vid_obs_upload_oam(VidObserver *ob, PfData oam, const VidSpritePlace *places);
void vid_obs_view(VidObserver *ob, VidView view, const int16_t layer_left[4], const int16_t layer_right[4],
                  const VidMarginEntry *margin, uint16_t margin_count, bool picture_beside_panels);
void vid_obs_fill_oam(VidObserver *ob, uint16_t word_addr, uint8_t value, uint32_t len);
void vid_obs_write_oam(VidObserver *ob, uint8_t v);


#endif
