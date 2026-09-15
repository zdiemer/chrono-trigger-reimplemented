/* The miss report: every instruction the interpreter ran where generated code
 * was expected, counted by address and widths, and classified as ROM or RAM
 * code, and RAM code by the RAM section ([[ramcode]] hints) it lies in.
 * `ct_dev --misses` writes it (with --native on --recomp only, the miss report
 * of docs/RECOMP.md, M3, and docs/MATCHING.md, C7: what generated blocks didn't
 * run), and so does a player built without the interpreter, to
 * <rom name>.misses.txt (docs/PLAYING.md). The file is also a coverage file
 * (tools/disasm reads `BB:AAAA m x` and ignores what follows `#`), which is how
 * misses are fed back (CT_RECOMP_COVERAGE). */
#ifndef CT_DEV_MISSES_H
#define CT_DEV_MISSES_H

#include <stdio.h>

#include "snes/system.h"

typedef struct Misses Misses;

Misses *misses_create(const Snes *s);
/* The interpreter is about to run `pc` (from the CPU's instruction hook, or a
 * system's on_miss). */
void misses_note(Misses *m, uint32_t pc, uint8_t p, bool e);
/* Adds the counts of a report written earlier; a missing file adds nothing.
 * Returns the number of entries read, or -1 if the file can't be read. */
int misses_load(Misses *m, const char *path);
/* Distinct entries (address, widths, emulation mode); `instructions`, if not
 * NULL, gets the instructions counted. */
size_t misses_count(const Misses *m, uint64_t *instructions);
/* The instruction the CPU of `s` is at, in words: address, widths, and ROM or
 * RAM code with its section, for example "7E:B574 m8 x16, RAM in no section". */
void misses_describe(const Snes *s, char *out, size_t n);
/* Writes the list to `path` and, unless `summary` is NULL, a summary to
 * `summary`. Returns 0 on success. */
int misses_write(Misses *m, const char *path, FILE *summary);
void misses_destroy(Misses *m);

#endif
