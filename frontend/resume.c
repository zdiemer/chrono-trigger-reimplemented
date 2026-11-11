#include "resume.h"
#include "trace.h"
#include <stdlib.h>
#include <string.h>

enum { HEADER = 68, PAYLOAD = HEADER + RESUME_SRAM };
static uint32_t read32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void write32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (i * 8));
}
bool resume_append(Resume *r, uint16_t first, uint16_t second)
{
    if (r->frames == UINT32_MAX) return false;
    if (r->frames == r->capacity) {
        uint32_t cap = r->capacity ? r->capacity : 1024;
        if (cap > UINT32_MAX / 2 || SIZE_MAX / (cap * 2) < sizeof *r->input) return false;
        cap *= 2;
        uint32_t *input = realloc(r->input, (size_t)cap * sizeof *input);
        if (!input) return false;
        r->input = input;
        r->capacity = cap;
    }
    r->input[r->frames++] = first | (uint32_t)second << 16;
    return true;
}
uint8_t *resume_write(const Resume *r, const char *build, size_t *len)
{
    if (r->scenes > RESUME_SCENES || strlen(build) != 32 ||
        (r->frames && (SIZE_MAX - PAYLOAD) / r->frames < 4)) return NULL;
    *len = PAYLOAD + (size_t)r->frames * 4;
    uint8_t *out = calloc(1, *len);
    if (!out) return NULL;
    memcpy(out, "CTNAT001", 8);
    memcpy(out + 8, build, 32);
    write32(out + 40, r->frames);
    write32(out + 44, r->outcome);
    out[48] = r->scenes;
    memcpy(out + 49, r->scene, RESUME_SCENES);
    memcpy(out + 53, r->song, RESUME_SCENES);
    memcpy(out + HEADER, r->sram, RESUME_SRAM);
    for (uint32_t i = 0; i < r->frames; i++) write32(out + PAYLOAD + (size_t)i * 4, r->input[i]);
    write32(out + 60, ct_crc32(out, 60));
    write32(out + 64, ct_crc32(out + HEADER, *len - HEADER));
    return out;
}
bool resume_read(Resume *r, const uint8_t *data, size_t len, const char *build)
{
    if (len < PAYLOAD || strlen(build) != 32 || memcmp(data, "CTNAT001", 8) ||
        memcmp(data + 8, build, 32) || data[48] > RESUME_SCENES ||
        read32(data + 60) != ct_crc32(data, 60)) return false;
    uint32_t frames = read32(data + 40);
    if ((len - PAYLOAD) / 4 != frames || (len - PAYLOAD) % 4 ||
        read32(data + 64) != ct_crc32(data + HEADER, len - HEADER)) return false;
    Resume loaded = { .frames = frames, .capacity = frames, .outcome = read32(data + 44), .scenes = data[48] };
    loaded.input = malloc(frames ? (size_t)frames * 4 : 1);
    if (!loaded.input) return false;
    memcpy(loaded.sram, data + HEADER, RESUME_SRAM);
    memcpy(loaded.scene, data + 49, RESUME_SCENES);
    memcpy(loaded.song, data + 53, RESUME_SCENES);
    for (uint32_t i = 0; i < frames; i++) loaded.input[i] = read32(data + PAYLOAD + (size_t)i * 4);
    resume_free(r);
    *r = loaded;
    return true;
}
void resume_free(Resume *r)
{
    free(r->input);
    memset(r, 0, sizeof *r);
}
