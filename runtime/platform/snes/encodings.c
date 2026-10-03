/* How the original programmed its transfers, line effects and screen setups,
 * by the site the ported call stands for (platform/snes/encodings.h).
 *
 * Every entry is one instruction sequence of the original's, found at the site
 * named: its DMA or HDMA channel, the mode it wrote, and the order of its
 * register writes. A port that moves to a vid_send_* call brings its site's
 * sequence here from the call, unchanged. */
#include "platform/snes/encodings.h"

#include <stdio.h>
#include <stdlib.h>

#include "platform/video.h"

/* The steps, short: the channel's mode (C) and register (D), its source (S,
 * low, high, bank), its size (Z, low, high; ZC a 16-bit clear before it), the
 * indirect bank (IB), the destination's address register (T, low, high), the
 * VRAM port (P), the start (GO), and the scenes' spill (SPZ, SPC). */
enum {
    C = VID_STEP_CONTROL, D = VID_STEP_DEST, SL = VID_STEP_SOURCE_LO, SH = VID_STEP_SOURCE_HI,
    SB = VID_STEP_SOURCE_BANK, ZL = VID_STEP_SIZE_LO, ZH = VID_STEP_SIZE_HI, ZC = VID_STEP_SIZE_CLEAR,
    IB = VID_STEP_INDIRECT_BANK, TL = VID_STEP_TARGET_LO, TH = VID_STEP_TARGET_HI, GO = VID_STEP_START,
    P = VID_STEP_VRAM_PORT, SPZ = VID_STEP_SPILL_SIZE_LO, SPC = VID_STEP_SPILL_CURSOR_LO,
};
#define STEPS(...) ((const uint8_t[]){ __VA_ARGS__, VID_STEP_END })

/* The transfer modes the sites write. */
enum {
    ONE_REGISTER = 0x00,       /* a byte at a time into one register */
    WORDS = 0x01,              /* into a register pair, a word at a time */
    TWO_BYTES = 0x02,          /* two bytes a time into one register, as CGRAM takes a color */
    FIXED_BYTE = 0x08,         /* the same source byte over and over */
    FILL_WORDS = 0x09,         /* the same byte into both halves of a word */
    WORDS_INDIRECT_BIT = 0x41, /* words, with HDMA's indirect bit, which a transfer ignores */
    FROM_REGISTER = 0x80,      /* out of the register into memory */
};

/* The steps a run of sites share. */
#define FIELD_EDGE_STEPS STEPS(ZL, ZH, TL, TH, SL, SH, P, C, D, SB, GO)
#define FIELD_EDGE(site) { site, 0, 0, 7, WORDS, FIELD_EDGE_STEPS, 0 }
#define FIELD_LINE_STEPS STEPS(C, D, SB, IB, SL, SH)
#define FIELD_LINE(site) { site, 0, 0, 0, 0, FIELD_LINE_STEPS, 0 }
#define SCENE_TRANSFER_STEPS STEPS(C, D, SL, SH, SB, ZL, ZH, GO)
#define RACE_UPLOAD_STEPS STEPS(TL, TH, ZL, ZH, C, D, SL, SH, SB, P, GO)
/* The battle's channel 7, programmed whole; a second transfer reusing its mode,
 * register and bank; and one reusing its mode and register only. */
#define BATTLE_FULL_STEPS STEPS(C, D, SL, SH, SB, ZL, ZH, TL, TH, GO)
#define BATTLE_AGAIN_STEPS STEPS(SL, SH, ZL, ZH, TL, TH, GO)
#define BATTLE_SAME_STEPS STEPS(SL, SH, SB, ZL, ZH, TL, TH, GO)
#define MENU_LINE(site) { site, 0, 0, 0, 0, STEPS(C, D, SL, SH, SB, IB), 0 }
#define EPOCH_UPLOAD_STEPS STEPS(TL, TH, P, C, D, SL, SH, SB, ZL, ZH, GO)

static const PfSnesEncoding encodings[] = {
    /* ---- Shared ---- */
    { 0xC07130, 0, 0, 7, ONE_REGISTER, STEPS(TL, C, D, SL, SH, SB, ZL, ZH, GO), 0 },   /* the working palette */
    { 0xFDE02B, 0, 0, 7, WORDS_INDIRECT_BIT, STEPS(C, D), 0 },                        /* animated tiles: the channel */
    { 0xFDE051, 6, 0x2C, 7, WORDS_INDIRECT_BIT, STEPS(TL, TH, SL, SH, SB, ZC, ZL, GO), 0 },   /* ... even frames' slots */
    { 0xFDE166, 6, 0x2C, 7, WORDS_INDIRECT_BIT, STEPS(TL, TH, SL, SH, SB, ZC, ZL, GO), 0 },   /* ... odd frames' */

    /* ---- The field ---- */
    { 0xC0ECA5, 0, 0, 7, ONE_REGISTER, STEPS(TL, TH, C, D, SL, SH, SB, ZL, ZH, GO), 0 },   /* the sprite table */
    { 0xC0B1BC, 0, 0, 7, WORDS, STEPS(D, C, SB), 0x7F0000 },   /* the VRAM queue: its channel, from bank $7F */
    { 0xC0E19A, 0, 0, 7, WORDS, STEPS(P, D, C, SB, TL, TH, SL, SH, ZL, ZH, GO), 0 },   /* a 24-sprite figure's tiles */
    { 0xC0B1D7, 0, 0, 7, WORDS, STEPS(TL, TH, SL, SH, ZL, ZH, GO), 0 },
    { 0xC0B1EB, 0, 0, 7, WORDS, STEPS(TL, TH, SL, SH, ZL, ZH, GO), 0 },
    /* the layers' edge runs, $C0:8445-$C0:87BF, a part each */
    FIELD_EDGE(0xC0844D), FIELD_EDGE(0xC08468), FIELD_EDGE(0xC0848A), FIELD_EDGE(0xC084C9), FIELD_EDGE(0xC084E4),
    FIELD_EDGE(0xC08506), FIELD_EDGE(0xC0852B), FIELD_EDGE(0xC08546), FIELD_EDGE(0xC08568), FIELD_EDGE(0xC0858D),
    FIELD_EDGE(0xC085A8), FIELD_EDGE(0xC085CA), FIELD_EDGE(0xC085EF), FIELD_EDGE(0xC0860A), FIELD_EDGE(0xC0862C),
    FIELD_EDGE(0xC08651), FIELD_EDGE(0xC0866C), FIELD_EDGE(0xC0868E), FIELD_EDGE(0xC086AF), FIELD_EDGE(0xC086C4),
    FIELD_EDGE(0xC086FB), FIELD_EDGE(0xC08710), FIELD_EDGE(0xC0872D), FIELD_EDGE(0xC08742), FIELD_EDGE(0xC0875F),
    FIELD_EDGE(0xC08774), FIELD_EDGE(0xC08791), FIELD_EDGE(0xC087A6), FIELD_EDGE(0xC087C3), FIELD_EDGE(0xC087D8),
    { 0xC02DCA, 0, 0, 7, WORDS, STEPS(TL, TH, P, C, D, SL, SH, SB, ZL, ZH, GO), 0 },   /* a transition's transfer */
    { 0xC02E01, 0, 0, 7, FROM_REGISTER, STEPS(C, D, SL, SH, SB, ZL, ZH, GO), 0 },     /* its memory fill */
    /* the eight line effects, $FD:C1EE */
    FIELD_LINE(0xFDC1F9), FIELD_LINE(0xFDC1FD), FIELD_LINE(0xFDC201), FIELD_LINE(0xFDC205),
    FIELD_LINE(0xFDC209), FIELD_LINE(0xFDC20D), FIELD_LINE(0xFDC211), FIELD_LINE(0xFDC215),

    /* ---- The menus ($C2:800E's mode) ---- */
    { 0xC28607, 0, 0, 7, ONE_REGISTER, STEPS(TL, TH, C, D, SL, SH, ZL, ZH, SB, GO), 0 },   /* the sprite table */
    { 0xC286BC, 0, 0, 7, ONE_REGISTER, STEPS(C, D, SL, SH, ZL, ZH, SB, TL, GO), 0 },      /* the palette */
    { 0xC28660, 0, 0, 0, 0, STEPS(SL, SH), 0 },                                             /* the math table's flip */
    { 0xC2867A, 0, 0, 7, WORDS, STEPS(C, D), 0 },                                           /* the VRAM queue's channel */
    { 0xC28686, 0, 0, 7, WORDS, STEPS(TL, TH, SL, SH, SB, ZL, ZH, GO), 0 },                /* ... each entry */
    { 0xC286ED, 0, 0, 0, 0, STEPS(C, D, SB, ZL), 0 },                                       /* the math line effect */
    { 0xC226AB, 0, 0, 7, FILL_WORDS, STEPS(TL, TH, P, C, D, SL, SH, SB, ZL, ZH, GO), 0 },  /* VRAM cleared */
    { 0xC22D70, 0, 0, 7, WORDS, STEPS(SL, SH, TL, TH, ZL, ZH, P, C, D, SB, GO), 0 },       /* the loader's upload */
    { 0xC22D4B, 0, 0, 7, ONE_REGISTER, STEPS(TL, C, D, SL, SH, SB, ZL, ZH, GO), 0 },      /* its palette */
    { 0xC2F342, 0, 0, 0, 0, STEPS(SB, IB, C, D), 0 },                                       /* a window command's slot */
    { 0xC2F35C, 0, 0, 0, 0, STEPS(SL, SH), 0 },                                             /* ... and its table */
    { 0xFFFC0A, 0, 0, 0, 0, STEPS(SL, SH), 0 },                                             /* the window line table */

    /* ---- The menus' sprite-object mode ($C2:0000) ---- */
    { 0xC20187, 0, 0, 0, ONE_REGISTER, STEPS(TL, TH, C, D, SL, SH, SB, ZL, ZH, GO), 0 },   /* the sprite table */
    { 0xC201AD, 0, 0, 0, ONE_REGISTER, STEPS(TL, C, D, SL, SH, SB, ZL, ZH, GO), 0 },      /* the palette */
    { 0xC20417, 0, 0, 0, WORDS, STEPS(SL, SH, TL, TH, ZL, ZH, SB, P, D, C, GO), 0 },       /* the VRAM queue */
    { 0xC294D8, 0, 0, 0, FILL_WORDS, STEPS(C, D, ZL, ZH, SL, SH, SB, GO), 0 },             /* VRAM filled */
    { 0xC29526, 0, 0, 0, FIXED_BYTE, STEPS(ZL, ZH, C, D, SL, SH, SB, GO), 0 },             /* work RAM cleared */
    { 0xC2954B, 0, 0, 0, ONE_REGISTER, STEPS(SL, SH, ZL, ZH, C, D, SB, GO), 0 },           /* work RAM copied */
    { 0xC20AF6, 0, 0, 7, WORDS, STEPS(TL, TH, P, C, D, SL, SH, SB, ZL, ZH, GO), 0 },       /* a map block's first column */
    { 0xC20B22, 0, 0, 7, WORDS, STEPS(TL, TH, SL, SH, SB, P, C, D, ZL, ZH, GO), 0 },       /* ... its second */
    /* the line effects the scenes' objects start, $C6:E000-$C6:E5A7 */
    MENU_LINE(0xC6E012), MENU_LINE(0xC6E32A), MENU_LINE(0xC6E45A), MENU_LINE(0xC6E352), MENU_LINE(0xC6E482),
    MENU_LINE(0xC6E5A7),
    /* the top layer's wave over the world maps */
    MENU_LINE(0xC2769A), MENU_LINE(0xC2779E),
    /* the Epoch's map view */
    { 0xC26340, 0, 0, 7, WORDS, EPOCH_UPLOAD_STEPS, 0 },          /* its sprite tiles */
    { 0xC26367, 0, 0, 7, WORDS, EPOCH_UPLOAD_STEPS, 0 },
    { 0xC269C8, 0, 0, 7, WORDS, EPOCH_UPLOAD_STEPS, 0 },          /* its graphics */
    { 0xC264EB, 0, 0, 7, ONE_REGISTER, STEPS(TL, TH, SL, SH, ZL, ZH, C, D, P, SB, GO), 0 },   /* a row of its picture */
    { 0xC2662F, 0, 0, 7, ONE_REGISTER, EPOCH_UPLOAD_STEPS, 0 },   /* a row of its tile numbers */
    MENU_LINE(0xC2694F),                                           /* its gradient */
    /* the time dial */
    { 0xC27393, 0, 0, 7, WORDS, EPOCH_UPLOAD_STEPS, 0 },          /* the gate's tiles */
    { 0xC2726D, 0, 0, 7, WORDS, EPOCH_UPLOAD_STEPS, 0 },          /* ... a run of them */
    { 0xC272EE, 0, 0, 7, WORDS, STEPS(C, D, P, TL, TH, SL, SH, SB, ZL, ZH, GO), 0 },   /* a tilemap row */
    { 0xC2722F, 0, 0, 7, ONE_REGISTER, EPOCH_UPLOAD_STEPS, 0 },   /* the gate's picture */
    MENU_LINE(0xC26AFA),                                           /* the gate's window */
    MENU_LINE(0xC26FBB), MENU_LINE(0xC26FC1), MENU_LINE(0xC26FDE), MENU_LINE(0xC26FE4),   /* its spin's matrix */

    /* ---- The battle ---- */
    { 0xCD09CE, 0, 0, 7, TWO_BYTES, STEPS(IB, TL, C, D, SL, SH, SB, ZL, ZH, GO), 0 },   /* the palette */
    { 0xCD3E95, 0, 0, 7, WORDS, STEPS(TL, TH, SL, SH, SB, C, D, ZL, ZH, GO), 0 },       /* a queued transfer */
    { 0xCD066F, 0, 0, 7, FILL_WORDS, STEPS(TL, TH, C, D, SL, SH, SB, ZL, ZH, GO), 0 },  /* the message's VRAM cleared */
    { 0xD1E8E0, 0, 0, 7, WORDS, STEPS(SL, SH, SB, C, D, ZH, TL, TH, ZL, GO), 0 },       /* a block's first tile */
    { 0xD1E902, 0, 0, 7, WORDS, STEPS(TL, TH, ZL, GO), 0 },                             /* ... the rest */
    { 0xCFE301, 0, 0, 7, ONE_REGISTER, STEPS(C, D, SL, SH, SB, ZL, ZH, TL, GO), 0 },    /* the sprite table */
    { 0xCFE33B, 0, 0, 7, 0, BATTLE_FULL_STEPS, 0 },   /* the described transfer (its mode is data) */
    { 0xCFE386, 0, 0, 7, WORDS, BATTLE_FULL_STEPS, 0 },                                 /* the window frames */
    { 0xCFE3C5, 0, 0, 7, ONE_REGISTER, STEPS(C, D, SL, SH, SB, ZL, ZH, TL, GO), 0 },    /* the palette */
    { 0xCFE40A, 0, 0, 7, WORDS, BATTLE_FULL_STEPS, 0 },       /* a queued upload's first transfer */
    { 0xCFE449, 0, 0, 7, WORDS, BATTLE_AGAIN_STEPS, 0 },      /* ... its second */
    { 0xCFE4A0, 0, 0, 7, WORDS, BATTLE_FULL_STEPS, 0 },       /* a combatant's graphics */
    { 0xCFE4E3, 0, 0, 7, WORDS, BATTLE_AGAIN_STEPS, 0 },
    { 0xCFE51C, 0, 0, 7, WORDS, BATTLE_FULL_STEPS, 0 },       /* the first queued upload again */
    { 0xCFE55B, 0, 0, 7, WORDS, BATTLE_AGAIN_STEPS, 0 },
    { 0xCD0AF0, 0, 0, 7, 0, STEPS(C, D, IB, SB, SL, SH), 0 },   /* the window's line table */
    { 0xCD09F7, 7, 0x1A, 0, 0, STEPS(C, D, SL, SH, SB, IB), 0 },   /* the seven line effects, $CD:09F1 */
    /* the graphics load, $CC:E000, a step a frame */
    { 0xCCE00C, 0, 0, 7, WORDS, BATTLE_FULL_STEPS, 0 }, { 0xCCE047, 0, 0, 7, WORDS, BATTLE_SAME_STEPS, 0 },
    { 0xCCE076, 0, 0, 7, WORDS, BATTLE_SAME_STEPS, 0 }, { 0xCCE0AF, 0, 0, 7, WORDS, BATTLE_FULL_STEPS, 0 },
    { 0xCCE0EB, 0, 0, 7, WORDS, BATTLE_SAME_STEPS, 0 }, { 0xCCE11B, 0, 0, 7, WORDS, BATTLE_SAME_STEPS, 0 },
    { 0xCCE14B, 0, 0, 7, WORDS, BATTLE_SAME_STEPS, 0 }, { 0xCCE17A, 0, 0, 7, WORDS, BATTLE_SAME_STEPS, 0 },
    { 0xCCE1B3, 0, 0, 7, WORDS, BATTLE_FULL_STEPS, 0 }, { 0xCCE1EE, 0, 0, 7, WORDS, BATTLE_FULL_STEPS, 0 },
    { 0xCCE22E, 0, 0, 7, WORDS, BATTLE_FULL_STEPS, 0 }, { 0xCCE267, 0, 0, 7, WORDS, BATTLE_AGAIN_STEPS, 0 },

    /* ---- The scenes ---- */
    { 0x7E30B6, 0, 0, 7, FROM_REGISTER, SCENE_TRANSFER_STEPS, 0 },   /* work RAM cleared: the direct page */
    { 0x7E30D2, 0, 0, 7, FROM_REGISTER, STEPS(SL, SH, ZL, ZH, GO), 0 },   /* ... from the tasks */
    { 0x7E30E6, 0, 0, 7, FROM_REGISTER, STEPS(SL, SH, ZL, ZH, GO), 0 },   /* ... from the stack */
    { 0x7E3811, 0, 0, 7, ONE_REGISTER, STEPS(TL, TH, SB, C, D, SL, SH, ZL, ZH, GO), 0 },   /* the sprite table */
    { 0x7E3850, 0, 0, 7, FROM_REGISTER, STEPS(SB, D, SL, SH, ZL, ZH, C, GO), 0 },          /* ... cleared */
    { 0x7E388D, 0, 0, 7, ONE_REGISTER, STEPS(C, TL, SB, D, SL, SH, ZL, ZH, GO), 0 },       /* the palette */
    { 0x7E38EA, 0, 0, 7, ONE_REGISTER, SCENE_TRANSFER_STEPS, 0 },   /* the loaders' transfer (its mode is data) */
    { 0x7E36E8, 0, 0, 0, 0, STEPS(C, D, SL, SH, SB, SPZ, IB, SPC), 0 },   /* line effects from descriptors */
    /* the race */
    { 0x7E3C22, 0, 0, 7, FROM_REGISTER, SCENE_TRANSFER_STEPS, 0 },   /* its buffers cleared */
    { 0x7E3C3E, 0, 0, 7, FROM_REGISTER, SCENE_TRANSFER_STEPS, 0 },
    { 0x7E3E71, 0, 0, 7, FROM_REGISTER, SCENE_TRANSFER_STEPS, 0 },
    { 0x7E3D41, 0, 0, 7, FROM_REGISTER, SCENE_TRANSFER_STEPS, 0 },   /* a read back out of VRAM */
    { 0x7E3DB0, 0, 0, 7, FILL_WORDS, STEPS(TL, TH, C, D, SL, SH, SB, ZL, ZH, GO), 0 },    /* the racers' maps blanked */
    { 0x7E3D81, 0, 0, 0, 0, STEPS(SL, SH), 0 },   /* the race's curtain: its window's table on odd steps */
    { 0x7E3D88, 0, 0, 0, 0, STEPS(SL, SH), 0 },   /* ... and on even ones */
    { 0x7E876E, 0, 0, 7, ONE_REGISTER, SCENE_TRANSFER_STEPS, 0 },    /* the plane's low bytes */
    { 0x7E4671, 0, 0, 7, ONE_REGISTER, STEPS(SL, SH, SB, C, D, P, TL, TH, ZL, ZH, GO), 0 },   /* a column */
    { 0x7E525B, 0, 0, 7, WORDS, RACE_UPLOAD_STEPS, 0 },   /* half a tilemap */
    { 0x7E5287, 0, 0, 7, WORDS, RACE_UPLOAD_STEPS, 0 },   /* the road strip */
    /* the title */
    { 0x7E59DD, 0, 0, 7, FILL_WORDS, STEPS(C, TL, TH, ZL, ZH, GO), 0 },   /* a fill that keeps the channel's source */
    { 0x7E5AEB, 0, 0, 7, FIXED_BYTE, SCENE_TRANSFER_STEPS, 0 },          /* low bytes filled */
    { 0x7E6215, 0, 0, 7, WORDS, STEPS(TL, TH, C, D, SL, SH, SB, ZL, ZH, P, GO), 0 },   /* BG3's offset rows */
    /* the staff roll */
    { 0x7E90BD, 0, 0, 7, FIXED_BYTE, STEPS(TL, TH, P, ZL, ZH, C, D, SL, SH, SB, GO), 0 },   /* the credits' tiles blanked */
    { 0x7E8F77, 0, 0, 7, FIXED_BYTE, STEPS(TL, TH, ZL, ZH, C, D, SL, SH, P, GO), 0 },       /* ... and set in front */
    { 0x7E9F1D, 0, 0, 7, ONE_REGISTER, STEPS(TL, TH, ZL, ZH, C, D, SL, SH, SB, P, GO), 0 },  /* a row of the credits */
};

void pf_snes_encoding_mismatch(uint32_t site, const char *call)
{
    fprintf(stderr, "%s at $%02X:%04X: the call and the original's encoding there disagree "
                    "(platform/snes/encodings.c)\n", call, (unsigned)(site >> 16), (unsigned)(site & 0xFFFF));
    abort();
}

static bool covers(const PfSnesEncoding *e, uint32_t site)
{
    if (e->copies == 0)
        return site == e->site;
    return site >= e->site && (site - e->site) % e->stride == 0 && (site - e->site) / e->stride < e->copies;
}

const PfSnesEncoding *pf_snes_encoding_find(uint32_t site)
{
    for (size_t i = 0; i < sizeof encodings / sizeof encodings[0]; i++)
        if (covers(&encodings[i], site))
            return &encodings[i];
    return NULL;
}

const PfSnesEncoding *pf_snes_encoding(uint32_t site, const char *call)
{
    const PfSnesEncoding *e = pf_snes_encoding_find(site);
    if (!e) {
        fprintf(stderr, "%s at $%02X:%04X: no encoding of the original's there (platform/snes/encodings.c)\n", call,
                (unsigned)(site >> 16), (unsigned)(site & 0xFFFF));
        abort();
    }
    return e;
}

/* ---- Whole screens ---- */

enum {
    EVERY_WINDOW_SETTING = VID_WINDOWS_BG1_BG2 | VID_WINDOWS_BG3_BG4 | VID_WINDOWS_OBJ_BACK | VID_WINDOWS_W1_LEFT |
                           VID_WINDOWS_W1_RIGHT | VID_WINDOWS_W2_LEFT | VID_WINDOWS_W2_RIGHT | VID_WINDOWS_BG_LOGIC |
                           VID_WINDOWS_OBJ_LOGIC,
    WINDOW_MASKS = VID_WINDOWS_MAIN | VID_WINDOWS_SUB,
    BOTH_SCREENS = VID_SCREEN_MAIN | VID_SCREEN_SUB,
    RGB = VID_RED | VID_GREEN | VID_BLUE,
};

#define PART(part, arg, site) { PF_SCREEN_##part, arg, site }
#define END { PF_SCREEN_END, 0, 0 }

/* $FD:C036, the register block at power-on. */
static const PfScreenStep power_on[] = {
    PART(DISPLAY, 0, 0xFDC040), PART(SPRITES, 0, 0xFDC044), PART(OAM_ADDRESS, 0, 0xFDC048), PART(MODE, 0, 0xFDC04E),
    PART(MOSAIC, 0, 0xFDC052),
    PART(BG_MAP, VID_BG1, 0xFDC054), PART(BG_MAP, VID_BG2, 0xFDC056), PART(BG_MAP, VID_BG3, 0xFDC058),
    PART(BG_MAP, VID_BG4, 0xFDC05A), PART(BG_CHARS, VID_BG1, 0xFDC05C), PART(BG_CHARS, VID_BG3, 0xFDC05E),
    PART(SCROLL, VID_BG1, 0xFDC060), PART(SCROLL, VID_BG2, 0xFDC068), PART(SCROLL, VID_BG3, 0xFDC070),
    PART(SCROLL, VID_BG4, 0xFDC078), PART(VRAM_PORT, 0, 0xFDC080), PART(VRAM_ADDRESS, 0, 0xFDC082),
    PART(MODE7, VID_M7_SELECT, 0xFDC086), PART(MODE7, VID_M7_MATRIX, 0xFDC088), PART(MODE7, VID_M7_CENTER, 0xFDC09D),
    PART(PALETTE_ADDRESS, 0, 0xFDC0A5), PART(WINDOWS, EVERY_WINDOW_SETTING, 0xFDC0A7),
    PART(WINDOWS, WINDOW_MASKS, 0xFDC0BD), PART(COLOR_MATH, VID_MATH_CONTROL | VID_MATH_LAYERS, 0xFDC0C1),
    PART(SCREENS, BOTH_SCREENS, 0xFDC0C7), PART(FIXED_COLOR, VID_GREEN | VID_BLUE, 0xFDC0CD),
    PART(VIDEO_CONFIG, 0, 0xFDC0D1), END,
};

/* $7E:30F9, a scene's first screen. */
static const PfScreenStep scene_first[] = {
    PART(DISPLAY, 0, 0x7E3108), PART(SPRITES, 0, 0x7E310C), PART(FIXED_COLOR, RGB, 0x7E3110),
    PART(OAM_ADDRESS, 0, 0x7E3118), PART(MODE, 0, 0x7E311A), PART(MOSAIC, 0, 0x7E311C),
    PART(BG_MAP, VID_BG1, 0x7E311E), PART(BG_MAP, VID_BG2, 0x7E311E), PART(BG_MAP, VID_BG3, 0x7E3120),
    PART(BG_MAP, VID_BG4, 0x7E3120), PART(BG_CHARS, VID_BG1, 0x7E3122), PART(BG_CHARS, VID_BG3, 0x7E3122),
    PART(SCROLL, VID_BG1, 0x7E3124), PART(SCROLL, VID_BG2, 0x7E312C), PART(SCROLL, VID_BG3, 0x7E3134),
    PART(SCROLL, VID_BG4, 0x7E313C), PART(VRAM_PORT, 0, 0x7E3144), PART(MODE7, VID_M7_SELECT, 0x7E3146),
    PART(MODE7, VID_M7_MATRIX, 0x7E3148), PART(MODE7, VID_M7_CENTER, 0x7E3158), PART(PALETTE_ADDRESS, 0, 0x7E315E),
    PART(WINDOWS, EVERY_WINDOW_SETTING, 0x7E3160), PART(SCREENS, BOTH_SCREENS, 0x7E316A),
    PART(WINDOWS, WINDOW_MASKS, 0x7E316C), PART(COLOR_MATH, VID_MATH_CONTROL, 0x7E316E),
    PART(COLOR_MATH, VID_MATH_LAYERS, 0x7E316E),
    /* a 16-bit store of 0 that reaches the fixed color with no channel bit
     * set, which changes nothing but the register's last byte */
    PART(FIXED_COLOR, 0, 0x7E3170), PART(VIDEO_CONFIG, 0, 0x7E3170), END,
};

/* $C2:0043, the menus' sprite-object mode's screen. */
static const PfScreenStep object_mode[] = {
    PART(SPRITES, 0, 0xC2004A), PART(OAM_ADDRESS, 0, 0xC2004F), PART(MODE, 0, 0xC20053), PART(MOSAIC, 0, 0xC20057),
    PART(BG_MAP, VID_BG1, 0xC2005B), PART(BG_MAP, VID_BG2, 0xC2005F), PART(BG_MAP, VID_BG3, 0xC20063),
    PART(BG_MAP, VID_BG4, 0xC20067), PART(BG_CHARS, VID_BG1, 0xC2006B), PART(BG_CHARS, VID_BG3, 0xC2006F),
    PART(SCROLL, VID_BG1, 0xC20073), PART(SCROLL, VID_BG2, 0xC2007B), PART(SCROLL, VID_BG3, 0xC20083),
    PART(SCROLL, VID_BG4, 0xC2008B), PART(VRAM_PORT, 0, 0xC20095), PART(VRAM_ADDRESS, 0, 0xC2009A),
    PART(MODE7, VID_M7_SELECT, 0xC2009E), PART(MODE7, VID_M7_MATRIX, 0xC200A0), PART(MODE7, VID_M7_CENTER, 0xC200B8),
    PART(PALETTE_ADDRESS, 0, 0xC200C0), PART(WINDOWS, EVERY_WINDOW_SETTING, 0xC200C2),
    PART(SCREENS, BOTH_SCREENS, 0xC200D6), PART(WINDOWS, WINDOW_MASKS, 0xC200DE),
    PART(COLOR_MATH, VID_MATH_CONTROL, 0xC200E4), PART(COLOR_MATH, VID_MATH_LAYERS, 0xC200E8),
    PART(FIXED_COLOR, RGB, 0xC200EC), PART(VIDEO_CONFIG, 0, 0xC200F0), END,
};

/* $C2:940D, the screen the menus' graphics loader builds its screens on. */
static const PfScreenStep loader_screen[] = {
    PART(DISPLAY, 0, 0xC2941F), PART(SPRITES, 0, 0xC29423), PART(MODE, 0, 0xC29427),
    PART(BG_MAP, VID_BG1, 0xC2942B), PART(BG_MAP, VID_BG2, 0xC2942F), PART(BG_MAP, VID_BG3, 0xC29433),
    PART(BG_MAP, VID_BG4, 0xC29437), PART(BG_CHARS, VID_BG1, 0xC2943B), PART(BG_CHARS, VID_BG3, 0xC2943F),
    PART(MOSAIC, 0, 0xC29443), PART(SCROLL, VID_BG1, 0xC29445), PART(SCROLL, VID_BG2, 0xC2944D),
    PART(SCROLL, VID_BG3, 0xC29455), PART(SCROLL, VID_BG4, 0xC2945D), PART(VRAM_PORT, 0, 0xC29467),
    PART(VRAM_ADDRESS, 0, 0xC2946C), PART(MODE7, VID_M7_SELECT, 0xC29470), PART(MODE7, VID_M7_MATRIX, 0xC29472),
    PART(MODE7, VID_M7_CENTER, 0xC2948E), PART(PALETTE_ADDRESS, 0, 0xC29492),
    PART(WINDOWS, EVERY_WINDOW_SETTING, 0xC29494), PART(SCREENS, BOTH_SCREENS, 0xC294A6),
    PART(WINDOWS, WINDOW_MASKS, 0xC294AA), PART(COLOR_MATH, VID_MATH_CONTROL, 0xC294AE),
    PART(COLOR_MATH, VID_MATH_LAYERS, 0xC294B0), PART(VIDEO_CONFIG, 0, 0xC294B2), PART(FIXED_COLOR, RGB, 0xC294B6),
    END,
};

static const struct {
    uint32_t site;
    const PfScreenStep *steps;
} screens[] = {
    { 0xFDC036, power_on },
    { 0x7E30F9, scene_first },
    { 0xC20043, object_mode },
    { 0xC2940D, loader_screen },
};

const PfScreenStep *pf_snes_screen_steps(uint32_t site)
{
    for (size_t i = 0; i < sizeof screens / sizeof screens[0]; i++)
        if (screens[i].site == site)
            return screens[i].steps;
    fprintf(stderr, "vid_apply_screen at $%02X:%04X: no screen setup of the original's there "
                    "(platform/snes/encodings.c)\n", (unsigned)(site >> 16), (unsigned)(site & 0xFFFF));
    abort();
}
