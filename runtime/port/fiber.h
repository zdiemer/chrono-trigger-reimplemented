/* Minimal cooperative coroutines for native routines that call back into the
 * interpreter: Windows fibers, or ucontext elsewhere. One thread only. */
#ifndef CT_PORT_FIBER_H
#define CT_PORT_FIBER_H

typedef struct Fiber Fiber;

/* Create a fiber that runs fn(arg) when first switched to. fn must never
 * return; switch away instead. */
Fiber *fiber_create(void (*fn)(void *arg), void *arg);
void fiber_destroy(Fiber *f);

/* The fiber representing the thread's original stack. */
Fiber *fiber_main(void);

/* Suspend the current fiber and run `to`. */
void fiber_switch(Fiber *to);

/* The fiber running now (fiber_main() before any switch). */
Fiber *fiber_current(void);

#endif
