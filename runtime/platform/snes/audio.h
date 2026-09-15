/* Audio service, SNES backend: the command buffer at $1E00, the driver at
 * $C7:0004 and the APU ports; with APU-direct selected at run time
 * (platform/direct/audio.h), the driver's waits run the SPC700 instead of the
 * machine. Included by platform/audio.h. */
#ifndef CT_PLATFORM_SNES_AUDIO_H
#define CT_PLATFORM_SNES_AUDIO_H

#include "platform/audio.h"
#include "platform/direct/audio.h"
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
    return au_direct_take_samples(pf_snes(pf), out, max_frames);
}

/* Before a driver port access: the SPC700 sees the time taken so far. */
static inline void au_snes_driver_sync(Snes *s)
{
    if (au_direct_running(s))
        au_direct_pass(s);
    else
        native_sync(s);
}

PF_API void au_driver_write(Platform *pf, PfOrigin o, uint8_t port, uint8_t v)
{
    Snes *s = pf_snes(pf);
    au_snes_driver_sync(s);
    pf_snes_begin("au_driver_write", o);
    pf_snes_put(s, (uint16_t)(0x2140 + (port & 3)), v);
    pf_snes_end();
}

PF_API uint8_t au_driver_read(Platform *pf, PfOrigin o, uint8_t port)
{
    Snes *s = pf_snes(pf);
    au_snes_driver_sync(s);
    pf_snes_begin("au_driver_read", o);
    uint8_t v = pf_snes_get(s, (uint16_t)(0x2140 + (port & 3)));
    pf_snes_end();
    return v;
}

PF_API bool au_driver_run_direct(Platform *pf, uint32_t lo, uint32_t hi) { return au_direct_run(pf_snes(pf), lo, hi); }

PF_API void au_driver_call(Platform *pf, PfOrigin o)
{
    Snes *s = pf_snes(pf);
    if (au_direct_running(s))
        au_direct_call(s, o.site, o.target);
    else
        m_jsr(s, o.site, o.target);
}

PF_API void au_select_backend(Platform *pf, AuBackend backend)
{
    au_direct_select(pf_snes(pf), backend == AU_BACKEND_DIRECT);
}

#endif
