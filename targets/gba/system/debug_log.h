/* The GBA build's text out: mGBA's debug log (targets/gba/system/system.c), which
 * stdout and stderr write to a line at a time. */
#ifndef CT_GBA_DEBUG_LOG_H
#define CT_GBA_DEBUG_LOG_H

#include <stdint.h>

/* Ask mGBA for its debug log; on hardware without it the text goes nowhere. */
void debug_log_open(void);

/* The processor's cycles, counted by two of the GBA's timers chained (the
 * third and fourth: the first two are the sound's to take), from
 * cycles_start; a count wraps after about four minutes. */
void cycles_start(void);

/* Read where it is asked for, a few loads with no call: the driver and the
 * mixing ask every frame and every chunk, from the cartridge and from fast
 * RAM. The high half is read again where the low one wrapped between the
 * two reads. */
#define CT_GBA_TIMER2_COUNT (*(volatile uint16_t *)0x04000108)
#define CT_GBA_TIMER3_COUNT (*(volatile uint16_t *)0x0400010C)
static inline uint32_t cycles_now(void)
{
    uint16_t high = CT_GBA_TIMER3_COUNT, low = CT_GBA_TIMER2_COUNT, again = CT_GBA_TIMER3_COUNT;
    if (again != high)
        low = CT_GBA_TIMER2_COUNT;
    return (uint32_t)again << 16 | low;
}

#endif
