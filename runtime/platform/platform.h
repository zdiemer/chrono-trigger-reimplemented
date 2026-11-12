/* Native platform services: video, audio, timing, input and saves. */
#ifndef CT_PLATFORM_PLATFORM_H
#define CT_PLATFORM_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Platform Platform;

/* Diagnostic and work-accounting provenance; never a dispatch destination. */
typedef struct {
    uint32_t site, target;
} PfOrigin;
#define PF_AT(addr) ((PfOrigin){ (addr), 0 })
#define PF_CALL(site, target) ((PfOrigin){ (site), (target) })
/* A loop of the original's that a call stands in for: the instruction it
 * comes back to (`head`) and the first after it (`exit`). */
#define PF_LOOP(head, exit) ((PfOrigin){ (head), (exit) })

/* The data a call refers to: a 24-bit game address while the WRAM layout is
 * kept, a host pointer for a store resource or owned memory. A resource
 * crossing a pack entry or bank boundary resolves its bytes individually.
 * The SNES backend retains addr solely for its reference DMA. */
typedef struct {
    uint32_t addr;
    const uint8_t *ptr;
    const uint8_t *(*byte_at)(uint32_t addr);
} PfData;
#define PF_GAME(a) ((PfData){ .addr = (a) })
/* A repeated byte, also supplying both halves of a VRAM fill word. */
#define PF_VALUE(v) ((PfData){ .ptr = (const uint8_t[]){ (v), (v) } })

static inline uint32_t pf_data_address(PfData src, uint32_t i)
{
    return src.byte_at ? (src.addr & 0xFF0000u) | (uint16_t)(src.addr + i) : src.addr + i;
}

static inline const uint8_t *pf_data_pointer(PfData src, uint32_t i)
{
    return src.ptr ? src.ptr + i : src.byte_at ? src.byte_at(pf_data_address(src, i)) : NULL;
}

#define PF_API extern

#endif
