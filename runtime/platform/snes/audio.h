/* Audio service, SNES backend: the command buffer at $1E00, the driver at
 * $C7:0004 and the APU ports. Included by platform/audio.h. */
#ifndef CT_PLATFORM_SNES_AUDIO_H
#define CT_PLATFORM_SNES_AUDIO_H

#include "platform/audio.h"
#include "platform/snes/backend.h"

PF_API void au_command(Platform *pf, PfOrigin o, AudioCommand c)
{
    Snes *s = pf_snes(pf);
    const uint8_t params[3] = { c.p1, c.p2, c.p3 };
    for (int i = 0; i < c.count && i < 3; i++)
        m_write8(s, 0x001E01 + (uint32_t)i, params[i]);
    m_write8(s, 0x001E00, c.command);
    m_jsl(s, o.site, o.target);
}

PF_API uint8_t au_status(Platform *pf, PfOrigin o, uint8_t port)
{
    pf_snes_begin("au_status", o);
    uint8_t v = pf_snes_get(pf_snes(pf), (uint16_t)(0x2140 + (port & 3)));
    pf_snes_end();
    return v;
}

PF_API size_t au_take_samples(Platform *pf, int16_t *out, size_t max_frames)
{
    return apu_take_samples(&pf_snes(pf)->apu, out, max_frames);
}

#endif
