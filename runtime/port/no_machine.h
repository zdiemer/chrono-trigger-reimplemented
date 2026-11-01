/* A fatal machine entry can report the driver's current frame before exit. */
#ifndef CT_PORT_NO_MACHINE_H
#define CT_PORT_NO_MACHINE_H

void no_machine_report_with(void (*report)(void *ctx, const char *routine), void *ctx);

#endif
