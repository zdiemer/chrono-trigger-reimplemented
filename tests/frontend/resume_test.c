#include "resume.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t value, frames; unsigned resets, begins, ends; bool fail_frame, fail_end; } Program;
static bool reset(void *ctx, const Resume *base)
{
    Program *p = ctx;
    p->value = base->sram[0]; p->frames = 0; p->resets++;
    return true;
}
static bool step(void *ctx, uint32_t pads)
{
    Program *p = ctx;
    if (p->fail_frame) return false;
    p->value = p->value * 3 + pads; p->frames++;
    return true;
}
static uint32_t checksum(void *ctx) { return ((Program *)ctx)->value; }
static bool begin(void *ctx, const void *extra) { (void)extra; ((Program *)ctx)->begins++; return true; }
static bool end(void *ctx, const void *extra)
{
    Program *p = ctx; p->ends++;
    return !p->fail_end && p->value == *(const uint32_t *)extra;
}
static int replay_test(void)
{
    Resume target = { .sram = {7} }, copy = {0};
    Program p = {0};
    ResumeHost host = { &p, reset, step, checksum, begin, end };
    reset(&p, &target);
    for (unsigned i = 0; i < 12; i++) {
        if (!resume_append(&target, (uint16_t)i, (uint16_t)(i + 1))) return 1;
        step(&p, target.input[i]);
    }
    target.outcome = p.value;
    if (!resume_copy(&copy, &target)) return 1;
    ResumeReplay replay;
    if (!resume_start(&replay, host, &copy, 0, true, &target.outcome) || p.frames) return 1;
    for (unsigned i = 0; i < 12; i++) {
        if (!replay.active || !resume_advance(&replay) || p.frames != i + 1) return 1;
    }
    if (replay.active || p.begins != 1 || p.ends != 1 || p.value != target.outcome) return 1;
    unsigned resets = p.resets;
    if (!resume_start(&replay, host, &copy, 12, false, NULL) || replay.active || p.resets != resets) return 1;
    p.fail_end = true;
    if (!resume_start(&replay, host, &copy, 0, true, &target.outcome)) return 1;
    while (replay.active) (void)resume_advance(&replay);
    if (!replay.failed) return 1;
    p.fail_end = false; copy.outcome ^= 1;
    if (!resume_start(&replay, host, &copy, 0, true, NULL)) return 1;
    while (replay.active) (void)resume_advance(&replay);
    if (!replay.failed) return 1;
    p.fail_frame = true;
    if (!resume_start(&replay, host, &copy, 0, true, NULL) || resume_advance(&replay) || replay.active) return 1;
    copy.frames = 0; copy.outcome = 7;
    if (!resume_start(&replay, host, &copy, 0, true, NULL) || replay.active) return 1;
    resume_free(&target); resume_free(&copy);
    return 0;
}

int main(void)
{
    const char *build = "0123456789abcdef0123456789abcdef";
    Resume a = { .scenes = 1, .scene = { 0x24 }, .outcome = 123 }, b = { 0 };
    memset(a.sram, 0x35, sizeof a.sram);
    for (unsigned i = 0; i < 3000; i++)
        if (!resume_append(&a, (uint16_t)i, (uint16_t)(i * 7))) return 1;
    size_t len;
    uint8_t *bytes = resume_write(&a, build, &len);
    if (!bytes || !resume_read(&b, bytes, len, build) || b.frames != a.frames || b.outcome != a.outcome ||
        memcmp(a.sram, b.sram, sizeof a.sram) || memcmp(a.input, b.input, a.frames * sizeof *a.input)) return 1;
    if (resume_read(&b, bytes, len - 1, build) ||
        resume_read(&b, bytes, len, "ffffffffffffffffffffffffffffffff")) return 1;
    for (size_t i = 0; i < len; i += 7) {
        bytes[i] ^= 1;
        if (resume_read(&b, bytes, len, build)) return 1;
        bytes[i] ^= 1;
    }
    if (b.frames != a.frames || b.outcome != a.outcome) return 1;
    free(bytes);
    resume_free(&a);
    resume_free(&b);
    bytes = resume_write(&a, build, &len);
    if (!bytes || !resume_read(&b, bytes, len, build) || b.frames) return 1;
    free(bytes);
    resume_free(&b);
    if (replay_test()) return 1;
    puts("resume_test: OK");
    return 0;
}
