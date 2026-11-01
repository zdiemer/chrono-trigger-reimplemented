/* Recorded pictures, with two neighbouring frames on either side so a small
 * timing shift can be distinguished from a different picture. Frame numbers
 * count completed frames, as --locations does. No pixels leave this helper. */
#ifndef CT_OUTCOME_PICTURE_H
#define CT_OUTCOME_PICTURE_H
#include <stdint.h>
#include <stdio.h>

enum { OUTCOME_EVERY = 300, OUTCOME_NEAR = 2 };

static int outcome_picture_due(unsigned long frame, unsigned long end)
{
    unsigned long rem = frame % OUTCOME_EVERY;
    return frame <= 3 || rem <= OUTCOME_NEAR || rem >= OUTCOME_EVERY - OUTCOME_NEAR ||
           frame + OUTCOME_NEAR >= end;
}

static void outcome_picture(const uint32_t *pixels, unsigned width, unsigned height, unsigned long frame)
{
    /* FNV-1a over RGB bytes in display order, independent of host byte order. */
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < width * height; i++)
        for (int shift = 16; shift >= 0; shift -= 8) {
            hash ^= (pixels[i] >> shift) & 255;
            hash *= UINT64_C(1099511628211);
        }
    fprintf(stderr, "outcome-picture: %lu %016llx\n", frame, (unsigned long long)hash);
}
#endif
