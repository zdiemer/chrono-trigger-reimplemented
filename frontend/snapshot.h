/* Snapshots of the native program at a frame's end (docs/PLAYING.md,
 * "Snapshots"): everything the next frame goes on from, so that putting one
 * back and playing on is the same as never having left it.
 *
 * A snapshot holds the game's state (runtime/port/state_host.h, the menus'
 * coroutines and the program's handlers with it), the services' state (the
 * timing service's waits and debt and its generator, the pads, the video
 * observer's memory and frame), the C sound driver and its mixer, every
 * suspended fiber -- the main thread's wait and the menus' tasks, deep in
 * their calls -- as its stack's bytes (runtime/port/fiber.h), and bytes the
 * driver keeps with them. The stacks hold the run's own pointers, so a
 * snapshot is good in the run that took it and in any run laid out the same
 * (runtime/port/arena.h): it records the addresses it depends on, and a
 * restore refuses one laid out otherwise, changing nothing.
 *
 * Bytes, not files: a driver names and stores them (snapshot_file wraps them
 * with a checksum). */
#ifndef CT_FRONTEND_SNAPSHOT_H
#define CT_FRONTEND_SNAPSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/native/host.h"
#include "sound_native.h"
#include "state.h"

typedef struct {
    GameState *game;
    Platform *pf;
    SoundNative *sound;
    const void *pack;          /* the asset pack's bytes, which the game's stacks can point into */
} SnapshotParts;

/* malloc'd, NULL where memory ran out or a fiber is running. `build` names
 * the build (32 characters) a snapshot is good for. */
uint8_t *snapshot_take(const SnapshotParts *p, const char *build, const void *driver, size_t driver_bytes, size_t *len);
/* False for bytes that aren't a snapshot of `build` laid out as this run is,
 * or whose driver bytes aren't `driver_bytes`; it changes nothing then, short
 * of running out of memory midway. */
bool snapshot_restore(const SnapshotParts *p, const char *build, const uint8_t *in, size_t len, void *driver,
                      size_t driver_bytes);

/* A snapshot as a file's bytes, checksummed; and the snapshot inside such
 * bytes, or NULL where they are damaged or aren't one. */
uint8_t *snapshot_file(const uint8_t *snapshot, size_t len, size_t *file_len);
const uint8_t *snapshot_in_file(const uint8_t *file, size_t len, size_t *snapshot_len);

#endif
