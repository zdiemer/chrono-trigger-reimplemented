/* Fibers on the GBA's ARM7TDMI (runtime/port/fiber.h), in place of
 * runtime/port/fiber.c.
 *
 * Every fiber but the driver's runs on a stack from a fixed pool in fast RAM
 * (gba.ld's .iwram_bss, zeroed at the start). A switch pushes the callee-saved registers and the return
 * address on the stack it leaves, keeps that stack pointer, and pops the
 * other fiber's from its own: the context is all on the fiber's stack. A new
 * fiber's stack starts as if it had been switched away from at the call into
 * fiber_body. Snapshots aren't kept on the GBA: fiber_snapshot answers 0.
 *
 * The slots' stacks are sized from the deepest use the native build measures
 * (docs/PLATFORM.md, "Memory, stacks and per-frame work against small
 * targets") and the GBA's own (ct_gba reports it at its end); the lowest
 * GUARD_BYTES of each stay zero while no fiber goes that deep, and a switch
 * away from a fiber that has gone into them stops the program, saying so. */
#include "port/fiber.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FIBER_STACK_SIZE = 3072, FIBER_SLOTS = 8, GUARD_BYTES = 64 };

struct Fiber {
    void (*fn)(void *);
    void *arg;
    Fiber *parent;
    bool used;
    uint8_t *stack;           /* its lowest byte; NULL for the driver's own */
    void *sp;                 /* where it stopped */
};

/* r0: where the leaving fiber's stack pointer goes; r1: the coming one's
 * (targets/gba/switch.s). */
void fiber_swap(void **from, void *to);

static Fiber main_fiber;
static Fiber pool[FIBER_SLOTS];
static uint8_t stacks[FIBER_SLOTS * FIBER_STACK_SIZE] __attribute__((section(".iwram_bss"), aligned(8)));
static Fiber *current = &main_fiber;

/* Reached only from a new fiber's first switch. */
__attribute__((used, noreturn)) static void fiber_body(void)
{
    Fiber *f = current;
    f->fn(f->arg);
    abort();   /* fiber functions must not return */
}

Fiber *fiber_main(void) { return &main_fiber; }
Fiber *fiber_current(void) { return current; }

/* What fiber_swap pops for a fiber that hasn't run: r4-r11 zero, and the
 * return into fiber_body (its Thumb bit set, which bx keeps). Once they are
 * popped the stack pointer is 8-aligned, as the ARM EABI wants it at a call:
 * a word of padding below the top makes the nine words' odd count even. */
enum { SWAP_WORDS = 9, STACK_ALIGN = 8 };
_Static_assert(FIBER_STACK_SIZE % STACK_ALIGN == 0, "fiber stacks keep their slots 8-aligned");
static void prepare(Fiber *f)
{
    uint32_t *entry = (uint32_t *)(f->stack + FIBER_STACK_SIZE) - 2;   /* the sp fiber_body starts with */
    uint32_t *sp = entry - SWAP_WORDS;
    if ((uintptr_t)entry % STACK_ALIGN) {
        fputs("fiber: a new fiber's stack isn't 8-aligned\n", stderr);
        abort();
    }
    memset(sp, 0, (SWAP_WORDS + 2) * sizeof *sp);
    sp[8] = (uint32_t)(uintptr_t)fiber_body;
    f->sp = sp;
}

static void check_guard(const Fiber *f)
{
    if (!f->stack)
        return;
    for (unsigned i = 0; i < GUARD_BYTES; i++)
        if (f->stack[i]) {
            fprintf(stderr, "fiber: slot %u's fiber went past its %u-byte stack\n",
                    (unsigned)(f - pool), (unsigned)FIBER_STACK_SIZE);
            abort();
        }
}

void fiber_switch(Fiber *to)
{
    Fiber *from = current;
    check_guard(from);
    current = to;
    fiber_swap(&from->sp, to->sp);
}

Fiber *fiber_create(void (*fn)(void *), void *arg)
{
    for (unsigned i = 0; i < FIBER_SLOTS; i++) {
        Fiber *f = &pool[i];
        if (f->used)
            continue;
        *f = (Fiber){ .fn = fn, .arg = arg, .parent = current, .used = true,
                      .stack = stacks + (size_t)i * FIBER_STACK_SIZE };
        prepare(f);
        return f;
    }
    fprintf(stderr, "fiber: all %u fiber slots are in use\n", (unsigned)FIBER_SLOTS);
    abort();
}

void fiber_destroy(Fiber *f)
{
    if (!f || f == &main_fiber)
        return;
    for (unsigned i = 0; i < FIBER_SLOTS; i++)
        if (pool[i].used && pool[i].parent == f)
            pool[i].parent = f->parent;
    f->used = false;
}

void fiber_destroy_tree(Fiber *f)
{
    if (!f || f == &main_fiber || f == current)
        return;
    for (unsigned i = 0; i < FIBER_SLOTS; i++)
        if (pool[i].used && pool[i].parent == f)
            fiber_destroy_tree(&pool[i]);
    fiber_destroy(f);
}

size_t fiber_snapshot(uint8_t *out, size_t room)
{
    (void)out, (void)room;
    return 0;
}

bool fiber_restorable(const uint8_t *in, size_t len)
{
    (void)in, (void)len;
    return false;
}

bool fiber_restore(const uint8_t *in, size_t len)
{
    (void)in, (void)len;
    return false;
}

unsigned fiber_slots(void) { return FIBER_SLOTS; }
size_t fiber_stack_size(void) { return FIBER_STACK_SIZE; }

size_t fiber_stack_high_water(unsigned slot)
{
    if (slot >= FIBER_SLOTS)
        return 0;
    const uint8_t *stack = stacks + (size_t)slot * FIBER_STACK_SIZE;
    size_t low = 0;
    while (low < FIBER_STACK_SIZE && stack[low] == 0)
        low++;
    return FIBER_STACK_SIZE - low;
}
