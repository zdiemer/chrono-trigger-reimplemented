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
bool resume_append(Resume *r, uint16_t first, uint16_t second);
uint8_t *resume_write(const Resume *r, const char *build, size_t *len);
/* Transactional: failed reads leave r untouched. */
bool resume_read(Resume *r, const uint8_t *data, size_t len, const char *build);
void resume_free(Resume *r);
#endif
