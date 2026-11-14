/* The GBA build's text out: mGBA's debug log (targets/gba/system.c), which
 * stdout and stderr write to a line at a time. */
#ifndef CT_GBA_DEBUG_LOG_H
#define CT_GBA_DEBUG_LOG_H

/* Ask mGBA for its debug log; on hardware without it the text goes nowhere. */
void debug_log_open(void);

#endif
