/* The cartridge reads ct_dev reports: those the catalogue of resources doesn't
 * hold, and with --asset-reads every address read, which tools/assets.py
 * builds the catalogue from (docs/PLATFORM.md, "The assets, as built"). */
#ifndef CT_DEV_ASSET_REPORT_H
#define CT_DEV_ASSET_REPORT_H

#include <stdio.h>

/* Record every address the accessors read from now on. Returns 0, or -1. */
int asset_reads_start(void);
/* Write what was recorded to `path`, a `BB:AAAA-BB:AAAA` range a line.
 * Returns 0, or -1 after printing why not. */
int asset_reads_write(const char *path);

/* An `asset outside the catalogue` line if any read landed in no resource
 * (tools/suite.py fails the run). Returns how many lines it printed. */
unsigned asset_report(FILE *f);

#endif
