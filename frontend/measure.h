/* What a run of the game holds and does, for comparing with a small target's
 * budgets (ct_native --measure; tools/measure.py, docs/PLATFORM.md "Memory,
 * stacks and per-frame work"):
 *
 * - the lasting allocations: the GameState, the arena, the renderer, the
 *   sound player and the picture's observer;
 * - each fiber stack's deepest use (runtime/port/fiber.h);
 * - per mode (the vblank handler the running mode installed: the field, a
 *   battle, the menus, a scene, the time warp), the frames it ran, its
 *   heaviest and 99th-percentile frame in host nanoseconds, split into the
 *   game's frame (its threads and handlers), the sound player and the
 *   renderer; and the cartridge resources one stay in the mode read, in
 *   bytes as the catalogue keeps them.
 *
 * Measuring changes nothing the game does: the resources are served by the
 * same store through a counting wrapper, its cache started afresh where the
 * mode changes so each stay's reads are seen (data/assets.h, assets_use). */
#ifndef CT_FRONTEND_MEASURE_H
#define CT_FRONTEND_MEASURE_H

#include <stdint.h>
#include <stdio.h>

#include "data/assets.h"
#include "platform/platform.h"
#include "state.h"

typedef struct Measure Measure;

/* A measure over the store the game reads from; *out is the store to give
 * the game (assets_use) in its place. */
Measure *measure_create(const AssetStore *inner, AssetStore *out);
void measure_destroy(Measure *m);

/* The host's monotonic clock, in nanoseconds. */
uint64_t measure_now_ns(void);

/* A frame ran: the time the game's frame, the sound player and the renderer
 * took. Call between frames. */
void measure_frame(Measure *m, GameState *g, uint64_t game_ns, uint64_t sound_ns, uint64_t render_ns);

/* The report, as `measure:` lines (tools/measure.py reads them). */
void measure_report(const Measure *m, FILE *out, const Platform *pf);

#endif
