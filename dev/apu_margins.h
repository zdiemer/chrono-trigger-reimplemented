/* ct_dev --apu-port-margins: how long after the SPC700 reads a message's last
 * two bytes the main CPU's next ones reach the ports.
 *
 * The driver on the SPC700 echoes a message's count on port 0 and reads its
 * last two bytes (ports 2-3) after it has (core/sound_driver.c), so the CPU
 * half's next message must not reach those ports before it has. In the
 * shipping mode the port waits for the echo where it stands (platform/snes/
 * audio.h) and then sends the next message in its own time, not the
 * original's, so what stands between it and a byte lost is this margin. Once
 * the driver runs, at every echo: the SPC700 cycles from it to the SPC700's
 * first read of port 2 and of port 3, and to the CPU's first write of each.
 * Where the CPU wrote one after the SPC700 read it, the write's lead over the
 * read is the margin (the least of them over the run, and their histogram);
 * where before, it read the next message's byte (counted: the original's
 * writes do that too, where a message carries nothing on the port, as the
 * end of a transfer and a table's words, on ports 1-2, don't). The times are
 * the APU's at each access, so within an SPC700 instruction of either side.
 * Development only (runtime/apu/apu.h, apu_port_read_observer and
 * apu_port_write_observer). */
#ifndef CT_DEV_APU_MARGINS_H
#define CT_DEV_APU_MARGINS_H

#include <stdio.h>

#include "snes/system.h"

void apu_margins_attach(Snes *s);
void apu_margins_report(FILE *out);

#endif
