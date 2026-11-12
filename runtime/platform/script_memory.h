/* Hardware observations embedded in the event language's memory operands.
 * Keep the original address decoding here, beside the services that answer
 * it, rather than treating an I/O address as a byte of host RAM. */
#ifndef CT_PLATFORM_SCRIPT_MEMORY_H
#define CT_PLATFORM_SCRIPT_MEMORY_H

#include "platform/audio.h"

static inline bool pf_script_status(Platform *pf, PfOrigin origin, uint32_t address, uint8_t *value)
{
    uint8_t bank = (uint8_t)(address >> 16);
    uint16_t at = (uint16_t)address;
    /* The APU ports repeat through $217F in each I/O bank. */
    if ((bank & 0x7F) < 0x40 && at >= 0x2140 && at < 0x2180) {
        *value = au_status(pf, origin, (uint8_t)(at & 3));
        return true;
    }
    return false;
}

#endif
