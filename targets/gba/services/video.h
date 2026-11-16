/* The GBA's picture (targets/gba/services/video.c): made once a frame, by the driver
 * between the game's frames, from the SNES video memory and settings the
 * video service keeps. */
#ifndef CT_GBA_VIDEO_H
#define CT_GBA_VIDEO_H

#include <stddef.h>
#include <stdint.h>

#include "platform/platform.h"

void gba_video_present(Platform *pf);
/* Waits for the vertical blank, where the driver makes the picture. */
void gba_video_await_blank(void);
/* The colours as the GBA shows them: a table of the 32,768 BGR555 colours'
 * own (the pack's, pack.py --profile), or NULL for the colours as they are. */
void gba_video_use_colours(const uint16_t *colours);
/* The bytes the SNES's video memory and settings take. */
size_t gba_video_bytes(void);

#endif
