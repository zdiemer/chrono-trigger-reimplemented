/* Work RAM as the original lays it out, for a native build of the flat
 * state (CT_FLAT_STATE: port/vars_host.h, vars_flat.c): its two banks in
 * the arena, a byte for each address. A target keeps them its own way
 * (targets/gba/memory.c). Bytes the map gives no owner read 0 and drop
 * writes, as the typed state's address view does. */
#ifdef CT_FLAT_STATE
#include <string.h>

#include "port/arena.h"
#include "port/vars_host.h"

enum { BANK_BYTES = 0x10000, WORK_RAM = 0x7E0000 };

void vars_host_init(VarsHost *h)
{
    for (unsigned b = 0; b < 2; b++) {
        if (!h->bank[b])
            h->bank[b] = arena_take(BANK_BYTES);
        if (h->bank[b])
            memset(h->bank[b], 0x55, BANK_BYTES);
    }
}

bool vars_host_read8(const VarsHost *h, uint32_t addr, uint8_t *value)
{
    if (!vars_flat_owned(addr))
        return false;
    uint32_t at = addr - WORK_RAM;
    *value = h->bank[at >> 16][at & 0xFFFF];
    return true;
}

bool vars_host_write8(VarsHost *h, uint32_t addr, uint8_t value)
{
    if (!vars_flat_owned(addr))
        return false;
    uint32_t at = addr - WORK_RAM;
    h->bank[at >> 16][at & 0xFFFF] = value;
    return true;
}
#endif
