#include "port/rom_image.h"

#include <stdbool.h>

#include "rom.h"

static const uint8_t *image;
static uint32_t image_bytes;
static unsigned long strays;
static uint32_t first_stray;

void rom_image_set(const uint8_t *bytes, uint32_t len)
{
    image = bytes;
    image_bytes = len;
    strays = 0;
}

unsigned long rom_image_strays(uint32_t *first)
{
    if (first)
        *first = first_stray;
    return strays;
}

/* Where a HiROM address lies in the image: banks $C0-$FF (and their mirrors
 * $40-$7D) whole, and the upper half of banks $00-$3F and $80-$BF. */
static bool image_offset(uint32_t addr, uint32_t *offset)
{
    uint8_t bank = (uint8_t)(addr >> 16);
    uint16_t at = (uint16_t)addr;
    bool whole = bank >= 0xC0 || (bank >= 0x40 && bank < 0x7E);
    bool upper = (bank < 0x40 || (bank >= 0x80 && bank < 0xC0)) && at >= 0x8000;
    if (!whole && !upper)
        return false;
    *offset = (uint32_t)(bank & 0x3F) << 16 | at;
    return *offset < image_bytes;
}

uint8_t rom_read8(const GameState *g, uint32_t at)
{
    (void)g;
    uint32_t offset;
    if (image && image_offset(at & 0xFFFFFF, &offset))
        return image[offset];
    if (!strays++)
        first_stray = at & 0xFFFFFF;
    return 0;
}

uint16_t rom_read16(const GameState *g, uint32_t at)
{
    return (uint16_t)(rom_read8(g, at) | rom_read8(g, at + 1) << 8);   /* on into the next bank, as m_read16 */
}
