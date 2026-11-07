/* The calibrated rates of the routines a port's work is timed as, for a build
 * with no machine (ct_native): each registered entry's and time tag's
 * cost_milli, the original's clocks per thousand of the port's bus clocks, as
 * tools/timing_gate.py last calibrated them on the machine. The build writes
 * the table from game/registry.c and game/time_tags.c (tools/time_rates.py),
 * sorted by address; runtime/port/state_host.c charges the game's own work at
 * them (docs/REIMPLEMENTATION.md, "The frame-level time model"). */
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
