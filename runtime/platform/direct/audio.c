#include "platform/direct/audio.h"

#include <stdlib.h>
#include <string.h>

#include "port/machine.h"
#include "port/native.h"

#define CLOCKS_PER_IO_CYCLE 6

/* A run that has had the SPC700 run this far ahead without finishing is given
 * up: the SPC700 isn't answering. The code then carries on in the machine's
 * timeline, as with the SNES backend. About 10 seconds. */
#define RUN_LIMIT_CLOCKS (10ull * 21477272ull)

/* Run ahead in pieces short enough that the S-DSP's output buffer (4096 frames)
 * can't fill up in one: 2^20 master clocks are about 1560 frames. */
#define RUN_CHUNK_CLOCKS (1u << 20)

/* Samples produced while running ahead, kept for the host: up to 2^20 frames
 * (about 33 seconds) when the host takes none. */
#define FIFO_FRAMES (1u << 20)

static struct {
    Snes *machine;          /* the machine APU-direct is selected for */
    int depth;              /* nested runs in progress */
    uint32_t lo, hi;        /* the outermost run's address range */
    uint32_t cost_milli;    /* time per bus clock of the code running now, x1000 (NativeEntry.cost_milli) */
    uint64_t run_clocks;    /* run ahead in the outermost run so far */
    bool given_up;

    int16_t *fifo;
    size_t head, count;

    unsigned long runs, given_up_runs;
    uint64_t ahead_clocks, native_calls, interpreted;
} dir;

void au_direct_select(Snes *s, bool on)
{
    if (on)
        dir.machine = s;
    else if (dir.machine == s)
        dir.machine = NULL;
}

bool au_direct_selected(const Snes *s) { return s && dir.machine == s; }

bool au_direct_running(const Snes *s) { return dir.depth > 0 && dir.machine == s; }

/* Move what the S-DSP has buffered into the FIFO, so running ahead doesn't
 * overflow its buffer. */
static void drain(Snes *s)
{
    if (!dir.fifo) {
        dir.fifo = malloc(sizeof *dir.fifo * FIFO_FRAMES * 2);
        if (!dir.fifo)
            return;
    }
    while (s->apu.dsp.out_count > 0 && dir.count < FIFO_FRAMES) {
        int16_t frame[2];
        dsp_take_samples(&s->apu.dsp, frame, 1);
        size_t at = (dir.head + dir.count) % FIFO_FRAMES;
        dir.fifo[at * 2] = frame[0];
        dir.fifo[at * 2 + 1] = frame[1];
        dir.count++;
    }
}

static void run_ahead(Snes *s, uint64_t clocks)
{
    dir.run_clocks += clocks;
    dir.ahead_clocks += clocks;
    while (clocks > 0) {
        uint32_t chunk = clocks < RUN_CHUNK_CLOCKS ? (uint32_t)clocks : RUN_CHUNK_CLOCKS;
        if (s->apu.dsp.out_count > DSP_OUT_FRAMES / 4)
            drain(s);
        apu_run_ahead(&s->apu, chunk);
        clocks -= chunk;
    }
}

void au_direct_pass(Snes *s)
{
    /* native code counts its bus accesses only; scale them by the entry's cost,
     * as ON mode does when it charges the machine */
    uint64_t clocks = (uint64_t)s->bus.clocks * dir.cost_milli / 1000 + (uint64_t)s->cpu.io_cycles * CLOCKS_PER_IO_CYCLE;
    s->bus.clocks = 0;
    s->cpu.io_cycles = 0;
    run_ahead(s, clocks);
}

bool au_direct_wait_port0(Snes *s, int32_t wake, uint32_t passes, uint32_t pass_clocks)
{
    au_direct_pass(s);
    for (uint32_t pass = 0; !native_wake_port0_satisfied(wake, apu_read_port(&s->apu, 0)); pass++) {
        if (dir.given_up || (passes && pass == passes))
            return false;
        if (dir.run_clocks > RUN_LIMIT_CLOCKS) {
            dir.given_up = true;
            dir.given_up_runs++;
            return false;
        }
        run_ahead(s, pass_clocks);
    }
    return true;
}

/* Run from PC while it stays in [lo, hi], until it reaches `ret` with S at
 * `s_after` (when s_after >= 0). */
static void run_loop(Snes *s, uint32_t lo, uint32_t hi, uint32_t ret, int32_t s_after)
{
    for (;;) {
        uint32_t pc = cpu_pc24(&s->cpu);
        if (s_after >= 0 && pc == ret && s->cpu.s == (uint16_t)s_after)
            return;
        if (pc < lo || pc > hi || dir.given_up)
            return;
        if (dir.run_clocks > RUN_LIMIT_CLOCKS) {
            dir.given_up = true;
            dir.given_up_runs++;
            return;
        }
        au_direct_pass(s);
        uint32_t outer_cost = dir.cost_milli;
        const NativeEntry *e = native_hand_entry(s);
        if (e) {
            dir.native_calls++;
            dir.cost_milli = e->cost_milli ? e->cost_milli : 1000;
            e->fn(s);
            if (e->idle) {
                /* a pass takes the loop's own time, on the APU */
                s->bus.clocks = 0;
                s->cpu.io_cycles = 0;
                run_ahead(s, e->idle_clocks ? e->idle_clocks : 42);
            } else {
                au_direct_pass(s);
            }
        } else if (s->interpreter) {
            /* no entry: the interpreter, with interrupts held off, its time exact */
            bool nmi = s->cpu.nmi_pending, irq = s->cpu.irq_line;
            s->cpu.nmi_pending = false;
            s->cpu.irq_line = false;
            dir.cost_milli = 1000;
            s->interpreter(&s->cpu);
            s->cpu.nmi_pending = nmi;
            s->cpu.irq_line = irq;
            dir.interpreted++;
            au_direct_pass(s);
        } else {
            /* A build without the interpreter (docs/PLAYING.md, "Without the
             * interpreter") has nothing to run an instruction no entry covers,
             * so the run is given up here and the code carries on in the
             * machine's timeline, as when the SPC700 doesn't answer. Reaching
             * the interpreter through the machine rather than calling cpu_step
             * is what keeps cpu65816.c out of a program that doesn't link it
             * (`snes_init` leaves `interpreter` NULL for that reason). */
            dir.given_up = true;
            dir.given_up_runs++;
        }
        dir.cost_milli = outer_cost;
    }
}

bool au_direct_run(Snes *s, uint32_t lo, uint32_t hi)
{
    if (!au_direct_selected(s) || dir.depth > 0)
        return false;
    dir.depth++;
    dir.runs++;
    dir.lo = lo;
    dir.hi = hi;
    dir.run_clocks = 0;
    dir.given_up = false;
    dir.cost_milli = 1000;
    au_direct_pass(s);
    run_loop(s, lo, hi, 0, -1);
    au_direct_pass(s);
    dir.depth--;
    return true;
}

void au_direct_call(Snes *s, uint32_t site, uint32_t target)
{
    uint16_t s_after = s->cpu.s;
    m_push16(s, (uint16_t)(site + 2));   /* JSR pushes the address of its last byte */
    s->cpu.pc = target & 0xFFFF;
    s->cpu.pb = target >> 16;
    au_direct_pass(s);
    run_loop(s, dir.lo, dir.hi, (site & 0xFF0000) | (uint16_t)(site + 3), s_after);
}

size_t au_direct_take_samples(Snes *s, int16_t *out, size_t max_frames)
{
    size_t n = 0;
    if (dir.machine == s) {
        while (n < max_frames && dir.count > 0) {
            out[n * 2] = dir.fifo[dir.head * 2];
            out[n * 2 + 1] = dir.fifo[dir.head * 2 + 1];
            dir.head = (dir.head + 1) % FIFO_FRAMES;
            dir.count--;
            n++;
        }
    }
    return n + apu_take_samples(&s->apu, out + n * 2, max_frames - n);
}

void au_direct_report(FILE *out)
{
    if (!dir.runs)
        return;
    fprintf(out,
            "audio direct: %lu runs, SPC700 run ahead %.2f s, %llu native calls and %llu instructions interpreted "
            "inside runs, %lu runs given up\n",
            dir.runs, (double)dir.ahead_clocks / 21477272.0, (unsigned long long)dir.native_calls,
            (unsigned long long)dir.interpreted, dir.given_up_runs);
}
