/* Frozen native work rates, sorted by routine identity. cost_milli scales
 * the port's accounted work in thousandths; state_host.c accumulates the
 * resulting frame debt. The committed table is time_rates.c. Changes require
 * native gameplay evidence; the build does not regenerate or calibrate it. */
#ifndef CT_PORT_TIME_RATES_H
#define CT_PORT_TIME_RATES_H

#include <stdint.h>

typedef struct {
    uint32_t routine;
    uint32_t cost_milli;
} TimeRate;

extern const TimeRate time_rates[];
extern const unsigned time_rate_count;

#endif
