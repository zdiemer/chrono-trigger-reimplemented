/* The SNES backend's knowledge of how the original programmed its transfers,
 * line effects and screen setups, by the call site (PfOrigin.site) that stands
 * for the original's code (docs/PLATFORM.md, "P2 as built").
 *
 * Shadow mode compares the DMA and HDMA registers as the original left them, so
 * the SNES backend has to make the writes the original made, in its order: an
 * empty upload stops at the size, a channel another routine programmed is not
 * written again, and a register written twice keeps the second value. None of
 * that is the game's business, so the calls in game/ say what moves where and
 * the backend looks the rest up here. Other backends never see it. */
#ifndef CT_PLATFORM_SNES_ENCODINGS_H
#define CT_PLATFORM_SNES_ENCODINGS_H

#include <stdbool.h>
#include <stdint.h>

/* How the original programs the channel at one site (or at a run of sites,
 * where the original's routine is several copies of the same code). */
typedef struct {
    uint32_t site;
    uint8_t copies;          /* sites site, site + stride, ...; 0 is one */
    uint8_t stride;
    uint8_t channel;         /* 0-7; a line effect's is its slot */
    uint8_t control;         /* $43x0 as the original writes it; a line effect's comes from its target */
    const uint8_t *steps;    /* VID_STEP_*, ending in VID_STEP_END */
    uint32_t source;         /* where a site that only prepares the channel points it */
} PfSnesEncoding;

/* The site's encoding, or NULL where the original programs no channel there
 * (a VRAM port set on its own). */
const PfSnesEncoding *pf_snes_encoding_find(uint32_t site);
/* The same for a call that can't be made without one: an unknown site is a
 * port whose encoding hasn't been added, and stops the program with its
 * address. */
const PfSnesEncoding *pf_snes_encoding(uint32_t site, const char *call);
/* A call that disagrees with its site's encoding (an upload that names a VRAM
 * port where the original's sets none, or the other way round): stops the
 * program with the site. */
void pf_snes_encoding_mismatch(uint32_t site, const char *call);

/* The modes' routines that send a buffer to VRAM from the main thread,
 * queueing it for the vblank handler and waiting for it to go
 * (vid_send_vram_and_wait), and where each takes its arguments: the source's
 * bank in A, its address in X, the VRAM address in Y and the size in a word of
 * the caller's direct page. NULL where the routine is none of them. */
typedef struct {
    uint32_t routine;
    uint8_t size_cell;
} PfSnesVramSender;
const PfSnesVramSender *pf_snes_vram_sender(uint32_t routine);

/* What the original writes, in order, where one call sets a whole screen up
 * (vid_apply_screen). Each part is one of the service's setters with the site
 * of the instruction it stands for; `arg` is the part's layer, register group
 * or channels. */
typedef enum {
    PF_SCREEN_END,
    PF_SCREEN_DISPLAY,
    PF_SCREEN_SPRITES,
    PF_SCREEN_OAM_ADDRESS,
    PF_SCREEN_MODE,
    PF_SCREEN_MOSAIC,
    PF_SCREEN_BG_MAP,         /* arg: the layer */
    PF_SCREEN_BG_CHARS,       /* arg: the pair's first layer */
    PF_SCREEN_SCROLL,         /* arg: the layer */
    PF_SCREEN_VRAM_PORT,
    PF_SCREEN_VRAM_ADDRESS,
    PF_SCREEN_MODE7,          /* arg: VID_M7_* */
    PF_SCREEN_PALETTE_ADDRESS,
    PF_SCREEN_WINDOWS,        /* arg: VID_WINDOWS_* */
    PF_SCREEN_SCREENS,        /* arg: VID_SCREEN_* */
    PF_SCREEN_COLOR_MATH,     /* arg: VID_MATH_* */
    PF_SCREEN_FIXED_COLOR,    /* arg: the channels written (0 writes none, which the scenes do) */
    PF_SCREEN_VIDEO_CONFIG,
} PfScreenPart;

typedef struct {
    uint8_t part;
    uint16_t arg;
    uint32_t site;
} PfScreenStep;

/* The order at a whole-screen site; stops the program at an unknown one. */
const PfScreenStep *pf_snes_screen_steps(uint32_t site);

#endif
