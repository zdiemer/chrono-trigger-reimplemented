/* Video service, host backend (runtime/platform/native/host.h): so far the
 * main thread's sends to VRAM, which a host makes at once, the picture being
 * drawn after the frame's work.
 *
 * The data comes from a host pointer (PfData.ptr). A game address alone has
 * nothing behind it here until the game's state is the host's own
 * (docs/PLAN.md, "Phase 3a"), and sends nothing. */
#include <string.h>

#include "platform/native/host.h"

void vid_send_vram_and_wait(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len)
{
    (void)o;
    if (!src.ptr)
        return;
    uint8_t *vram = pf->video.vram;
    uint32_t at = (uint32_t)vram_addr * 2u % VID_HOST_VRAM_BYTES;
    for (uint32_t i = 0; i < len; i++)   /* the address wraps round VRAM, as the SNES's does */
        vram[(at + i) % VID_HOST_VRAM_BYTES] = src.ptr[i];
}
