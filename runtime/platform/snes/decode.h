/* VidFrame decoder: rebuilds the retained frame state from the PPU register
 * layer, line by line (docs/PLATFORM.md, P2).
 *
 * It works whoever wrote the registers -- ported code through the services,
 * generated blocks, the interpreter or HDMA -- so a renderer fed by it runs on
 * every movie, and the frame it builds is what the observer's command-built
 * frame is checked against.
 */
#ifndef CT_PLATFORM_SNES_DECODE_H
#define CT_PLATFORM_SNES_DECODE_H

#include "platform/video.h"
#include "snes/ppu.h"

typedef struct VidDecoder VidDecoder;

VidDecoder *vid_decoder_create(void);
void vid_decoder_destroy(VidDecoder *d);

/* The beam has reached `line` (0-224), with the registers as they stood while
 * the line was drawn (the on_line hook, before that line's HDMA writes). */
void vid_decode_line(VidDecoder *d, const Ppu *ppu, uint16_t line);

/* The frame, valid from line 224 until the next line 0. */
const VidFrame *vid_decoder_frame(const VidDecoder *d);

/* How often a mid-frame change of VRAM or CGRAM had no snapshot left, so spans
 * before it read memory as it ended up rather than as they saw it. 0 over every
 * movie; a nonzero count means the pools are too small. */
uint32_t vid_decoder_snapshots_lost(const VidDecoder *d);

/* The most snapshots of each one frame needed, for sizing the pools. */
void vid_decoder_snapshot_peak(const VidDecoder *d, int *vram, int *cgram, int *sprites);

/* One line's state on its own, for a renderer that doesn't keep spans. */
void vid_decode_state(const Ppu *ppu, VidLineState *st);

#endif
