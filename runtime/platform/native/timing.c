/* Timing service, host backend (runtime/platform/native/host.h): the frame as
 * the driver runs it, with no beam and no interrupts.
 *
 * What the game asks of the beam has an answer here (docs/PLATFORM.md,
 * "Timing without a beam"): the handlers it enables run once a frame, in
 * order, and never preempt the main thread; the acknowledges say "pending"
 * inside their own handler; a wait for the next frame yields to the driver; a
 * budget takes the work charged so far toward its deadline; a wait for a line has nothing to wait
 * for, the frame being drawn whole; and what looked random from the beam's
 * position comes from a generator the driver seeds. */
#include "platform/native/host.h"
#include "platform/time_model.h"

static uint32_t sum(uint32_t a, uint32_t b) { return b > UINT32_MAX - a ? UINT32_MAX : a + b; }

/* ---- The frame ----------------------------------------------------------------------------- */

static void main_thread_body(void *arg)
{
    Platform *pf = arg;
    TmHost *t = &pf->timing;
    t->program.main_thread(pf, t->program.ctx);
    /* the game's main thread doesn't return; should it, every frame after has
     * no main thread's work */
    for (;;) {
        t->waiting = t->main;
        fiber_switch(t->driver);
    }
}

void tm_host_start(Platform *pf, const TmHostProgram *program, uint32_t seed)
{
    TmHost *t = &pf->timing;
    *t = (TmHost){ .program = *program, .entropy = seed ? seed : 1 };
    t->driver = fiber_current();
    t->main = fiber_create(main_thread_body, pf);
    t->waiting = t->main;
}

static bool sits_this_frame_out(TmHost *t);

void tm_host_frame(Platform *pf)
{
    TmHost *t = &pf->timing;
    vid_host_frame_begins(pf);
    if ((t->interrupts & TM_INT_VBLANK) && t->program.vblank) {
        t->part = TM_HOST_IN_VBLANK;
        t->program.vblank(pf, t->program.ctx);
    }
    t->part = TM_HOST_IN_MAIN;
    /* what the handlers did since the main thread last ran, the line
     * handler's and the vblank's, is the frame's own time, not the main
     * thread's: it is what the frame leaves the main thread less */
    t->handlers = sum(t->handler_charges, t->program.owed ? t->program.owed(t->program.ctx) : 0);
    t->handler_charges = 0;
    if (sits_this_frame_out(t))
        t->lag_frames++;
    else if (t->waiting)
        fiber_switch(t->waiting);
    if ((t->interrupts & TM_INT_LINE) && t->program.line) {
        t->part = TM_HOST_IN_LINE;
        t->program.line(pf, t->program.ctx);
    }
    t->part = TM_HOST_IDLE;
    t->frame++;
}

uint16_t tm_host_picture_lines(const Platform *pf)
{
    const TmHost *t = &pf->timing;
    return (t->interrupts & TM_INT_LINE) ? (uint16_t)(t->line + 1) : 0;
}

uint32_t tm_host_frames(const Platform *pf) { return pf->timing.frame; }

/* ---- The frame-level time model ------------------------------------------------------------ */


void tm_host_charge(Platform *pf, uint32_t clocks)
{
    TmHost *t = &pf->timing;
    if (t->part == TM_HOST_IN_VBLANK || t->part == TM_HOST_IN_LINE)
        t->handler_charges = sum(t->handler_charges, clocks);
    else
        t->owed = sum(t->owed, clocks);
}

/* A transfer halts the processor for its bytes wherever it is made: the main
 * thread's owes it, a handler's is the frame's own (tm_host_charge). */
void tm_charge_transfer(Platform *pf, PfOrigin o, uint32_t bytes)
{
    (void)o;
    if (pf->timing.part != TM_HOST_IDLE)
        tm_host_charge(pf, time_of_transfer(bytes));
}

uint32_t tm_host_lag_frames(const Platform *pf) { return pf->timing.lag_frames; }

void tm_host_game_access(Platform *pf, uint32_t clocks)
{
    TmHost *t = &pf->timing;
    if (t->program.accessed)
        t->program.accessed(t->program.ctx, clocks);
}

/* The whole frames the main thread's work fills: each frame has its time
 * less the handlers' for it (as much as half a frame: a handler that takes
 * more leaves the frame to a main thread that does little), and what is left
 * over fits in the frame the wait is in. */
static uint32_t main_threads_frame(const TmHost *t)
{
    return TIME_FRAME_CLOCKS - (t->handlers < TIME_FRAME_CLOCKS / 2 ? t->handlers : TIME_FRAME_CLOCKS / 2);
}

static void take_the_games_account(TmHost *t)
{
    if (t->program.owed)
        t->owed = sum(t->owed, t->program.owed(t->program.ctx));
}

/* The lag log: a stretch of lag as it ends, from the frame it began in. */
static void report_lag(TmHost *t)
{
    if (t->lag_count && t->program.lagged)
        t->program.lagged(t->program.ctx, t->lag_start, t->lag_count, t->lag_owed);
}

static void begin_lag(TmHost *t, TmHostLag how, uint32_t debt)
{
    t->lag = how;
    t->debt = debt;
    t->lag_start = t->frame;
    t->lag_owed = t->owed;
    t->lag_count = 0;
}

static void end_lag(TmHost *t)
{
    report_lag(t);
    t->lag = TM_HOST_RUNNING;
    t->debt = 0;
}

/* Whether the main thread sits this frame out, its work still filling it.
 * Each frame gives it what that frame's own handlers leave (main_threads_frame,
 * taken as the frame starts): the handlers run in every lag frame, and a
 * frame whose handlers did a load's heavy sends doesn't make the frames after
 * it, whose handlers do little, as short. */
static bool sits_this_frame_out(TmHost *t)
{
    uint32_t frame = main_threads_frame(t);
    switch (t->lag) {
    case TM_HOST_LAGGING_IN_WAIT:
        /* the wait lasts through this frame; the next is the one its work
         * no longer fills */
        t->lag_count++;
        if (t->debt < frame)
            end_lag(t);
        else
            t->debt -= frame;
        return true;
    case TM_HOST_LAGGING_IN_WORK:
        if (t->debt >= frame) {
            t->debt -= frame;
            t->lag_count++;
            return true;
        }
        /* the work goes on in this frame, with what is left of it owed */
        t->owed = t->debt;
        end_lag(t);
        return false;
    case TM_HOST_RUNNING:
        break;
    }
    return false;
}

/* The main thread's wait with time owed, its own charges and what the game's
 * state kept (TmHostProgram.owed): the frames its work fills past the one it
 * is in are lag frames, and the rest is the frame's own. */
static void lag_for_what_is_owed(TmHost *t)
{
    take_the_games_account(t);
    uint32_t frame = main_threads_frame(t);
    if (t->owed >= frame)
        begin_lag(t, TM_HOST_LAGGING_IN_WAIT, t->owed - frame);
    t->owed = 0;
}

void tm_host_sit_out_owed(Platform *pf)
{
    TmHost *t = &pf->timing;
    if (t->part != TM_HOST_IN_MAIN)
        return;
    take_the_games_account(t);
    uint32_t frame = main_threads_frame(t);
    if (t->owed < frame)
        return;
    /* the main thread goes on in the first frame its work doesn't fill,
     * with what is left over still owed */
    begin_lag(t, TM_HOST_LAGGING_IN_WORK, t->owed - frame);
    t->owed = 0;
    t->waiting = fiber_current();
    fiber_switch(t->driver);
}

/* ---- The service's calls ------------------------------------------------------------------- */

void tm_enable_interrupts(Platform *pf, PfOrigin o, uint8_t sources)
{
    (void)o;
    /* the frames the main thread's work so far spans went by with the
     * handlers it had, the original's setup running with them off */
    if (sources != pf->timing.interrupts)
        tm_host_sit_out_owed(pf);
    pf->timing.interrupts = sources;
}

void tm_set_line(Platform *pf, PfOrigin o, uint16_t line)
{
    (void)o;
    pf->timing.line = line;
}

void tm_reset_timers(Platform *pf, PfOrigin o) { (void)pf; (void)o; }
void tm_fast_rom(Platform *pf, PfOrigin o, bool fast) { (void)pf; (void)o; (void)fast; }

uint8_t tm_ack_vblank(Platform *pf, PfOrigin o)
{
    (void)o;
    return pf->timing.part == TM_HOST_IN_VBLANK ? TM_VBLANK_PENDING : 0;
}

uint8_t tm_ack_line_irq(Platform *pf, PfOrigin o)
{
    (void)o;
    return pf->timing.part == TM_HOST_IN_LINE ? TM_LINE_IRQ_PENDING : 0;
}

bool tm_odd_frame(Platform *pf, PfOrigin o)
{
    (void)o;
    return pf->timing.frame & 1;
}

uint8_t tm_reset_beam_latch(Platform *pf, PfOrigin o) { (void)pf; (void)o; return 0; }
void tm_sync(Platform *pf) { (void)pf; }

/* The main thread waits for a frame, or a coroutine it runs in turn; the next
 * frame resumes the one that waited. A handler that asked would have no frame
 * to give up, and goes on. */
void tm_wait_frame(Platform *pf, PfOrigin o)
{
    (void)o;
    TmHost *t = &pf->timing;
    if (t->part != TM_HOST_IN_MAIN)
        return;
    lag_for_what_is_owed(t);
    t->waiting = fiber_current();
    fiber_switch(t->driver);
}

void tm_wait_frames(Platform *pf, PfOrigin o, uint8_t frames)
{
    do
        tm_wait_frame(pf, o);
    while (--frames);
}

/* The handlers run once a frame, after which the game looks again at what
 * they left. */
void tm_wait_for_handlers(Platform *pf, PfOrigin o) { tm_wait_frame(pf, o); }

bool tm_wait_for_handlers_held(Platform *pf, PfOrigin o)
{
    tm_wait_frame(pf, o);
    return true;
}

void tm_wait_frame_then_enable(Platform *pf, PfOrigin o, uint8_t sources, uint16_t line)
{
    tm_wait_frame(pf, o);
    if (line != TM_LINE_UNCHANGED)
        tm_set_line(pf, o, line);
    tm_enable_interrupts(pf, o, sources);
}

bool tm_frame_time_left(Platform *pf, PfOrigin o, uint16_t line)
{
    (void)o;
    TmHost *t = &pf->timing;
    if (t->part != TM_HOST_IN_MAIN || line >= TIME_BUDGET_REOPENS_LINE)
        return true;
    take_the_games_account(t);
    uint32_t elapsed = sum(t->owed, t->handlers);
    /* A load that already fills a frame must yield before doing more queued
     * work. Within this frame, follow the game's allowed window: it opens
     * at line 240, wraps through frame start, and closes at the given line.
     * NMI starts our frame at 225; it is not the window's opening. */
    if (elapsed >= TIME_FRAME_CLOCKS)
        return false;
    uint32_t phase = (TIME_VBLANK_LINE + elapsed / TIME_LINE_CLOCKS) % TIME_FRAME_LINES;
    return phase >= TIME_BUDGET_REOPENS_LINE || phase < line;
}

/* Where the beam is in a handler: the line its part of the frame starts at,
 * on by the handlers' work so far (the game's own accesses taken into the
 * handlers' account, where they would go as the main thread starts). */
static uint16_t handlers_line(TmHost *t)
{
    if (t->program.owed)
        t->handler_charges = sum(t->handler_charges, t->program.owed(t->program.ctx));
    uint16_t start = t->part == TM_HOST_IN_LINE ? (uint16_t)(t->line + 1) : TIME_VBLANK_LINE;
    return (uint16_t)((start + t->handler_charges / TIME_LINE_CLOCKS) % TIME_FRAME_LINES);
}

/* A handler's wait for a line: nothing to wait for in a picture drawn whole,
 * but the original's handler holds the processor until the beam gets there --
 * the battle's frame work, while effects run, waits for line 64 ($CD:3B82),
 * a third of the frame after the vblank -- and that time is the frame's, not
 * the main thread's: it is charged to the handlers. A wait from the main
 * thread charges nothing. */
void tm_wait_line(Platform *pf, PfOrigin o, uint16_t line)
{
    (void)o;
    TmHost *t = &pf->timing;
    if (t->part != TM_HOST_IN_VBLANK && t->part != TM_HOST_IN_LINE)
        return;
    uint16_t at = handlers_line(t);
    uint16_t lines = (uint16_t)((line % TIME_FRAME_LINES + TIME_FRAME_LINES - at) % TIME_FRAME_LINES);
    t->handler_charges = sum(t->handler_charges, (uint32_t)lines * TIME_LINE_CLOCKS);
}
void tm_wait_line_drawn(Platform *pf, PfOrigin o) { (void)pf; (void)o; }
void tm_wait_line_started(Platform *pf, PfOrigin o) { (void)pf; (void)o; }

/* A 32-bit xorshift: small, and the same on every host. */
static uint16_t next_entropy(Platform *pf)
{
    uint32_t x = pf->timing.entropy;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    pf->timing.entropy = x;
    return (uint16_t)(x >> 8);
}

TmEntropy tm_entropy(Platform *pf, PfOrigin o, TmEntropyWidth width)
{
    (void)o;
    TmEntropy e;
    for (unsigned i = 0; i < TM_ENTROPY_VALUES; i++) {
        uint16_t v = next_entropy(pf);
        e.value[i] = width == TM_ENTROPY_WORDS ? v : (uint8_t)v;
    }
    return e;
}
