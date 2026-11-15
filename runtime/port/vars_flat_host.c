/* Work RAM as the original lays it out, for a build of the flat state
 * (CT_FLAT_STATE: port/vars_host.h, vars_flat.c), native or a small target's: its
 * two banks in the arena, a byte for each address, its pages one run of
 * memory, but for the top a target keeps in memory of its own
 * (VARS_FLAT_COLD_FROM, vars_host_cold), which is read a byte at a time.
 * Bytes the map gives no owner read 0 and drop writes, as the typed state's
 * address view does: they hold 0 from the start and are never written, so a
 * span of the banks reads as the address view reads it. */
#ifdef CT_FLAT_STATE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/arena.h"
#include "port/vars_host.h"

enum { WORK_RAM = 0x7E0000, WORK_RAM_BYTES = 0x20000, PAGE_MASK = VARS_FLAT_PAGE_BYTES - 1 };

void vars_host_init(VarsHost *h)
{
    if (!h->page[0]) {
        uint8_t *run = arena_take(VARS_FLAT_COLD_FROM), *cold = NULL;
        if (!run) {
            fputs("vars: no memory for work RAM's banks\n", stderr);
            abort();
        }
#if VARS_FLAT_COLD_FROM < 0x20000
        cold = vars_host_cold();
#endif
        for (uint32_t p = 0; p < VARS_FLAT_PAGES; p++) {
            uint32_t at = p << VARS_FLAT_PAGE_SHIFT;
            h->page[p] = at < VARS_FLAT_COLD_FROM ? run + at : cold + (at - VARS_FLAT_COLD_FROM);
        }
    }
    for (uint32_t at = 0; at < WORK_RAM_BYTES; at++)
        *vars_flat_byte(h, at) = vars_flat_owned(WORK_RAM + at) ? 0x55 : 0;
}

/* A byte no owner keeps reads as the 0 it holds (above), so a read needs no
 * look at its owners. */
bool vars_host_read8(const VarsHost *h, uint32_t addr, uint8_t *value)
{
    uint32_t at = addr - WORK_RAM;
    if (at >= WORK_RAM_BYTES)
        return false;
    *value = *vars_flat_byte(h, at);
    return true;
}

bool vars_host_write8(VarsHost *h, uint32_t addr, uint8_t value)
{
    if (!vars_flat_owned(addr))
        return false;
    *vars_flat_byte(h, addr - WORK_RAM) = value;
    return true;
}

void vars_host_read_span(const VarsHost *h, uint32_t addr, uint8_t *out, uint32_t len)
{
    while (len) {
        uint32_t at = addr - WORK_RAM;
        if (at >= WORK_RAM_BYTES) {
            /* outside work RAM: nothing kept, as vars_host_read8 has it */
            memset(out, 0, len);
            return;
        }
        uint32_t end = at < VARS_FLAT_COLD_FROM ? VARS_FLAT_COLD_FROM : (at | PAGE_MASK) + 1;
        uint32_t n = len < end - at ? len : end - at;
        if (at < VARS_FLAT_COLD_FROM) {
            memcpy(out, h->page[0] + at, n);
        } else {
            const volatile uint8_t *from = vars_flat_byte(h, at);
            for (uint32_t i = 0; i < n; i++)
                out[i] = from[i];
        }
        out += n, addr += n, len -= n;
    }
}
#endif
