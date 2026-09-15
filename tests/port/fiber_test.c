/* Checks the fiber switches the native runtime relies on (runtime/port/fiber.c):
 * ping-pong between a fiber and the main stack with state kept in locals on
 * both sides, deep stacks, switching between two fibers directly, and freeing
 * a fiber suspended mid-stack. Exits non-zero on failure.
 *
 *   fiber_test */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "port/fiber.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ---- ping-pong ---------------------------------------------------------------- */

static uint64_t shared;

static void counter_body(void *arg)
{
    uint64_t step = *(uint64_t *)arg;
    uint64_t mine = 0;
    double acc = 0.5;
    for (;;) {
        mine += step;
        acc *= 1.0000001;
        shared = mine ^ (uint64_t)(acc * 1000.0);
        fiber_switch(fiber_main());
    }
}

static void test_ping_pong(void)
{
    uint64_t step = 3;
    Fiber *f = fiber_create(counter_body, &step);
    uint64_t expect = 0;
    double acc = 0.5;
    int bad = 0;
    for (int i = 0; i < 200000; i++) {
        fiber_switch(f);
        expect += step;
        acc *= 1.0000001;
        if (shared != (expect ^ (uint64_t)(acc * 1000.0)))
            bad++;
    }
    CHECK(bad == 0, "ping-pong: %d of 200000 switches saw the wrong state", bad);
    fiber_destroy(f);
}

/* ---- deep stack --------------------------------------------------------------- */

static uint32_t deep_result;

static uint32_t recurse(int depth)
{
    uint8_t frame[256];
    memset(frame, depth & 0xFF, sizeof frame);
    uint32_t below = depth ? recurse(depth - 1) : 0;
    uint32_t sum = 0;
    for (size_t i = 0; i < sizeof frame; i++)
        sum += frame[i];
    return below + sum;
}

static void deep_body(void *arg)
{
    int depth = *(int *)arg;
    for (;;) {
        deep_result = recurse(depth);
        fiber_switch(fiber_main());
    }
}

static void test_deep_stack(void)
{
    int depth = 1000;   /* about 300 KiB of the 512 KiB stack */
    Fiber *f = fiber_create(deep_body, &depth);
    uint32_t expect = 0;
    for (int d = 0; d <= depth; d++)
        expect += 256u * (uint32_t)(d & 0xFF);
    for (int i = 0; i < 3; i++) {
        deep_result = 0;
        fiber_switch(f);
        CHECK(deep_result == expect, "deep stack: got %u, expected %u", (unsigned)deep_result, (unsigned)expect);
    }
    fiber_destroy(f);
}

/* ---- fiber to fiber ----------------------------------------------------------- */

static Fiber *fa, *fb;
static char trail[64];
static size_t trail_len;

static void note(char c)
{
    if (trail_len + 1 < sizeof trail)
        trail[trail_len++] = c;
}

static void b_body(void *arg)
{
    (void)arg;
    for (;;) {
        note('b');
        fiber_switch(fa);
        note('B');
        fiber_switch(fiber_main());
    }
}

static void a_body(void *arg)
{
    (void)arg;
    for (;;) {
        note('a');
        fiber_switch(fb);
        note('A');
        fiber_switch(fb);
    }
}

static void test_fiber_to_fiber(void)
{
    fa = fiber_create(a_body, NULL);
    fb = fiber_create(b_body, NULL);
    for (int i = 0; i < 2; i++) {
        fiber_switch(fa);
        note('m');
    }
    trail[trail_len] = 0;
    CHECK(!strcmp(trail, "abABmabABm"), "fiber to fiber: order %s", trail);
    /* Both fibers are left suspended mid-body. */
    fiber_destroy(fa);
    fiber_destroy(fb);
}

/* ---- abandoned fibers ------------------------------------------------------------ */

static int resumed;

static void abandoned_body(void *arg)
{
    (void)arg;
    fiber_switch(fiber_main());
    resumed = 1;   /* never reached: the fiber is destroyed while suspended */
    fiber_switch(fiber_main());
}

static void test_abandon(void)
{
    for (int i = 0; i < 1000; i++) {
        Fiber *f = fiber_create(abandoned_body, NULL);
        fiber_switch(f);
        fiber_destroy(f);
    }
    CHECK(!resumed, "an abandoned fiber resumed");
    fiber_destroy(fiber_main());   /* a no-op */
}

int main(void)
{
    test_ping_pong();
    test_deep_stack();
    test_fiber_to_fiber();
    test_abandon();
    if (failures) {
        printf("fiber_test: %d failures\n", failures);
        return 1;
    }
    printf("fiber_test: all checks passed\n");
    return 0;
}
