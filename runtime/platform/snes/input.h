/* Input service, SNES backend: the auto-joypad registers $4218-$421F.
 * Included by platform/input.h. */
#ifndef CT_PLATFORM_SNES_INPUT_H
#define CT_PLATFORM_SNES_INPUT_H

#include "platform/input.h"
#include "platform/snes/backend.h"

PF_API PadBits in_read(Platform *pf, PfOrigin o, uint8_t pad)
{
    Snes *s = pf_snes(pf);
    uint16_t reg = (uint16_t)(0x4218 + 2 * (pad & 3));
    pf_snes_begin("in_read", o);
    uint8_t lo = pf_snes_get(s, reg);
    uint8_t hi = pf_snes_get(s, (uint16_t)(reg + 1));
    pf_snes_end();
    return (PadBits)(hi << 8 | lo);
}

PF_API uint8_t in_read_low(Platform *pf, PfOrigin o, uint8_t pad)
{
    pf_snes_begin("in_read_low", o);
    uint8_t v = pf_snes_get(pf_snes(pf), (uint16_t)(0x4218 + 2 * (pad & 3)));
    pf_snes_end();
    return v;
}

PF_API uint8_t in_read_high(Platform *pf, PfOrigin o, uint8_t pad)
{
    pf_snes_begin("in_read_high", o);
    uint8_t v = pf_snes_get(pf_snes(pf), (uint16_t)(0x4219 + 2 * (pad & 3)));
    pf_snes_end();
    return v;
}

PF_API void in_host_set(Platform *pf, uint8_t pad, PadBits buttons) { pf_snes(pf)->bus.pad[pad & 3] = buttons; }

#endif
