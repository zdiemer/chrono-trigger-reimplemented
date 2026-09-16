/* ct_dev --px-check: compares the native renderer's frame
 * (platform/native/render.h), drawn from the VidFrame the decoder builds, with
 * the scanline renderer's (snes/render.h), drawn from the PPU registers line by
 * line.
 *
 * That is P3's acceptance check (docs/PLATFORM.md): the native renderer is done
 * when every frame of every movie is identical at 256x224. Differences are
 * counted by kind, with the state of the first differing line and whether video
 * memory changed while the frame was being drawn, which is what tells a
 * renderer bug from a gap in what the VidFrame carries.
 *
 * It works in any --native mode: the decoder reads the register layer, so it
 * doesn't care who wrote it.
 */
#ifndef CT_DEV_PX_CHECK_H
#define CT_DEV_PX_CHECK_H

#include <stdio.h>

#include "snes/render.h"
#include "snes/system.h"

typedef struct PxCheck PxCheck;

/* `reference` is the framebuffer ct_dev draws with render_line. */
PxCheck *px_check_create(Snes *s, const Renderer *reference);
/* From the on_line hook, for lines 0-224, after render_line. */
void px_check_line(PxCheck *pc, uint16_t line);
int px_check_write(PxCheck *pc, const char *path, FILE *summary);
/* Frames compared and frames identical, for the caller's exit status. */
uint64_t px_check_frames(const PxCheck *pc);
uint64_t px_check_equal(const PxCheck *pc);
void px_check_destroy(PxCheck *pc);

#endif
