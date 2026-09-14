#include "media.h"

#include <stdlib.h>
#include <string.h>

#include "trace.h"

static void be32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8];
    be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);

    uint8_t *crc_buf = malloc(len + 4);
    memcpy(crc_buf, type, 4);
    if (len)
        memcpy(crc_buf + 4, data, len);
    uint8_t crc[4];
    be32(crc, ct_crc32(crc_buf, len + 4));
    free(crc_buf);
    fwrite(crc, 1, 4, f);
}

int ct_png_write(const char *path, const uint32_t *pixels, unsigned width, unsigned height) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;

    /* Raw scanlines: filter byte 0 then RGB. */
    size_t row = 1 + (size_t)width * 3;
    size_t raw_len = row * height;
    uint8_t *raw = malloc(raw_len);
    for (unsigned y = 0; y < height; y++) {
        uint8_t *p = raw + y * row;
        *p++ = 0;
        for (unsigned x = 0; x < width; x++) {
            uint32_t c = pixels[y * width + x];
            *p++ = (c >> 16) & 0xFF;
            *p++ = (c >> 8) & 0xFF;
            *p++ = c & 0xFF;
        }
    }

    /* zlib stream of stored deflate blocks. */
    size_t blocks = raw_len / 65535 + 1;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(z_len), *q = z;
    *q++ = 0x78;
    *q++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_len || off == 0;) {
        size_t n = raw_len - off > 65535 ? 65535 : raw_len - off;
        int last = off + n == raw_len;
        *q++ = last ? 1 : 0;
        *q++ = n & 0xFF;
        *q++ = n >> 8;
        *q++ = ~n & 0xFF;
        *q++ = (~n >> 8) & 0xFF;
        memcpy(q, raw + off, n);
        for (size_t i = 0; i < n; i++) {
            a = (a + raw[off + i]) % 65521;
            b = (b + a) % 65521;
        }
        q += n;
        off += n;
        if (last)
            break;
    }
    be32(q, (b << 16) | a);
    q += 4;

    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, width);
    be32(ihdr + 4, height);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* truecolor */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0; /* adaptive filtering */
    ihdr[12] = 0; /* no interlace */
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (uint32_t)(q - z));
    png_chunk(f, "IEND", NULL, 0);

    free(raw);
    free(z);
    return fclose(f) == 0 ? 0 : -1;
}

static void le32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = v >> 24;
}

static void wav_header(ct_wav *wav) {
    uint32_t data_len = wav->frames * 4;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    le32(h + 4, 36 + data_len);
    memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16);
    h[20] = 1; /* PCM */
    h[21] = 0;
    h[22] = 2; /* channels */
    h[23] = 0;
    le32(h + 24, wav->sample_rate);
    le32(h + 28, wav->sample_rate * 4);
    h[32] = 4; /* block align */
    h[33] = 0;
    h[34] = 16; /* bits per sample */
    h[35] = 0;
    memcpy(h + 36, "data", 4);
    le32(h + 40, data_len);
    fseek(wav->f, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, wav->f);
}

int ct_wav_open(ct_wav *wav, const char *path, uint32_t sample_rate) {
    memset(wav, 0, sizeof *wav);
    wav->f = fopen(path, "wb");
    if (!wav->f)
        return -1;
    wav->sample_rate = sample_rate;
    wav_header(wav);
    return 0;
}

void ct_wav_write(ct_wav *wav, const int16_t *stereo, size_t frames) {
    if (!wav->f)
        return;
    for (size_t i = 0; i < frames * 2; i++) {
        uint8_t s[2] = {(uint16_t)stereo[i] & 0xFF, (uint16_t)stereo[i] >> 8};
        fwrite(s, 1, 2, wav->f);
    }
    wav->frames += (uint32_t)frames;
}

int ct_wav_close(ct_wav *wav) {
    if (!wav->f)
        return 0;
    wav_header(wav);
    int rc = fclose(wav->f);
    wav->f = NULL;
    return rc == 0 ? 0 : -1;
}
