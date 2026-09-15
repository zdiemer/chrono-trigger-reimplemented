/* ct_dev --misses: every instruction the interpreter ran, counted by address and
 * widths, and classified as ROM or RAM code, and RAM code by the RAM section
 * ([[ramcode]] hints) it lies in. With --native on --recomp only this is the
 * miss report of docs/RECOMP.md (M3) and docs/MATCHING.md (C7): what generated
 * blocks didn't run. The file is also a coverage file (tools/disasm reads
 * `BB:AAAA m x` and ignores what follows `#`). */
#ifndef CT_DEV_MISSES_H
#define CT_DEV_MISSES_H

#include <stdio.h>

#include "snes/system.h"

typedef struct Misses Misses;

Misses *misses_create(const Snes *s);
/* From the CPU's instruction hook: the interpreter is about to run `pc`. */
void misses_note(Misses *m, uint32_t pc, uint8_t p, bool e);
/* Writes the list to `path` and a one-line summary to `summary`. Returns 0 on success. */
int misses_write(Misses *m, const char *path, FILE *summary);
void misses_destroy(Misses *m);

#endif
