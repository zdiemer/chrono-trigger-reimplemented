/* The game's work RAM on the GBA, as the original lays it out
 * (CT_FLAT_STATE: runtime/port/vars_host.h, vars_flat.c), in place of
 * runtime/port/vars_flat_host.c.
 *
 * Its first bank ($7E) is in work RAM; its second ($7F) is in video RAM,
 * which no picture uses in this build. Video RAM takes no byte writes (a
 * byte written there lands in both halves of its word), so a byte is
 * written into its word. Bytes the map gives no owner read 0 and drop
 * writes, as the typed state's address view does. */
#include <string.h>

#include "port/vars_host.h"

enum { BANK_BYTES = 0x10000, WORK_RAM = 0x7E0000 };

static uint8_t first_bank[BANK_BYTES];
#define SECOND_BANK ((volatile uint16_t *)0x06000000)

void vars_host_init(VarsHost *h)
{
    h->bank[0] = first_bank;
    h->bank[1] = (uint8_t *)SECOND_BANK;
    memset(first_bank, 0x55, sizeof first_bank);
    for (uint32_t i = 0; i < BANK_BYTES / 2; i++)
        SECOND_BANK[i] = 0x5555;
}

bool vars_host_read8(const VarsHost *h, uint32_t addr, uint8_t *value)
{
    if (!vars_flat_owned(addr))
        return false;
    uint32_t at = addr - WORK_RAM;
    if (at < BANK_BYTES) {
        *value = h->bank[0][at];
        return true;
    }
    uint16_t word = SECOND_BANK[(at - BANK_BYTES) >> 1];
    *value = (uint8_t)(at & 1 ? word >> 8 : word);
    return true;
}

bool vars_host_write8(VarsHost *h, uint32_t addr, uint8_t value)
{
    if (!vars_flat_owned(addr))
        return false;
    uint32_t at = addr - WORK_RAM;
    if (at < BANK_BYTES) {
        h->bank[0][at] = value;
        return true;
    }
    volatile uint16_t *word = &SECOND_BANK[(at - BANK_BYTES) >> 1];
    *word = (uint16_t)(at & 1 ? (*word & 0x00FF) | value << 8 : (*word & 0xFF00) | value);
    return true;
}
