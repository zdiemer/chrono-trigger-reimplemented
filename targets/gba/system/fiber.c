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
 * targets") and the GBA's own (ct_gba reports it at its end). The first
 * slot is the first fiber's, the game's main thread (made once, before any
 * task), which goes deepest; the tasks' slots are smaller, which leaves fast
 * RAM to the code that runs most. The lowest GUARD_BYTES of each stay zero
 * while no fiber goes that deep, and a switch away from a fiber that has
 * gone into them stops the program, saying so. */
#include "port/fiber.h"

#include "system/fiber.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MAIN_STACK_SIZE = 3072, TASK_STACK_SIZE = 1536, FIBER_SLOTS = 9, GUARD_BYTES = 64,
    STACKS_SIZE = MAIN_STACK_SIZE + (FIBER_SLOTS - 1) * TASK_STACK_SIZE,
};

struct Fiber {
    void (*fn)(void *);
    void *arg;
    Fiber *parent;
    bool used;
    uint8_t *stack;           /* its lowest byte; NULL for the driver's own */
    size_t size;
    void *sp;                 /* where it stopped */
};

/* r0: where the leaving fiber's stack pointer goes; r1: the coming one's
 * (targets/gba/system/switch.s). */
void fiber_swap(void **from, void *to);

static Fiber main_fiber;
static Fiber pool[FIBER_SLOTS];
static uint8_t stacks[STACKS_SIZE] __attribute__((section(".iwram_bss"), aligned(8)));

size_t gba_fiber_slot_size(unsigned slot) { return slot == 0 ? MAIN_STACK_SIZE : TASK_STACK_SIZE; }

static uint8_t *slot_stack(unsigned slot)
{
    return stacks + (slot == 0 ? 0 : MAIN_STACK_SIZE + (size_t)(slot - 1) * TASK_STACK_SIZE);
}
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
_Static_assert(MAIN_STACK_SIZE % STACK_ALIGN == 0 && TASK_STACK_SIZE % STACK_ALIGN == 0,
               "fiber stacks keep their slots 8-aligned");
static void prepare(Fiber *f)
{
    uint32_t *entry = (uint32_t *)(f->stack + f->size) - 2;   /* the sp fiber_body starts with */
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
                    (unsigned)(f - pool), (unsigned)f->size);
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
                      .stack = slot_stack(i), .size = gba_fiber_slot_size(i) };
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
/* The largest slot's: the main thread's. */
size_t fiber_stack_size(void) { return MAIN_STACK_SIZE; }

size_t fiber_stack_high_water(unsigned slot)
{
    if (slot >= FIBER_SLOTS)
        return 0;
    const uint8_t *stack = slot_stack(slot);
    size_t size = gba_fiber_slot_size(slot), low = 0;
    while (low < size && stack[low] == 0)
        low++;
    return size - low;
}
