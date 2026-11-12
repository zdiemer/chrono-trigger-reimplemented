/* The arena (port/arena.h): one region reserved at a fixed address, handed
 * out from its start. Pages are committed as the arena grows on Windows, and
 * by the system on first touch elsewhere. */
#include "port/arena.h"

enum { ARENA_ALIGN = 64 };
#define ARENA_BYTES ((size_t)256 << 20)
/* An address high in the user space of every 64-bit host this builds for,
 * far from where images, heaps and stacks are put. */
#if UINTPTR_MAX > 0xFFFFFFFFu
#define ARENA_ADDRESS ((void *)(uintptr_t)0x0000070000000000ull)
#else
#define ARENA_ADDRESS NULL
#endif

static uint8_t *base;
static size_t used, committed;
static bool fixed = true;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static uint8_t *reserve(void)
{
    void *at = ARENA_ADDRESS ? VirtualAlloc(ARENA_ADDRESS, ARENA_BYTES, MEM_RESERVE, PAGE_READWRITE) : NULL;
    if (!at) {
        fixed = false;
        at = VirtualAlloc(NULL, ARENA_BYTES, MEM_RESERVE, PAGE_READWRITE);
    }
    return at;
}

static bool commit(size_t upto)
{
    if (upto <= committed)
        return true;
    size_t grow = (upto - committed + 0xFFFF) & ~(size_t)0xFFFF;
    if (committed + grow > ARENA_BYTES)
        grow = ARENA_BYTES - committed;
    if (!VirtualAlloc(base + committed, grow, MEM_COMMIT, PAGE_READWRITE))
        return false;
    committed += grow;
    return true;
}

#else
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

static uint8_t *reserve(void)
{
    void *want = ARENA_ADDRESS;
    void *at = mmap(want, ARENA_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (at == MAP_FAILED)
        return NULL;
    fixed = want && at == want;
    return at;
}

static bool commit(size_t upto)
{
    committed = upto;
    return true;
}
#endif

void *arena_take(size_t bytes)
{
    if (!base && !(base = reserve()))
        return NULL;
    size_t at = (used + ARENA_ALIGN - 1) & ~(size_t)(ARENA_ALIGN - 1);
    if (bytes > ARENA_BYTES - at || !commit(at + bytes))
        return NULL;
    used = at + bytes;
    return base + at;   /* never handed out before: still zero */
}

bool arena_fixed(void) { return fixed; }
