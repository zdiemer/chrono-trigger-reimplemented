#include "trace.h"

#include <stdlib.h>
#include <string.h>

/* Runs separated by fewer bytes than a run header are merged. */
#define RUN_MERGE_GAP 8

struct ct_trace_writer {
    FILE *f;
    int region_count;
    ct_region_desc *regions;
    uint8_t **prev;
    int wrote_keyframe;
    int error;
    /* Scratch buffer for one frame record. */
    uint8_t *buf;
    size_t len, cap;
};

uint64_t ct_fnv1a64(uint64_t h, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

uint32_t ct_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

uint64_t ct_framebuffer_hash(const uint32_t *pixels, uint16_t width, uint16_t height) {
    uint8_t px[4] = {width & 0xFF, width >> 8, height & 0xFF, height >> 8};
    uint64_t h = ct_fnv1a64(CT_FNV1A64_INIT, px, 4);
    for (size_t i = 0, n = (size_t)width * height; i < n; i++) {
        uint32_t p = pixels[i] & 0x00FFFFFFu;
        px[0] = p & 0xFF;
        px[1] = (p >> 8) & 0xFF;
        px[2] = (p >> 16) & 0xFF;
        px[3] = 0;
        h = ct_fnv1a64(h, px, 4);
    }
    return h;
}

static void reserve(ct_trace_writer *w, size_t extra) {
    if (w->len + extra <= w->cap)
        return;
    size_t cap = w->cap ? w->cap : 1 << 16;
    while (cap < w->len + extra)
        cap *= 2;
    uint8_t *buf = realloc(w->buf, cap);
    if (!buf) {
        fprintf(stderr, "trace: out of memory\n");
        exit(1);
    }
    w->buf = buf;
    w->cap = cap;
}

static void put_bytes(ct_trace_writer *w, const void *data, size_t len) {
    reserve(w, len);
    memcpy(w->buf + w->len, data, len);
    w->len += len;
}

static void put_u8(ct_trace_writer *w, uint8_t v) { put_bytes(w, &v, 1); }

static void put_u16(ct_trace_writer *w, uint16_t v) {
    uint8_t b[2] = {v & 0xFF, v >> 8};
    put_bytes(w, b, 2);
}

static void put_u32(ct_trace_writer *w, uint32_t v) {
    uint8_t b[4] = {v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, v >> 24};
    put_bytes(w, b, 4);
}

static void put_u64(ct_trace_writer *w, uint64_t v) {
    put_u32(w, (uint32_t)v);
    put_u32(w, (uint32_t)(v >> 32));
}

static void patch_u32(ct_trace_writer *w, size_t at, uint32_t v) {
    uint8_t b[4] = {v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, v >> 24};
    memcpy(w->buf + at, b, 4);
}

static void flush(ct_trace_writer *w) {
    if (w->len && fwrite(w->buf, 1, w->len, w->f) != w->len)
        w->error = 1;
    w->len = 0;
}

ct_trace_writer *ct_trace_open(const char *path, uint32_t rom_crc32, const char *producer,
                               const ct_region_desc *regions, int region_count) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return NULL;
    ct_trace_writer *w = calloc(1, sizeof *w);
    w->f = f;
    w->region_count = region_count;
    w->regions = calloc(region_count, sizeof *w->regions);
    w->prev = calloc(region_count, sizeof *w->prev);
    for (int i = 0; i < region_count; i++) {
        w->regions[i] = regions[i];
        w->prev[i] = calloc(1, regions[i].size ? regions[i].size : 1);
    }

    uint16_t producer_len = (uint16_t)strlen(producer);
    put_bytes(w, "CTTR", 4);
    put_u16(w, CT_TRACE_VERSION);
    put_u16(w, (uint16_t)(16 + 16 * region_count + producer_len));
    put_u32(w, rom_crc32);
    put_u16(w, (uint16_t)region_count);
    put_u16(w, producer_len);
    for (int i = 0; i < region_count; i++) {
        char name[8] = {0};
        size_t name_len = strlen(regions[i].name);
        memcpy(name, regions[i].name, name_len < sizeof name ? name_len : sizeof name);
        put_u8(w, regions[i].id);
        put_u8(w, 0);
        put_u16(w, 0);
        put_u32(w, regions[i].size);
        put_bytes(w, name, sizeof name);
    }
    put_bytes(w, producer, producer_len);
    flush(w);
    return w;
}

static void put_region(ct_trace_writer *w, int r, const uint8_t *cur, int keyframe) {
    uint32_t size = w->regions[r].size;
    uint8_t *prev = w->prev[r];

    put_u64(w, ct_fnv1a64(CT_FNV1A64_INIT, cur, size));
    size_t count_at = w->len;
    put_u32(w, 0);

    uint32_t runs = 0;
    if (keyframe) {
        if (size) {
            put_u32(w, 0);
            put_u32(w, size);
            put_bytes(w, cur, size);
            runs = 1;
        }
    } else {
        uint32_t i = 0;
        while (i < size) {
            if (cur[i] == prev[i]) {
                i++;
                continue;
            }
            uint32_t start = i, end = i + 1; /* [start, end) differs, gaps included */
            for (uint32_t j = end; j < size && j - end < RUN_MERGE_GAP; j++) {
                if (cur[j] != prev[j])
                    end = j + 1;
            }
            put_u32(w, start);
            put_u32(w, end - start);
            put_bytes(w, cur + start, end - start);
            runs++;
            i = end;
        }
    }
    patch_u32(w, count_at, runs);
    memcpy(prev, cur, size);
}

int ct_trace_write_frame(ct_trace_writer *w, uint32_t frame, uint16_t input, const uint8_t *const *regions,
                         uint16_t fb_width, uint16_t fb_height, uint64_t fb_hash) {
    int keyframe = !w->wrote_keyframe;
    w->wrote_keyframe = 1;

    put_u32(w, 0); /* record size, patched below */
    put_u32(w, frame);
    put_u16(w, input);
    put_u8(w, keyframe ? 1 : 0);
    put_u8(w, 0);
    put_u16(w, fb_width);
    put_u16(w, fb_height);
    put_u64(w, fb_hash);
    for (int r = 0; r < w->region_count; r++)
        put_region(w, r, regions[r], keyframe);
    patch_u32(w, 0, (uint32_t)(w->len - 4));
    flush(w);
    return w->error ? -1 : 0;
}

int ct_trace_close(ct_trace_writer *w) {
    if (!w)
        return 0;
    int error = w->error;
    if (fclose(w->f) != 0)
        error = 1;
    for (int i = 0; i < w->region_count; i++)
        free(w->prev[i]);
    free(w->prev);
    free(w->regions);
    free(w->buf);
    free(w);
    return error;
}
