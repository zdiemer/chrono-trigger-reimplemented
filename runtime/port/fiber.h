/* Cooperative coroutines on one thread, whose suspended state is data: the
 * main thread's waits and the menus' tasks are fibers, and a snapshot of the
 * game holds them as bytes (runtime/port/fiber.c). */
#ifndef CT_PORT_FIBER_H
#define CT_PORT_FIBER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Fiber Fiber;

/* Create a fiber that runs fn(arg) when first switched to; the program stops,
 * saying so, when the pool's slots are all in use. fn must never return;
 * switch away instead. */
Fiber *fiber_create(void (*fn)(void *arg), void *arg);
/* Give back a suspended fiber's slot; its continuation is dropped. */
void fiber_destroy(Fiber *f);
/* Abandon a suspended program and every fiber it created (including tasks
 * suspended inside a frame wait). Call from outside that program's tree. */
void fiber_destroy_tree(Fiber *f);

/* The fiber representing the thread's original stack. */
Fiber *fiber_main(void);

/* Suspend the current fiber and run `to`. */
void fiber_switch(Fiber *to);

/* The fiber running now (fiber_main() before any switch). */
Fiber *fiber_current(void);

/* Every fiber but the thread's own, as bytes: which slots are in use, what
 * each runs and where it stopped. Taken and restored on the thread's own
 * stack while every other fiber is suspended. fiber_snapshot writes into
 * `out` when `room` is enough and answers the bytes it needs either way
 * (0: not on the thread's own stack). A restore puts every slot back as the
 * snapshot had it, the slots it didn't use dropped; it is good only in a
 * process whose fiber stacks are where the snapshot's were (port/arena.h),
 * and false, changing nothing, otherwise or for bytes that aren't one.
 * fiber_restorable answers whether fiber_restore would succeed, changing
 * nothing, for a caller that has more to restore before the fibers switch. */
size_t fiber_snapshot(uint8_t *out, size_t room);

/* The fiber stacks' slots, how big each is, and the most of slot `slot`'s
 * stack any fiber has used so far: its stacks start zeroed (port/arena.h)
 * and grow down, so the lowest byte not zero marks the deepest a fiber went
 * (ct_native --measure). */
unsigned fiber_slots(void);
size_t fiber_stack_size(void);
size_t fiber_stack_high_water(unsigned slot);
bool fiber_restorable(const uint8_t *in, size_t len);
bool fiber_restore(const uint8_t *in, size_t len);

#endif
