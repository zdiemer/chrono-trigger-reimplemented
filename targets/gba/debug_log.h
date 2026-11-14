/* The GBA build's text out: mGBA's debug log (targets/gba/system.c), which
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
uint32_t cycles_now(void);

#endif
