/* The game's save in the cartridge's SRAM, and work RAM's top after it
 * (cartridge_save.h). */
#include "services/cartridge_save.h"

#include <stdbool.h>

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
