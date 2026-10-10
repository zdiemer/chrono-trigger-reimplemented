/* Checks the timing service's host backend (runtime/platform/native/timing.c)
 * on a made-up game: the frame's order (the vblank handler, the main thread to
 * its wait, the line handler), the handlers following what the game enables,
 * a wait deep in the main thread's stack, a wait from a coroutine the main
 * thread runs (as the menus' tasks are), the acknowledges answering inside
 * their own handler, the budget and the line wait, the frame parity, the
 * picture's end, the generator's replay, and the pads. Exits non-zero on
 * failure.
 *
 *   timing_test */
#include <stdio.h>
#include <string.h>

#include "platform/native/host.h"

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

/* What ran, a letter a part: V the vblank handler, M the main thread, L the
 * line handler, | a frame's end. */
static char order[256];
static size_t order_len;
static void note(char c)
{
    if (order_len + 1 < sizeof order)
        order[order_len++] = c;
}

static int main_frames;
static bool acks_ok = true;

/* A wait for the next frame from deep in the main thread's stack, as an event
 * command's is, with state on the stack across it. */
static unsigned wait_deep(Platform *pf, unsigned depth)
{
    volatile unsigned here = depth * 7u;
    if (depth == 0) {
        tm_wait_frame(pf, PF_CALL(0, 0));
        return here;
    }
    return wait_deep(pf, depth - 1) + here;
}

static void game_main(Platform *pf, void *ctx)
{
    (void)ctx;
    /* the first frame: the handlers come on, the picture ending after line 211 */
    tm_set_line(pf, PF_AT(0), 211);
    tm_enable_interrupts(pf, PF_AT(0), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
    for (;;) {
        note('M');
        main_frames++;
        if (tm_ack_vblank(pf, PF_AT(0)) || tm_ack_line_irq(pf, PF_AT(0)))
            acks_ok = false;
        if (main_frames == 3)   /* a load with the handlers off */
            tm_enable_interrupts(pf, PF_AT(0), TM_INT_AUTO_JOYPAD);
        if (main_frames == 4)
            tm_enable_interrupts(pf, PF_AT(0), TM_INT_VBLANK);
        unsigned sum = wait_deep(pf, 20);
        CHECK(sum == 7u * (20u * 21u / 2u), "the stack under a wait kept its state (%u)", sum);
    }
}

static void game_vblank(Platform *pf, void *ctx)
{
    (void)ctx;
    note('V');
    if (!(tm_ack_vblank(pf, PF_AT(0)) & TM_VBLANK_PENDING) || tm_ack_line_irq(pf, PF_AT(0)))
        acks_ok = false;
    tm_wait_frame(pf, PF_CALL(0, 0));   /* a handler never waits: it goes on */
}

static void game_line(Platform *pf, void *ctx)
{
    (void)ctx;
    note('L');
    if (!(tm_ack_line_irq(pf, PF_AT(0)) & TM_LINE_IRQ_PENDING) || tm_ack_vblank(pf, PF_AT(0)))
        acks_ok = false;
}

/* A second game: its main thread runs a task, a coroutine of its own, which
 * waits a frame; the next frame goes on in the task, which hands back to the
 * main thread, which then waits. */
static Fiber *task_fiber, *main_fiber_of_game;

static void task_body(void *arg)
{
    Platform *p = arg;
    note('T');
    tm_wait_frame(p, PF_CALL(0, 0));
    note('t');
    fiber_switch(main_fiber_of_game);
    for (;;)
        fiber_switch(main_fiber_of_game);
}

static void tasks_main(Platform *p, void *ctx)
{
    (void)ctx;
    main_fiber_of_game = fiber_current();
    task_fiber = fiber_create(task_body, p);
    fiber_switch(task_fiber);
    for (;;) {
        note('M');
        tm_wait_frame(p, PF_CALL(0, 0));
    }
}

/* A third game: its main thread brings the screen back after a load with
 * the handlers off, waiting a frame and then turning them on, as the field's
 * screen setup does; the line handler runs in the frame they come on. */
static void bring_back_main(Platform *p, void *ctx)
{
    (void)ctx;
    note('M');
    tm_enable_interrupts(p, PF_AT(0), TM_INT_AUTO_JOYPAD);
    tm_wait_frame_then_enable(p, PF_CALL(0, 0), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD, 211);
    note('m');
    for (;;) {
        note('M');
        tm_wait_frame(p, PF_CALL(0, 0));
    }
}

static Platform pf;

int main(void)
{
    TmHostProgram program = { game_main, game_vblank, game_line, NULL };
    tm_host_start(&pf, &program, 12345);

    /* frame 1: no handlers yet; the main thread turns them on, and the line
     * handler runs in the same frame, after it */
    tm_host_frame(&pf);
    note('|');
    CHECK(tm_host_picture_lines(&pf) == 212, "the picture ends after line 211 (%u)", tm_host_picture_lines(&pf));
    tm_host_frame(&pf);   /* 2: all three */
    note('|');
    tm_host_frame(&pf);   /* 3: the vblank handler, then the main thread turns them off: no line handler */
    note('|');
    CHECK(tm_host_picture_lines(&pf) == 0, "no line handler, the whole picture (%u)", tm_host_picture_lines(&pf));
    tm_host_frame(&pf);   /* 4: the main thread alone; it turns the vblank handler on */
    note('|');
    tm_host_frame(&pf);   /* 5: vblank and main */
    note('|');
    order[order_len] = 0;
    CHECK(strcmp(order, "ML|VML|VM|M|VM|") == 0, "the frames' order: %s", order);
    CHECK(main_frames == 5, "a main thread pass a frame (%d)", main_frames);
    CHECK(acks_ok, "the acknowledges are pending inside their own handler only");
    CHECK(tm_host_frames(&pf) == 5, "five frames (%u)", tm_host_frames(&pf));

    /* the frame's parity follows the frames */
    bool odd = tm_odd_frame(&pf, PF_AT(0));
    tm_host_frame(&pf);
    CHECK(tm_odd_frame(&pf, PF_AT(0)) != odd, "the parity alternates");

    /* a host has time for all the deferrable work, and no line to wait for */
    CHECK(tm_frame_time_left(&pf, PF_AT(0), 0), "time left at any line");
    tm_wait_line(&pf, PF_CALL(0, 0), 64);

    /* the generator replays from its seed */
    uint16_t first[8], again[8];
    for (int i = 0; i < 8; i++)
        first[i] = tm_entropy(&pf, PF_AT(0));
    static Platform replay;
    tm_host_start(&replay, &program, 12345);
    for (int i = 0; i < 8; i++)
        again[i] = tm_entropy(&replay, PF_AT(0));
    CHECK(memcmp(first, again, sizeof first) == 0, "the same seed draws the same words");
    CHECK(first[0] != first[1] || first[1] != first[2], "the words vary");

    /* the pads, as the driver sets them */
    CHECK(in_ready(&pf, PF_AT(0)), "the controllers are read");
    in_host_set(&pf, 1, IN_BUTTON_A | IN_BUTTON_START);
    CHECK(in_read(&pf, PF_AT(0), 1) == (IN_BUTTON_A | IN_BUTTON_START), "pad 2 as set");
    CHECK(in_read_low(&pf, PF_AT(0), 1) == (uint8_t)IN_BUTTON_A, "its low half");
    CHECK(in_read_high(&pf, PF_AT(0), 1) == (uint8_t)(IN_BUTTON_START >> 8), "its high half");
    CHECK(in_read(&pf, PF_AT(0), 0) == 0, "pad 1 untouched");

    /* a wait from the task resumes the task, not the main thread */
    static Platform tasks;
    TmHostProgram with_tasks = { tasks_main, NULL, NULL, NULL };
    order_len = 0;
    tm_host_start(&tasks, &with_tasks, 1);
    for (int i = 0; i < 3; i++) {
        tm_host_frame(&tasks);
        note('|');
    }
    order[order_len] = 0;
    CHECK(strcmp(order, "T|tM|M|") == 0, "a task's wait resumes the task: %s", order);

    /* the screen brought back: a frame with no handlers, then they come on */
    static Platform back;
    TmHostProgram bring_back = { bring_back_main, game_vblank, game_line, NULL };
    order_len = 0;
    tm_host_start(&back, &bring_back, 1);
    for (int i = 0; i < 3; i++) {
        tm_host_frame(&back);
        note('|');
    }
    order[order_len] = 0;
    CHECK(strcmp(order, "M|mML|VML|") == 0, "the handlers come on after the wait: %s", order);
    CHECK(tm_host_picture_lines(&back) == 212, "the picture ends after line 211 again (%u)", tm_host_picture_lines(&back));

    printf("timing_test: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
