/* Save service, SNES backend: SRAM at $30:6000-$7FFF (mirrored in banks
 * $20-$3F and $A0-$BF). Included by platform/save.h. */
#ifndef CT_PLATFORM_SNES_SAVE_H
#define CT_PLATFORM_SNES_SAVE_H

#include "platform/save.h"
#include "platform/snes/backend.h"

PF_API uint8_t sv_read8(Platform *pf, PfOrigin o, uint16_t offset)
{
    (void)o;
    return m_read8(pf_snes(pf), 0x306000 + (uint32_t)(offset % SAVE_SIZE));
}

PF_API void sv_write8(Platform *pf, PfOrigin o, uint16_t offset, uint8_t v)
{
    (void)o;
    m_write8(pf_snes(pf), 0x306000 + (uint32_t)(offset % SAVE_SIZE), v);
}

PF_API const uint8_t *sv_host_bytes(Platform *pf, bool *dirty)
{
    if (dirty)
        *dirty = true;
    return pf_snes(pf)->bus.sram;
}

#endif
