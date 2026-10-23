/* The census of how the game is entered (docs/PLAN.md, "The native program,
 * and the registry taken down", step 1; tools/census.py reads what this
 * writes). In the shipping mode (`--native on --generated on`) a port runs
 * either because the machine reached its address and the runtime found it in
 * game/registry.c, or because another port called its work as C. The first is
 * the registry's reason to exist; the census counts both, per registered
 * entry, and names who still makes the machine do it.
 *
 * An entry the machine enters is filed under how it got there, from what
 * happened since the last entry ran:
 *
 *   vector     the machine took an interrupt or reset and its vector names the
 *              entry (the cartridge's vectors, read once at the start);
 *   generated  the latest instruction was the original's, run as generated code;
 *   interp     the latest instruction was the original's, interpreted;
 *   call       a port called out to it through the machine (native_call and
 *              its kin, a bridge's call-out);
 *   stop       a port stopped at it (m_stop_at, m_jump_to: the port's routine
 *              went on there in the original, by a jump or by falling through);
 *   yield      a port yielded to another task, whose switch it is;
 *   return     a port returned, or returned from an interrupt, to it: it is
 *              where the original's code comes back to after a stretch that is
 *              no longer one on the machine;
 *   wait       an idle entry: a pass of a wait loop the machine still makes,
 *              counted apart from the rest whoever reached it, since what
 *              takes it off the machine is the timing service's wait, not a
 *              call (docs/PLATFORM.md, "Timing without a beam").
 *
 * The caller is the port (`call`, `stop`, `yield`: the one that made the
 * hand-off; `return`: the latest to run, which is the callee rather than the
 * caller where a port's callee returned into its caller's frame), or the
 * instruction's address (`generated`, `interp`), or the vector's.
 *
 * One census per process: the runtime's census hooks and the generated
 * code's instruction hook are global. It takes the runtime's observer, so it
 * doesn't run beside --fallbacks. */
#ifndef CT_DEV_CENSUS_H
#define CT_DEV_CENSUS_H

#include <stdio.h>

#include "port/native.h"
#include "snes/system.h"

typedef struct Census Census;

/* Counts on `s` under `rt` (ON mode): installs the census hooks, the runtime's
 * observer and the generated code's instruction hook. The host feeds the
 * interpreter's instructions with census_note. */
Census *census_create(Snes *s, NativeRuntime *rt);
void census_note(Census *c, uint32_t pc);
/* Writes the counts (tools/census.py's format). Returns 0 on success. */
int census_write(const Census *c, const char *path);
/* One line for the run's log: entries entered by the machine, and how often. */
void census_summary(const Census *c, FILE *out);
void census_destroy(Census *c);

#endif
