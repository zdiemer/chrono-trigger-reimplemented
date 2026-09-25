/* The original's routine as if it were a hand port (ct_dev --as-port
 * BBAAAA[,BBAAAA...]): what the shipping mode would do with a port
 * of it, before anyone writes the port (docs/PORTING.md, "A port that spans
 * frames").
 *
 * Each address gets a registry entry whose function interprets the original
 * routine on the machine with time stopped -- the CPU stepped directly, no
 * interrupt polled, no scanline passing -- until it returns to its caller, and
 * owes the time its bus accesses and internal cycles took (cost_milli 1000),
 * the way a hand port finishes instantly and pays afterwards. So a routine
 * whose work spans frames, with interrupts landing inside it in the original,
 * runs as one port would: all of it at once, the interrupts taken while its
 * time is paid.
 *
 * A routine that waits for something time brings (a frame, an interrupt's
 * flag, the APU) can't finish with time stopped: after AS_PORT_STEP_LIMIT
 * instructions the entry gives up and stops where it is (a port may stop
 * anywhere), with a note on stderr. Hand ports and generated blocks inside
 * the routine don't run: the interpreter runs all of it. */
#ifndef CT_DEV_AS_PORT_H
#define CT_DEV_AS_PORT_H

#include "natives.h"

enum { AS_PORT_STEP_LIMIT = 2000000 };

/* Adds an entry to t for each address in `spec` ("C2E613" or
 * "C2E613,C2E7C3", '$' and ':' allowed). Returns 0, or -1 with a message on
 * stderr. */
int as_port_add(NativeTable *t, const char *spec);

#endif
