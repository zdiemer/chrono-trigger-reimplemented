/* Line protocol and replay checkpoints for a program whose stacks cannot be
 * serialized. The host supplies a deterministic power-on reset and one frame. */
#ifndef CT_ROUTE_DRIVER_H
#define CT_ROUTE_DRIVER_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    void *(*save)(void *ctx);
    void (*drop)(void *checkpoint);
    bool (*begin)(void *ctx, const void *checkpoint);
    bool (*end)(void *ctx, const void *checkpoint);
} RouteReplay;
typedef struct {
    void *ctx;
    bool (*reset)(void *ctx);
    bool (*step)(void *ctx, uint32_t pads);
    uint8_t (*read)(void *ctx, uint32_t address);
    void (*write)(void *ctx, uint32_t address, uint8_t value);
    bool (*shot)(void *ctx, const char *path);
    const RouteReplay *replay; /* optional host service checkpoints for faster stack reconstruction */
} RouteHost;

/* seed is the initial SRAM; identity distinguishes asset catalogues.
 * Files embed both and reject a different base before touching the program.
 * Returns nonzero on a failed frame or replay. Commands always answer a line. */
int route_driver(const RouteHost *host, const uint8_t *seed, size_t seed_size,
                 uint32_t identity, FILE *input, FILE *output);
#endif
