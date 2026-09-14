/* .cttr per-frame state trace writer. Format: docs/ORACLE.md.
 *
 * Independent of libretro so the port can write the same traces. */
#ifndef CT_TRACE_H
#define CT_TRACE_H

#include <stdint.h>
#include <stdio.h>

#define CT_TRACE_VERSION 1

enum ct_region_id {
    CT_REGION_WRAM = 0,
    CT_REGION_VRAM = 1,
    CT_REGION_SRAM = 2,
    CT_REGION_CGRAM = 3,
    CT_REGION_OAM = 4,
};

typedef struct {
    uint8_t id;       /* enum ct_region_id */
    const char *name; /* at most 8 bytes */
    uint32_t size;
} ct_region_desc;

typedef struct ct_trace_writer ct_trace_writer;

uint64_t ct_fnv1a64(uint64_t h, const uint8_t *data, size_t len);
#define CT_FNV1A64_INIT 0xcbf29ce484222325ull

uint32_t ct_crc32(const uint8_t *data, size_t len);

ct_trace_writer *ct_trace_open(const char *path, uint32_t rom_crc32, const char *producer,
                               const ct_region_desc *regions, int region_count);

/* regions[i] points at region i's current contents, sized as declared at open.
 * The first frame written is a keyframe (every region stored in full). */
int ct_trace_write_frame(ct_trace_writer *w, uint32_t frame, uint16_t input, const uint8_t *const *regions,
                         uint16_t fb_width, uint16_t fb_height, uint64_t fb_hash);

/* Returns 0 on success, nonzero if any write failed. */
int ct_trace_close(ct_trace_writer *w);

/* Hash of a framebuffer given as XRGB8888 pixels; see docs/ORACLE.md. */
uint64_t ct_framebuffer_hash(const uint32_t *pixels, uint16_t width, uint16_t height);

#endif
