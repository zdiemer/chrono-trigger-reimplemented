/* The cartridge reads ct_dev reports: those the catalogue of resources doesn't
 * hold, and with --asset-reads every address read, which tools/assets.py
 * builds the catalogue from (docs/PLATFORM.md, "The assets, as built"). */
#ifndef CT_DEV_ASSET_REPORT_H
#define CT_DEV_ASSET_REPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Record every address the accessors read from now on. Returns 0, or -1. */
int asset_reads_start(void);
/* Write what was recorded to `path`, a `BB:AAAA-BB:AAAA` range a line.
 * Returns 0, or -1 after printing why not. */
int asset_reads_write(const char *path);

/* Read the game's resources from the asset pack at `path` (frontend/assets.h),
 * importing it from the ROM image first where there is none or it is of
 * another catalogue. Returns 0, or -1 after printing why not. */
int asset_pack_use(const char *path, const uint8_t *rom, size_t size);

/* An `asset outside the catalogue` line if any read landed in no resource
 * (tools/suite.py fails the run). Returns how many lines it printed. */
unsigned asset_report(FILE *f);
/* The same for an `asset differs from the cartridge` line: a read whose
 * store's byte isn't the machine's (a pack that isn't the ROM's). */

#endif
