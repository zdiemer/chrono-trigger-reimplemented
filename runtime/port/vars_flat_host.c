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

/* A fill keeps to the owned bytes, as vars_host_write8 does: the rest hold
 * the 0 they started with. */
void vars_host_fill_span(VarsHost *h, uint32_t addr, uint8_t value, uint32_t len)
{
    while (len) {
        uint32_t at = addr - WORK_RAM;
        if (at >= WORK_RAM_BYTES)
            return;
        uint32_t end = at < VARS_FLAT_COLD_FROM ? VARS_FLAT_COLD_FROM : (at | PAGE_MASK) + 1;
        uint32_t n = len < end - at ? len : end - at;
        /* a byte at a time, as the cold top's memory takes it */
        volatile uint8_t *to = vars_flat_byte(h, at);
        for (uint32_t i = 0; i < n; i++)
            if (vars_flat_owned(addr + i))
                to[i] = value;
        addr += n, len -= n;
    }
}

/* How far the storage runs on unbroken from `at` (bytes from $7E:0000): to
 * the cold top, or within it to work RAM's end, the cold top's pages being
 * one run of its own memory too (vars_host_init). */
static uint32_t run_from(uint32_t at)
{
    return at < VARS_FLAT_COLD_FROM ? VARS_FLAT_COLD_FROM - at : WORK_RAM_BYTES - at;
}

/* A span written where it is kept: as one copy, a word at a time where it
 * can, where both ends are ordinary memory and don't overlap; otherwise a
 * byte at a time from the first, as the cold top's memory takes them and so
 * that a copy over itself reads what it wrote. */
static void put_bytes(volatile uint8_t *to, const volatile uint8_t *in, uint32_t n, bool in_words)
{
    uintptr_t from = (uintptr_t)in, at = (uintptr_t)to;
    if (in_words && (at + n <= from || from + n <= at)) {
        memcpy((uint8_t *)to, (const uint8_t *)in, n);
        return;
    }
    for (uint32_t i = 0; i < n; i++)
        to[i] = in[i];
}

/* A span that reaches past one of the storage's runs or a byte no owner
 * keeps: only the owned bytes, as vars_host_write8 has it. Seldom written,
 * kept apart from put_span, which runs from a small target's fast memory. */
__attribute__((noinline)) static void write_owned_bytes(VarsHost *h, uint32_t at, const volatile uint8_t *in, uint32_t len)
{
    for (; len && at < WORK_RAM_BYTES; at++, in++, len--)
        if (vars_flat_owners(at) >> (at & 7u) & 1u)
            *vars_flat_byte(h, at) = *in;
}

/* `len` bytes from `in` written from `at` (bytes from $7E:0000); `in` is in
 * ordinary memory where `in_words` says so, not the cold top's. Most spans
 * lie in one of the storage's runs, in pages owners keep whole, and go
 * straight in. */
__attribute__((noinline)) static void put_span(VarsHost *h, uint32_t at, const volatile uint8_t *in, uint32_t len, bool in_words)
{
    if (!len || at >= WORK_RAM_BYTES)
        return;
    if (len <= run_from(at) && vars_flat_wholly_owned(at, len))
        put_bytes(vars_flat_byte(h, at), in, len, in_words && at < VARS_FLAT_COLD_FROM);
    else
        write_owned_bytes(h, at, in, len);
}

void vars_host_write_span(VarsHost *h, uint32_t addr, const uint8_t *in, uint32_t len)
{
    put_span(h, addr - WORK_RAM, in, len, true);
}

/* A copy whose source runs past one of the storage's runs, a byte at a time. */
static void copy_bytes(VarsHost *h, uint32_t from, uint32_t to, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint8_t value = 0;
        vars_host_read8(h, from + i, &value);
        vars_host_write8(h, to + i, value);
    }
}

/* From a source in one of the storage's runs, written as the span it is. */
void vars_host_copy_span(VarsHost *h, uint32_t from, uint32_t to, uint32_t len)
{
    uint32_t source = from - WORK_RAM;
    if (source < WORK_RAM_BYTES && len <= run_from(source))
        put_span(h, to - WORK_RAM, vars_flat_byte(h, source), len, source < VARS_FLAT_COLD_FROM);
    else
        copy_bytes(h, from, to, len);
}
#endif
