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
bool resume_reserve(Resume *r, uint32_t frames)
{
    if (frames <= r->capacity) return true;
    uint32_t cap = r->capacity < UINT32_MAX / 2 ? r->capacity * 2 : UINT32_MAX;
    if (cap < 1024) cap = 1024;
    if (cap < frames) cap = frames;
    if (SIZE_MAX / cap < sizeof *r->input) return false;
    uint32_t *input = realloc(r->input, (size_t)cap * sizeof *input);
    if (!input) return false;
    r->input = input;
    r->capacity = cap;
    return true;
}
bool resume_copy(Resume *to, const Resume *from)
{
    if (!resume_reserve(to, from->frames)) return false;
    uint32_t *input = to->input, capacity = to->capacity;
    if (from->frames) memcpy(input, from->input, (size_t)from->frames * sizeof *input);
    *to = *from;
    to->input = input;
    to->capacity = capacity;
    return true;
}
bool resume_append(Resume *r, uint16_t first, uint16_t second)
{
    if (r->frames == UINT32_MAX || !resume_reserve(r, r->frames + 1)) return false;
    r->input[r->frames++] = first | (uint32_t)second << 16;
    return true;
}
static bool finish(ResumeReplay *r)
{
    if (r->services && !r->host.end(r->host.ctx, r->services)) r->failed = true;
    if (!r->failed && r->host.checksum(r->host.ctx) != r->target->outcome) r->failed = true;
    r->active = false;
    return !r->failed;
}
bool resume_start(ResumeReplay *r, ResumeHost host, const Resume *target,
                  uint32_t start, bool restart, const void *services)
{
    *r = (ResumeReplay){ .host = host, .target = target, .services = services, .frame = start };
    if (start > target->frames || (restart && start) ||
        (services && (!restart || !host.begin || !host.end)) ||
        (restart && !host.reset(host.ctx, target)) ||
        (services && !host.begin(host.ctx, services))) {
        r->failed = true;
        return false;
    }
    r->active = start < target->frames;
    return r->active || finish(r);
}
bool resume_advance(ResumeReplay *r)
{
    if (!r->active) return !r->failed;
    if (!r->host.step(r->host.ctx, r->target->input[r->frame])) {
        r->failed = true;
        r->active = false;
        return false;
    }
    r->frame++;
    return r->frame < r->target->frames || finish(r);
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
