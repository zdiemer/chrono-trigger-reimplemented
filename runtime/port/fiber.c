/* Fibers whose suspended state is data (port/fiber.h).
 *
 * Every fiber but the thread's own runs on a stack from a fixed pool in the
 * arena (port/arena.h), so a suspended fiber is the bytes its stack holds
 * from where it stopped to its top, and the context that resumes it. A
 * snapshot copies those; a restore copies them back into the same slots, at
 * the same addresses, which is what lets the pointers inside them stay true.
 *
 * Windows x86-64 switches with a few lines of assembly that push the
 * callee-saved registers (the SSE ones included) and the thread block's stack
 * bounds onto the stack leaving and pop the ones of the stack coming in, so
 * the whole context is on the fiber's own stack and its stack pointer is all
 * there is besides. Elsewhere (Linux, macOS) the context is POSIX ucontext,
 * held in the fiber's slot: removed from POSIX.1-2008 and deprecated on
 * macOS, but implemented by glibc and libSystem on x86-64 and ARM64, and the
 * one portable way to switch stacks without assembly.
 * tests/port/fiber_test.c exercises the switches and the snapshots. */

#ifdef __APPLE__
/* Without _XOPEN_SOURCE, macOS's <ucontext.h> refuses to compile, because
 * ucontext_t would lack the machine context storage getcontext writes to. */
#define _XOPEN_SOURCE 700
#endif

#include "fiber.h"
#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FIBER_STACK_SIZE = 512u * 1024u, FIBER_SLOTS = 16 };

#if defined(_WIN32)
#if !defined(__x86_64__)
#error "fiber.c switches stacks on Windows only for x86-64"
#endif

typedef struct {
    void *sp;                 /* the stack pointer it stopped at; everything else is pushed below its top */
} Context;

/* rcx: where the leaving fiber's stack pointer goes; rdx: the coming one's.
 * The thread block's stack base, limit and deallocation stack (gs:0x08,
 * 0x10, 0x1478) are switched with the stacks, as the system's own fibers do. */
void fiber_swap(void **from, void *to);
__asm__(".text\n"
        ".globl fiber_swap\n"
        ".def fiber_swap; .scl 2; .type 32; .endef\n"
        "fiber_swap:\n"
        "    pushq %rbp\n"
        "    pushq %rbx\n"
        "    pushq %rdi\n"
        "    pushq %rsi\n"
        "    pushq %r12\n"
        "    pushq %r13\n"
        "    pushq %r14\n"
        "    pushq %r15\n"
        "    pushq %gs:0x08\n"
        "    pushq %gs:0x10\n"
        "    pushq %gs:0x1478\n"
        "    subq $176, %rsp\n"
        "    movaps %xmm6, 0(%rsp)\n"
        "    movaps %xmm7, 16(%rsp)\n"
        "    movaps %xmm8, 32(%rsp)\n"
        "    movaps %xmm9, 48(%rsp)\n"
        "    movaps %xmm10, 64(%rsp)\n"
        "    movaps %xmm11, 80(%rsp)\n"
        "    movaps %xmm12, 96(%rsp)\n"
        "    movaps %xmm13, 112(%rsp)\n"
        "    movaps %xmm14, 128(%rsp)\n"
        "    movaps %xmm15, 144(%rsp)\n"
        "    stmxcsr 160(%rsp)\n"
        "    fnstcw 164(%rsp)\n"
        "    movq %rsp, (%rcx)\n"
        "    movq %rdx, %rsp\n"
        "    ldmxcsr 160(%rsp)\n"
        "    fldcw 164(%rsp)\n"
        "    movaps 0(%rsp), %xmm6\n"
        "    movaps 16(%rsp), %xmm7\n"
        "    movaps 32(%rsp), %xmm8\n"
        "    movaps 48(%rsp), %xmm9\n"
        "    movaps 64(%rsp), %xmm10\n"
        "    movaps 80(%rsp), %xmm11\n"
        "    movaps 96(%rsp), %xmm12\n"
        "    movaps 112(%rsp), %xmm13\n"
        "    movaps 128(%rsp), %xmm14\n"
        "    movaps 144(%rsp), %xmm15\n"
        "    addq $176, %rsp\n"
        "    popq %gs:0x1478\n"
        "    popq %gs:0x10\n"
        "    popq %gs:0x08\n"
        "    popq %r15\n"
        "    popq %r14\n"
        "    popq %r13\n"
        "    popq %r12\n"
        "    popq %rsi\n"
        "    popq %rdi\n"
        "    popq %rbx\n"
        "    popq %rbp\n"
        "    ret\n"
        /* A new fiber's first switch returns here, its stack 16-aligned:
         * the shadow space, then the call into C, which never returns. */
        ".globl fiber_first_entry\n"
        ".def fiber_first_entry; .scl 2; .type 32; .endef\n"
        "fiber_first_entry:\n"
        "    subq $32, %rsp\n"
        "    call fiber_body\n"
        "    ud2\n");
void fiber_first_entry(void);

/* What fiber_swap pops for a fiber that hasn't run: the SSE registers zero
 * with the default control words, the thread block's bounds for its own
 * stack, the general registers zero, and the return into fiber_first_entry. */
enum { SWAP_SSE_BYTES = 176, SWAP_WORDS = 3 + 8, START_FRAME = 512 };
#else

#include <ucontext.h>

#ifdef __APPLE__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

typedef struct {
    ucontext_t uc;
    uintptr_t live;           /* below the lowest byte of its stack the context can need */
} Context;
#endif

struct Fiber {
    void (*fn)(void *);
    void *arg;
    Fiber *parent;
    bool used;
    uint8_t *stack;           /* its lowest byte; NULL for the thread's own */
    Context ctx;
};

static Fiber main_fiber;
static Fiber pool[FIBER_SLOTS];
static uint8_t *stacks;
static Fiber *current = &main_fiber;

static uint8_t *stack_top(const Fiber *f) { return f->stack + FIBER_STACK_SIZE; }

void fiber_body(void);
void fiber_body(void)
{
    Fiber *f = current;
    f->fn(f->arg);
    abort();   /* fiber functions must not return */
}

Fiber *fiber_main(void) { return &main_fiber; }
Fiber *fiber_current(void) { return current; }

#ifdef _WIN32
static void prepare(Fiber *f)
{
    uint8_t *top = stack_top(f);
    uint8_t *sp = top - START_FRAME;
    memset(sp, 0, START_FRAME);
    uint32_t mxcsr = 0x1F80;
    uint16_t fpcw = 0x037F;
    memcpy(sp + 160, &mxcsr, sizeof mxcsr);
    memcpy(sp + 164, &fpcw, sizeof fpcw);
    uint64_t *words = (uint64_t *)(sp + SWAP_SSE_BYTES);
    words[0] = (uint64_t)(uintptr_t)f->stack;   /* deallocation stack */
    words[1] = (uint64_t)(uintptr_t)f->stack;   /* stack limit */
    words[2] = (uint64_t)(uintptr_t)top;        /* stack base */
    words[SWAP_WORDS] = (uint64_t)(uintptr_t)fiber_first_entry;
    f->ctx.sp = sp;
}

static uintptr_t live_from(const Fiber *f) { return (uintptr_t)f->ctx.sp; }

void fiber_switch(Fiber *to)
{
    Fiber *from = current;
    current = to;
    fiber_swap(&from->ctx.sp, to->ctx.sp);
}
#else
static void trampoline(void) { fiber_body(); }

/* Below anything the caller's frame holds: a callee's own frame. */
__attribute__((noinline)) static uintptr_t stack_here(void)
{
    return (uintptr_t)__builtin_frame_address(0);
}

/* Room below the deepest address seen for what the switch itself saves. */
enum { SWITCH_MARGIN = 512, FIRST_FRAME = 4096 };

static void prepare(Fiber *f)
{
    if (getcontext(&f->ctx.uc) != 0)
        abort();
    f->ctx.uc.uc_stack.ss_sp = f->stack;
    f->ctx.uc.uc_stack.ss_size = FIBER_STACK_SIZE;
    f->ctx.uc.uc_link = NULL;
    makecontext(&f->ctx.uc, trampoline, 0);
    f->ctx.live = (uintptr_t)stack_top(f) - FIRST_FRAME;
}

static uintptr_t live_from(const Fiber *f) { return f->ctx.live; }

void fiber_switch(Fiber *to)
{
    Fiber *from = current;
    current = to;
    from->ctx.live = stack_here() - SWITCH_MARGIN;
    if (swapcontext(&from->ctx.uc, &to->ctx.uc) != 0)
        abort();
}
#endif

Fiber *fiber_create(void (*fn)(void *), void *arg)
{
    if (!stacks && !(stacks = arena_take((size_t)FIBER_STACK_SIZE * FIBER_SLOTS))) {
        fputs("fiber: no memory for the fibers' stacks\n", stderr);
        abort();
    }
    for (unsigned i = 0; i < FIBER_SLOTS; i++) {
        Fiber *f = &pool[i];
        if (f->used)
            continue;
        *f = (Fiber){ .fn = fn, .arg = arg, .parent = current, .used = true,
                      .stack = stacks + (size_t)i * FIBER_STACK_SIZE };
        prepare(f);
        return f;
    }
    /* A task that silently never ran would leave the game where no input takes it. */
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

/* ---- Snapshots ------------------------------------------------------------------------------ */

/* A slot as a snapshot keeps it: its place in the pool, its parent's (or
 * the thread's, or none), its body, its context and its live stack. */
enum { PARENT_MAIN = 0xFFFFFFFEu, PARENT_NONE = 0xFFFFFFFFu };
typedef struct {
    uint32_t slot, parent, live_bytes;
    void (*fn)(void *);
    void *arg;
    Context ctx;
} SlotRecord;
typedef struct {
    char magic[4];
    uint32_t slots;
    uintptr_t stacks;         /* where the pool was: a snapshot holds only for it */
} SnapshotHeader;

static uint32_t parent_of(const Fiber *f)
{
    if (!f->parent)
        return PARENT_NONE;
    if (f->parent == &main_fiber)
        return PARENT_MAIN;
    return (uint32_t)(f->parent - pool);
}

size_t fiber_snapshot(uint8_t *out, size_t room)
{
    if (current != &main_fiber)
        return 0;
    size_t need = sizeof(SnapshotHeader);
    SnapshotHeader h = { { 'F', 'I', 'B', '1' }, 0, (uintptr_t)stacks };
    for (unsigned i = 0; i < FIBER_SLOTS; i++) {
        if (!pool[i].used)
            continue;
        h.slots++;
        need += sizeof(SlotRecord) + (size_t)((uintptr_t)stack_top(&pool[i]) - live_from(&pool[i]));
    }
    if (!out || room < need)
        return need;
    memcpy(out, &h, sizeof h);
    size_t at = sizeof h;
    for (unsigned i = 0; i < FIBER_SLOTS; i++) {
        const Fiber *f = &pool[i];
        if (!f->used)
            continue;
        uint32_t live = (uint32_t)((uintptr_t)stack_top(f) - live_from(f));
        SlotRecord r = { i, parent_of(f), live, f->fn, f->arg, f->ctx };
        memcpy(out + at, &r, sizeof r);
        at += sizeof r;
        memcpy(out + at, stack_top(f) - live, live);
        at += live;
    }
    return need;
}

bool fiber_restorable(const uint8_t *in, size_t len)
{
    SnapshotHeader h;
    if (current != &main_fiber || len < sizeof h)
        return false;
    memcpy(&h, in, sizeof h);
    if (memcmp(h.magic, "FIB1", 4) || h.slots > FIBER_SLOTS || (h.slots && h.stacks != (uintptr_t)stacks))
        return false;
    size_t at = sizeof h;
    bool seen[FIBER_SLOTS] = { false };
    for (uint32_t n = 0; n < h.slots; n++) {
        SlotRecord r;
        if (len - at < sizeof r)
            return false;
        memcpy(&r, in + at, sizeof r);
        at += sizeof r;
        if (r.slot >= FIBER_SLOTS || seen[r.slot] || r.live_bytes > FIBER_STACK_SIZE || len - at < r.live_bytes ||
            (r.parent >= FIBER_SLOTS && r.parent != PARENT_MAIN && r.parent != PARENT_NONE))
            return false;
        seen[r.slot] = true;
        at += r.live_bytes;
    }
    return at == len;
}

bool fiber_restore(const uint8_t *in, size_t len)
{
    /* Check every record before changing a slot. */
    if (!fiber_restorable(in, len))
        return false;
    SnapshotHeader h;
    memcpy(&h, in, sizeof h);
    for (unsigned i = 0; i < FIBER_SLOTS; i++)
        pool[i].used = false;
    size_t at = sizeof h;
    for (uint32_t n = 0; n < h.slots; n++) {
        SlotRecord r;
        memcpy(&r, in + at, sizeof r);
        at += sizeof r;
        Fiber *f = &pool[r.slot];
        *f = (Fiber){ .fn = r.fn, .arg = r.arg, .used = true, .ctx = r.ctx,
                      .stack = stacks + (size_t)r.slot * FIBER_STACK_SIZE,
                      .parent = r.parent == PARENT_MAIN ? &main_fiber : r.parent == PARENT_NONE ? NULL : &pool[r.parent] };
        memcpy(stack_top(f) - r.live_bytes, in + at, r.live_bytes);
        at += r.live_bytes;
    }
    return true;
}

unsigned fiber_slots(void) { return FIBER_SLOTS; }
size_t fiber_stack_size(void) { return FIBER_STACK_SIZE; }

size_t fiber_stack_high_water(unsigned slot)
{
    if (!stacks || slot >= FIBER_SLOTS)
        return 0;
    const uint8_t *stack = stacks + (size_t)slot * FIBER_STACK_SIZE;
    size_t low = 0;
    while (low < FIBER_STACK_SIZE && stack[low] == 0)
        low++;
    return FIBER_STACK_SIZE - low;
}
