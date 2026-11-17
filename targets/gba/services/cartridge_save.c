/* The game's save in the cartridge's SRAM, and work RAM's top after it
 * (cartridge_save.h). */
#include "services/cartridge_save.h"

#include <stdbool.h>
#include <stdint.h>

#include "port/vars_host.h"

#define SRAM ((uint8_t *)0x0E000000)

/* What tells a flash cart and mGBA the cartridge has 32 KiB of SRAM: the
 * string they look for in the image, which the open reads so that the link
 * keeps it. */
static const char save_kind[] = "SRAM_V113";

enum { SRAM_BYTES = 0x8000, MARK_BYTES = 4, COLD_AT = SAVE_SIZE + MARK_BYTES };
static const uint8_t mark[MARK_BYTES] = { 'C', 'T', 'S', 'V' };
_Static_assert(COLD_AT + (0x20000 - VARS_FLAT_COLD_FROM) <= SRAM_BYTES, "work RAM's cold top doesn't fit the SRAM");

uint8_t *cartridge_save_open(void)
{
    volatile uint8_t *sram = SRAM;
    bool marked = *(const volatile char *)save_kind != 0;
    for (unsigned i = 0; i < sizeof mark; i++)
        marked = marked && sram[SAVE_SIZE + i] == mark[i];
    if (!marked) {
        for (unsigned i = 0; i < SAVE_SIZE; i++)
            sram[i] = 0;
        for (unsigned i = 0; i < sizeof mark; i++)
            sram[SAVE_SIZE + i] = mark[i];
    }
    return SRAM;
}

uint8_t *vars_host_cold(void) { return SRAM + COLD_AT; }

/* Runs into and out of the cold top: the SRAM takes a byte at a time, the
 * ordinary memory's end goes two words at a time once aligned, four of the
 * SRAM's bytes to each, where a byte loop spent as long again on itself as
 * on the SRAM. A world map's load sends its graphics through the SRAM,
 * about 30 KiB each way. They run from the cartridge, fast RAM having no
 * room for them, the SRAM's own waits taking most of their time. */
typedef uint32_t __attribute__((may_alias)) Word;

__attribute__((noinline)) void vars_host_cold_write(volatile uint8_t *to, const uint8_t *from, uint32_t n)
{
    for (; n && ((uintptr_t)from & 3u); n--)
        *to++ = *from++;
    for (; n >= 8; n -= 8, to += 8, from += 8) {
        uint32_t w = *(const Word *)from, v = *(const Word *)(from + 4);
        to[0] = (uint8_t)w, to[1] = (uint8_t)(w >> 8), to[2] = (uint8_t)(w >> 16), to[3] = (uint8_t)(w >> 24);
        to[4] = (uint8_t)v, to[5] = (uint8_t)(v >> 8), to[6] = (uint8_t)(v >> 16), to[7] = (uint8_t)(v >> 24);
    }
    for (; n; n--)
        *to++ = *from++;
}

__attribute__((noinline)) void vars_host_cold_read(uint8_t *to, const volatile uint8_t *from, uint32_t n)
{
    for (; n && ((uintptr_t)to & 3u); n--)
        *to++ = *from++;
    for (; n >= 8; n -= 8, to += 8, from += 8) {
        *(Word *)to = (uint32_t)from[0] | (uint32_t)from[1] << 8 | (uint32_t)from[2] << 16 | (uint32_t)from[3] << 24;
        *(Word *)(to + 4) = (uint32_t)from[4] | (uint32_t)from[5] << 8 | (uint32_t)from[6] << 16 | (uint32_t)from[7] << 24;
    }
    for (; n; n--)
        *to++ = *from++;
}
