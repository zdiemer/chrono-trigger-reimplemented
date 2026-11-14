/* The game's save in the cartridge's SRAM (cartridge_save.h). The SRAM is on
 * an 8-bit bus, so it is read and written a byte at a time. */
#include "services/cartridge_save.h"

#include "platform/save.h"

#define SRAM ((volatile uint8_t *)0x0E000000)

/* What tells a flash cart and mGBA the cartridge has 32 KiB of SRAM: the
 * string they look for in the image, which the load reads so that the link
 * keeps it. */
static const char save_kind[] = "SRAM_V113";

static const uint8_t mark[4] = { 'C', 'T', 'S', 'V' };

bool cartridge_save_load(uint8_t *out)
{
    if (!*(const volatile char *)save_kind)
        return false;
    for (unsigned i = 0; i < sizeof mark; i++)
        if (SRAM[SAVE_SIZE + i] != mark[i])
            return false;
    for (unsigned i = 0; i < SAVE_SIZE; i++)
        out[i] = SRAM[i];
    return true;
}

void cartridge_save_store(const uint8_t *bytes)
{
    for (unsigned i = 0; i < SAVE_SIZE; i++)
        SRAM[i] = bytes[i];
    for (unsigned i = 0; i < sizeof mark; i++)
        SRAM[SAVE_SIZE + i] = mark[i];
}
