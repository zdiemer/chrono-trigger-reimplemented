/* Video service, host backend (runtime/platform/native/host.h): so far the
 * main thread's sends to VRAM, which a host makes at once, the picture being
 * drawn after the frame's work.
 *
 * The data comes from a host pointer (PfData.ptr). A game address alone has
 * nothing behind it on a host until the game's state is the host's own
 * (docs/PLAN.md, "Phase 3a"): such a send stops the program, naming its site,
 * rather than leave VRAM without what the game sent. */
#include <stdio.h>
#include <stdlib.h>

#include "platform/native/host.h"

void vid_send_vram_and_wait(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len)
{
    if (!src.ptr) {
        fprintf(stderr, "vid_send_vram_and_wait at $%02X:%04X: game address $%06X has no host memory yet\n",
                (unsigned)(o.site >> 16), (unsigned)(o.site & 0xFFFF), (unsigned)src.addr);
        abort();
    }
    uint8_t *vram = pf->video.vram;
    uint32_t at = (uint32_t)vram_addr * 2u % VID_HOST_VRAM_BYTES;
    for (uint32_t i = 0; i < len; i++)   /* the address wraps round VRAM, as the SNES's does */
        vram[(at + i) % VID_HOST_VRAM_BYTES] = src.ptr[i];
}

/* No transfers wait for the vblank on a host: the sends are made at once. */
void vid_stop_uploads(Platform *pf, PfOrigin o) { (void)pf, (void)o; }

void vid_enable_line_effects(Platform *pf, PfOrigin o, uint8_t slots)
{
    (void)o;
    pf->video.line_effects = slots;
}

/* What a screen sets is kept as far as the host has somewhere to keep it: the
 * display. The rest has no host half until the picture is drawn from the
 * calls (docs/PLAN.md, Phase 4a, "Video fed by service calls"). */
void vid_apply_screen(Platform *pf, PfOrigin o, const VidScreen *screen)
{
    (void)o;
    if (screen->display)
        pf->video.display = *screen->display;
    pf->video.screens_applied++;
}
