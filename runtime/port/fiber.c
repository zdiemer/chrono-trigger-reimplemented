/* Windows: fibers. Elsewhere (Linux, macOS): POSIX ucontext.
 *
 * ucontext was removed from POSIX.1-2008 and is marked deprecated on macOS,
 * but glibc and macOS's libSystem both still implement it on x86-64 and ARM64,
 * and it is the only portable way to switch stacks without assembly. A thread
 * per fiber, handing control over with a condition variable, was the other
 * option; it's rejected because a switch would cost a scheduler round trip
 * (native routines switch at least twice per call), and because a suspended
 * fiber whose continuation will never run (job_abandon in native.c) must be
 * freed without unwinding it, which a thread can't do safely.
 * tests/port/fiber_test.c exercises the switches this runtime relies on. */

#ifdef __APPLE__
/* Without _XOPEN_SOURCE, macOS's <ucontext.h> refuses to compile, because
 * ucontext_t would lack the machine context storage getcontext writes to. */
#define _XOPEN_SOURCE 700
#endif

#include "fiber.h"

#include <stdlib.h>

#define FIBER_STACK_SIZE (512u * 1024u)

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct Fiber {
    LPVOID handle;
    void (*fn)(void *);
    void *arg;
};

static Fiber main_fiber;

static void WINAPI trampoline(LPVOID param)
{
    Fiber *f = param;
    f->fn(f->arg);
    abort();   /* fiber functions must not return */
}

Fiber *fiber_main(void)
{
    if (!main_fiber.handle) {
        main_fiber.handle = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(NULL);
        if (!main_fiber.handle)
            abort();
    }
    return &main_fiber;
}

Fiber *fiber_create(void (*fn)(void *), void *arg)
{
    fiber_main();
    Fiber *f = calloc(1, sizeof *f);
    f->fn = fn;
    f->arg = arg;
    f->handle = CreateFiber(FIBER_STACK_SIZE, trampoline, f);
    if (!f->handle)
        abort();
    return f;
}

void fiber_destroy(Fiber *f)
{
    if (!f || f == &main_fiber)
        return;
    DeleteFiber(f->handle);
    free(f);
}

void fiber_switch(Fiber *to) { SwitchToFiber(to->handle); }

#else

#include <ucontext.h>

#ifdef __APPLE__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

struct Fiber {
    ucontext_t ctx;
    void *stack;
    void (*fn)(void *);
    void *arg;
};

static Fiber main_fiber;
static Fiber *current = &main_fiber;

static void trampoline(void)
{
    current->fn(current->arg);
    abort();
}

Fiber *fiber_main(void) { return &main_fiber; }

Fiber *fiber_create(void (*fn)(void *), void *arg)
{
    Fiber *f = calloc(1, sizeof *f);
    if (!f)
        abort();
    f->fn = fn;
    f->arg = arg;
    f->stack = malloc(FIBER_STACK_SIZE);
    if (!f->stack || getcontext(&f->ctx) != 0)
        abort();
    f->ctx.uc_stack.ss_sp = f->stack;
    f->ctx.uc_stack.ss_size = FIBER_STACK_SIZE;
    f->ctx.uc_link = NULL;
    makecontext(&f->ctx, trampoline, 0);
    return f;
}

void fiber_destroy(Fiber *f)
{
    if (!f || f == &main_fiber)
        return;
    free(f->stack);
    free(f);
}

void fiber_switch(Fiber *to)
{
    Fiber *from = current;
    current = to;
    if (swapcontext(&from->ctx, &to->ctx) != 0)
        abort();
}

#endif
