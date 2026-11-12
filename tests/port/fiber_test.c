/* Checks the fiber switches the native runtime relies on (runtime/port/fiber.c):
 * ping-pong between a fiber and the main stack with state kept in locals on
 * both sides, deep stacks, switching between two fibers directly, freeing a
 * fiber suspended mid-stack, and snapshots: fibers suspended deep in their
 * calls taken as bytes and put back, which must go on exactly as they did
 * from the snapshot. Exits non-zero on failure.
 *
 *   fiber_test */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

/* ---- snapshots ------------------------------------------------------------------ */

static uint32_t produced;

/* A generator whose whole state is on its stack, deep in its calls: each
 * switch back leaves the next value of a sequence that depends on every
 * frame's locals. */
static uint32_t generate(uint32_t seed, int depth)
{
    if (depth < 0)
        return seed;
    volatile uint32_t mine = seed * 2654435761u + (uint32_t)depth;
    double scale = 1.0 + depth / 64.0;
    if (depth)
        return generate(mine, depth - 1) ^ mine;
    for (;;) {
        mine = mine * 1103515245u + 12345u;
        scale *= 1.0000003;
        produced = mine ^ (uint32_t)(scale * 4096.0);
        fiber_switch(fiber_main());
    }
}

static void generator_body(void *arg)
{
    generate(*(uint32_t *)arg, 40);
}

static void relay_body(void *arg)
{
    Fiber *gen = arg;
    uint32_t total = 0;
    for (;;) {
        fiber_switch(gen);
        total = total * 31u + produced;
        produced = total;
        fiber_switch(fiber_main());
    }
}

static uint8_t *take(size_t *len)
{
    *len = fiber_snapshot(NULL, 0);
    uint8_t *bytes = malloc(*len);
    CHECK(bytes && fiber_snapshot(bytes, *len) == *len, "snapshot: could not take one");
    return bytes;
}

static void test_snapshots(void)
{
    uint32_t seed = 7;
    Fiber *gen = fiber_create(generator_body, &seed);
    for (int i = 0; i < 10; i++)
        fiber_switch(gen);
    size_t len;
    uint8_t *snap = take(&len);
    uint32_t first[50];
    for (int i = 0; i < 50; i++) {
        fiber_switch(gen);
        first[i] = produced;
    }
    /* go further, and make and drop another fiber, then put the snapshot back */
    for (int i = 0; i < 1000; i++)
        fiber_switch(gen);
    Fiber *other = fiber_create(generator_body, &seed);
    fiber_switch(other);
    CHECK(fiber_restore(snap, len), "snapshot: restore refused");
    int bad = 0;
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 50; i++) {
            fiber_switch(gen);
            bad += produced != first[i];
        }
        CHECK(fiber_restore(snap, len), "snapshot: second restore refused");
    }
    CHECK(bad == 0, "snapshot: %d of 150 values differ after restores", bad);
    CHECK(!fiber_restore(snap, len - 1), "snapshot: a short one was taken");
    free(snap);
    fiber_destroy(gen);

    /* two fibers that switch to each other, one created after the snapshot's
     * own fibers were gone */
    Fiber *g2 = fiber_create(generator_body, &seed);
    Fiber *relay = fiber_create(relay_body, g2);
    for (int i = 0; i < 5; i++)
        fiber_switch(relay);
    snap = take(&len);
    uint32_t produced_then = produced;   /* the test's own state, outside the fibers */
    uint32_t second[20];
    for (int i = 0; i < 20; i++) {
        fiber_switch(relay);
        second[i] = produced;
    }
    fiber_destroy(relay);
    fiber_destroy(g2);
    CHECK(fiber_restore(snap, len), "snapshot: restore of a pair refused");
    produced = produced_then;
    bad = 0;
    for (int i = 0; i < 20; i++) {
        fiber_switch(relay);
        bad += produced != second[i];
    }
    CHECK(bad == 0, "snapshot: %d of 20 relayed values differ", bad);
    free(snap);
    fiber_destroy(relay);
    fiber_destroy(g2);
    fiber_destroy(other);
}

int main(void)
{
    test_ping_pong();
    test_deep_stack();
    test_fiber_to_fiber();
    test_abandon();
    test_snapshots();
    for (int i = 0; i < 10; i++) {
        Fiber *f = fiber_create(abandoned_body, NULL);
        fiber_switch(f);
        fiber_destroy_tree(f);
    }
    CHECK(!resumed, "tree teardown resumed an abandoned stack");
    test_ping_pong();
    if (failures) {
        printf("fiber_test: %d failures\n", failures);
        return 1;
    }
    printf("fiber_test: all checks passed\n");
    return 0;
}
