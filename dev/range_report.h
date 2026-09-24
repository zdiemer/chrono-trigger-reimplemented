/* The accessors called out of range (port/vars_ram.h), as the lines ct_dev
 * and ct_fuzz print: one per variable, `accessor out of range: <var>[<index>]
 * (<bound> <limit>) x<count> (first at $BB:AAAA in <accessor>; ...)`, which
 * tools/suite.py fails a movie for. Prints nothing in a build without
 * CT_ACCESSOR_CHECKS. */
#ifndef CT_DEV_RANGE_REPORT_H
#define CT_DEV_RANGE_REPORT_H

#include <stdio.h>

/* Returns how many variables went out of range. */
unsigned range_report(FILE *f);

#endif
