/* The GBA's picture (targets/gba/services/video.c): made once a frame, by the driver
 * between the game's frames, from the SNES video memory and settings the
 * video service keeps. */
#ifndef CT_GBA_VIDEO_H
#define CT_GBA_VIDEO_H

#include <stddef.h>
#include <stdint.h>

#include "platform/platform.h"

/* The picture of the game's frame `frame` (counted from 1), made and put on
 * the screen, or from the next vertical blank where it was made too late
 * for this one. */
void gba_video_present(Platform *pf, uint32_t frame);
/* `shown` told the frame whose picture goes on the screen, as it goes on
 * (drive_showing). */
void gba_video_tell_shown(void (*shown)(uint32_t frame));
/* Waits for the vertical blank, where the driver makes the picture. */
void gba_video_await_blank(void);
/* The colours as the GBA shows them: a table of the 32,768 BGR555 colours'
 * own (the pack's, pack.py --profile), or NULL for the colours as they are. */
void gba_video_use_colours(const uint16_t *colours);
/* Where the game keeps the `len` bytes a send names from `addr` on, in one
 * run of memory, or NULL where it doesn't (state_host_span_in_place): such a
 * source is compared into VRAM where it lies rather than copied out first. */
void gba_video_read_in_place(const uint8_t *(*in_place)(void *ctx, uint32_t addr, uint32_t len));
/* The bytes the SNES's video memory and settings take. */
size_t gba_video_bytes(void);

#endif
