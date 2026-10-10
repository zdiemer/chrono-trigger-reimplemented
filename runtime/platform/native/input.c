/* Input service, host backend (runtime/platform/native/host.h): the pads as
 * the driver last set them, once a frame. The controllers are always read
 * by the time the game looks. */
#include "platform/native/host.h"

bool in_ready(Platform *pf, PfOrigin o) { (void)pf; (void)o; return true; }

PadBits in_read(Platform *pf, PfOrigin o, uint8_t pad)
{
    (void)o;
    return pf->pads[pad & 3];
}

uint8_t in_read_low(Platform *pf, PfOrigin o, uint8_t pad) { return (uint8_t)in_read(pf, o, pad); }
uint8_t in_read_high(Platform *pf, PfOrigin o, uint8_t pad) { return (uint8_t)(in_read(pf, o, pad) >> 8); }
void in_set_io_lines(Platform *pf, PfOrigin o, uint8_t lines) { (void)pf; (void)o; (void)lines; }
void in_host_set(Platform *pf, uint8_t pad, PadBits buttons) { pf->pads[pad & 3] = buttons; }
