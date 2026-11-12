/* The route protocol (docs/MOVIES.md): a line of commands in, a line of
 * answer out, over a program the host runs a frame at a time. Its slots and
 * state files are the host's complete frame-end snapshots, so a load costs
 * the snapshot's size, not the frames that led to it. */
#ifndef CT_ROUTE_DRIVER_H
#define CT_ROUTE_DRIVER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    void *ctx;
    bool (*step)(void *ctx, uint32_t pads);
    uint8_t (*read)(void *ctx, uint32_t address);
    void (*write)(void *ctx, uint32_t address, uint8_t value);
    bool (*shot)(void *ctx, const char *path);
    /* The program as it stands at the end of the last frame, as bytes the
     * host reads back (malloc'd; NULL where it can't take one), and the
     * program put back as such bytes had it, false changing nothing where
     * they aren't one this run can resume (port/arena.h). */
    uint8_t *(*snapshot)(void *ctx, size_t *len);
    bool (*restore)(void *ctx, const uint8_t *bytes, size_t len);
} RouteHost;

/* seed is the initial SRAM; identity distinguishes asset catalogues.
 * Files embed both and reject a different base before touching the program.
 * Returns nonzero on a failed frame. Commands always answer a line. */
int route_driver(const RouteHost *host, const uint8_t *seed, size_t seed_size,
                 uint32_t identity, FILE *input, FILE *output);
#endif
