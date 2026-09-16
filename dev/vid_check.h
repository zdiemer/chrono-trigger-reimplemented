/* ct_dev --vid-check: compares the VidFrame the video service calls build
 * (platform/observe.h) with the one decoded from the PPU registers
 * (platform/snes/decode.h), frame by frame.
 *
 * That is P2's acceptance check (docs/PLATFORM.md): a subsystem is behind the
 * API when, over the movies that cover it, the two frames agree. Differences
 * name what a renderer fed by commands alone would get wrong, which is the list
 * of register writers still to port. Needs --native on: in shadow mode the
 * ported routines run on clones.
 */
#ifndef CT_DEV_VID_CHECK_H
#define CT_DEV_VID_CHECK_H

#include <stdio.h>

#include "snes/system.h"

typedef struct VidCheck VidCheck;

VidCheck *vid_check_create(Snes *s);
/* From the on_line hook, for lines 0-224. */
void vid_check_line(VidCheck *vc, uint16_t line);
int vid_check_write(VidCheck *vc, const char *path, FILE *summary);
void vid_check_destroy(VidCheck *vc);

#endif
