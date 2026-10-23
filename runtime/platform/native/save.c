/* Save service, host backend (runtime/platform/native/host.h): the
 * cartridge's save memory as the driver hands it over (Platform.save). With
 * none, the cartridge holds nothing: every byte reads 0 and writes go
 * nowhere. */
#include "platform/native/host.h"

uint8_t sv_read8(Platform *pf, PfOrigin o, uint16_t offset)
{
    (void)o;
    return sv_peek8(pf, offset);
}

void sv_write8(Platform *pf, PfOrigin o, uint16_t offset, uint8_t v)
{
    (void)o;
    if (!pf->save.bytes)
        return;
    pf->save.bytes[offset % SAVE_SIZE] = v;
    pf->save.dirty = true;
}

uint8_t sv_peek8(Platform *pf, uint16_t offset) { return pf->save.bytes ? pf->save.bytes[offset % SAVE_SIZE] : 0; }

const uint8_t *sv_host_bytes(Platform *pf, bool *dirty)
{
    *dirty = pf->save.dirty;
    pf->save.dirty = false;
    return pf->save.bytes;
}
