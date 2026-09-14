/* Dependency-free PNG and WAV writers for oracle captures. */
#ifndef CT_MEDIA_H
#define CT_MEDIA_H

#include <stdint.h>
#include <stdio.h>

/* Writes XRGB8888 pixels as an uncompressed RGB PNG. Returns 0 on success. */
int ct_png_write(const char *path, const uint32_t *pixels, unsigned width, unsigned height);

typedef struct {
    FILE *f;
    uint32_t sample_rate;
    uint32_t frames;
} ct_wav;

int ct_wav_open(ct_wav *wav, const char *path, uint32_t sample_rate);
void ct_wav_write(ct_wav *wav, const int16_t *stereo, size_t frames);
int ct_wav_close(ct_wav *wav);

#endif
