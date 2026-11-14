/* The GBA's picture (targets/gba/video.c): made once a frame, by the driver
 * between the game's frames, from the SNES video memory and settings the
 * video service keeps. */
#ifndef CT_GBA_VIDEO_H
#define CT_GBA_VIDEO_H

#include <stddef.h>

#include "platform/platform.h"

void gba_video_present(Platform *pf);
/* The bytes the SNES's video memory and settings take. */
size_t gba_video_bytes(void);

#endif
