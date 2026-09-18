/* ct_dev --io-log: every access to the hardware registers ($2100-$217F,
 * $4016-$4017, $4200-$421F, $4300-$437F) attributed to what made it: a hand
 * port and the platform service call it went through (or none), an idle entry,
 * a generated block, the interpreter's instruction, HDMA, and the bytes of a
 * DMA transfer (attributed to whoever started the transfer). Counted by origin
 * and register, and by when in the frame each access landed: a vblank line,
 * a picture line in forced blank, or a picture line with the display on (the
 * PPU's own view; docs/RECOMP.md, "Where video writes land", which also says
 * how tools/vblank_check.py compares two runs' logs). See docs/PLATFORM.md. */
#ifndef CT_DEV_IO_LOG_H
#define CT_DEV_IO_LOG_H

#include <stdio.h>

#include "snes/system.h"

typedef struct IoLog IoLog;

/* Starts observing the machine's bus. */
IoLog *io_log_create(Snes *s);
/* From the CPU's instruction hook: the interpreter is about to run `pc`. */
void io_log_note_instruction(IoLog *log, uint32_t pc);
/* From the system's line hook: that line's HDMA writes follow. */
void io_log_note_hdma(IoLog *log);
/* Writes the table to `path` and a summary to `summary`. Returns 0 on success. */
int io_log_write(IoLog *log, const char *path, FILE *summary);
void io_log_destroy(IoLog *log);

#endif
