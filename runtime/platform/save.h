/* Save service (docs/PLATFORM.md): byte access to the 8 KiB save memory. */
#ifndef CT_PLATFORM_SAVE_H
#define CT_PLATFORM_SAVE_H

#include "platform/platform.h"

enum { SAVE_SIZE = 0x2000 };

PF_API uint8_t sv_read8(Platform *pf, PfOrigin o, uint16_t offset);
PF_API void sv_write8(Platform *pf, PfOrigin o, uint16_t offset, uint8_t v);
/* A byte looked at without the machine's time or any other effect, for a
 * port's guard, which runs between the original's instructions. */
PF_API uint8_t sv_peek8(Platform *pf, uint16_t offset);
/* Host side: the SAVE_SIZE bytes to persist. *dirty is set when they may have
 * changed since the last call (the SNES backend doesn't track writes: always). */
PF_API const uint8_t *sv_host_bytes(Platform *pf, bool *dirty);

#ifdef CT_PLATFORM_SNES
#include "platform/snes/save.h"
#endif

#endif
