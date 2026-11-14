/* Video observer: folds every video service call into a VidFrame, so the
 * command-built frame can be compared with the one a backend decodes from its
 * own state (docs/PLATFORM.md, P2).
 *
 * The observer knows nothing about registers. Each call updates the retained
 * state semantically, uploads are applied to its own copies of video memory
 * (read from game memory through `read`), and per-line effects are expanded
 * from the game's tables the way a native backend would have to expand them.
 *
 * The host drives the frame: vid_obs_line() once per line 0-224, as the
 * hardware reaches it, which is where the state of the line just reached is
 * taken. Only meaningful with the ports running (ct_dev --native on): in shadow
 * mode they run on clones and would be seen twice.
 */
#ifndef CT_PLATFORM_OBSERVE_H
#define CT_PLATFORM_OBSERVE_H

#include "platform/video.h"

/* `read` fetches a byte of game memory without side effects (snes_bus_peek). */
VidObserver *vid_obs_create(uint8_t (*read)(void *ctx, uint32_t addr), void *ctx);
void vid_obs_destroy(VidObserver *ob);
/* The bytes the observer holds: itself, and the copies of memory it has
 * taken for the frames' lines so far (ct_native --measure). */
size_t vid_obs_bytes(const VidObserver *ob);
/* The observer as bytes for a snapshot: its memory, registers, the frame it
 * last built and the copies of memory that frame's lines read. Writes when
 * `room` is enough, and answers the bytes it needs either way. A restore
 * keeps the observer's own reader and buffers, and is false, changing
 * nothing, for bytes that aren't a snapshot. */
size_t vid_obs_snapshot(const VidObserver *ob, uint8_t *out, size_t room);
bool vid_obs_restore(VidObserver *ob, const uint8_t *in, size_t len);

/* A frame is about to start: the per-line effect slots reload their tables.
 * The host calls it at line 0, before vid_obs_line(). */
void vid_obs_frame_start(VidObserver *ob);
/* The consumer has finished with the frame. Subsequent uploads need not
 * preserve its spans; frame_start begins the next capture. */
void vid_obs_frame_end(VidObserver *ob);
/* The beam has reached `line` (0-224), before that line's per-line effects are
 * applied. The frame is complete after line 224. */
void vid_obs_line(VidObserver *ob, uint16_t line);

/* The frame the calls built, complete at line 224 and valid until frame_end
 * or the next frame_start. Uploads before the consumer finishes preserve it. */
const VidFrame *vid_obs_frame(const VidObserver *ob);

/* Peak retained versions per frame; storage is allocated as needed and reused.
 * There is room for a distinct version at every emitted span. */
void vid_obs_snapshot_peak(const VidObserver *ob, int *vram, int *cgram, int *sprites);

/* Start from a state the observer didn't build, so that a subsystem can be
 * checked from the frame it takes over: everything the calls before it left is
 * taken as given. Call it at a line 0, before vid_obs_line(). */
void vid_obs_seed(VidObserver *ob, const VidLineState *state, const uint8_t *vram, const uint16_t *cgram,
                  const uint8_t *oam);
/* The video memory alone, for a comparison that starts at power-on: every
 * register is the game's to set, but what VRAM, CGRAM and OAM hold before it
 * does is the machine's and no call produces it. */
void vid_obs_seed_memory(VidObserver *ob, const uint8_t *vram, const uint16_t *cgram, const uint8_t *oam);

/* The first difference between the decoded frame `a` and the command-built
 * frame `b`: `kind` names the part that differs (a static string, for counting
 * them), `text` describes it. Returns false when the frames agree.
 *
 * Lines are compared by the state each line ends up with rather than by span
 * boundaries: an upload that rewrites a palette with the same colors starts a
 * span on one side only, which changes nothing a renderer draws. */
typedef struct {
    const char *kind;
    char text[192];
} VidDiff;
bool vid_frame_diff(const VidFrame *a, const VidFrame *b, VidDiff *out);

#endif
