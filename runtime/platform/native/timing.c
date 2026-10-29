/* Timing service, host backend (runtime/platform/native/host.h): the frame as
 * the driver runs it, with no beam and no interrupts.
 *
 * What the game asks of the beam has an answer here (docs/PLATFORM.md,
 * "Timing without a beam"): the handlers it enables run once a frame, in
 * order, and never preempt the main thread; the acknowledges say "pending"
 * inside their own handler; a wait for the next frame yields to the driver; a
 * budget has time for all its work; a wait for a line has nothing to wait
 * for, the frame being drawn whole; and what looked random from the beam's
 * position comes from a generator the driver seeds. */
#include "platform/native/host.h"

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

void tm_host_frame(Platform *pf)
{
    TmHost *t = &pf->timing;
    vid_host_frame_begins(pf);
    if ((t->interrupts & TM_INT_VBLANK) && t->program.vblank) {
        t->part = TM_HOST_IN_VBLANK;
        t->program.vblank(pf, t->program.ctx);
    }
    t->part = TM_HOST_IN_MAIN;
    if (t->waiting)
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

/* ---- The service's calls ------------------------------------------------------------------- */

void tm_enable_interrupts(Platform *pf, PfOrigin o, uint8_t sources)
{
    (void)o;
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
    (void)pf; (void)o; (void)line;
    return true;
}

void tm_wait_line(Platform *pf, PfOrigin o, uint16_t line) { (void)pf; (void)o; (void)line; }
void tm_wait_line_drawn(Platform *pf, PfOrigin o) { (void)pf; (void)o; }

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
