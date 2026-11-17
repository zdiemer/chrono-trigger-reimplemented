/* The GBA build driven from outside (drive_box.h): the driver's side. */
#ifndef CT_GBA_DRIVE_H
#define CT_GBA_DRIVE_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* Name the box in the debug log; true where a runner armed it on reading it. */
bool drive_open(void);
/* The next frame's pads, as a pad word (the first controller's low half, the
 * second's high): the runner's next, waited for between frames, answering
 * what the runner asks meanwhile. `report` writes the driver's report. */
uint32_t drive_next_pads(GameState *g, void (*report)(void *ctx), void *ctx);
/* Tell a runner which of the game's frames the picture put on the screen
 * is (counted from 1), so that it can say what each GBA frame shows
 * (host/census.py). */
void drive_showing(uint32_t frame);

#endif
