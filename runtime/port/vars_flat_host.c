/* Work RAM as the original lays it out, for a build of the flat state
 * (CT_FLAT_STATE: port/vars_host.h, vars_flat.c), native or the GBA's: its
 * two banks in the arena, a byte for each address. Bytes the map gives no
 * owner read 0 and drop writes, as the typed state's address view does: they
 * hold 0 from the start and are never written, so a span of the banks reads
 * as the address view reads it. */
#ifdef CT_FLAT_STATE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/arena.h"
#include "port/vars_host.h"

enum { BANK_BYTES = 0x10000, WORK_RAM = 0x7E0000 };

void vars_host_init(VarsHost *h)
{
    for (unsigned b = 0; b < 2; b++) {
        if (!h->bank[b])
            h->bank[b] = arena_take(BANK_BYTES);
        if (!h->bank[b]) {
            fputs("vars: no memory for work RAM's banks\n", stderr);
            abort();
        }
        memset(h->bank[b], 0x55, BANK_BYTES);
    }
    for (uint32_t at = 0; at < 2u * BANK_BYTES; at++)
        if (!vars_flat_owned(WORK_RAM + at))
            h->bank[at >> 16][at & 0xFFFF] = 0;
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

void vars_host_read_span(const VarsHost *h, uint32_t addr, uint8_t *out, uint32_t len)
{
    while (len) {
        uint32_t at = addr - WORK_RAM;
        if (at >= 2u * BANK_BYTES) {
            /* outside work RAM: nothing kept, as vars_host_read8 has it */
            memset(out, 0, len);
            return;
        }
        uint32_t in_bank = BANK_BYTES - (at & 0xFFFF);
        uint32_t n = len < in_bank ? len : in_bank;
        memcpy(out, &h->bank[at >> 16][at & 0xFFFF], n);
        out += n, addr += n, len -= n;
    }
}
#endif
