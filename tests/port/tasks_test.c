/* Checks the tasks as coroutines (game/core/tasks.c) on made-up tasks: the
 * turns in slot order once a frame, a wait deep in a task's own calls, a task
 * that ends, one stopped where it waits, a slot started again, a turn of one
 * slot taken in the caller's order, and the tasks ended. Exits non-zero on
 * failure.
 *
 *   tasks_test */
#include <stdio.h>
#include <string.h>

#include "core/tasks.h"

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

static Tasks tasks;
static char order[128];
static size_t order_len;
static void note(char c)
{
    if (order_len + 1 < sizeof order)
        order[order_len++] = c;
    order[order_len] = 0;
}

/* A wait from deep in the task's calls, with state on its stack across it. */
static unsigned wait_deep(unsigned depth)
{
    volatile unsigned here = depth * 3u;
    if (depth == 0) {
        tasks_wait_a_frame(&tasks);
        return here;
    }
    return wait_deep(depth - 1) + here;
}

static unsigned deep_sum;

/* A: a turn a frame, forever. B: three turns, then it ends. C: deep waits. */
static void task_a(GameState *g, Platform *pf, void *arg)
{
    (void)g, (void)pf, (void)arg;
    for (;;) {
        note('a');
        tasks_wait_a_frame(&tasks);
    }
}

static void task_b(GameState *g, Platform *pf, void *arg)
{
    (void)g, (void)pf;
    char c = *(const char *)arg;
    for (int i = 0; i < 3; i++) {
        note(c);
        tasks_wait_a_frame(&tasks);
    }
}

static void task_c(GameState *g, Platform *pf, void *arg)
{
    (void)g, (void)pf, (void)arg;
    for (;;) {
        note('c');
        deep_sum += wait_deep(5);
    }
}

static void frame(void)
{
    tasks_run_a_frame(&tasks);
    note('|');
}

int main(void)
{
    static const char b = 'b', d = 'd';
    tasks_init(&tasks, NULL, NULL);
    tasks_start(&tasks, 2, task_c, NULL);
    tasks_start(&tasks, 0, task_a, NULL);
    tasks_start(&tasks, 1, task_b, (void *)&b);
    for (int i = 0; i < 4; i++)
        frame();
    CHECK(!strcmp(order, "abc|abc|abc|ac|"), "turns in slot order, B ending after three: %s", order);
    CHECK(!tasks_running(&tasks, 1), "B ended");
    CHECK(deep_sum == 3u * (0 + 3 + 6 + 9 + 12 + 15), "C's stack kept across its waits: %u", deep_sum);

    order_len = 0;
    tasks_stop(&tasks, 0);
    tasks_start(&tasks, 1, task_b, (void *)&d);
    frame();
    frame();
    CHECK(!strcmp(order, "dc|dc|"), "A stopped where it waited, B's slot started again: %s", order);

    order_len = 0;
    tasks_take_a_turn(&tasks, 2);
    tasks_take_a_turn(&tasks, 1);
    tasks_take_a_turn(&tasks, 0);
    CHECK(!strcmp(order, "cd"), "a turn of each slot in the caller's order, none for a stopped one: %s", order);

    tasks_end(&tasks);
    order_len = 0;
    frame();
    CHECK(!strcmp(order, "|"), "the tasks ended: %s", order);

    if (failures)
        printf("tasks_test: %d failure(s)\n", failures);
    else
        printf("tasks_test: ok\n");
    return failures ? 1 : 0;
}
