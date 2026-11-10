/* Checks the timing service's host backend (runtime/platform/native/timing.c)
 * on a made-up game: the frame's order (the vblank handler, the main thread to
 * its wait, the line handler), the handlers following what the game enables,
 * a wait deep in the main thread's stack, a wait from a coroutine the main
 * thread runs (as the menus' tasks are), the acknowledges answering inside
 * their own handler, the budget and the line wait, the frame parity, the
 * picture's end, the generator's replay, the pads, a wait routine that looks
 * again after the handlers until its request is answered, a wait for a
 * number of frames, a send to VRAM from the main thread, and the time model's
 * lag frames (the main thread sitting out the frames its charged work took,
 * at its next wait or where it stands).
 * Exits non-zero on failure.
 *
 *   timing_test */
#include <stdio.h>
#include <string.h>

#include "platform/native/host.h"
#include "platform/time_model.h"
#include "platform/observe.h"

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

/* A fourth game: its main thread asks the vblank handler for a frame and
 * looks again after the handlers have run until the handler has done it, as
 * the battle's wait routines do; then waits three frames in one call, and
 * sends to VRAM from the main thread. */
static uint8_t asked;
static int asking_frames_done;
static const uint8_t tiles[4] = { 0x12, 0x34, 0x56, 0x78 };

static void asking_main(Platform *p, void *ctx)
{
    (void)ctx;
    tm_enable_interrupts(p, PF_AT(0), TM_INT_VBLANK);
    for (;;) {
        note('M');
        asked++;
        while (asked)
            tm_wait_for_handlers(p, PF_LOOP(0, 0));
        note('m');
        tm_wait_frames(p, PF_CALL(0, 0), 3);
        note('w');
        vid_send_vram_and_wait(p, PF_CALL(0, 0), 0x7FFF, (PfData){ .ptr = tiles }, sizeof tiles);
        asking_frames_done++;
    }
}

static void asking_vblank(Platform *p, void *ctx)
{
    (void)p;
    (void)ctx;
    note('V');
    asked = 0;
}

/* A fifth game: its main thread's work is charged three frames of the
 * original's time and a little (an upload, and the work around it, the game's
 * own count through the program's `owed`), then waits; the handler runs on
 * meanwhile. */
static uint32_t game_owes;

static uint32_t lagging_owed(void *ctx)
{
    (void)ctx;
    uint32_t owed = game_owes;
    game_owes = 0;
    return owed;
}

static void lagging_main(Platform *p, void *ctx)
{
    (void)ctx;
    tm_enable_interrupts(p, PF_AT(0), TM_INT_VBLANK);
    for (int pass = 0;; pass++) {
        note('M');
        if (pass == 0) {
            tm_host_charge(p, 2 * TIME_FRAME_CLOCKS);
            game_owes = TIME_FRAME_CLOCKS + 1;
        }
        tm_wait_frame(p, PF_CALL(0, 0));
    }
}

/* A sixth: the same work owed, sat out where the main thread stands (before
 * a song's start, which the sound driver's time runs on from), then a quarter
 * of a frame more, which stays owed. */
static void sitting_out_main(Platform *p, void *ctx)
{
    (void)ctx;
    tm_enable_interrupts(p, PF_AT(0), TM_INT_VBLANK);
    for (int pass = 0;; pass++) {
        note('M');
        if (pass == 0) {
            tm_host_charge(p, 2 * TIME_FRAME_CLOCKS);
            game_owes = TIME_FRAME_CLOCKS + 1;
            tm_host_sit_out_owed(p);
            note('s');
            tm_host_charge(p, TIME_FRAME_CLOCKS / 4);
            tm_host_sit_out_owed(p);
            note('t');
        }
        tm_wait_frame(p, PF_CALL(0, 0));
    }
}

/* A seventh: a handler whose work takes a quarter of each frame, and a main
 * thread whose work takes a frame and a half: the frame left it is three
 * quarters, so its work fills two. */
static void busy_vblank(Platform *p, void *ctx)
{
    asking_vblank(p, ctx);
    game_owes += TIME_FRAME_CLOCKS / 4;
}

static void beside_handlers_main(Platform *p, void *ctx)
{
    (void)ctx;
    tm_enable_interrupts(p, PF_AT(0), TM_INT_VBLANK);
    for (int pass = 0;; pass++) {
        note('M');
        if (pass == 1)
            game_owes = TIME_FRAME_CLOCKS * 3 / 2;
        tm_wait_frame(p, PF_CALL(0, 0));
    }
}

/* An eighth: two frames of work done with the vblank handler off, then the
 * handler turned on: the two frames pass with it off, as the original's
 * setup ran. */
static void setting_up_main(Platform *p, void *ctx)
{
    (void)ctx;
    for (int pass = 0;; pass++) {
        note('M');
        if (pass == 0) {
            game_owes = 2 * TIME_FRAME_CLOCKS;
            tm_enable_interrupts(p, PF_AT(0), TM_INT_VBLANK);
            note('e');
        }
        tm_wait_frame(p, PF_CALL(0, 0));
    }
}

/* Handler service charges during a load must not accumulate as work for the
 * main thread to sit out after it resumes. Each handler takes an eighth of
 * a frame; the main thread's next half-frame still fits alongside them. */
static unsigned charged_main_calls;

static void charging_handler(Platform *p, void *ctx)
{
    (void)ctx;
    tm_host_charge(p, TIME_FRAME_CLOCKS / 8);
}

static void beside_charges_main(Platform *p, void *ctx)
{
    (void)ctx;
    tm_enable_interrupts(p, PF_AT(0), TM_INT_VBLANK | TM_INT_LINE);
    for (;;) {
        if (charged_main_calls == 0)
            tm_host_charge(p, 2 * TIME_FRAME_CLOCKS);
        else if (charged_main_calls == 1)
            tm_host_charge(p, TIME_FRAME_CLOCKS / 2);
        charged_main_calls++;
        tm_wait_frame(p, PF_AT(0));
    }
}


/* Supported native escapes bypass the emulated bus, but their transfers
 * still take time. A zero length means a full 64 KiB transfer. */
static unsigned dma_main_calls;
static uint32_t dma_reads, dma_writes;
static uint8_t dma_read(void *ctx, uint32_t at) { (void)ctx; (void)at; dma_reads++; return 0x5A; }
static void dma_write(void *ctx, uint32_t at, uint8_t v) { (void)ctx; (void)at; (void)v; dma_writes++; }
static void dma_main(Platform *p, void *ctx)
{
    (void)ctx;
    for (;;) {
        if (!dma_main_calls) {
            VidSnesTransfer transfer = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = 0x7E0000, .size = 0 };
            vid_snes_copy(p, PF_AT(0), &transfer);
            transfer.dest = VID_SNES_WRAM_DATA;
            vid_snes_copy(p, PF_AT(0), &transfer);
            transfer.dest = VID_SNES_VRAM_DATA_LOW;
            transfer.target_reg = VID_SNES_VRAM_ADDRESS;
            transfer.mode_from_data = true;
            transfer.mode = 1;
            vid_snes_copy(p, PF_AT(0), &transfer);
        }
        dma_main_calls++;
        tm_wait_frame(p, PF_AT(0));
    }
}

/* The game's budget reopens after NMI, at line 240, and closes after the
 * frame wraps. Pending game work and service work share this account. */
static unsigned budget_checks;
static void budget_main(Platform *p, void *ctx)
{
    (void)ctx;
    enum { OPENS = (240 - 225) * 1364, CLOSES = (262 - 225 + 72) * 1364 };
    tm_enable_interrupts(p, PF_AT(0), TM_INT_VBLANK | TM_INT_LINE);
    CHECK(!tm_frame_time_left(p, PF_AT(0), 72), "NMI has not reopened the field budget");
    CHECK(!tm_frame_time_left(p, PF_AT(0), 225), "line 225 is not an unlimited budget");
    CHECK(tm_frame_time_left(p, PF_AT(0), 240), "line 240 leaves the budget open");
    tm_host_charge(p, OPENS - 1);
    CHECK(!tm_frame_time_left(p, PF_AT(0), 72), "one clock before the window opens");
    tm_host_charge(p, 1);
    CHECK(tm_frame_time_left(p, PF_AT(0), 72), "the window opens at line 240");
    game_owes = CLOSES - OPENS - 1;
    CHECK(tm_frame_time_left(p, PF_AT(0), 72), "one clock remains before the deadline");
    CHECK(tm_frame_time_left(p, PF_AT(0), 72), "reading the account twice does not charge it twice");
    tm_host_charge(p, 1);
    CHECK(!tm_frame_time_left(p, PF_AT(0), 72), "the deadline ends this frame's script turns");
    budget_checks++;
    tm_wait_frame(p, PF_AT(0));
    CHECK(tm_frame_time_left(p, PF_AT(0), 72), "waiting renews the main thread's budget");
    CHECK(!tm_frame_time_left(p, PF_AT(0), 0), "handlers consume the earliest deadline");
    tm_host_charge(p, CLOSES - TIME_FRAME_CLOCKS / 4);
    CHECK(!tm_frame_time_left(p, PF_AT(0), 72), "handler service work counts toward the deadline");
    tm_host_charge(p, TIME_FRAME_CLOCKS);
    CHECK(!tm_frame_time_left(p, PF_AT(0), 72), "a load must yield before another frame's queued work");
    budget_checks++;
    for (;;)
        tm_wait_frame(p, PF_AT(0));
}

static Platform pf;

/* The scene's horizontal BG2 wave changes one coordinate on each line.
 * The other layers and BG2's vertical position must retain their scroll. */
static uint8_t wave_table_byte(void *ctx, uint32_t at)
{
    const uint8_t *table = ctx;
    return at < 20 ? table[at] : 0;
}

static void test_second_layer_wave(void)
{
    uint8_t table[20] = { 0x82, 16, 0, 0 };
    table[16] = 3;
    table[18] = 0xFF;
    table[19] = 3;
    VidObserver *ob = vid_obs_create(wave_table_byte, table);
    CHECK(ob != NULL, "video observer allocated for the wave");
    if (!ob)
        return;
    vid_obs_scroll(ob, VID_BG1, 71, 83, VID_SCROLL_H | VID_SCROLL_V);
    vid_obs_scroll(ob, VID_BG2, 17, 29, VID_SCROLL_H | VID_SCROLL_V);
    VidLineEffect effect = { .target = VID_LINE_SCROLL_BG2_H, .table = PF_GAME(0),
                             .indirect = true, .indirect_bank = 0 };
    vid_obs_line_effect(ob, 1, &effect);
    vid_obs_enable_line_effects(ob, 1u << 1);
    vid_obs_frame_start(ob);
    for (unsigned line = 0; line <= 224; line++)
        vid_obs_line(ob, (uint16_t)line);
    const VidFrame *frame = vid_obs_frame(ob);
    CHECK(frame->span_count == 2, "two horizontal wave positions (%u)", frame->span_count);
    if (frame->span_count == 2) {
        CHECK(frame->spans[0].line == 1 && frame->spans[0].state.bg[1].hofs == 3,
              "the wave's first line reads its first word");
        CHECK(frame->spans[1].line == 2 && frame->spans[1].state.bg[1].hofs == 1023,
              "the next line reads both bytes of its next word");
    }
    for (unsigned i = 0; i < frame->span_count; i++) {
        const VidLineState *state = &frame->spans[i].state;
        CHECK(state->bg[0].hofs == 71 && state->bg[0].vofs == 83 && state->bg[1].vofs == 29,
              "the horizontal wave preserves other scroll coordinates");
    }
    CHECK(vid_obs_unmodelled(ob) == 0, "the wave has a native video implementation");
    vid_obs_destroy(ob);
}

int main(void)
{
    test_second_layer_wave();
    TmHostProgram program = { game_main, game_vblank, game_line, NULL, NULL, NULL, NULL };
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
    tm_wait_line_drawn(&pf, PF_LOOP(0, 0));

    /* the generator replays from its seed */
    TmEntropy first[8], again[8];
    for (int i = 0; i < 8; i++)
        first[i] = tm_entropy(&pf, PF_AT(0), TM_ENTROPY_WORDS);
    static Platform replay;
    tm_host_start(&replay, &program, 12345);
    for (int i = 0; i < 8; i++)
        again[i] = tm_entropy(&replay, PF_AT(0), TM_ENTROPY_WORDS);
    CHECK(memcmp(first, again, sizeof first) == 0, "the same seed draws the same noise");
    CHECK(first[0].value[0] != first[0].value[1] || first[0].value[1] != first[1].value[0], "the noise varies");
    TmEntropy bytes = tm_entropy(&pf, PF_AT(0), TM_ENTROPY_BYTES);
    CHECK(bytes.value[0] <= 0xFF && bytes.value[3] <= 0xFF, "bytes are bytes");

    /* the pads, as the driver sets them */
    CHECK(in_ready(&pf, PF_AT(0)), "the controllers are read");
    in_host_set(&pf, 1, IN_BUTTON_A | IN_BUTTON_START);
    CHECK(in_read(&pf, PF_AT(0), 1) == (IN_BUTTON_A | IN_BUTTON_START), "pad 2 as set");
    CHECK(in_read_low(&pf, PF_AT(0), 1) == (uint8_t)IN_BUTTON_A, "its low half");
    CHECK(in_read_high(&pf, PF_AT(0), 1) == (uint8_t)(IN_BUTTON_START >> 8), "its high half");
    CHECK(in_read(&pf, PF_AT(0), 0) == 0, "pad 1 untouched");

    /* a wait from the task resumes the task, not the main thread */
    static Platform tasks;
    TmHostProgram with_tasks = { tasks_main, NULL, NULL, NULL, NULL, NULL, NULL };
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
    TmHostProgram bring_back = { bring_back_main, game_vblank, game_line, NULL, NULL, NULL, NULL };
    order_len = 0;
    tm_host_start(&back, &bring_back, 1);
    for (int i = 0; i < 3; i++) {
        tm_host_frame(&back);
        note('|');
    }
    order[order_len] = 0;
    CHECK(strcmp(order, "M|mML|VML|") == 0, "the handlers come on after the wait: %s", order);
    CHECK(tm_host_picture_lines(&back) == 212, "the picture ends after line 211 again (%u)", tm_host_picture_lines(&back));

    /* the battle's waits: the handler answers the main thread's request; a
     * number of frames is that many; the VRAM a send names gets its bytes,
     * the address wrapping round */
    static Platform asking;
    TmHostProgram ask = { asking_main, asking_vblank, NULL, NULL, NULL, NULL, NULL };
    order_len = 0;
    vid_host_start(&asking, NULL, NULL, NULL);
    tm_host_start(&asking, &ask, 1);
    for (int i = 0; i < 6; i++) {
        tm_host_frame(&asking);
        note('|');
    }
    order[order_len] = 0;
    CHECK(strcmp(order, "M|Vm|V|V|VwM|Vm|") == 0, "the handler's answer and the frames waited: %s", order);
    CHECK(asking_frames_done == 1, "one round sent (%d)", asking_frames_done);
    const uint8_t *vram = vid_host_picture(&asking)->vram;
    CHECK(vram[0xFFFE] == 0x12 && vram[0xFFFF] == 0x34 && vram[0] == 0x56 && vram[1] == 0x78,
          "the bytes in VRAM from word $7FFF on, wrapping");
    vid_host_stop(&asking);

    /* three frames and a clock owed: three lag frames, in which the handler
     * runs and the main thread doesn't */
    static Platform lagging;
    TmHostProgram lag = { lagging_main, asking_vblank, NULL, NULL, lagging_owed, NULL, NULL };
    order_len = 0;
    tm_host_start(&lagging, &lag, 1);
    for (int i = 0; i < 6; i++) {
        tm_host_frame(&lagging);
        note('|');
    }
    order[order_len] = 0;
    CHECK(strcmp(order, "M|V|V|V|VM|VM|") == 0, "the main thread sits out the frames it owes: %s", order);
    CHECK(tm_host_lag_frames(&lagging) == 3, "three lag frames (%u)", tm_host_lag_frames(&lagging));

    /* the same three frames sat out where the main thread stands: it goes on
     * in the third frame after, where it was; a quarter frame stays owed */
    static Platform sitting;
    TmHostProgram sit = { sitting_out_main, asking_vblank, NULL, NULL, lagging_owed, NULL, NULL };
    order_len = 0;
    tm_host_start(&sitting, &sit, 1);
    for (int i = 0; i < 5; i++) {
        tm_host_frame(&sitting);
        note('|');
    }
    order[order_len] = 0;
    CHECK(strcmp(order, "M|V|V|Vst|VM|") == 0, "the main thread sits out what it owes where it stands: %s", order);
    CHECK(tm_host_lag_frames(&sitting) == 2, "two lag frames (%u)", tm_host_lag_frames(&sitting));

    /* the handlers' time comes off the frame the main thread's work has */
    static Platform beside;
    TmHostProgram busy = { beside_handlers_main, busy_vblank, NULL, NULL, lagging_owed, NULL, NULL };
    order_len = 0;
    game_owes = 0;
    tm_host_start(&beside, &busy, 1);
    for (int i = 0; i < 5; i++) {
        tm_host_frame(&beside);
        note('|');
    }
    order[order_len] = 0;
    CHECK(strcmp(order, "M|VM|V|V|VM|") == 0, "the work fills the frames the handlers leave: %s", order);
    CHECK(tm_host_lag_frames(&beside) == 2, "two lag frames (%u)", tm_host_lag_frames(&beside));

    /* what was done with the handler off passes with it off */
    static Platform setting_up;
    TmHostProgram setup = { setting_up_main, asking_vblank, NULL, NULL, lagging_owed, NULL, NULL };
    order_len = 0;
    game_owes = 0;
    tm_host_start(&setting_up, &setup, 1);
    for (int i = 0; i < 4; i++) {
        tm_host_frame(&setting_up);
        note('|');
    }
    order[order_len] = 0;
    CHECK(strcmp(order, "M||e|VM|") == 0, "the setup's frames pass before the handler is on: %s", order);

    static Platform charges;
    TmHostProgram charged = { beside_charges_main, charging_handler, charging_handler, NULL, NULL, NULL, NULL };
    tm_host_start(&charges, &charged, 1);
    for (unsigned frame = 0; frame < 6; frame++)
        tm_host_frame(&charges);
    CHECK(charged_main_calls == 4, "handler charges leave four main-thread passes (%u)", charged_main_calls);
    CHECK(tm_host_lag_frames(&charges) == 2, "only the load causes lag (%u)", tm_host_lag_frames(&charges));


    static Platform transfers;
    vid_host_start(&transfers, dma_read, dma_write, NULL);
    TmHostProgram dma = { dma_main, NULL, NULL, NULL, NULL, NULL, NULL };
    tm_host_start(&transfers, &dma, 1);
    for (unsigned frame = 0; frame < 6; frame++)
        tm_host_frame(&transfers);
    CHECK(dma_main_calls == 2 && tm_host_lag_frames(&transfers) == 4,
          "three 64 KiB transfers fill four lag frames (%u calls, %u lag)", dma_main_calls, tm_host_lag_frames(&transfers));
    CHECK(dma_reads == 2 * 0x10000u && dma_writes == 2 * 0x10000u,
          "encoded zero length transfers all 64 KiB (%u reads, %u writes)", dma_reads, dma_writes);
    CHECK(vid_host_picture(&transfers)->vram[0] == 0x5A, "the timed VRAM transfer writes its data");
    vid_host_stop(&transfers);

    static Platform budget;
    TmHostProgram budgets = { budget_main, charging_handler, charging_handler, NULL, lagging_owed, NULL, NULL };
    game_owes = 0;
    tm_host_start(&budget, &budgets, 1);
    tm_host_frame(&budget);
    tm_host_frame(&budget);
    CHECK(budget_checks == 2, "both frame budgets checked (%u)", budget_checks);

    printf("timing_test: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
