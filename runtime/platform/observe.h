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

/* The beam has reached `line` (0-224), before that line's per-line effects are
 * applied. Line 0 starts a frame; the frame is complete after line 224. */
void vid_obs_line(VidObserver *ob, uint16_t line);

/* The frame the calls built, valid from line 224 until the next line 0. */
const VidFrame *vid_obs_frame(const VidObserver *ob);

/* How many calls the observer could not model (vid_snes_transfer). */
uint32_t vid_obs_unmodelled(const VidObserver *ob);

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
