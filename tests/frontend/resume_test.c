#include "resume.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    puts("resume_test: OK");
    return 0;
}
