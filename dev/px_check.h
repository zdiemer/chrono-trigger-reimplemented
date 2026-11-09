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
 * The default works in any --native mode: the decoder reads the register
 * layer. --px-check-services instead draws the command-built frame and needs
 * --native on with hand ports. Only initial video memory is seeded from the
 * machine; no decoded PPU state reaches that renderer.
 */
#ifndef CT_DEV_PX_CHECK_H
#define CT_DEV_PX_CHECK_H

#include <stdio.h>

#include "snes/render.h"
#include "snes/system.h"

typedef struct PxCheck PxCheck;

/* `reference` is the framebuffer ct_dev draws with render_line. */
PxCheck *px_check_create(Snes *s, const Renderer *reference, bool services);
void px_check_frame_top(PxCheck *pc);
/* From the on_line hook, for lines 0-224, after render_line. */
void px_check_line(PxCheck *pc, uint16_t line);
int px_check_write(PxCheck *pc, const char *path, FILE *summary);
/* Frames compared and frames identical, for the caller's exit status. */
uint64_t px_check_frames(const PxCheck *pc);
uint64_t px_check_equal(const PxCheck *pc);
void px_check_destroy(PxCheck *pc);

#endif
