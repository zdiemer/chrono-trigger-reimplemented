/* Native accessor diagnostics and uncharged validation reads. */
#ifndef CT_PORT_VARS_RAM_H
#define CT_PORT_VARS_RAM_H

#include "port/vars_pages.h"
#include <stdbool.h>
#include <stdint.h>
#include "state.h"

/* Validation can inspect state and resources without charging work. */
void vars_peek(bool on);
bool vars_peeking(void);

/* Bounds diagnostics for CT_ACCESSOR_CHECKS builds. Indexed overruns retain
 * the legacy byte-layout behavior while recording the invalid access. */
typedef struct {
    const char *accessor;   /* the accessor called, e.g. "set_field_object_role" */
    const char *bound;      /* which limit: "count", "bytes", "rows", "columns", "instances" */
    unsigned index;
    unsigned limit;
    uint32_t pc;            /* reserved diagnostic origin; native accesses report zero */
} VarsRangeHit;
enum { VARS_RANGE_HITS_KEPT = 4 };
typedef struct {
    const char *var;
    unsigned long count;
    unsigned kept;          /* how many of first[] are filled */
    VarsRangeHit first[VARS_RANGE_HITS_KEPT];
} VarsRangeFault;
const VarsRangeFault *vars_range_faults(unsigned *count);
void vars_range_reset(void);

#endif
