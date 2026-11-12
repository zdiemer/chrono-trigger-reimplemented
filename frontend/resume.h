/* Portable native checkpoints reconstruct C continuations from deterministic
 * input. No stack addresses, paths, devices or host loop live in this format. */
#ifndef CT_RESUME_H
#define CT_RESUME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { RESUME_SRAM = 8192, RESUME_SCENES = 4 };
typedef struct {
    uint8_t sram[RESUME_SRAM];
    uint8_t scenes, scene[RESUME_SCENES], song[RESUME_SCENES];
    uint32_t *input;
    uint32_t frames, capacity;
    uint32_t outcome;
} Resume;
/* A driver owns the loop: advance reconstructs at most one complete frame.
 * Optional service checkpoints accelerate replay without changing its inputs;
 * their end check and the state checksum must both agree before completion. */
typedef struct {
    void *ctx;
    bool (*reset)(void *ctx, const Resume *base);
    bool (*step)(void *ctx, uint32_t pads);
    uint32_t (*checksum)(void *ctx);
    bool (*begin)(void *ctx, const void *services);
    bool (*end)(void *ctx, const void *services);
} ResumeHost;
typedef struct {
    ResumeHost host;
    const Resume *target;
    const void *services;
    uint32_t frame;
    bool active, failed;
} ResumeReplay;
/* start may retain an already-live, verified input prefix when restart is false. */
bool resume_start(ResumeReplay *r, ResumeHost host, const Resume *target,
                  uint32_t start, bool restart, const void *services);
bool resume_advance(ResumeReplay *r);
bool resume_reserve(Resume *r, uint32_t frames);
bool resume_copy(Resume *to, const Resume *from);
bool resume_append(Resume *r, uint16_t first, uint16_t second);
uint8_t *resume_write(const Resume *r, const char *build, size_t *len);
/* Transactional: failed reads leave r untouched. */
bool resume_read(Resume *r, const uint8_t *data, size_t len, const char *build);
void resume_free(Resume *r);
#endif
