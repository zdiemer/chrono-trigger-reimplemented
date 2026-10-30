#include "native.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "fiber.h"
#include "port/generated_snes.h"

static void push8_raw(Snes *s, uint8_t v)
{
    snes_bus_write(&s->bus, s->cpu.s, v);
    s->cpu.s--;
}

static void push16_raw(Snes *s, uint16_t v)
{
    push8_raw(s, v >> 8);
    push8_raw(s, v & 0xFF);
}

#define MAX_ACTIVE 32
/* Details kept for the report: of mismatches, and apart from them of
 * everything else (an abandoned comparison, a timing note), so that notes
 * can't crowd out the mismatch the gate fails on. native_set_details lowers
 * or raises how many of each are kept, up to MAX_DETAILS. */
#define MAX_DETAILS 256
#define DEFAULT_DETAILS 16
#define RESUME_SLOTS 4096
#define MAX_DEAD_STACK 96     /* stack bytes a port has declared don't care at once (native_stack_dont_care): a menu task's stack below its wait, and a few more */
#define MAX_SAVED_FLAGS 16    /* P bytes a port has saved on the stack and not restored yet (native_flags_saved) */
#define MAX_TIME_SCOPES 16    /* native_time_as scopes a stretch's owner is looked for in (deeper ones nest uncounted) */
#define MAX_STRETCH_RUNS 8     /* sibling runs one stretch's clocks are tallied for (stretch_owner) */
#define MAX_SEGMENT_PARTS 128  /* distinct routines one stretch of a port attributes time to (the battle timers' dispatch, a whole turn, reaches more than 32; the field's frame, followed through a scene or the menus on their own stacks, more than 48) */
#define MAX_PARKED 16          /* routines parked in yields at once (the menus run eight tasks) */

/* A native_time_as scope: the routine's table index (-1 the port's own), and
 * for a sibling's run (native_time_as_sibling) the run's number in its job, 0
 * for a plain scope. */
typedef struct {
    int idx;
    unsigned run;
} TimeScope;

/* Of one stretch's bus clocks, those spent inside a sibling's run (its scope
 * and the scopes it opened), the run at `depth` among the open scopes. */
typedef struct {
    unsigned run;
    int idx, depth;
    uint32_t bus;
} RunClocks;

/* A port's bus clocks in one stretch attributed to the routine at table index
 * `entry` (native_time_as). */
typedef struct SegmentPart {
    int entry;
    uint32_t bus, dma;
} SegmentPart;

/* ---- jobs: native routines running on their own stacks ------------------------ */

typedef enum { JOB_RUNNING, JOB_CALLING, JOB_SYNCING, JOB_LATCHING, JOB_WAITING, JOB_DONE } JobState;

/* A native routine in progress. It runs on a fiber so that calling code that
 * isn't ported (m_jsr/m_jsl) can suspend it: the call is set up on the machine,
 * control goes back to the system loop, the interpreter runs the callee with
 * time passing as usual, and the routine resumes once the callee returns. */
typedef struct Job {
    Fiber *fiber;
    const NativeEntry *entry;
    Snes *m;               /* machine the routine runs on */
    JobState state;
    uint32_t call_target;  /* JOB_CALLING: the call it made */
    uint32_t call_ret;     /* where the callee returns to, and S after it has */
    uint16_t call_s_after;
    uint16_t call_dont_care;
    bool call_waits;           /* JOB_CALLING: native_wait_call's, which stands for the port's own wait */
    bool call_yields;          /* JOB_CALLING: native_yield_call_dc's, to another task... */
    NativeGoesOn goes_on;      /* ...and whether the task can still go on from it */
    uint32_t wait_exit;        /* JOB_WAITING: native_wait_loop's exit (its head is call_target)... */
    int32_t wake;              /* ...and what ends the wait (NATIVE_WAKE_HANDLER, or a line) */
    uint32_t wake_clocks;      /* NATIVE_WAKE_TIME: how long it lasts */
    /* native_wait_loop_or_give_up: where the loop leaves when it gives up,
     * after polling for wait_polling clocks a wait_pass_clocks pass at a time
     * (ON); and whether it ended on what it waited for */
    uint32_t wait_given_up;
    uint32_t wait_polling, wait_pass_clocks;
    bool wait_answered;
    uint16_t stop_dont_care;   /* set by native_dont_care_at_stop */
    uint32_t stop_at;          /* ...with where it applies (0: not called) */
    bool stop_own;             /* ...and whether it is the stop's own (native_own_dont_care_at_stop) */
    bool returned;             /* native_note_return was called */
    bool returned_rti;         /* ...from native_note_rti: the port returned from an interrupt */
    /* Time attribution (native_time_as): the table index of the routine the
     * port's bus clocks count as now (-1: its own entry), the machine's bus
     * clocks and DMA clocks when that began, and this stretch's clocks by
     * routine so far, with attributions that named no hand port or found no
     * room. */
    int part;
    uint32_t part_bus, part_dma;
    int nparts;
    SegmentPart parts[MAX_SEGMENT_PARTS];
    unsigned bad_tags, part_overflows;
    uint32_t bad_tag;
    /* The scopes native_time_as and native_time_as_sibling have open,
     * outermost first, how deep they go now and how many sibling runs have
     * been numbered; and of this stretch's bus clocks, all of them and those
     * each sibling's run held, which name the stretch's owner
     * (stretch_owner). */
    TimeScope scopes[MAX_TIME_SCOPES];
    int depth;
    unsigned runs;
    uint32_t stretch_bus;
    RunClocks run_clocks[MAX_STRETCH_RUNS];
    int nrun_clocks;
    /* Stack bytes the port declared don't care while they stay pushed
     * (native_stack_dont_care), by address. */
    uint16_t dead_stack[MAX_DEAD_STACK];
    int ndead_stack;
    /* The P bytes it saved on the stack and hasn't restored yet
     * (native_flags_saved), innermost last. */
    uint16_t saved_flags[MAX_SAVED_FLAGS];
    int nsaved_flags;
    struct Job *next_free;
} Job;

static Job *current_job;
static Job *free_jobs;

/* An idle or generated entry running on the machine outside a job. */
static const NativeEntry *running_direct;

const NativeEntry *native_running_entry(void) { return current_job ? current_job->entry : running_direct; }

static void job_body(void *arg)
{
    Job *j = arg;
    for (;;) {
        j->entry->fn(j->m);
        j->state = JOB_DONE;
        fiber_switch(fiber_main());
    }
}

static Job *job_get(void)
{
    Job *j = free_jobs;
    if (j) {
        free_jobs = j->next_free;
        return j;
    }
    j = calloc(1, sizeof *j);
    j->fiber = fiber_create(job_body, j);
    return j;
}

static void job_put(Job *j) { j->next_free = free_jobs, free_jobs = j; }

/* A suspended job whose continuation will never run: its stack can't be
 * unwound, so retire the fiber. */
static void job_abandon(Job *j)
{
    fiber_destroy(j->fiber);
    free(j);
}

/* Run a job until it finishes or makes a call-out. */
static void job_run(Job *j)
{
    Job *outer = current_job;
    current_job = j;
    j->state = JOB_RUNNING;
    fiber_switch(j->fiber);
    current_job = outer;
}

/* ---- runtime ------------------------------------------------------------------ */

typedef enum {
    PH_SEGMENT,   /* native code ran up to a call or return; the original is catching up */
    PH_CALLOUT,   /* both are inside a callee the native routine called */
    PH_SYNC,      /* ON: suspended in native_sync until its time has passed */
    PH_LATCH,     /* SHADOW: suspended in native_await_latch until the original latches the counters */
    PH_WAIT,      /* ON: suspended in native_wait_loop or native_wait_call until what it waits for */
} Phase;

/* ON mode: time a hand port still owes, and where the machine is while it is
 * paid: where the port stopped (its return address, the first instruction of a
 * callee it called out to, or, for a port suspended in native_sync, PC and S
 * as the port left them), with S as it was there. An interrupt taken during
 * the payment returns there. */
typedef struct Debt {
    uint32_t clocks;
    uint32_t pc;
    uint16_t s;
    /* An interrupt was taken at the boundary and its handler hasn't come back
     * to it yet, and when (s->master): the debt waits for it, wherever the
     * handler moves the stack (pay_debt). */
    bool handler;
    uint64_t handler_at;
} Debt;

/* ON: how long a debt waits for a handler taken at its boundary that runs on
 * a stack of its own. A vblank handler finishes inside the frame it starts in;
 * one that hasn't by then left the boundary for good (a task switch), and the
 * time is paid where the machine is. */
#define DEBT_HANDLER_CLOCKS ((uint64_t)SNES_LINES_PER_FRAME * SNES_CLOCKS_PER_LINE)

#define MAX_DEBTS 16

/* ON: the time an idle entry's exit pass takes, when it waits for a change
 * rather than passing its own time (idle_clocks 0). A waiting pass skips to
 * the point the change happens, so the exit pass sees it there; the original
 * sees it on the first read of its next pass, a few cycles later, and then
 * falls through its branch. Charging nothing put the code after the loop at
 * the change itself: the line IRQs' forced blank after their hblank wait
 * landed at hclock 1102, before the line is drawn (1104), and blacked out the
 * playfield's last line in every field and battle frame (docs/REIMPLEMENTATION.md,
 * "The timing gate"). This is the shortest such loop's pass, LDA
 * abs / AND # / BEQ (9 cycles, 6 master clocks each at FastROM and I/O
 * speed), so the exit lands no earlier than the original's could. */
#define IDLE_EXIT_CLOCKS 54
/* ... and the part of that pass after its read, AND # (or BIT #) and the BEQ
 * falling through (4 cycles at FastROM speed): where the read that sees the
 * change comes late, behind the line's HDMA, the original still runs these
 * before it leaves the loop. */
#define POLL_TAIL_CLOCKS 24

/* A registered routine that has been entered and not yet left. */
typedef struct Active {
    const NativeEntry *entry;
    Job *job;              /* NULL once the native routine has returned */
    Phase phase;
    Snes *clone;           /* SHADOW: the machine the native routine runs on (clones[level]) */
    uint64_t since;        /* SHADOW: the write epoch the clone was last refreshed at */
    uint16_t entry_s;
    uint16_t ret_s;        /* after the native routine returned: S and PC the original must match */
    uint32_t ret_pc;
    uint64_t segment_start;
    uint32_t segment_frame;      /* frame counter when the segment started */
    bool segment_vblank;         /* ...and whether it started on a vblank line */
    uint32_t segment_bus_clocks;
    uint32_t segment_dma_clocks;   /* of segment_bus_clocks, DMA's */
    uint32_t dma_mark;             /* the machine's bus.dma_clocks when the port's clocks were cleared */
    unsigned long segment_port_clears[2];   /* apu_port_clears when the segment started */
    uint64_t segment_horizon, segment_interrupt_horizon;   /* SHADOW: at the segment's start (timing spike) */
    bool segment_interrupted, interrupted, mismatched;
    uint16_t segment_low_s;   /* SHADOW: the lowest S the original has reached in the segment */
    /* SHADOW, an interrupt entry (NativeEntry.interrupt): the original is at an
     * RTI in the segment, and it has run one; and the stack it moved from in
     * the segment, as the lowest S reached there and S as it left (0: none),
     * whose popped bytes are dead too (follow_the_stack). */
    bool at_rti, returned_by_rti;
    uint16_t left_low_s, left_s;
    /* SHADOW: the stack bytes the port declared dead (native_stack_dont_care)
     * and left on the stack where it stopped, which its last comparison
     * leaves out as its stretches' did. */
    uint16_t left_dead_stack[MAX_DEAD_STACK];
    int nleft_dead_stack;
    uint16_t ret_dont_care;   /* SHADOW: the native routine's native_dont_care_at_stop */
    uint32_t ret_at;          /* ...and where it said the set applies */
    bool ret_own;             /* ...and that the set is the stop's own */
    Cpu call_cpu;             /* SHADOW: the original's registers at the last call-out */
    bool ret_returned;        /* ...or that it finished by returning */
    /* SHADOW: passes of the original through the point the native routine got
     * to (a call it made, or where it finished) that were compared and
     * differed, and the first one's difference (see reach_point). */
    unsigned failed_passes;
    char failed_why[256], last_why[200];
    uint32_t sync_pc;      /* ON, PH_SYNC and PH_WAIT: where the machine is while the port is suspended */
    uint16_t sync_s;
    int32_t wake;          /* ON, PH_WAIT: what ends the wait... */
    uint32_t wake_clocks;  /* ...NATIVE_WAKE_TIME's length, and the master clock it ends at once the time owed is paid */
    uint64_t wake_until;
    bool wake_timed;
    uint32_t polling_left, pass_clocks;   /* ...a loop that gives up: the polling it has left, a pass at a time */
    bool woke;             /* ...and, for a wait on the handlers, that one has been taken where the port is */
    bool exit_passed;      /* ON, PH_WAIT for the hblank: its exit pass's time has passed (IDLE_EXIT_CLOCKS) */
    bool wait_compared;    /* SHADOW, a wait in the port's loop: the stretch before it was compared */
    /* PH_CALLOUT: an interrupt taken while the callee runs, or a stack
     * switch (stack_switched), and the S it returns to: until it has, S above
     * the call's return is its handler on a stack of its own (the menus'
     * vblank handler) or the callee on one, not the callee unwinding past its
     * return. PH_SEGMENT, a port's stretch: the same, S above the entry's
     * being the handler's or the switched stack's, not the original's
     * return. */
    bool handler_away;
    uint16_t handler_returns_s;
    /* SHADOW: the original moved onto a stack of the routine's own in the
     * middle of the port's stretch (the menus' entry, onto theirs), and the S
     * it comes back to: kept across the stretch's call-outs, which are made on
     * that stack, until S is back within the entry's frames. */
    bool switched_away;
    uint16_t switched_returns_s;
    /* SHADOW, PH_LATCH: the original's latches when the port began to wait;
     * and that the stretch the original is catching up with began before the
     * wait, which the stretch's timing goes on from. */
    uint32_t latch_mark;
    bool stretch_goes_on;
    /* SHADOW: the stretch's bus clocks by the routine they are attributed to
     * (the job's parts when it stopped), and its attributions that named no
     * hand port or found no room. */
    int segment_nparts;
    SegmentPart segment_parts[MAX_SEGMENT_PARTS];
    unsigned segment_bad_tags, segment_part_overflows;
    uint32_t segment_bad_tag;
    int segment_owner;   /* SHADOW: the entry whose stretch it is in the timing report (stretch_owner), -1 the port's */
} Active;

/* SHADOW mode record of the original's execution, for replaying generated
 * blocks against it (see "generated blocks in shadow mode" below). */
typedef struct Access {
    uint32_t addr;
    uint32_t clocks;       /* bus clocks the access took (a $420B write includes its DMA) */
    uint8_t value;
    bool write;
} Access;

enum { B_INTERRUPT = 1, B_WAITING = 2, B_STOPPED = 4 };

/* The machine at the start of a step (an instruction boundary). */
typedef struct Boundary {
    uint64_t master;       /* the CPU's own time: master clocks minus its pauses (refresh, HDMA) */
    uint32_t acc;          /* accesses recorded before this step */
    uint32_t frame;
    int32_t gen;           /* the generated entry that applies here, decided while recording
                              where guards or emulation mode matter; GEN_LATER otherwise */
    uint32_t pc24;
    uint16_t a, x, y, s, d;
    uint8_t db, p, flags;
    bool e;
} Boundary;

typedef struct Replay {
    NativeRuntime *rt;
    uint32_t pos, end;     /* next access to consume, and where the record stops */
    uint64_t clocks;
    bool dead;             /* stopped consuming: failed or ran out of record */
    bool bad;
    bool exhausted;
    bool exhausted_at_fetch;   /* ran out at the first byte of an instruction */
    uint32_t exhaust_addr;
    Cpu exhaust_cpu;
    uint64_t exhaust_clocks;
    char why[200];
} Replay;

struct NativeRuntime {
    const NativeEntry *table;
    int count;
    NativeMode mode;
    NativeStats *stats;
    uint8_t filter[0x2000];      /* bit per 16-bit address: some entry might be here */
    Active active[MAX_ACTIVE];
    int nactive;
    /* SHADOW: the machine copies native routines run on, one per nesting level,
     * kept between calls so a refresh copies only what changed
     * (snes_clone_since), and the epoch each was last refreshed at (0: never). */
    Snes *clones[MAX_ACTIVE];
    uint64_t clone_since[MAX_ACTIVE];
    /* Routines parked in a yield to another task (native_yield_call_dc), each
     * in PH_CALLOUT with the machine copy it runs on in SHADOW mode (taken
     * from its level's, which another routine at that level gets afresh) and
     * that copy's epoch; and the frame and S at the latest step, where a
     * parked routine's task may have been dropped since. */
    struct {
        Active a;
        Snes *clone;
        uint64_t since;
    } parked[MAX_PARKED];
    int nparked;
    uint32_t parked_frame;
    uint16_t parked_s;
    /* ON mode: master clocks native code still owes, where it owes them
     * (paying debt, below). */
    Debt debts[MAX_DEBTS];
    int ndebts;
    char details[2][MAX_DETAILS][400];   /* [0] mismatches, [1] notes */
    int ndetails[2];
    unsigned long dropped[2];            /* past the limit */
    int detail_limit;

    int generated;               /* generated entries in the table */
    Snes *attached;
    CpuBus attached_bus;         /* the machine's CPU bus hooks before recording */
    Access *acc;                 /* SHADOW with generated entries: the record */
    uint32_t nacc, cap_acc;
    Boundary *bnd;
    uint32_t nbnd, cap_bnd;
    Snes *replay_machine;        /* its Cpu runs replayed blocks */
    Replay replay;
    unsigned long gen_unfinished;

    /* ON mode: where generated blocks stopped before an instruction, so that
     * code resumes there in the same block (direct-mapped by address and widths). */
    struct { uint32_t key; NativeFn fn; bool (*guard)(Snes *s); } resume[RESUME_SLOTS];
    unsigned long gen_stops, gen_resumes;
    /* ON mode: every instruction of every block (native_set_index), for
     * continuing in generated code where no entry or stop applies. */
    const GeneratedIndexEntry *index;
    int index_count;
    uint8_t index_filter[0x2000];   /* bit per 16-bit address: some index entry might be here */
    unsigned long gen_indexed;
    /* SHADOW: bit per 16-bit address with a generated entry that has a guard or
     * needs emulation mode, decided while recording (Boundary.gen). */
    uint8_t record_filter[0x2000];

    /* Timing measurement and the oracle (native_set_port_timing,
     * native_set_timing_report). */
    PortTiming port_timing;
    bool port_horizon_interrupts;
    bool timing_report;
    bool page_report;
    uint64_t step_master, prev_step_master;   /* SHADOW: master clock at the latest two steps */
    uint16_t prev_s;                          /* SHADOW: the original's S at the latest step */
    uint16_t step_s, before_step_s;           /* the machine's S at the latest two steps (every mode) */
    Snes *timing_copy;                        /* the machine as it was before a port ran, to put it back */
    uint64_t timing_copy_since;
    Snes *timing_scratch;                     /* ORACLE: the machine the generated code is timed on */
    uint64_t timing_scratch_since;

    NativeObserver observer;                  /* hand-offs to the original, for a tool (all NULL: none) */
};

void native_set_port_timing(NativeRuntime *rt, PortTiming timing) { rt->port_timing = timing; }
void native_set_timing_report(NativeRuntime *rt, bool on) { rt->timing_report = on; }
void native_set_page_report(NativeRuntime *rt, bool on) { rt->page_report = on; }

void native_set_observer(NativeRuntime *rt, const NativeObserver *observer)
{
    if (observer)
        rt->observer = *observer;
    else
        memset(&rt->observer, 0, sizeof rt->observer);
}

/* A call of a hand port made with D = d (native_set_page_report). */
static void count_page(NativeStats *st, uint16_t d)
{
    for (unsigned k = 0; k < st->page_nd; k++)
        if (st->page_d[k] == d) {
            st->page_calls[k]++;
            return;
        }
    if (st->page_nd == NATIVE_PAGE_SLOTS) {
        st->page_other++;
        return;
    }
    st->page_d[st->page_nd] = d;
    st->page_calls[st->page_nd++] = 1;
}
void native_set_port_horizon(NativeRuntime *rt, bool interrupts_only) { rt->port_horizon_interrupts = interrupts_only; }

/* I/O accesses rc_io_sync saw that depend on or change the time (the timing
 * spike's oracle counts them while it times a call). */
static unsigned long rc_timed_io;

#define GEN_LATER (-2)
#define CLOCKS_PER_IO_CYCLE_NATIVE 6

static const NativeRuntime *active_runtime;   /* for synchronous native_call */

uint64_t generated_executed;
void (*generated_insn_hook)(Snes *s, uint32_t pc24);
NativeCensusHooks native_census_hooks;
RcTiming rc_timing = { .limit = UINT32_MAX };

uint64_t native_generated_instructions(void) { return generated_executed; }

/* By address; hand-written entries before generated ones at the same address. */
static int entry_cmp(const void *a, const void *b)
{
    const NativeEntry *p = a, *q = b;
    if (p->addr != q->addr)
        return p->addr < q->addr ? -1 : 1;
    return (int)p->generated - (int)q->generated;
}

NativeRuntime *native_create(const NativeEntry *table, int count, NativeMode mode)
{
    NativeRuntime *rt = calloc(1, sizeof *rt);
    rt->detail_limit = DEFAULT_DETAILS;
    NativeEntry *sorted = malloc(sizeof *sorted * (size_t)(count ? count : 1));
    memcpy(sorted, table, sizeof *sorted * (size_t)count);
    qsort(sorted, (size_t)count, sizeof *sorted, entry_cmp);
    rt->table = sorted;
    rt->count = count;
    rt->mode = mode;
    rt->stats = calloc((size_t)(count ? count : 1), sizeof *rt->stats);
    for (int i = 0; i < count; i++) {
        uint16_t a = sorted[i].addr & 0xFFFF;
        rt->filter[a >> 3] |= (uint8_t)(1 << (a & 7));
        rt->generated += sorted[i].generated;
        if (sorted[i].generated && (sorted[i].guard || sorted[i].emulation))
            rt->record_filter[a >> 3] |= (uint8_t)(1 << (a & 7));
    }
    active_runtime = rt;
    return rt;
}

static void active_pop(NativeRuntime *rt)
{
    Active *a = &rt->active[--rt->nactive];
    if (a->job) {
        job_abandon(a->job);
        a->job = NULL;
    }
    a->clone = NULL;
}

/* Start counting a port's bus clocks on m, its DMA transfers' among them, and
 * the routines it attributes them to (the attribution itself carries on). */
static void count_from(Active *a, Snes *m)
{
    m->bus.clocks = 0;
    a->dma_mark = m->bus.dma_clocks;
    Job *j = a->job;
    if (j) {
        j->part_bus = 0;
        j->part_dma = m->bus.dma_clocks;
        j->nparts = 0;
        j->bad_tags = j->part_overflows = 0;
        j->stretch_bus = 0;
        j->nrun_clocks = 0;
    }
}

/* A port's estimated time for `bus` clocks of its accesses, `dma` of them DMA
 * transfers, `parts` of them attributed to other routines (native_time_as):
 * DMA takes the same time in the port as in the original, so that part is as
 * it is, and cost_milli estimates the rest (the time of the instructions
 * around the accesses, which the port doesn't make): the routine's own for
 * each attributed part, the entry's for the rest. */
static uint64_t estimate_parts(const NativeRuntime *rt, const NativeEntry *e, uint32_t bus, uint32_t dma,
                               const SegmentPart *parts, int nparts)
{
    uint64_t milli = 0;
    for (int k = 0; k < nparts; k++) {
        const SegmentPart *p = &parts[k];
        milli += (uint64_t)p->dma * 1000 + (uint64_t)(p->bus - p->dma) * rt->table[p->entry].cost_milli;
        bus -= p->bus;
        dma -= p->dma;
    }
    return (milli + (uint64_t)dma * 1000 + (uint64_t)(bus - dma) * e->cost_milli) / 1000;
}

/* The bus clocks j's machine spent since the attribution last changed go to
 * the routine they are attributed to (its own entry's aren't listed). */
static void part_flush(Job *j)
{
    Snes *m = j->m;
    uint32_t bus = m->bus.clocks - j->part_bus, dma = m->bus.dma_clocks - j->part_dma;
    j->part_bus = m->bus.clocks;
    j->part_dma = m->bus.dma_clocks;
    j->stretch_bus += bus;
    int open = j->depth < MAX_TIME_SCOPES ? j->depth : MAX_TIME_SCOPES;
    for (int k = 0; bus && k < open; k++) {
        const TimeScope *scope = &j->scopes[k];
        if (!scope->run)
            continue;
        int r = 0;
        while (r < j->nrun_clocks && j->run_clocks[r].run != scope->run)
            r++;
        if (r == j->nrun_clocks) {
            if (r == MAX_STRETCH_RUNS)
                continue;   /* no room: the run can't own the stretch */
            j->run_clocks[j->nrun_clocks++] = (RunClocks){ scope->run, scope->idx, k, 0 };
        }
        j->run_clocks[r].bus += bus;
    }
    if (j->part < 0 || !bus)
        return;
    for (int k = 0; k < j->nparts; k++) {
        if (j->parts[k].entry == j->part) {
            j->parts[k].bus += bus;
            j->parts[k].dma += dma;
            return;
        }
    }
    if (j->nparts == MAX_SEGMENT_PARTS) {
        j->part_overflows++;   /* no room: they stay the port's own */
        return;
    }
    j->parts[j->nparts++] = (SegmentPart){ j->part, bus, dma };
}

/* Whose stretch a stretch of a port is in the timing report: the routine of
 * the innermost sibling's run (native_time_as_sibling) that held more than
 * half of its bus clocks, where that is a hand port's entry other than the
 * running one, else the running port's own (-1). A sibling run as C where the
 * original's call reached it through the machine has stretches that are its
 * work, or nearly: the middle of one that waits for frames (an action's
 * showing, run by the battle's steps), and its first and last, with the few
 * clocks of the call round them. They are measured as the sibling's, so
 * that its calibration and its worst stretches are its own, as the
 * original's call of it is, rather than the caller's few clocks taking up
 * the sibling's error. */
static int stretch_owner(const Job *j)
{
    int owner = -1, depth = -1;
    for (int r = 0; r < j->nrun_clocks; r++) {
        const RunClocks *run = &j->run_clocks[r];
        if ((uint64_t)run->bus * 2 > j->stretch_bus && run->depth > depth && run->idx >= 0 && active_runtime
            && &active_runtime->table[run->idx] != j->entry) {
            owner = run->idx;
            depth = run->depth;
        }
    }
    return owner;
}

/* SHADOW: bring the active routine's machine copy up to date with s. */
/* After a call-out returns, SHADOW mode gives the native routine the
 * original's machine. A register the call declared don't-care and the callee
 * left as it found it is the native routine's own again, though: in ON mode
 * the callee carries the native value through, so a routine that relies on
 * the original's value there would pass here and ship a wrong register.
 * `mine` is the native routine's registers at the call, `was` the original's
 * then; the clone holds the original's now. */
static void keep_untouched(Cpu *clone, const Cpu *mine, const Cpu *was, uint16_t dont_care)
{
    if ((dont_care & (DC_A | DC_AL)) && (clone->a & 0xFF) == (was->a & 0xFF))
        clone->a = (uint16_t)((clone->a & 0xFF00) | (mine->a & 0xFF));
    if ((dont_care & (DC_A | DC_AH)) && (clone->a & 0xFF00) == (was->a & 0xFF00))
        clone->a = (uint16_t)((clone->a & 0xFF) | (mine->a & 0xFF00));
    if ((dont_care & DC_X) && clone->x == was->x)
        clone->x = mine->x;
    if ((dont_care & DC_Y) && clone->y == was->y)
        clone->y = mine->y;
    if ((dont_care & DC_D) && clone->d == was->d)
        clone->d = mine->d;
    if ((dont_care & DC_DB) && clone->db == was->db)
        clone->db = mine->db;
    static const struct { uint16_t dc; uint8_t flag; } flags[] = {
        { DC_N, CPU_FLAG_N }, { DC_V, CPU_FLAG_V }, { DC_Z, CPU_FLAG_Z }, { DC_C, CPU_FLAG_C },
    };
    for (int i = 0; i < 4; i++)
        if ((dont_care & flags[i].dc) && (clone->p & flags[i].flag) == (was->p & flags[i].flag))
            clone->p = (uint8_t)((clone->p & ~flags[i].flag) | (mine->p & flags[i].flag));
}

static void clone_refresh(NativeRuntime *rt, Active *a, Snes *s)
{
    int level = (int)(a - rt->active);
    if (!rt->clones[level])
        rt->clones[level] = malloc(sizeof *rt->clones[level]);
    a->clone = rt->clones[level];
    a->since = rt->clone_since[level] = snes_clone_since(a->clone, s, rt->clone_since[level]);
    count_from(a, a->clone);
    a->job->m = a->clone;
}

void native_destroy(NativeRuntime *rt)
{
    if (!rt)
        return;
    if (active_runtime == rt)
        active_runtime = NULL;
    while (rt->nactive > 0)
        active_pop(rt);
    for (int i = 0; i < rt->nparked; i++) {
        job_abandon(rt->parked[i].a.job);
        free(rt->parked[i].clone);
    }
    if (rt->attached && rt->acc)
        rt->attached->cpu.bus = rt->attached_bus;
    for (int i = 0; i < MAX_ACTIVE; i++)
        free(rt->clones[i]);
    free(rt->acc);
    free(rt->bnd);
    free(rt->replay_machine);
    free(rt->timing_copy);
    free(rt->timing_scratch);
    for (int i = 0; i < rt->count; i++)
        free(rt->stats[i].t_parts);
    free((void *)rt->table);
    free(rt->stats);
    free(rt);
}

int native_suspended(const NativeRuntime *rt) { return rt->nactive + rt->nparked; }
int native_parked(const NativeRuntime *rt) { return rt->nparked; }

uint64_t native_owed(const NativeRuntime *rt)
{
    uint64_t clocks = 0;
    for (int i = 0; i < rt->ndebts; i++)
        clocks += rt->debts[i].clocks;
    return clocks;
}

bool native_compares(const NativeRuntime *rt, uint32_t addr)
{
    for (int i = 0; i < rt->nactive; i++)
        if (rt->active[i].entry->addr == addr)
            return true;
    for (int i = 0; i < rt->nparked; i++)
        if (rt->parked[i].a.entry->addr == addr)
            return true;
    return false;
}

/* The first entry at pc (hand-written ones come first), or -1. */
static int find_entry(const NativeRuntime *rt, uint32_t pc)
{
    uint16_t a = pc & 0xFFFF;
    if (!(rt->filter[a >> 3] & (1 << (a & 7))))
        return -1;
    int lo = 0, hi = rt->count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (rt->table[mid].addr < pc)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < rt->count && rt->table[lo].addr == pc ? lo : -1;
}

/* Entry preconditions: register widths/modes on P, native mode (emulation mode
 * for the generated entries made for it), and the guard. A generated block also
 * needs the CPU running (not halted by WAI or STP). */
static bool entry_applies(const NativeEntry *e, Snes *s)
{
    if ((s->cpu.p & e->p_mask) != e->p_value || s->cpu.e != e->emulation)
        return false;
    if (e->generated && (s->cpu.waiting || s->cpu.stopped))
        return false;
    return !e->guard || e->guard(s);
}

/* Which of entry_applies' conditions turned a hand entry away, in words (it
 * did): the observer's `why`. The guard is the last asked, so it isn't asked
 * again here. */
static const char *refusal_reason(const NativeEntry *e, const Snes *s)
{
    if ((s->cpu.p & e->p_mask) != e->p_value)
        return "widths";
    if (s->cpu.e != e->emulation)
        return "emulation";
    return "guard";
}

const NativeEntry *native_hand_entry(Snes *s) { return native_hand_entry_at(s, cpu_pc24(&s->cpu)); }

const NativeEntry *native_hand_entry_at(Snes *s, uint32_t pc)
{
    const NativeRuntime *rt = active_runtime;
    int idx = rt ? find_entry(rt, pc) : -1;
    for (; idx >= 0 && idx < rt->count && rt->table[idx].addr == pc && !rt->table[idx].generated; idx++)
        if (entry_applies(&rt->table[idx], s))
            return &rt->table[idx];
    return NULL;
}

/* ---- comparison --------------------------------------------------------------- */

static void detail(NativeRuntime *rt, const char *prefix, const NativeEntry *e, const char *what)
{
    int kind = strcmp(prefix, "mismatch") != 0;
    if (rt->ndetails[kind] >= rt->detail_limit) {
        rt->dropped[kind]++;
        return;
    }
    snprintf(rt->details[kind][rt->ndetails[kind]++], sizeof rt->details[0][0], "%s %06X %s: %s", prefix, e->addr,
             e->name, what);
}

void native_set_details(NativeRuntime *rt, int limit)
{
    rt->detail_limit = limit < 0 ? 0 : limit > MAX_DETAILS ? MAX_DETAILS : limit;
}

static bool diff_bytes(char *out, size_t n, const char *name, const uint8_t *a, const uint8_t *b, size_t size,
                       size_t base)
{
    /* memcmp first: it is several times faster than a byte loop that stops at
     * the first difference, and this runs on every shadowed call. The loop
     * only runs to describe a difference. */
    if (!memcmp(a, b, size))
        return false;
    for (size_t i = 0; i < size; i++) {
        if (a[i] != b[i]) {
            snprintf(out, n, "%s[%04zX] original %02X native %02X", name, base + i, a[i], b[i]);
            return true;
        }
    }
    return false;
}

/* diff_bytes over the pages of a tracked memory (SNES_TRACK_PAGE_BITS) that
 * either machine wrote since `since`: the others are equal, as they were when
 * the copy was refreshed. */
static bool diff_pages(char *out, size_t n, const char *name, const uint8_t *a, const uint8_t *b, size_t size,
                       const uint64_t *a_epoch, const uint64_t *b_epoch, size_t count, uint64_t since)
{
    size_t page = size / count;
    for (size_t i = 0; i < count; i++)
        if ((a_epoch[i] >= since || b_epoch[i] >= since) &&
            diff_bytes(out, n, name, a + i * page, b + i * page, page, i * page))
            return true;
    return false;
}

#define CHECK_FIELD(label, x, y)                                                                         \
    if ((x) != (y)) {                                                                                    \
        snprintf(out, n, "%s original %lX native %lX", label, (unsigned long)(x), (unsigned long)(y));   \
        return true;                                                                                     \
    }

/* Returns true and describes the first difference if the machines differ in
 * anything CPU-side code can observe or change. Work RAM and VRAM are compared
 * only in the pages written since write epoch `since` (0: all of them). */
static bool compare_machines(const Snes *o, const Snes *c, uint16_t dont_care, uint64_t since, char *out, size_t n)
{
    uint8_t ignored_flags = (uint8_t)((dont_care & DC_N ? CPU_FLAG_N : 0) | (dont_care & DC_V ? CPU_FLAG_V : 0) |
                                      (dont_care & DC_Z ? CPU_FLAG_Z : 0) | (dont_care & DC_C ? CPU_FLAG_C : 0) |
                                      (dont_care & DC_WIDTH ? CPU_FLAG_M : 0));
    uint8_t p_mask = (uint8_t)~ignored_flags;
    bool a_low = !(dont_care & (DC_A | DC_AL)), a_high = !(dont_care & (DC_A | DC_AH));
    if (a_low && a_high) {
        CHECK_FIELD("A", o->cpu.a, c->cpu.a)
    } else if (a_low) {
        CHECK_FIELD("A's low byte", o->cpu.a & 0xFF, c->cpu.a & 0xFF)
    } else if (a_high) {
        CHECK_FIELD("A's high byte", o->cpu.a >> 8, c->cpu.a >> 8)
    }
    if (!(dont_care & DC_X))
        CHECK_FIELD("X", o->cpu.x, c->cpu.x)
    if (!(dont_care & DC_Y))
        CHECK_FIELD("Y", o->cpu.y, c->cpu.y)
    CHECK_FIELD("S", o->cpu.s, c->cpu.s)
    if (!(dont_care & DC_D))
        CHECK_FIELD("D", o->cpu.d, c->cpu.d)
    if (!(dont_care & DC_DB))
        CHECK_FIELD("DB", o->cpu.db, c->cpu.db)
    CHECK_FIELD("PB", o->cpu.pb, c->cpu.pb)
    CHECK_FIELD("PC", o->cpu.pc, c->cpu.pc)
    CHECK_FIELD("P", o->cpu.p & p_mask, c->cpu.p & p_mask)
    CHECK_FIELD("E", o->cpu.e, c->cpu.e)

    if (diff_pages(out, n, "WRAM", o->bus.wram, c->bus.wram, sizeof o->bus.wram, o->bus.wram_page_epoch,
                   c->bus.wram_page_epoch, SNES_WRAM_PAGES, since) ||
        diff_bytes(out, n, "SRAM", o->bus.sram, c->bus.sram, sizeof o->bus.sram, 0) ||
        diff_pages(out, n, "VRAM", o->ppu.vram, c->ppu.vram, sizeof o->ppu.vram, o->ppu.vram_page_epoch,
                   c->ppu.vram_page_epoch, sizeof o->ppu.vram_page_epoch / sizeof o->ppu.vram_page_epoch[0],
                   since) ||
        diff_bytes(out, n, "CGRAM", (const uint8_t *)o->ppu.cgram, (const uint8_t *)c->ppu.cgram,
                   sizeof o->ppu.cgram, 0) ||
        diff_bytes(out, n, "OAM", o->ppu.oam, c->ppu.oam, sizeof o->ppu.oam, 0))
        return true;

    /* PPU registers that enabled HDMA channels write each scanline change with
     * time, not with the code under test, so skip them and the state they feed. */
    static const uint8_t unit[8][4] = {
        { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 0, 0, 1, 1 },
        { 0, 1, 2, 3 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 0, 0, 1, 1 },
    };
    uint64_t hdma_regs = 0;
    for (int ch = 0; ch < 8; ch++) {
        if (!(o->bus.hdmaen & (1 << ch)))
            continue;
        const uint8_t *r = o->bus.dma[ch].reg;
        for (int i = 0; i < 4; i++) {
            uint8_t reg = (uint8_t)(r[1] + unit[r[0] & 7][i]);
            if (reg < 0x40)
                hdma_regs |= 1ull << reg;
        }
    }
    const Ppu *po = &o->ppu, *pc = &c->ppu;
    for (int reg = 0; reg < 0x40; reg++) {
        if (!(hdma_regs >> reg & 1) && po->reg[reg] != pc->reg[reg]) {
            snprintf(out, n, "PPU $21%02X original %02X native %02X", reg, po->reg[reg], pc->reg[reg]);
            return true;
        }
    }
    #define HDMA_TOUCHES(lo, hi) ((hdma_regs >> (lo)) & ((2ull << ((hi) - (lo))) - 1))
    if (!HDMA_TOUCHES(0x16, 0x19)) {
        CHECK_FIELD("VMADD", po->vram_addr, pc->vram_addr)
        CHECK_FIELD("VRAM prefetch", po->vram_prefetch, pc->vram_prefetch)
    }
    if (!HDMA_TOUCHES(0x21, 0x22)) {
        CHECK_FIELD("CGADD", po->cg_addr, pc->cg_addr)
        CHECK_FIELD("CGRAM flip-flop", po->cg_second, pc->cg_second)
        CHECK_FIELD("CGRAM latch", po->cg_latch, pc->cg_latch)
    }
    if (!HDMA_TOUCHES(0x02, 0x04)) {
        CHECK_FIELD("OAM reload", po->oam_reload, pc->oam_reload)
        CHECK_FIELD("OAMADD", po->oam_addr, pc->oam_addr)
        CHECK_FIELD("OAM latch", po->oam_latch, pc->oam_latch)
    }
    if (!HDMA_TOUCHES(0x0D, 0x14)) {
        CHECK_FIELD("BGnOFS latch", po->bgofs_latch, pc->bgofs_latch)
        CHECK_FIELD("BGnHOFS latch", po->bghofs_latch, pc->bghofs_latch)
    }
    if (!HDMA_TOUCHES(0x0D, 0x0E) && !HDMA_TOUCHES(0x1B, 0x20))
        CHECK_FIELD("Mode 7 latch", po->m7_latch, pc->m7_latch)
    #undef HDMA_TOUCHES

    const SnesBus *bo = &o->bus, *bc = &c->bus;
    CHECK_FIELD("NMITIMEN", bo->nmitimen, bc->nmitimen)
    CHECK_FIELD("WRIO", bo->wrio, bc->wrio)
    CHECK_FIELD("WRMPYA", bo->wrmpya, bc->wrmpya)
    CHECK_FIELD("WRMPYB", bo->wrmpyb, bc->wrmpyb)
    CHECK_FIELD("WRDIV", bo->wrdiv, bc->wrdiv)
    CHECK_FIELD("RDDIV", bo->rddiv, bc->rddiv)
    CHECK_FIELD("RDMPY", bo->rdmpy, bc->rdmpy)
    CHECK_FIELD("HTIME", bo->htime, bc->htime)
    CHECK_FIELD("VTIME", bo->vtime, bc->vtime)
    CHECK_FIELD("HDMAEN", bo->hdmaen, bc->hdmaen)
    CHECK_FIELD("MEMSEL", bo->fastrom, bc->fastrom)
    CHECK_FIELD("MDMAEN", bo->mdmaen, bc->mdmaen)
    CHECK_FIELD("WRAM port address", bo->wram_port_addr, bc->wram_port_addr)
    CHECK_FIELD("joypad latch", bo->latch, bc->latch)
    /* HDMA advances each channel's table address ($43x8-9), line counter ($43xA)
     * and indirect address ($43x5-6) every scanline, whatever the CPU is doing,
     * so those change with time and aren't compared. */
    const uint16_t hdma_timed = (1u << 5) | (1u << 6) | (1u << 8) | (1u << 9) | (1u << 10);
    for (int ch = 0; ch < 8; ch++) {
        for (int r = 0; r < 16; r++) {
            if (hdma_timed & (1u << r))
                continue;
            if (bo->dma[ch].reg[r] != bc->dma[ch].reg[r]) {
                snprintf(out, n, "DMA $43%X%X original %02X native %02X", ch, r, bo->dma[ch].reg[r],
                         bc->dma[ch].reg[r]);
                return true;
            }
        }
    }
    if (diff_bytes(out, n, "APU port (CPU to APU)", o->apu.cpu_in, c->apu.cpu_in, sizeof o->apu.cpu_in, 0))
        return true;
    return false;
}

bool native_compare(const Snes *o, const Snes *c, uint16_t dont_care, char *out, size_t n)
{
    return compare_machines(o, c, dont_care, 0, out, n);
}

void native_copy_popped_stack(const Snes *o, Snes *c, uint16_t lowest_s)
{
    for (uint32_t addr = (uint32_t)lowest_s + 1; addr <= o->cpu.s && addr < 0x2000; addr++)
        c->bus.wram[addr] = o->bus.wram[addr];
}

/* The stack bytes a port declared dead while they stay pushed, copied from
 * the original's machine where they are still above its S; or, those it left
 * on a stack where it stopped, wherever the original's S has gone since (to
 * another stack: a menu task's wait). */
static void copy_dead_stack(const Snes *o, Snes *c, const uint16_t *at, int n, bool left)
{
    for (int k = 0; k < n; k++)
        if ((left || at[k] > o->cpu.s) && at[k] < 0x2000)
            c->bus.wram[at[k]] = o->bus.wram[at[k]];
}

/* The flags of the P bytes a port saved and will restore itself, taken from
 * the original's where it has the byte pushed too: they are compared as the
 * registers they become again (native_flags_saved). */
enum { SAVED_FLAG_BITS = CPU_FLAG_N | CPU_FLAG_V | CPU_FLAG_Z | CPU_FLAG_C };

static void copy_saved_flags(const Snes *o, Snes *c, const uint16_t *at, int n)
{
    for (int k = 0; k < n; k++)
        if (at[k] > o->cpu.s && at[k] < 0x2000)
            c->bus.wram[at[k]] = (uint8_t)((c->bus.wram[at[k]] & ~SAVED_FLAG_BITS) | (o->bus.wram[at[k]] & SAVED_FLAG_BITS));
}

void native_flags_saved(Snes *s)
{
    Job *j = current_job;
    if (j && j->m == s && j->nsaved_flags < MAX_SAVED_FLAGS)
        j->saved_flags[j->nsaved_flags++] = (uint16_t)(s->cpu.s + 1);
}

void native_flags_restored(Snes *s)
{
    Job *j = current_job;
    if (j && j->m == s && j->nsaved_flags > 0)
        j->nsaved_flags--;
}

void native_stack_dont_care(Snes *s, uint16_t at, uint16_t bytes)
{
    Job *j = current_job;
    if (!j || j->m != s)
        return;
    for (uint16_t k = 0; k < bytes && j->ndead_stack < MAX_DEAD_STACK; k++)
        j->dead_stack[j->ndead_stack++] = (uint16_t)(at + k);
}

void native_stack_cared(Snes *s)
{
    Job *j = current_job;
    if (j && j->m == s)
        j->ndead_stack = 0;
}

uint16_t native_frame_dead_direct[MAX_DEAD_STACK];
int native_nframe_dead_direct;

void native_frame_enter(Snes *s, uint16_t bytes)
{
    s->cpu.s = (uint16_t)(s->cpu.s - bytes);
    Job *j = current_job;
    if (j && j->m == s) {
        native_stack_dont_care(s, (uint16_t)(s->cpu.s + 1), bytes);
        return;
    }
    for (uint16_t k = 0; k < bytes && native_nframe_dead_direct < MAX_DEAD_STACK; k++)
        native_frame_dead_direct[native_nframe_dead_direct++] = (uint16_t)(s->cpu.s + 1 + k);
}

void native_frame_leave(Snes *s, uint16_t bytes)
{
    Job *j = current_job;
    if (j && j->m == s) {
        /* the frame's bytes are no longer declared */
        uint16_t lo = (uint16_t)(s->cpu.s + 1), hi = (uint16_t)(s->cpu.s + bytes);
        int kept = 0;
        for (int k = 0; k < j->ndead_stack; k++)
            if (j->dead_stack[k] < lo || j->dead_stack[k] > hi)
                j->dead_stack[kept++] = j->dead_stack[k];
        j->ndead_stack = kept;
    } else {
        uint16_t lo = (uint16_t)(s->cpu.s + 1), hi = (uint16_t)(s->cpu.s + bytes);
        int kept = 0;
        for (int k = 0; k < native_nframe_dead_direct; k++)
            if (native_frame_dead_direct[k] < lo || native_frame_dead_direct[k] > hi)
                native_frame_dead_direct[kept++] = native_frame_dead_direct[k];
        native_nframe_dead_direct = kept;
    }
    s->cpu.s = (uint16_t)(s->cpu.s + bytes);
}

uint16_t native_stop_dont_care_direct;
uint32_t native_stop_at_direct;
bool native_stop_own_direct;
bool native_returned_direct;

void native_note_return(Snes *s)
{
    Job *j = current_job;
    if (j && j->m == s)
        j->returned = true;
    else
        native_returned_direct = true;
}

void native_note_rti(Snes *s)
{
    native_note_return(s);
    Job *j = current_job;
    if (j && j->m == s)
        j->returned_rti = true;
}

static void declare_stop(Snes *s, uint32_t at, uint16_t dont_care, bool own)
{
    Job *j = current_job;
    if (j && j->m == s) {
        j->stop_dont_care = dont_care;
        j->stop_at = at;
        j->stop_own = own;
    } else {
        native_stop_dont_care_direct = dont_care;
        native_stop_at_direct = at;
        native_stop_own_direct = own;
    }
}

void native_dont_care_at_stop(Snes *s, uint32_t at, uint16_t dont_care) { declare_stop(s, at, dont_care, false); }
void native_own_dont_care_at_stop(Snes *s, uint32_t at, uint16_t dont_care) { declare_stop(s, at, dont_care, true); }

bool native_is_return(const Snes *s, uint32_t at)
{
    uint8_t op = snes_bus_peek(&s->bus, at & 0xFFFFFF);
    return op == 0x60 || op == 0x6B || op == 0x40;
}

/* ---- intercept -------------------------------------------------------------- */

static bool interrupt_deliverable(const Snes *s)
{
    return s->cpu.nmi_pending || (s->cpu.irq_line && !(s->cpu.p & CPU_FLAG_I));
}

static NativeStats *stats_of(NativeRuntime *rt, const Active *a) { return &rt->stats[a->entry - rt->table]; }

/* `p`'s clocks among a stretch's parts in st's timing report. */
static void add_time_part(NativeStats *st, const SegmentPart *p)
{
    unsigned i = 0;
    while (i < st->t_nparts && st->t_parts[i].entry != p->entry)
        i++;
    if (i == st->t_nparts) {
        st->t_parts = realloc(st->t_parts, sizeof *st->t_parts * (i + 1));
        st->t_parts[i] = (NativeTimePart){ p->entry, 0, 0 };
        st->t_nparts++;
    }
    st->t_parts[i].bus += p->bus;
    st->t_parts[i].dma += p->dma;
}

/* Close a segment of original execution that native code stood in for. */
static void end_segment(NativeRuntime *rt, Active *a, Snes *s)
{
    NativeStats *st = stats_of(rt, a);
    uint64_t original = s->master - s->stall_total - a->segment_start;   /* CPU time */
    st->original_clocks += original;
    st->native_bus_clocks += a->segment_bus_clocks;
    st->t_bad_tags += a->segment_bad_tags;
    if (a->segment_bad_tags)
        st->t_bad_tag = a->segment_bad_tag;
    st->t_part_overflows += a->segment_part_overflows;
    /* Timing: the estimate against the original, which only a stretch no
     * interrupt fell in measures, and whether the stretch fit (its last
     * instruction, the step before this one, started before the horizon it had
     * when it began). Shadow mode's original runs on the interpreter, so this
     * is the same in every run whatever the ports' estimates are. */
    if (a->segment_interrupted)
        return;
    uint64_t estimate = estimate_parts(rt, a->entry, a->segment_bus_clocks, a->segment_dma_clocks, a->segment_parts,
                                       a->segment_nparts);
    /* the stretch's timing is its owner's (stretch_owner): the owner's own
     * clocks aren't a part of its own, and the running port's are */
    int owner = a->segment_owner;
    SegmentPart own = { (int)(a->entry - rt->table), a->segment_bus_clocks, a->segment_dma_clocks };
    if (owner >= 0)
        st = &rt->stats[owner];
    for (int k = 0; k < a->segment_nparts; k++) {
        const SegmentPart *p = &a->segment_parts[k];
        own.bus -= p->bus;
        own.dma -= p->dma;
        if (p->entry != owner)
            add_time_part(st, p);
    }
    if (owner >= 0 && own.bus)
        add_time_part(st, &own);
    uint64_t error = estimate > original ? estimate - original : original - estimate;
    st->t_stretches++;
    st->t_original_clocks += original;
    st->t_bus_clocks += a->segment_bus_clocks;
    st->t_dma_clocks += a->segment_dma_clocks;
    st->t_estimate_exact += error == 0;
    st->t_estimate_error += error;
    if (error > st->t_estimate_max_error)
        st->t_estimate_max_error = error > UINT32_MAX ? UINT32_MAX : (uint32_t)error;
    unsigned bucket = 0;
    for (uint64_t e = error; e && bucket < NATIVE_ERROR_BUCKETS - 1; e >>= 1)
        bucket++;
    st->t_error_hist[bucket]++;
    uint32_t error32 = error > UINT32_MAX ? UINT32_MAX : (uint32_t)error;
    /* A vblank stretch is one the original began and ended in the same vblank
     * (the frame counter goes up as vblank begins): the error of one that
     * outlasts it moves work that comes after it on the picture, in the
     * original's time as in the port's, so it is judged with those. */
    bool within_vblank = a->segment_vblank && s->frame == a->segment_frame && s->vline >= SNES_VBLANK_LINE;
    if (within_vblank) {
        st->t_vblank_stretches++;
        st->t_vblank_error_hist[bucket]++;
        if (error32 > st->t_vblank_max_error)
            st->t_vblank_max_error = error32;
    } else if (error32 > st->t_picture_max_error) {
        st->t_picture_max_error = error32;
    }
    if (st->t_nextra <= 8) {
        uint32_t extra = (uint32_t)(original - a->segment_bus_clocks);
        unsigned k = 0;
        while (k < st->t_nextra && k < 8 && st->t_extra[k] != extra)
            k++;
        if (k == st->t_nextra) {
            if (k < 8)
                st->t_extra[k] = extra;
            st->t_nextra++;
        }
    }
    if (rt->prev_step_master <= a->segment_horizon) {
        st->t_fit_horizon++;
        st->t_fit_horizon_clocks += original;
    }
    if (rt->prev_step_master <= a->segment_interrupt_horizon) {
        st->t_fit_interrupt++;
        st->t_fit_interrupt_clocks += original;
    }
}

/* SHADOW: whether the native routine's machine differs from the original's
 * now, with the first difference in why. An interrupt in the segment ran on
 * the original only: then nothing is compared (a->interrupted) and the answer
 * is false. */
static bool differs(Active *a, Snes *s, uint16_t dont_care, char *why, size_t n)
{
    if (a->segment_interrupted) {
        a->interrupted = true;
        return false;
    }
    /* The SPC700 clears the CPU-to-APU ports ($F1 bits 4-5) when its own code
     * decides to, whatever the CPU is doing: a pair it cleared while the
     * original ran isn't compared. */
    for (int pair = 0; pair < 2; pair++)
        if (apu_port_clears[pair] != a->segment_port_clears[pair])
            memcpy(a->clone->apu.cpu_in + 2 * pair, s->apu.cpu_in + 2 * pair, 2);
    /* Vblank reloads the OAM address (outside forced blank), so it moves if a
     * vblank passed while the original ran with NMI disabled. */
    if (s->frame != a->segment_frame)
        a->clone->ppu.oam_addr = s->ppu.oam_addr;
    /* What the original pushed and pulled again since the segment began is dead
     * (native_copy_popped_stack), and so is what the port declared so of what
     * it keeps pushed (native_stack_dont_care). */
    native_copy_popped_stack(s, a->clone, a->segment_low_s);
    /* And for an interrupt entry, what was popped of the stack it left
     * (follow_the_stack): the stack is in bank $00's low RAM, the first $2000
     * bytes of work RAM, so a bank-$00 address past them isn't work RAM. */
    for (uint32_t addr = (uint32_t)a->left_low_s + 1; addr <= a->left_s && addr < 0x2000; addr++)
        a->clone->bus.wram[addr] = s->bus.wram[addr];
    if (a->job) {
        copy_dead_stack(s, a->clone, a->job->dead_stack, a->job->ndead_stack, false);
        copy_saved_flags(s, a->clone, a->job->saved_flags, a->job->nsaved_flags);
    } else {
        copy_dead_stack(s, a->clone, a->left_dead_stack, a->nleft_dead_stack, true);
    }
    return compare_machines(s, a->clone, dont_care, a->since, why, n);
}

/* The most passes of the original through one point that reach_point
 * compares before it reports the first one's difference. */
#define MAX_POINT_PASSES 65536u

/* SHADOW: the native routine differed from the original at every pass through
 * the point it got to: report the first pass's difference. */
static void point_failed(NativeRuntime *rt, Active *a, const char *then)
{
    char msg[360];
    a->mismatched = true;
    if (a->failed_passes > 1)
        snprintf(msg, sizeof msg, "%.160s (and at %u later passes, %.24s; at the last: %.120s)", a->failed_why,
                 a->failed_passes - 1, then, a->last_why);
    else
        snprintf(msg, sizeof msg, "%.250s", a->failed_why);
    detail(rt, "mismatch", a->entry, msg);
}

/* SHADOW: the original is at the point the native routine got to (the call it
 * made, or where it finished) with the same S. Returns true when that settles
 * the comparison: the machines are the same, an interrupt fell in the segment
 * (nothing to compare), or they have differed at MAX_POINT_PASSES passes; false
 * to wait for the original's next pass through the same point, the first
 * pass's difference kept (a->failed_passes, a->failed_why).
 *
 * Why a later pass can count: a routine that stops at, or calls out from, an
 * instruction the original passes more than once (a loop's head, an
 * interpreter's dispatch) stands for some number of passes, which the address
 * doesn't say, and the original's first pass is often not the one. So every
 * pass is compared, and the first where the machines are the same is taken.
 * That can't let a wrong routine through. The comparison covers everything
 * the original's code can observe, except what is dead there (the declared
 * sets, which tools/dontcare_check.py checks against liveness at the point,
 * and the stack below S) and what changes with time rather than with code
 * (HDMA's registers, the SPC700's port clears, the OAM reload), which the
 * first pass leaves out as well. What the original does from a pass is
 * decided by the machine at that pass, so from a pass where the two are the
 * same the original goes on exactly as the machine does in ON mode from where
 * the native routine left it: whichever pass it is, what follows is what the
 * original did. A routine that is different at every pass is reported with the
 * first pass's difference, as when only the first pass counted: once the
 * original leaves the point's frame, or an interrupt comes (its handler runs
 * on the original only), or after MAX_POINT_PASSES; one still waiting when the
 * run ends counts as failed (native_failed). */
static bool reach_point(NativeRuntime *rt, Active *a, Snes *s, uint16_t dont_care, const char *where)
{
    char why[200];
    if (!differs(a, s, dont_care, why, sizeof why)) {
        a->failed_passes = 0;   /* (an interrupt after a failed pass is point_given_up's) */
        return true;
    }
    if (a->failed_passes++ == 0)
        snprintf(a->failed_why, sizeof a->failed_why, "%.48s: %.200s", where, why);
    else
        memcpy(a->last_why, why, sizeof a->last_why);
    if (a->failed_passes < MAX_POINT_PASSES)
        return false;
    point_failed(rt, a, "no more compared");
    a->failed_passes = 0;
    return true;
}

/* SHADOW: after a pass that differed, whether the comparison is over without
 * one that matched (the original left the point's frame, or an interrupt
 * came): then it is reported, and true. */
static bool point_given_up(NativeRuntime *rt, Active *a, bool left)
{
    if (!a->failed_passes || !(left || a->segment_interrupted))
        return false;
    point_failed(rt, a, left ? "then the original left" : "then an interrupt");
    a->failed_passes = 0;
    return true;
}

static void finish(NativeRuntime *rt)
{
    Active *a = &rt->active[rt->nactive - 1];
    NativeStats *st = stats_of(rt, a);
    if (rt->mode == NATIVE_SHADOW && rt->observer.finished)
        rt->observer.finished(rt->observer.ctx, a->entry);
    if (rt->mode == NATIVE_SHADOW) {
        if (a->mismatched)
            st->mismatched++;
        else if (a->interrupted)
            st->interrupted++;
        else
            st->matched++;
    }
    active_pop(rt);
}

/* ---- yields to another task (native_yield_call_dc) ----------------------------- */

/* A parked routine whose task won't go on from its yield: it never finishes,
 * as the original's routine wouldn't, and is counted so. */
static void drop_parked(NativeRuntime *rt, int i, const char *why)
{
    Active *a = &rt->parked[i].a;
    NativeStats *st = stats_of(rt, a);
    if (st->abandoned++ == 0) {
        char msg[160];
        snprintf(msg, sizeof msg, "parked in its yield to %06X (return %06X, S %04X): %s", a->job->call_target,
                 a->job->call_ret, a->job->call_s_after, why);
        detail(rt, "abandoned", a->entry, msg);
    }
    if (rt->mode == NATIVE_SHADOW) {
        if (a->mismatched)
            st->mismatched++;
        else if (a->interrupted)
            st->interrupted++;
        else
            st->matched++;
    }
    job_abandon(a->job);
    free(rt->parked[i].clone);
    memmove(&rt->parked[i], &rt->parked[i + 1], sizeof rt->parked[0] * (size_t)(rt->nparked - i - 1));
    rt->nparked--;
}

/* The innermost routine has just yielded (PH_CALLOUT, the machine at the
 * task switch): set it aside, its machine copy with it, so that the other
 * tasks' routines run above nothing of it. A yield to the same return and S
 * as one parked already means that one's stack has been used again. */
static void park(NativeRuntime *rt, Snes *s)
{
    int level = rt->nactive - 1;
    Active *a = &rt->active[level];
    const Job *j = a->job;
    for (int i = 0; i < rt->nparked; i++) {
        const Job *k = rt->parked[i].a.job;
        if (k->call_ret == j->call_ret && k->call_s_after == j->call_s_after) {
            drop_parked(rt, i, "yielded to again from the same place on the same stack");
            break;
        }
    }
    if (rt->nparked == MAX_PARKED)
        drop_parked(rt, 0, "no room for another: the longest parked dropped");
    if (rt->nparked == 0) {
        rt->parked_frame = s->frame;
        rt->parked_s = s->cpu.s;
    }
    int at = rt->nparked++;
    rt->parked[at].a = *a;
    rt->parked[at].clone = NULL;
    rt->parked[at].since = 0;
    if (rt->mode == NATIVE_SHADOW) {
        rt->parked[at].clone = rt->clones[level];
        rt->parked[at].since = rt->clone_since[level];
        rt->clones[level] = NULL;
        rt->clone_since[level] = 0;
    }
    a->job = NULL;
    a->clone = NULL;
    rt->nactive--;
    if (rt->observer.parked)
        rt->observer.parked(rt->observer.ctx, s, rt->parked[at].a.entry);
}

/* A parked routine whose task is back at its yield's return with the yield's
 * S goes back on top, in PH_CALLOUT, to go on as from any call-out. */
static bool unpark(NativeRuntime *rt, Snes *s, uint32_t pc)
{
    for (int i = 0; i < rt->nparked; i++) {
        const Job *j = rt->parked[i].a.job;
        if (pc != j->call_ret || s->cpu.s != j->call_s_after)
            continue;
        if (rt->nactive == MAX_ACTIVE) {
            drop_parked(rt, i, "back at its return with no room to go on");
            return false;
        }
        int level = rt->nactive++;
        Active *a = &rt->active[level];
        *a = rt->parked[i].a;
        if (rt->mode == NATIVE_SHADOW) {
            free(rt->clones[level]);
            rt->clones[level] = rt->parked[i].clone;
            rt->clone_since[level] = rt->parked[i].since;
            a->clone = rt->clones[level];
        }
        a->handler_away = false;
        memmove(&rt->parked[i], &rt->parked[i + 1], sizeof rt->parked[0] * (size_t)(rt->nparked - i - 1));
        rt->nparked--;
        if (rt->observer.resumed)
            rt->observer.resumed(rt->observer.ctx, s, a->entry, a->entry_s);
        return true;
    }
    return false;
}

/* ON: add time a port owes at where the machine is now. */
static void owe(NativeRuntime *rt, const Snes *s, uint32_t clocks)
{
    if (!clocks)
        return;
    uint32_t pc = cpu_pc24(&s->cpu);
    Debt *top = rt->ndebts ? &rt->debts[rt->ndebts - 1] : NULL;
    if (top && ((top->pc == pc && top->s == s->cpu.s) || rt->ndebts == MAX_DEBTS)) {
        top->clocks += clocks;   /* the same point (or no room: paid with the latest) */
        return;
    }
    rt->debts[rt->ndebts++] = (Debt){ .clocks = clocks, .pc = pc, .s = s->cpu.s };
}

/* A step of up to `clocks` that native code (a debt's payment, an idle wait)
 * takes as one, cut short to end just after the next point an interrupt can
 * be raised (snes_interrupt_horizon), by the step's last cycle, where the poll
 * that recognizes it is: so the interrupt is taken on time, not a step late.
 * An interrupt already raised that no poll has seen yet (the horizon is behind
 * us: a V-timer IRQ raised at the start of the line this step starts on) is
 * seen by a step as short as one cycle. */
static uint32_t until_interrupt(const Snes *s, uint32_t clocks)
{
    uint64_t h = snes_interrupt_horizon(s);
    uint64_t room = (h >= s->master ? h - s->master : 0) + 1 + CLOCKS_PER_IO_CYCLE_NATIVE;
    return room < clocks ? (uint32_t)room : clocks;
}

enum { DEBT_INTERRUPT = -1, DEBT_WAITS = -2 };

/* ON: the step that pays some of the latest debt; DEBT_INTERRUPT when the
 * interpreter is to take an interrupt here, or DEBT_WAITS when the machine is
 * inside that interrupt's handler and runs as usual.
 *
 * The original's time passes a scanline at a time, so frame boundaries and
 * line events still happen while the routine "runs". An interrupt the CPU
 * recognizes during that time is taken there, as the original would take it
 * somewhere inside the routine: the handler runs on time (it sees the port's
 * work already done, a timing difference), returns to the boundary, and the
 * rest is paid then. Holding interrupts until the whole debt was paid, as
 * this did before, started NMI handlers late by up to the length of the call
 * they interrupted, and pushed their video writes out of vblank (docs/REIMPLEMENTATION.md,
 * "Where video writes land"). So a payment step also ends just after the next
 * point an interrupt can be raised (snes_interrupt_horizon), where the poll
 * can see it. A port suspended in native_sync is interrupted the same way: its
 * PC and S stay put while it waits, the handler returns to them, and the port
 * goes on once the rest is paid (check_top). Holding interrupts for those
 * too made a line interrupt late during the sound driver's uploads. */
static int32_t pay_debt(NativeRuntime *rt, Snes *s, uint32_t pc)
{
    Debt *d = &rt->debts[rt->ndebts - 1];
    bool here = pc == d->pc && s->cpu.s == d->s;
    if (here)
        d->handler = false;   /* back from any handler taken here */
    else if (s->cpu.s < d->s)
        return DEBT_WAITS;   /* inside a handler taken during the payment: the debt waits for its return */
    else if (d->handler && s->master - d->handler_at < DEBT_HANDLER_CLOCKS)
        /* The same, for a handler that moved the stack above the boundary's
         * (the menus' vblank handler runs on a stack of its own at $1DFD).
         * Paying the rest inside it, as this did, held interrupts there for
         * as long as the port's time ran on: a port whose time spans a frame
         * had the next vblank's interrupt taken inside the handler of the
         * last, whose stack switch overwrote the S it had saved, and the game
         * ran into a BRK (docs/PORTING.md, "A port that spans frames"). */
        return DEBT_WAITS;
    if (here && interrupt_deliverable(s)) {
        d->handler = true;
        d->handler_at = s->master;
        return DEBT_INTERRUPT;
    }
    /* Not here, not deeper and no handler of its own running: the machine
     * left the boundary some other way (an interrupt it didn't take after
     * all, a handler that never came back): the time is paid where it is. */
    uint32_t spend = until_interrupt(s, d->clocks < SNES_CLOCKS_PER_LINE ? d->clocks : SNES_CLOCKS_PER_LINE);
    d->clocks -= spend;
    if (!d->clocks)
        rt->ndebts--;
    return (int32_t)spend;
}

bool (*native_return_poison)(uint32_t entry, bool to_a_bridge, uint64_t *bits, uint16_t *set);
static void scramble(Cpu *c, uint16_t dont_care, uint64_t bits);

/* ON: a port that returned to the original's code, its declared don't-care
 * registers scrambled where the tool asks (native_return_poison). */
static void poison_return(const NativeEntry *e, Snes *m, bool to_a_bridge)
{
    uint64_t bits;
    uint16_t set = e->dont_care;
    if (set && native_return_poison && native_return_poison(e->addr, to_a_bridge, &bits, &set))
        scramble(&m->cpu, (uint16_t)(set & e->dont_care), bits);
}

/* After running native code for `a`: account for it and record where it stopped. */
static void note_a_port_s_stack_switch(NativeRuntime *rt, const Snes *s, uint16_t entered_at);

static void after_run(NativeRuntime *rt, Active *a, Snes *s)
{
    Job *j = a->job;
    Snes *m = j->m;
    part_flush(j);
    if (rt->mode == NATIVE_ON) {
        owe(rt, s, (uint32_t)estimate_parts(rt, a->entry, m->bus.clocks, m->bus.dma_clocks - a->dma_mark, j->parts,
                                            j->nparts));
        m->bus.clocks = 0;
        if (j->state == JOB_SYNCING || j->state == JOB_WAITING) {
            a->phase = j->state == JOB_SYNCING ? PH_SYNC : PH_WAIT;
            a->sync_pc = cpu_pc24(&s->cpu);
            a->sync_s = s->cpu.s;
            a->wake = j->wake;
            a->wake_clocks = j->wake_clocks;
            a->wake_timed = false;
            a->polling_left = j->wait_polling;
            a->pass_clocks = j->wait_pass_clocks;
            a->woke = false;
            a->exit_passed = false;
            return;
        }
    } else {
        a->segment_bus_clocks = m->bus.clocks;
        a->segment_dma_clocks = m->bus.dma_clocks - a->dma_mark;
        if (!a->stretch_goes_on) {
            a->segment_start = s->master - s->stall_total;
            a->segment_frame = s->frame;
            a->segment_vblank = s->vline >= SNES_VBLANK_LINE;
            a->segment_horizon = snes_horizon(s);
            a->segment_interrupt_horizon = snes_interrupt_horizon(s);
            memcpy(a->segment_port_clears, apu_port_clears, sizeof apu_port_clears);
            a->segment_interrupted = false;
            a->segment_low_s = s->cpu.s;
            a->left_low_s = a->left_s = 0;
        }
        a->stretch_goes_on = j->state == JOB_LATCHING;
        a->wait_compared = false;
        a->at_rti = false;
        a->segment_nparts = j->nparts;
        memcpy(a->segment_parts, j->parts, sizeof *j->parts * (size_t)j->nparts);
        a->segment_bad_tags = j->bad_tags;
        a->segment_bad_tag = j->bad_tag;
        a->segment_part_overflows = j->part_overflows;
        a->segment_owner = stretch_owner(j);
    }
    if (rt->mode == NATIVE_ON && j->state == JOB_CALLING && !j->call_waits && !j->call_yields && rt->observer.called_out)
        rt->observer.called_out(rt->observer.ctx, s, a->entry, j->call_target);
    if (rt->mode == NATIVE_ON && j->state == JOB_DONE && !j->returned && rt->observer.stopped)
        rt->observer.stopped(rt->observer.ctx, s, a->entry, a->entry_s);
    if (rt->mode == NATIVE_ON && j->state == JOB_DONE && j->returned_rti && rt->observer.returned_from_interrupt)
        rt->observer.returned_from_interrupt(rt->observer.ctx, s, a->entry);
    if (rt->mode == NATIVE_ON && j->state == JOB_DONE && j->returned && !j->returned_rti)
        poison_return(a->entry, m, false);
    if (j->state == JOB_DONE) {
        memcpy(a->left_dead_stack, j->dead_stack, sizeof *j->dead_stack * (size_t)j->ndead_stack);
        a->nleft_dead_stack = j->ndead_stack;
        a->ret_dont_care = j->stop_dont_care;
        a->ret_at = j->stop_at;
        a->ret_own = j->stop_own;
        a->ret_returned = j->returned;
        job_put(j);
        a->job = NULL;
        a->ret_s = m->cpu.s;
        a->ret_pc = cpu_pc24(&m->cpu);
    }
    if (rt->mode == NATIVE_SHADOW && j->state == JOB_LATCHING) {
        a->phase = PH_LATCH;
        a->latch_mark = s->ppu.latch_count;
        return;
    }
    a->phase = rt->mode == NATIVE_ON ? PH_CALLOUT : PH_SEGMENT;
    a->handler_away = false;
    if (rt->mode == NATIVE_ON && !a->job) {
        uint16_t entered_at = a->entry_s;
        finish(rt);
        note_a_port_s_stack_switch(rt, s, entered_at);
    } else if (rt->mode == NATIVE_ON && a->job->call_yields) {
        park(rt, s);
    }
}

/* ON: whether the wait a port is suspended in is over (native_wait_loop,
 * native_wait_call): a handler has been taken where the port stands, the line
 * is in its horizontal blank, or the picture has reached the line, past the
 * vertical blank. */
static bool wait_over(Active *a, const Snes *s)
{
    if (a->wake == NATIVE_WAKE_HANDLER)
        return a->woke;
    if (a->wake == NATIVE_WAKE_OWN_TIME)
        return a->wake_clocks == 0;
    if (a->wake == NATIVE_WAKE_TIME) {
        if (!a->wake_timed) {
            a->wake_timed = true;
            a->wake_until = s->master + a->wake_clocks;
        }
        return s->master >= a->wake_until;
    }
    if (a->wake == NATIVE_WAKE_HBLANK)
        return s->bus.in_hblank;   /* what the original's poll of $4212 reads */
    if (native_wake_is_on_port0(a->wake))
        return native_wake_port0_satisfied(a->wake, s->apu.cpu_out[0]);
    return s->vline < SNES_VBLANK_LINE && s->vline >= (uint32_t)a->wake;
}

/* ON: the time a waiting port lets pass before it looks again: to the next
 * change of the hblank bit for a wait on it, a pass of the original's poll
 * for a wait on the sound processor (which answers in its own time), else a
 * line. */
static uint32_t wait_step(const Active *a, const Snes *s)
{
    if (a->wake == NATIVE_WAKE_HBLANK)
        return snes_clocks_to_next_poll_change(s);
    if (a->pass_clocks)
        return a->pass_clocks < a->polling_left ? a->pass_clocks : a->polling_left;
    if (native_wake_is_on_port0(a->wake))
        return NATIVE_PORT0_POLL_CLOCKS;
    if (a->wake == NATIVE_WAKE_TIME)
        return (uint32_t)(a->wake_until - s->master);
    if (a->wake == NATIVE_WAKE_OWN_TIME)
        return a->wake_clocks;
    return SNES_CLOCKS_PER_LINE;
}

/* SHADOW: the original at the head of the loop a port waits in, or at its
 * exit, with the port's S (native_wait_loop). The stretch the port ran
 * before the wait is compared at the first such point no interrupt came
 * before. Nothing the port waits on changes until a handler has run, so the
 * port goes on at the first point after one, with the handler's work taken
 * from the original and its own registers kept, or where the original leaves
 * the loop. The pass the handler fell in is the one not compared. A wait on
 * the sound processor goes on only where the original leaves, at the exit or
 * where it gives up (native_wait_loop_or_give_up). */
static bool wait_point(NativeRuntime *rt, Active *a, Snes *s, uint32_t pc)
{
    Job *j = a->job;
    /* the original comes to the loop's head before it can leave the loop: an
     * exit before that is where it left the last one (a loop that gives up
     * leaves where it goes round to start over) */
    if (!a->wait_compared && pc != j->call_target)
        return false;
    bool leaves = pc == j->wait_exit || (j->wait_given_up && pc == j->wait_given_up);
    Cpu mine = a->clone->cpu;
    if (!a->wait_compared) {
        /* the port stands in the loop where the original is */
        a->clone->cpu.pb = (uint8_t)(pc >> 16);
        a->clone->cpu.pc = (uint16_t)pc;
        bool same = reach_point(rt, a, s, j->call_dont_care, "at the wait");   /* interrupted: counted so */
        a->clone->cpu = mine;
        if (!same)
            return false;
        end_segment(rt, a, s);
        a->wait_compared = true;
    }
    /* a handler's work is what a wait on the handlers or the picture waits
     * for; the sound processor answers in its own time, when the original
     * leaves the loop */
    if (!leaves && (!a->segment_interrupted || native_wake_is_on_port0(j->wake)))
        return false;
    j->wait_answered = pc != j->wait_given_up || pc == j->wait_exit;
    clone_refresh(rt, a, s);
    a->clone->cpu = mine;
    job_run(a->job);
    after_run(rt, a, s);
    return true;
}

/* A stack switch past `limit` (a call's return, or an entry's;
 * NATIVE_STACK_SWITCH_DISTANCE) is not a return past it: the S the code comes
 * back to is noted as an interrupt's is (Active.handler_away). */
static void note_stack_switch(NativeRuntime *rt, Active *a, const Snes *s, uint16_t limit)
{
    if (!a->handler_away && rt->before_step_s <= limit &&
        (uint32_t)s->cpu.s > (uint32_t)limit + NATIVE_STACK_SWITCH_DISTANCE) {
        a->handler_away = true;
        a->handler_returns_s = rt->before_step_s;
    }
}

/* ON: a port that finished on another stack, a switch it made itself (the
 * menus' entry moves to their own): to the routine whose call-out it ran in,
 * the port's run is one step, from the S it was entered with, so the switch is
 * noted against that S as note_stack_switch notes one the original makes
 * instruction by instruction. */
static void note_a_port_s_stack_switch(NativeRuntime *rt, const Snes *s, uint16_t entered_at)
{
    if (!rt->nactive)
        return;
    Active *caller = &rt->active[rt->nactive - 1];
    if (caller->phase != PH_CALLOUT || !caller->job || caller->handler_away)
        return;
    uint16_t limit = caller->job->call_s_after;
    if (entered_at <= limit && (uint32_t)s->cpu.s > (uint32_t)limit + NATIVE_STACK_SWITCH_DISTANCE) {
        caller->handler_away = true;
        caller->handler_returns_s = entered_at;
    }
}

/* SHADOW: the highest S the original can be at and still be in the call `a`
 * compares: where the call began, past the return address and the caller's
 * frame the routine pulls (NativeEntry.pulls). */
static uint32_t top_of_the_call(const Active *a) { return (uint32_t)a->entry_s + 1u + a->entry->pulls; }

/* SHADOW: whether the original has returned from the call `a` compares while
 * the port is still running: S above the call's top, or for an interrupt
 * entry, an RTI run; not while the original is on a stack it switched to
 * (note_stack_switch). */
static bool returned_from_entry(NativeRuntime *rt, Active *a, const Snes *s)
{
    if (a->entry->interrupt)
        return a->returned_by_rti;
    if (a->phase == PH_SEGMENT) {   /* where the switch's return is followed (check_top) */
        uint16_t limit = (uint16_t)top_of_the_call(a);
        if (a->switched_away && s->cpu.s >= a->switched_returns_s && s->cpu.s <= limit)
            a->switched_away = false;
        if (!a->switched_away && !a->handler_away && rt->before_step_s <= limit &&
            (uint32_t)s->cpu.s > (uint32_t)limit + NATIVE_STACK_SWITCH_DISTANCE) {
            a->switched_away = true;
            a->switched_returns_s = rt->before_step_s;
        }
    }
    return !a->handler_away && !a->switched_away && s->cpu.s > top_of_the_call(a);
}

/* Check the innermost active routine against where the machine is now.
 * Returns true if it changed (so the next one may need checking). */
static bool check_top(NativeRuntime *rt, Snes *s, uint32_t pc, int32_t *result)
{
    Active *a = &rt->active[rt->nactive - 1];
    NativeStats *st = stats_of(rt, a);

    if (a->phase == PH_SEGMENT) {   /* SHADOW only */
        if (!a->job) {
            /* Above both the stop point and the entry: an early stop can be
             * below the entry (after pushes), where the original starts. An
             * interrupt entry's original has left once it has run its RTI. */
            bool left = a->entry->interrupt ? a->returned_by_rti
                                             : s->cpu.s > a->ret_s && s->cpu.s > a->entry_s + a->entry->pulls;
            if (point_given_up(rt, a, left)) {
                finish(rt);
                return true;
            }
            if (s->cpu.s == a->ret_s && pc == a->ret_pc) {
                /* the stop's address, the first time the original gets there */
                if (a->failed_passes == 0 && a->ret_at && a->ret_at != pc && !native_is_return(s, a->ret_at)) {
                    char msg[96];
                    snprintf(msg, sizeof msg, "stopped at %06X, but declared what is dead at %06X", pc,
                             a->ret_at);
                    a->mismatched = true;
                    detail(rt, "mismatch", a->entry, msg);
                } else if (a->failed_passes == 0 && !a->ret_at && !a->ret_returned && a->entry->dont_care) {
                    char msg[112];
                    snprintf(msg, sizeof msg, "stopped at %06X without declaring it (m_stop_at), with a don't-care set",
                             pc);
                    a->mismatched = true;
                    detail(rt, "mismatch", a->entry, msg);
                }
                if (!reach_point(rt, a, s, native_finish_dont_care(a->entry->dont_care, a->ret_dont_care, a->ret_own),
                                 "at return"))
                    return false;
                end_segment(rt, a, s);
                if (!a->ret_returned && rt->observer.stopped)
                    rt->observer.stopped(rt->observer.ctx, s, a->entry, a->entry_s);
                finish(rt);
                return true;
            }
            if (left) {
                /* An interrupt handler may switch stacks (the menu code runs
                 * tasks on their own stacks); nothing to compare then. */
                if (a->segment_interrupted)
                    a->interrupted = true;
                else {
                    a->mismatched = true;
                    detail(rt, "mismatch", a->entry,
                           a->entry->interrupt ? "original returned from the interrupt elsewhere than the native one"
                                               : "original left the routine without returning where the native one did");
                }
                finish(rt);
                return true;
            }
            return false;
        }
        const Job *j = a->job;
        if (a->handler_away && s->cpu.s >= a->handler_returns_s && s->cpu.s <= top_of_the_call(a))
            a->handler_away = false;   /* the interrupt has returned to the stretch */
        bool returned = !a->handler_away && returned_from_entry(rt, a, s);
        if (point_given_up(rt, a, returned)) {
            finish(rt);
            return true;
        }
        if (j->state == JOB_WAITING && s->cpu.s == a->clone->cpu.s &&
            (pc == j->call_target || pc == j->wait_exit || (j->wait_given_up && pc == j->wait_given_up)))
            return wait_point(rt, a, s, pc);
        if (pc == j->call_target && s->cpu.s == a->clone->cpu.s) {
            char where[48];
            snprintf(where, sizeof where, "at call to %06X returning to %06X", j->call_target, j->call_ret);
            if (!reach_point(rt, a, s, j->call_dont_care, where))
                return false;
            end_segment(rt, a, s);
            a->call_cpu = s->cpu;
            a->phase = PH_CALLOUT;
            a->handler_away = false;
            if (j->call_yields) {
                park(rt, s);
                return true;
            }
            if (rt->observer.called_out && !j->call_waits)
                rt->observer.called_out(rt->observer.ctx, s, a->entry, j->call_target);
            return false;
        }
        if (returned && a->segment_interrupted) {
            a->interrupted = true;
            finish(rt);
            return true;
        }
        if (returned) {
            char msg[96];
            snprintf(msg, sizeof msg, "original returned without the call to %06X the native routine made",
                     j->call_target);
            a->mismatched = true;
            detail(rt, "mismatch", a->entry, msg);
            finish(rt);
            return true;
        }
        return false;
    }

    if (a->phase == PH_LATCH) {   /* SHADOW only */
        if (a->segment_interrupted) {
            a->interrupted = true;   /* the latch may be the handler's */
            finish(rt);
            return true;
        }
        if (returned_from_entry(rt, a, s)) {
            a->mismatched = true;
            detail(rt, "mismatch", a->entry, "original returned without reading the beam where the native routine did");
            finish(rt);
            return true;
        }
        if (s->ppu.latch_count == a->latch_mark)
            return false;
        a->clone->ppu.h_latch = s->ppu.h_latch;
        a->clone->ppu.v_latch = s->ppu.v_latch;
        job_run(a->job);
        after_run(rt, a, s);
        return true;
    }

    if (a->phase == PH_WAIT) {   /* ON only; intercept has paid the debt by now */
        /* The machine stands where the port is while it waits: the handlers
         * taken there return to it, and until the wait is over the time
         * passes a step at a time, as the original's loop would pass it. */
        if (pc != a->sync_pc || s->cpu.s != a->sync_s || interrupt_deliverable(s))
            return false;
        bool over = wait_over(a, s);
        a->job->wait_answered = over;
        if (!over && (!a->pass_clocks || a->polling_left)) {
            uint32_t step = until_interrupt(s, wait_step(a, s));
            if (a->wake == NATIVE_WAKE_OWN_TIME)   /* the handlers taken meanwhile aren't the wait's */
                a->wake_clocks -= step < a->wake_clocks ? step : a->wake_clocks;
            if (a->pass_clocks)
                a->polling_left -= step < a->polling_left ? step : a->polling_left;
            *result = (int32_t)step;
            return true;
        }
        if (a->wake == NATIVE_WAKE_HBLANK && !a->exit_passed) {
            /* the original's last pass ends no sooner than a pass after
             * the blank began, nor sooner than its tail after the read that
             * saw it, which comes as late as now where the line's HDMA held
             * the processor into the blank */
            a->exit_passed = true;
            uint32_t into = snes_clocks_into_hblank(s);
            *result = (int32_t)(into < IDLE_EXIT_CLOCKS - POLL_TAIL_CLOCKS ? IDLE_EXIT_CLOCKS - into : POLL_TAIL_CLOCKS);
            return true;
        }
        count_from(a, s);
        job_run(a->job);
        after_run(rt, a, s);
        *result = 0;
        return true;
    }

    if (a->phase == PH_SYNC) {   /* ON only; intercept has paid the debt by now */
        /* An interrupt taken during the payment runs first, and one due now is
         * taken before the port goes on; it returns here. */
        if (pc != a->sync_pc || s->cpu.s != a->sync_s || interrupt_deliverable(s))
            return false;
        count_from(a, s);
        job_run(a->job);
        after_run(rt, a, s);
        *result = 0;
        return true;
    }

    /* PH_CALLOUT: waiting for the callee to return. */
    const Job *j = a->job;
    if (s->cpu.s == j->call_s_after && pc == j->call_ret) {
        if (interrupt_deliverable(s))
            return false;   /* resume after the interrupt returns here */
        if (rt->mode == NATIVE_SHADOW) {
            Cpu mine = a->clone->cpu;
            clone_refresh(rt, a, s);
            keep_untouched(&a->clone->cpu, &mine, &a->call_cpu, j->call_dont_care);
        } else {
            count_from(a, s);
        }
        job_run(a->job);
        after_run(rt, a, s);
        if (rt->mode == NATIVE_ON)
            *result = 0;   /* the machine moved; look again before interpreting */
        return true;
    }
    if (a->handler_away && s->cpu.s >= a->handler_returns_s && s->cpu.s <= j->call_s_after)
        a->handler_away = false;   /* the interrupt has returned to the callee, or it to its stack */
    note_stack_switch(rt, a, s, j->call_s_after);
    if (s->cpu.s > j->call_s_after && !a->handler_away) {
        /* The callee unwound past its own return (discarding the caller's frame);
         * the rest of the routine never runs, natively or originally. */
        st->abandoned++;
        if (st->abandoned == 1) {
            char msg[128];
            snprintf(msg, sizeof msg, "callee %06X unwound past its return: at %06X with S %04X above %04X",
                     j->call_target, pc, s->cpu.s, j->call_s_after);
            detail(rt, "abandoned", a->entry, msg);
        }
        finish(rt);
        return true;
    }
    return false;
}

/* SHADOW: whether the original is at the point `a` waits for, with the same
 * S: the call it made or where it finished (PH_SEGMENT), or where its
 * call-out returns (PH_CALLOUT). */
static bool at_point(const Active *a, const Snes *s, uint32_t pc)
{
    if (a->phase == PH_CALLOUT)
        return a->job && pc == a->job->call_ret && s->cpu.s == a->job->call_s_after;
    if (a->phase != PH_SEGMENT)
        return false;
    if (a->job)
        return (pc == a->job->call_target ||
                (a->job->state == JOB_WAITING &&
                 (pc == a->job->wait_exit || (a->job->wait_given_up && pc == a->job->wait_given_up)))) &&
               s->cpu.s == a->clone->cpu.s;
    return pc == a->ret_pc && s->cpu.s == a->ret_s;
}

/* SHADOW: entries started inside another's stretch.
 *
 * An entry the original reaches while it catches up with a native routine
 * (inside the stretch that routine stood in for: an inlined helper, or a
 * registered address in the middle of the routine's path) is compared on its
 * own, nested, as it always was. Only check_top's innermost routine is
 * checked, though, so a nested comparison still running when the original
 * gets to the point an enclosing routine waits for used to take that point
 * for itself -- the same call, typically -- and the enclosing comparison then
 * failed ("original returned without the call", PORTING.md's Call-outs). Now
 * the outermost routine waiting at the original's point comes first: the
 * nested ones are checked there, innermost first, as before, and one that
 * finishes or goes on (its own point is here too, or its callee unwound past
 * it) is dealt with as it was; any other is set aside, counted as abandoned and
 * not compared further (a difference it had shown stays a mismatch, and one
 * waiting for a later pass is one now). That compares less only for a call the
 * shipping mode never makes: there the enclosing routine runs natively and the
 * nested entry isn't called at all, and the enclosing comparison, which now
 * takes place, covers everything the stretch did. */
static void settle_nested(NativeRuntime *rt, Snes *s, uint32_t pc)
{
    int outer = -1;
    for (int i = 0; i + 1 < rt->nactive && outer < 0; i++)
        if (at_point(&rt->active[i], s, pc))
            outer = i;
    if (outer < 0)
        return;
    const NativeEntry *enclosing = rt->active[outer].entry;
    while (rt->nactive - 1 > outer) {
        Active *a = &rt->active[rt->nactive - 1];
        int32_t result = -1;
        if (check_top(rt, s, pc, &result))
            continue;   /* it finished here too, or went on, as it would have */
        NativeStats *st = stats_of(rt, a);
        if (a->failed_passes) {
            /* it differed here, and no later pass can be waited for */
            point_failed(rt, a, "then an enclosing routine's point");
            a->failed_passes = 0;
        }
        if (a->mismatched) {
            st->mismatched++;
        } else if (st->abandoned++ == 0) {
            char msg[160];
            snprintf(msg, sizeof msg, "started inside %06X %.40s's stretch and still running at its point: not compared",
                     enclosing->addr, enclosing->name);
            detail(rt, "abandoned", a->entry, msg);
        }
        /* set aside, its comparison is over: an observer's stretch for it
         * ends here (the fallback measure no longer counts what follows as
         * the port's) */
        if (rt->observer.finished)
            rt->observer.finished(rt->observer.ctx, a->entry);
        active_pop(rt);
        /* the enclosing comparison covers what the original runs from here,
         * which an observer that ended its stretch where the nested one began
         * at the same S (a task's loop's head reached from its wait) is told */
        if (rt->nactive - 1 == outer && rt->observer.resumed)
            rt->observer.resumed(rt->observer.ctx, s, enclosing, rt->active[outer].entry_s);
    }
}

/* ---- generated blocks in shadow mode ---------------------------------------------
 *
 * Cloning the machine for every block, as for hand ports, would cost a few
 * hundred kilobytes of copying and comparing per block entry. Instead the
 * interpreter's CPU bus accesses are recorded (address, value, direction, bus
 * time), with the registers and master clock at every instruction boundary.
 * Every so often the record is replayed: at each boundary where a generated
 * block applies (right widths, native mode, not an interrupt entry), the block
 * runs on a scratch CPU from the boundary's registers, with its bus accesses
 * served from the record. Each access must be the one the original made, in the
 * same order (instruction fetches included), with the same value written; where
 * the block stops, the original must be at an instruction boundary with the same
 * registers, and the same time must have passed. Everything a block can observe
 * comes through its accesses, so this checks every block the machine reaches,
 * with nothing sampled and no machine copies. An interrupt before the block
 * stops ends the check at the interrupt's boundary, where the registers and
 * time so far are compared (counted as interrupted). Blocks then chain: the next
 * check starts where this block stopped, as ON mode's dispatch would. */

#define RECORD_BOUNDARIES (1u << 17)
#define RECORD_ACCESSES (1u << 21)

static uint8_t record_read(void *ctx, uint32_t addr)
{
    SnesBus *bus = ctx;
    uint32_t before = bus->clocks;
    uint8_t v = snes_bus_read(ctx, addr);
    NativeRuntime *rt = (NativeRuntime *)active_runtime;
    if (rt && rt->attached && bus == &rt->attached->bus && rt->nacc < rt->cap_acc)
        rt->acc[rt->nacc++] = (Access){ addr, bus->clocks - before, v, false };
    return v;
}

static void record_write(void *ctx, uint32_t addr, uint8_t v)
{
    SnesBus *bus = ctx;
    uint32_t before = bus->clocks;
    snes_bus_write(ctx, addr, v);
    NativeRuntime *rt = (NativeRuntime *)active_runtime;
    if (rt && rt->attached && bus == &rt->attached->bus && rt->nacc < rt->cap_acc)
        rt->acc[rt->nacc++] = (Access){ addr, bus->clocks - before, v, true };
}

static void replay_fail(Replay *r, const char *why)
{
    if (!r->dead) {
        r->bad = true;
        snprintf(r->why, sizeof r->why, "%s", why);
    }
    r->dead = true;
}

/* The next recorded access, or NULL (and the replay stops consuming) if the
 * record ends here. */
static const Access *replay_next(Replay *r, const Cpu *c, uint32_t addr, bool fetch_start)
{
    if (r->dead)
        return NULL;
    if (r->pos >= r->end) {
        r->dead = true;
        r->exhausted = true;
        r->exhausted_at_fetch = fetch_start;
        r->exhaust_addr = addr;
        if (c)
            r->exhaust_cpu = *c;
        r->exhaust_clocks = r->clocks;
        return NULL;
    }
    return &r->rt->acc[r->pos];
}

static void describe_access(char *out, size_t n, const Access *a)
{
    if (a->write)
        snprintf(out, n, "wrote %02X to %06X", a->value, a->addr);
    else
        snprintf(out, n, "read %06X", a->addr);
}

static uint8_t replay_read(void *ctx, uint32_t addr)
{
    Replay *r = ctx;
    const Access *a = replay_next(r, NULL, addr, false);
    if (!a)
        return 0;
    if (a->write || a->addr != addr) {
        char orig[48], why[160];
        describe_access(orig, sizeof orig, a);
        snprintf(why, sizeof why, "block read %06X where the original %s", addr, orig);
        replay_fail(r, why);
        return 0;
    }
    r->pos++;
    r->clocks += a->clocks;
    return a->value;
}

static void replay_write(void *ctx, uint32_t addr, uint8_t v)
{
    Replay *r = ctx;
    const Access *a = replay_next(r, NULL, addr, false);
    if (!a)
        return;
    if (!a->write || a->addr != addr || a->value != v) {
        char orig[48], why[160];
        describe_access(orig, sizeof orig, a);
        snprintf(why, sizeof why, "block wrote %02X to %06X where the original %s", v, addr, orig);
        replay_fail(r, why);
        return;
    }
    r->pos++;
    r->clocks += a->clocks;
}

void native_replay_fetch(Cpu *c, uint32_t addr, unsigned n, uint8_t last)
{
    Replay *r = c->bus.ctx;
    for (unsigned i = 0; i < n; i++) {
        uint32_t at = (addr & 0xFF0000) | (uint16_t)(addr + i);
        const Access *a = replay_next(r, c, at, i == 0);
        if (!a)
            return;
        if (a->write || a->addr != at || (i == n - 1 && a->value != last)) {
            char orig[48], why[160];
            describe_access(orig, sizeof orig, a);
            snprintf(why, sizeof why, "block fetched %06X where the original %s", at, orig);
            replay_fail(r, why);
            return;
        }
        r->pos++;
        r->clocks += a->clocks;
    }
}

void native_replay_fetch_live(Cpu *c, uint32_t addr, unsigned n, unsigned start)
{
    Replay *r = c->bus.ctx;
    for (unsigned i = 0; i < n; i++) {
        uint32_t at = (addr & 0xFF0000) | (uint16_t)(addr + i);
        rc_timing.op[start + i] = 0;
        const Access *a = replay_next(r, c, at, i == 0);
        if (!a)
            return;
        if (a->write || a->addr != at) {
            char orig[48], why[160];
            describe_access(orig, sizeof orig, a);
            snprintf(why, sizeof why, "block fetched %06X where the original %s", at, orig);
            replay_fail(r, why);
            return;
        }
        rc_timing.op[start + i] = a->value;
        r->pos++;
        r->clocks += a->clocks;
    }
}

/* The first generated entry that applies to the machine as it is (widths,
 * emulation mode, guard), or -1. */
static int generated_applying(const NativeRuntime *rt, Snes *s)
{
    uint32_t pc = cpu_pc24(&s->cpu);
    int i = find_entry(rt, pc);
    if (i < 0)
        return -1;
    for (; i < rt->count && rt->table[i].addr == pc; i++) {
        const NativeEntry *e = &rt->table[i];
        if (e->generated && entry_applies(e, s))
            return i;
    }
    return -1;
}

static void record_boundary(NativeRuntime *rt, Snes *s)
{
    const Cpu *c = &s->cpu;
    uint16_t a16 = (uint16_t)c->pc;
    Boundary *b = &rt->bnd[rt->nbnd++];
    /* Guards look at RAM as it is now; decide those entries while recording. */
    b->gen = rt->record_filter[a16 >> 3] & (1 << (a16 & 7)) ? generated_applying(rt, s) : GEN_LATER;
    b->master = s->master - s->stall_total;
    b->acc = rt->nacc;
    b->frame = s->frame;
    b->pc24 = cpu_pc24(c);
    b->a = c->a, b->x = c->x, b->y = c->y, b->s = c->s, b->d = c->d;
    b->db = c->db, b->p = c->p, b->e = c->e;
    b->flags = (uint8_t)((interrupt_deliverable(s) ? B_INTERRUPT : 0) | (c->waiting ? B_WAITING : 0) |
                         (c->stopped ? B_STOPPED : 0));
}

static int find_generated(const NativeRuntime *rt, const Boundary *b)
{
    if (b->gen != GEN_LATER)
        return b->gen;
    int i = find_entry(rt, b->pc24);
    if (i < 0)
        return -1;
    for (; i < rt->count && rt->table[i].addr == b->pc24; i++) {
        const NativeEntry *e = &rt->table[i];
        if (e->generated && (b->p & e->p_mask) == e->p_value && e->emulation == b->e && !e->guard)
            return i;
    }
    return -1;
}

/* Registers where a replayed block stopped against a recorded boundary. */
static bool registers_differ(const Cpu *c, uint32_t pc24, const Boundary *b, char *out, size_t n)
{
#define REG(label, x, y)                                                                              \
    if ((x) != (y)) {                                                                                 \
        snprintf(out, n, "%s original %lX block %lX", label, (unsigned long)(y), (unsigned long)(x)); \
        return true;                                                                                  \
    }
    REG("PC", pc24, b->pc24)
    REG("A", c->a, b->a)
    REG("X", c->x, b->x)
    REG("Y", c->y, b->y)
    REG("S", c->s, b->s)
    REG("D", c->d, b->d)
    REG("DB", c->db, b->db)
    REG("P", c->p, b->p)
    REG("E", c->e, b->e)
    REG("WAI", c->waiting, (b->flags & B_WAITING) != 0)
    REG("STP", c->stopped, (b->flags & B_STOPPED) != 0)
#undef REG
    return false;
}

static void generated_mismatch(NativeRuntime *rt, int idx, const Boundary *start, const Boundary *at,
                               const char *why)
{
    rt->stats[idx].mismatched++;
    char msg[300];
    snprintf(msg, sizeof msg, "from frame %u, stopped at %06X: %s", (unsigned)start->frame,
             (unsigned)at->pc24, why);
    detail(rt, "mismatch", &rt->table[idx], msg);
}

/* Replay the record up to the last boundary whose blocks can be decided, and
 * drop what has been checked. `final`: the record ends here for good. */
static void replay_record(NativeRuntime *rt, bool final)
{
    uint32_t nb = rt->nbnd;
    if (nb == 0)
        return;
    /* next_int[i]: the first boundary at or after i where an interrupt entry runs */
    uint32_t *next_int = malloc(sizeof *next_int * nb);
    uint32_t next = nb;
    for (uint32_t i = nb; i-- > 0;) {
        if (rt->bnd[i].flags & B_INTERRUPT)
            next = i;
        next_int[i] = next;
    }
    Snes *m = rt->replay_machine;
    Replay *r = &rt->replay;
    uint32_t i = 0;
    while (i + 1 < nb) {
        const Boundary *b = &rt->bnd[i];
        if (b->flags & (B_INTERRUPT | B_WAITING | B_STOPPED)) {
            i++;
            continue;
        }
        int idx = find_generated(rt, b);
        if (idx < 0) {
            i++;
            continue;
        }
        uint32_t k = next_int[i + 1];
        bool open = k >= nb;
        memset(r, 0, offsetof(Replay, why));
        r->rt = rt;
        r->pos = b->acc;
        /* up to the interrupt, or to the latest boundary: accesses after it
         * belong to a step still in progress */
        r->end = open ? rt->bnd[nb - 1].acc : rt->bnd[k].acc;
        Cpu *c = &m->cpu;
        c->a = b->a, c->x = b->x, c->y = b->y, c->s = b->s, c->d = b->d;
        c->db = b->db, c->p = b->p, c->e = b->e;
        c->pb = b->pc24 >> 16, c->pc = b->pc24 & 0xFFFF;
        c->io_cycles = 0;
        c->waiting = c->stopped = false;
        c->bus = (CpuBus){ r, replay_read, replay_write };
        rc_timing = (RcTiming){ .limit = UINT32_MAX };
        rt->table[idx].fn(m);
        rc_timing = (RcTiming){ .limit = UINT32_MAX };

        if (r->bad) {
            rt->stats[idx].calls++;
            /* where in the original: the step holding the failing access */
            uint32_t lo = i, hi = nb - 1;
            while (lo < hi) {
                uint32_t mid = (lo + hi + 1) / 2;
                if (rt->bnd[mid].acc <= r->pos)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            generated_mismatch(rt, idx, b, &rt->bnd[lo], r->why);
            i++;
            continue;
        }
        if (r->exhausted) {
            if (open) {
                if (!final)
                    break;   /* the original hasn't got that far yet */
                rt->gen_unfinished++;
                i++;
                continue;
            }
            rt->stats[idx].calls++;
            const Boundary *at = &rt->bnd[k];
            char why[160];
            uint64_t spent = r->exhaust_clocks + 6ull * r->exhaust_cpu.io_cycles;
            if (!r->exhausted_at_fetch)
                generated_mismatch(rt, idx, b, at, "the original took an interrupt inside an instruction of the block");
            else if (registers_differ(&r->exhaust_cpu, r->exhaust_addr, at, why, sizeof why))
                generated_mismatch(rt, idx, b, at, why);
            else if (spent != at->master - b->master) {
                snprintf(why, sizeof why, "time original %llu block %llu (to an interrupt)",
                         (unsigned long long)(at->master - b->master), (unsigned long long)spent);
                generated_mismatch(rt, idx, b, at, why);
            } else
                rt->stats[idx].interrupted++;
            i = k;
            continue;
        }
        rt->stats[idx].calls++;
        /* the block stopped: the original's first boundary after the same accesses */
        uint32_t lo = i + 1, hi = nb;
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            if (rt->bnd[mid].acc < r->pos)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo >= nb || rt->bnd[lo].acc != r->pos) {
            generated_mismatch(rt, idx, b, &rt->bnd[lo >= nb ? nb - 1 : lo],
                               "the block stopped inside an instruction of the original");
            i++;
            continue;
        }
        const Boundary *at = &rt->bnd[lo];
        char why[160];
        uint64_t spent = r->clocks + 6ull * c->io_cycles;
        if (registers_differ(c, cpu_pc24(c), at, why, sizeof why))
            generated_mismatch(rt, idx, b, at, why);
        else if (spent != at->master - b->master) {
            snprintf(why, sizeof why, "time original %llu block %llu", (unsigned long long)(at->master - b->master),
                     (unsigned long long)spent);
            generated_mismatch(rt, idx, b, at, why);
        } else
            rt->stats[idx].matched++;
        i = lo;
    }
    free(next_int);
    if (final)
        i = nb;
    /* keep boundaries from i on */
    uint32_t base = i < nb ? rt->bnd[i].acc : rt->nacc;
    memmove(rt->acc, rt->acc + base, sizeof *rt->acc * (rt->nacc - base));
    rt->nacc -= base;
    memmove(rt->bnd, rt->bnd + i, sizeof *rt->bnd * (nb - i));
    rt->nbnd = nb - i;
    for (uint32_t j = 0; j < rt->nbnd; j++)
        rt->bnd[j].acc -= base;
}

/* At the start of every step. The record is checked when it fills up, right
 * after a boundary (so every recorded access belongs to a step that ended). */
static void record_step(NativeRuntime *rt, Snes *s)
{
    record_boundary(rt, s);
    if (rt->nbnd < rt->cap_bnd && rt->nacc + 0x10000 < rt->cap_acc)
        return;
    replay_record(rt, false);
    if (rt->nbnd >= rt->cap_bnd || rt->nacc + 0x10000 >= rt->cap_acc) {
        /* a block whose record doesn't fit: give up on it */
        rt->gen_unfinished++;
        rt->nbnd = 0;
        rt->nacc = 0;
        record_boundary(rt, s);
    }
}

/* ---- generated blocks in ON mode ------------------------------------------------
 *
 * A block runs against the system's horizon (snes_horizon): it stops before the
 * first instruction that would start past it, so everything the system does
 * between steps (line events, interrupt recognition, the frame's end) happens
 * between the same instructions as with the interpreter. I/O accesses first let
 * the machine catch up with the start of their instruction (rc_io_sync), which
 * is the time the interpreter's I/O sees. The step's time is what the block
 * spent beyond that, and its last cycle is the block's last instruction's. */

void rc_io_sync(uint32_t addr, bool write)
{
    uint16_t a = (uint16_t)addr;
    /* Registers nothing but CPU writes changes: the WRAM port and the math. */
    if ((a >= 0x2180 && a <= 0x2183) || (a >= 0x4202 && a <= 0x4206) || (a >= 0x4214 && a <= 0x4217))
        return;
    rc_timed_io++;
    uint32_t delta = rc_timing.insn_start - rc_timing.synced;
    if (delta) {
        snes_sync(rc_timing.s, delta);
        rc_timing.synced = rc_timing.insn_start;
    }
    /* NMITIMEN and the timer positions change when interrupts happen (and
     * enabling NMIs with RDNMI set requests one): let the system look again
     * after this instruction. */
    if (write && a >= 0x4200 && a <= 0x420A)
        rc_timing.limit = 0;
}

/* A RAM block wrote at `addr` (rc_snes_write): if that can change the block's
 * own code (its instructions' bytes, or anything through the WRAM port or a DMA
 * transfer), stop before the next instruction so the guard is checked again. */
void rc_code_write(uint32_t addr)
{
    uint8_t bank = (uint8_t)(addr >> 16);
    uint16_t a = (uint16_t)addr;
    uint32_t w;
    if (bank == 0x7E || bank == 0x7F)
        w = addr;
    else if (!(bank & 0x40) && a < 0x2000)
        w = 0x7E0000 | a;
    else {
        if (!(bank & 0x40) && (a == 0x2180 || a == 0x420B))
            rc_timing.limit = 0;
        return;
    }
    for (uint32_t i = 0; i < rc_timing.code_count; i++)
        if (w - rc_timing.code[2 * i] < rc_timing.code[2 * i + 1])
            rc_timing.limit = 0;
}

/* Address and widths, as generated_index's keys: pc | P's M and X << 24 | E << 30
 * (emulation mode counts as 8-bit). */
static inline uint32_t resume_key(const Snes *s)
{
    if (s->cpu.e)
        return cpu_pc24(&s->cpu) | (uint32_t)(CPU_FLAG_M | CPU_FLAG_X) << 24 | 1u << 30;
    return cpu_pc24(&s->cpu) | (uint32_t)(s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) << 24;
}

static inline unsigned resume_slot(uint32_t key) { return (key * 2654435761u) >> 20; }

/* Stands in for the entry of a block resumed where it stopped, for attribution
 * (native_running_entry). */
static const NativeEntry resumed_block = { .name = "(resumed generated block)", .generated = true };

static int32_t run_block(NativeRuntime *rt, Snes *s, NativeFn fn, const NativeEntry *entry)
{
    uint64_t horizon = snes_horizon(s);
    uint64_t room = horizon > s->master ? horizon - s->master : 0;
    s->bus.clocks = 0;
    s->cpu.io_cycles = 0;
    rc_timing = (RcTiming){ .s = s, .limit = room < UINT32_MAX ? (uint32_t)room : UINT32_MAX - 1 };
    running_direct = entry;
    fn(s);
    running_direct = NULL;
    uint32_t clocks = s->bus.clocks + 6 * s->cpu.io_cycles;
    uint32_t synced = rc_timing.synced;
    NativeFn stopped = rc_timing.stopped;
    bool (*stopped_guard)(Snes *) = rc_timing.stopped_guard;
    rc_timing = (RcTiming){ .limit = UINT32_MAX };
    s->cpu.io_cycles = 0;
    if (clocks == 0)
        return -1;   /* not resumable here after all: let the interpreter run it */
    s->intercept_last_cycle = s->cpu.last_cycle_io ? 6 : s->bus.clocks - s->bus.access_start;
    if (stopped) {
        rt->gen_stops++;
        uint32_t key = resume_key(s);
        unsigned slot = resume_slot(key);
        rt->resume[slot].key = key | 1u << 31;
        rt->resume[slot].fn = stopped;
        rt->resume[slot].guard = stopped_guard;
    }
    return (int32_t)(clocks - synced);
}

static const NativeEntry indexed_block = { .name = "(generated block at an instruction)", .generated = true };

void native_set_index(NativeRuntime *rt, const GeneratedIndexEntry *index, int count)
{
    rt->index = index;
    rt->index_count = count;
    memset(rt->index_filter, 0, sizeof rt->index_filter);
    for (int i = 0; i < count; i++) {
        uint16_t a = (uint16_t)index[i].key;
        rt->index_filter[a >> 3] |= (uint8_t)(1 << (a & 7));
    }
}

/* ON mode, where no entry applies: continue in generated code if a block stopped
 * here, or else if any block has the instruction (for code reached through a
 * computed jump, or where an interrupt handler took the stop's slot). */
static int32_t continue_generated(NativeRuntime *rt, Snes *s)
{
    if (s->cpu.waiting || s->cpu.stopped)
        return -1;
    uint32_t key = resume_key(s);
    unsigned slot = resume_slot(key);
    if (rt->gen_stops && rt->resume[slot].key == (key | 1u << 31) &&
        (!rt->resume[slot].guard || rt->resume[slot].guard(s))) {
        rt->gen_resumes++;
        return run_block(rt, s, rt->resume[slot].fn, &resumed_block);
    }
    uint16_t a16 = (uint16_t)key;
    if (!(rt->index_filter[a16 >> 3] & (1 << (a16 & 7))))
        return -1;
    int lo = 0, hi = rt->index_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (rt->index[mid].key < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (; lo < rt->index_count && rt->index[lo].key == key; lo++) {
        const GeneratedIndexEntry *x = &rt->index[lo];
        if (x->guard && !x->guard(s))
            continue;
        rt->gen_indexed++;
        return run_block(rt, s, x->fn, &indexed_block);
    }
    return -1;
}

/* ---- the timing oracle (PORT_TIMING_ORACLE) --------------------------------------- */

/* Put s back as it was when `copy` was refreshed from it (write epoch `since`),
 * keeping s's hooks. */
static void restore_machine(Snes *s, Snes *copy, uint64_t since)
{
    uint8_t *wram_written = s->bus.wram_written;
    void (*on_access)(SnesBus *) = s->bus.on_access;
    void (*cpu_on_insn)(void *, uint32_t, uint8_t, bool) = s->cpu.on_insn;
    void (*apu_on_insn)(void *, const Apu *) = s->apu.on_insn;
    void (*apu_on_dsp_write)(void *, const Apu *, uint8_t, uint8_t) = s->apu.on_dsp_write;
    void (*on_line)(void *, Snes *, uint16_t) = s->on_line;
    void (*on_frame_top)(void *, Snes *) = s->on_frame_top;
    int32_t (*icpt)(void *, Snes *) = s->intercept;
    bool (*on_miss)(void *, Snes *) = s->on_miss;
    snes_clone_since(s, copy, since);
    s->bus.wram_written = wram_written;
    s->bus.on_access = on_access;
    s->cpu.on_insn = cpu_on_insn;
    s->apu.on_insn = apu_on_insn;
    s->apu.on_dsp_write = apu_on_dsp_write;
    s->on_line = on_line;
    s->on_frame_top = on_frame_top;
    s->intercept = icpt;
    s->on_miss = on_miss;
}

/* Master clocks `cpu_clocks` of a port's time may take from now: exactly that
 * against snes_horizon, which no pause lies before; against the interrupt
 * horizon, a bound with the pauses of the lines it crosses. */
static uint64_t span_of(const NativeRuntime *rt, const Snes *s, uint64_t cpu_clocks)
{
    return rt->port_horizon_interrupts ? snes_span_bound(s, cpu_clocks) : cpu_clocks;
}

/* The oracle: exact time for a hand port (PORT_TIMING_ORACLE).
 *
 * A prototype, to test whether a port can be exact when given the original's
 * exact time (docs/REIMPLEMENTATION.md, "Can a hand port be exact?"): the port runs on
 * the machine, then the generated code for the call runs on a copy of the
 * machine as it was, to where the port stopped, which gives the exact time,
 * the last cycle and whether it did anything that depends on the time. Only if
 * it did nothing of the kind and all of it fits before the horizon (so nothing
 * the system does falls inside it) is the port's call kept, charged that time;
 * otherwise the machine is put back and the generated block, exact by itself,
 * runs instead. The copy's end state doubles as a check of the port. */

/* The generated code that runs at the machine's PC, or NULL. */
static NativeFn generated_at(const NativeRuntime *rt, Snes *s)
{
    int idx = generated_applying(rt, s);
    if (idx >= 0)
        return rt->table[idx].fn;
    uint32_t key = resume_key(s);
    uint16_t a16 = (uint16_t)key;
    if (!(rt->index_filter[a16 >> 3] & (1 << (a16 & 7))))
        return NULL;
    int lo = 0, hi = rt->index_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (rt->index[mid].key < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (; lo < rt->index_count && rt->index[lo].key == key; lo++)
        if (!rt->index[lo].guard || rt->index[lo].guard(s))
            return rt->index[lo].fn;
    return NULL;
}

typedef struct OracleTime {
    uint64_t clocks;        /* CPU time to the return */
    uint64_t last_start;    /* when its last instruction started */
    uint32_t last_cycle;
    bool ok;                /* timed, with nothing time-dependent inside */
    bool untimed;           /* no generated code for part of it */
} OracleTime;

/* Run generated code on m from its PC until it reaches end_pc with S at end_s
 * (where the port stopped). */
static OracleTime oracle_time(const NativeRuntime *rt, Snes *m, uint32_t end_pc, uint16_t end_s, uint64_t room)
{
    OracleTime o = { .last_cycle = CLOCKS_PER_IO_CYCLE_NATIVE, .ok = true };
    unsigned long io_before = rc_timed_io;
    NativeFn next = NULL;
    for (int n = 0; o.ok; n++) {
        NativeFn fn = next ? next : generated_at(rt, m);
        if (!fn || n > 1000000 || m->cpu.waiting || m->cpu.stopped) {
            o.ok = false;
            o.untimed = true;
            break;
        }
        m->bus.clocks = 0;
        m->cpu.io_cycles = 0;
        rc_timing = (RcTiming){ .s = m, .limit = UINT32_MAX - 1 };
        fn(m);
        uint32_t t = m->bus.clocks + CLOCKS_PER_IO_CYCLE_NATIVE * m->cpu.io_cycles;
        o.last_start = o.clocks + rc_timing.insn_start;
        next = rc_timing.stopped;
        if (rc_timing.limit == 0)
            o.ok = false;   /* it made an interrupt deliverable, or wrote code */
        rc_timing = (RcTiming){ .limit = UINT32_MAX };
        m->cpu.io_cycles = 0;
        if (!t) {
            o.ok = false;
            break;
        }
        o.clocks += t;
        if (o.clocks > room + 0x10000) {
            o.last_start = o.clocks;   /* far past the room: it can't fit, wherever it ends */
            break;
        }
        o.last_cycle = m->cpu.last_cycle_io ? CLOCKS_PER_IO_CYCLE_NATIVE : m->bus.clocks - m->bus.access_start;
        if (cpu_pc24(&m->cpu) == end_pc && m->cpu.s == end_s)
            break;
        if (m->cpu.s > end_s) {
            o.ok = false;   /* the original went elsewhere */
            o.untimed = true;
        }
    }
    if (rc_timed_io != io_before)
        o.ok = false;
    return o;
}

/* ON, PORT_TIMING_ORACLE: run hand entry e exactly if it can be. Returns false
 * (with the machine put back) to leave the call to the generated block. */
static bool oracle_run(NativeRuntime *rt, const NativeEntry *e, NativeStats *st, Snes *s, int32_t *result)
{
    if (!rt->timing_copy)
        rt->timing_copy = malloc(sizeof *rt->timing_copy);
    if (!rt->timing_scratch)
        rt->timing_scratch = malloc(sizeof *rt->timing_scratch);
    uint64_t backup_since = rt->timing_copy_since = snes_clone_since(rt->timing_copy, s, rt->timing_copy_since);
    Snes *m = rt->timing_scratch;
    uint64_t since = rt->timing_scratch_since = snes_clone_since(m, s, rt->timing_scratch_since);
    uint64_t horizon = rt->port_horizon_interrupts ? snes_interrupt_horizon(s) : snes_horizon(s);
    uint64_t room = horizon > s->master ? horizon - s->master : 0;

    Active *a = &rt->active[rt->nactive++];
    memset(a, 0, sizeof *a);
    a->segment_owner = -1;
    a->entry = e;
    a->entry_s = s->cpu.s;
    a->job = job_get();
    a->job->entry = e;
    a->job->stop_dont_care = 0;
    a->job->stop_at = 0;
    a->job->stop_own = false;
    a->job->returned = false;
    a->job->part = -1;
    a->job->depth = 0;
    a->job->runs = 0;
    a->job->ndead_stack = 0;
    a->job->m = s;
    count_from(a, s);
    job_run(a->job);
    bool escaped = a->job->state != JOB_DONE;
    if (escaped) {
        /* it called out or synced: the prototype only runs whole calls */
        active_pop(rt);
    } else {
        job_put(a->job);
        a->job = NULL;
        rt->nactive--;
    }
    OracleTime o = { 0 };
    if (!escaped)
        o = oracle_time(rt, m, cpu_pc24(&s->cpu), s->cpu.s, room);
    if (escaped || !o.ok || span_of(rt, s, o.last_start) > room || o.clocks > INT32_MAX) {
        restore_machine(s, rt->timing_copy, backup_since);
        rt->timing_copy_since = rt->timing_scratch_since = 0;
        st->t_fallback++;
        if (escaped)
            st->t_escapes++;
        else if (o.untimed)
            st->t_untimed++;
        else if (!o.ok)
            st->t_timed_io++;
        else
            st->t_no_fit++;
        return false;
    }

    /* The port's registers must be the original's: an interrupt right after
     * the return pushes them. Registers the callers don't read (dont_care) may
     * differ; count those and take the original's. */
    Cpu *c = &s->cpu;
    const Cpu *oc = &m->cpu;
    if (c->a != oc->a || c->x != oc->x || c->y != oc->y || c->p != oc->p || c->d != oc->d || c->db != oc->db) {
        st->t_register_fixups++;
        c->a = oc->a, c->x = oc->x, c->y = oc->y, c->p = oc->p, c->d = oc->d, c->db = oc->db;
    }
    s->bus.open_bus = m->bus.open_bus;
    char why[200];
    if (compare_machines(m, s, 0, since, why, sizeof why)) {
        if (!st->t_state_diffs) {
            char msg[256];
            snprintf(msg, sizeof msg, "port state differs from the timed original: %s", why);
            detail(rt, "timing", e, msg);
        }
        st->t_state_diffs++;
    }
    st->t_exact++;
    s->bus.clocks = 0;
    s->intercept_last_cycle = o.last_cycle;
    *result = (int32_t)o.clocks;
    return true;
}

/* The most an instruction pulls (RTI's P, PC and bank) or pushes (an
 * interrupt's): S moving further in a step is a stack switch. */
#define MAX_PULL 4
#define MAX_PUSH 4
#define OP_RTI 0x40

/* SHADOW: the original's stack, for the comparisons waiting for it: the lowest
 * S each segment has reached (what was pushed and pulled again below S is
 * dead, native_copy_popped_stack); and for an interrupt entry
 * (NativeEntry.interrupt), the RTI that ends its call, and a jump of S past
 * what an instruction pushes or pulls, which is the handler moving to a stack
 * of its own or back: the popped region starts again there, and the stack it
 * left keeps its own, so the bytes between the two stacks are compared, not
 * taken as popped. */
static void follow_the_stack(NativeRuntime *rt, const Snes *s, uint32_t pc)
{
    uint16_t sp = s->cpu.s;
    bool moved = sp > rt->prev_s + MAX_PULL || sp + MAX_PUSH < rt->prev_s;
    bool at_rti = !s->cpu.e && snes_bus_peek(&s->bus, pc) == OP_RTI;
    for (int i = 0; i < rt->nactive; i++) {
        Active *a = &rt->active[i];
        if (a->phase != PH_SEGMENT && a->phase != PH_LATCH)
            continue;
        if (a->entry->interrupt) {
            if (moved) {
                a->left_low_s = a->segment_low_s;
                a->left_s = rt->prev_s;
                a->segment_low_s = sp;
            }
            if (a->at_rti)
                a->returned_by_rti = true;   /* the RTI seen at the step before has run */
            a->at_rti = at_rti;
        }
        if (sp < a->segment_low_s)
            a->segment_low_s = sp;
    }
    rt->prev_s = sp;
}

/* Parked routines whose tasks can't go on from their yields any more, asked
 * of each (NativeGoesOn) where that may have changed: at a frame's start (a
 * task stopped, the mode closed) and at a stack switch (another task going
 * on, or this one started afresh above its yield). */
static void drop_the_dropped(NativeRuntime *rt, Snes *s)
{
    bool new_frame = s->frame != rt->parked_frame;
    bool switched = s->cpu.s > rt->parked_s + MAX_PULL || s->cpu.s + MAX_PUSH < rt->parked_s;
    rt->parked_frame = s->frame;
    rt->parked_s = s->cpu.s;
    if (!new_frame && !switched)
        return;
    for (int i = rt->nparked - 1; i >= 0; i--) {
        const Job *j = rt->parked[i].a.job;
        if (!j->goes_on(s, j->call_s_after))
            drop_parked(rt, i, "its task can't go on from it");
    }
}

static int32_t intercept(void *ctx, Snes *s)
{
    NativeRuntime *rt = ctx;
    uint32_t pc = cpu_pc24(&s->cpu);

    if (rt->acc)
        record_step(rt, s);
    rt->prev_step_master = rt->step_master;
    rt->step_master = s->master;
    rt->before_step_s = rt->step_s;
    rt->step_s = s->cpu.s;

    /* ON: a handler taken where a port waits for one (PH_WAIT), whether the
     * time it owes is still being paid there or not. */
    if (rt->mode == NATIVE_ON && rt->nactive) {
        Active *top = &rt->active[rt->nactive - 1];
        if (top->phase == PH_WAIT && pc == top->sync_pc && s->cpu.s == top->sync_s && interrupt_deliverable(s))
            top->woke = true;
    }

    /* Native code finishes instantly but the original took time: pay it
     * (pay_debt), unless an interrupt taken during the payment is running. */
    if (rt->ndebts) {
        int32_t spend = pay_debt(rt, s, pc);
        if (spend != DEBT_WAITS)
            return spend;   /* the time, or -1: the interpreter takes the interrupt */
    }

    if (rt->mode == NATIVE_SHADOW)
        follow_the_stack(rt, s, pc);

    if (rt->mode == NATIVE_SHADOW && rt->nactive > 1)
        settle_nested(rt, s, pc);
    int32_t result = -1;
    while (rt->nactive > 0 && check_top(rt, s, pc, &result)) {
        if (result >= 0)
            return result;
    }
    /* a task back at a parked routine's yield goes on in it (an interrupt
     * due here is taken first, and returns here) */
    while (rt->nparked && !interrupt_deliverable(s) && unpark(rt, s, pc)) {
        while (rt->nactive > 0 && check_top(rt, s, pc, &result)) {
            if (result >= 0)
                return result;
        }
    }
    if (rt->nparked)
        drop_the_dropped(rt, s);

    if (interrupt_deliverable(s)) {
        for (int i = 0; i < rt->nactive; i++) {
            Active *a = &rt->active[i];
            if (a->phase == PH_SEGMENT || a->phase == PH_LATCH)
                a->segment_interrupted = true;
            if (a->phase == PH_CALLOUT && !a->handler_away && a->job && s->cpu.s <= a->job->call_s_after) {
                a->handler_away = true;
                a->handler_returns_s = s->cpu.s;
            }
            if (a->phase == PH_SEGMENT && !a->handler_away && a->job && !a->entry->interrupt &&
                s->cpu.s <= top_of_the_call(a)) {
                a->handler_away = true;
                a->handler_returns_s = s->cpu.s;
            }
        }
        return -1;
    }

    int idx = find_entry(rt, pc);
    /* ON, for the observer: the first hand entry here that turned the call away */
    const NativeEntry *refused = NULL;
    const char *refused_why = NULL;
    for (; idx >= 0 && idx < rt->count && rt->table[idx].addr == pc; idx++) {
        const NativeEntry *e = &rt->table[idx];
        NativeStats *st = &rt->stats[idx];

        if (e->generated) {
            if (refused && rt->observer.refused) {
                rt->observer.refused(rt->observer.ctx, s, refused, refused_why);
                refused = NULL;
            }
            /* SHADOW checks generated blocks by replaying the record */
            if (rt->mode != NATIVE_ON || !entry_applies(e, s))
                continue;
            st->calls++;
            return run_block(rt, s, e->fn, e);
        }

        /* SHADOW: the original back at the start of the routine the innermost
         * comparison is of, with the S it was entered with, is still inside
         * that comparison's stretch, not in a new call (a call pushes its
         * return): a block move's instruction ($7E:9690's MVN) runs again in
         * place for each byte it moves. So it isn't compared again, which
         * would nest a comparison a byte. */
        if (rt->mode == NATIVE_SHADOW && rt->nactive > 0) {
            const Active *in = &rt->active[rt->nactive - 1];
            if (in->entry == e && in->phase == PH_SEGMENT && in->entry_s == s->cpu.s)
                return -1;
        }

        st->calls++;

        /* SHADOW: a port waiting for the original to latch the beam
         * (native_await_latch) is resumed by the original's next latch, which
         * only the innermost comparison watches; an entry the original reaches
         * meanwhile is inside the stretch that port stands for, so it isn't
         * compared on its own, which would hide the latch from the port (the
         * stars' scatter, $7E:5777, inside the setups that scatter them). Such
         * calls are counted on the entry's line, as not compared; its guard
         * doesn't run. */
        if (rt->mode == NATIVE_SHADOW && rt->nactive > 0 && rt->active[rt->nactive - 1].phase == PH_LATCH) {
            st->in_latch_wait++;
            return -1;
        }

        if (rt->page_report)
            count_page(st, s->cpu.d);
        if (!entry_applies(e, s)) {
            st->skipped++;
            if (!refused) {
                refused = e;
                refused_why = refusal_reason(e, s);
            }
            continue;
        }

        if (rt->mode == NATIVE_ON && rt->port_timing == PORT_TIMING_ORACLE && e->idle)
            continue;   /* the oracle runs only whole routines */

        if (e->idle) {
            if (rt->mode != NATIVE_ON) {
                st->skipped++;
                if (rt->observer.idle)
                    rt->observer.idle(rt->observer.ctx, s, e);
                /* the original runs the pass: no later entry here applies,
                 * and none turned it away */
                return -1;
            }
            if (native_census_hooks.entered)
                native_census_hooks.entered(native_census_hooks.ctx, s, e);
            running_direct = e;
            e->fn(s);
            running_direct = NULL;
            if (cpu_pc24(&s->cpu) == pc)
                return (int32_t)(e->idle_clocks ? e->idle_clocks
                                                : until_interrupt(s, snes_clocks_to_next_poll_change(s)));
            return e->idle_clocks ? 0 : IDLE_EXIT_CLOCKS;
        }

        if (rt->nactive == MAX_ACTIVE) {
            st->skipped++;
            if (rt->observer.skipped)
                rt->observer.skipped(rt->observer.ctx, s, e);
            continue;
        }
        if (rt->mode == NATIVE_ON && rt->port_timing == PORT_TIMING_ORACLE) {
            int32_t result;
            if (oracle_run(rt, e, st, s, &result))
                return result;
            continue;   /* the generated block at the same address runs */
        }
        Active *a = &rt->active[rt->nactive++];
        memset(a, 0, sizeof *a);
        a->segment_owner = -1;
        a->entry = e;
        a->entry_s = s->cpu.s;
        a->job = job_get();
        a->job->entry = e;
        a->job->stop_dont_care = 0;
        a->job->stop_at = 0;
        a->job->stop_own = false;
        a->job->returned = false;
        a->job->returned_rti = false;
        a->job->part = -1;
        a->job->depth = 0;
        a->job->runs = 0;
        a->job->ndead_stack = 0;
        a->job->nsaved_flags = 0;
        if (rt->observer.entered)
            rt->observer.entered(rt->observer.ctx, s, e);
        if (rt->mode == NATIVE_ON && native_census_hooks.entered)
            native_census_hooks.entered(native_census_hooks.ctx, s, e);
        if (rt->mode == NATIVE_SHADOW) {
            clone_refresh(rt, a, s);
        } else {
            a->job->m = s;
            count_from(a, s);
        }
        job_run(a->job);
        after_run(rt, a, s);
        return rt->mode == NATIVE_ON ? 0 : -1;
    }
    if (refused && rt->observer.refused)
        rt->observer.refused(rt->observer.ctx, s, refused, refused_why);
    return rt->mode == NATIVE_ON && rt->generated ? continue_generated(rt, s) : -1;
}

unsigned long native_call_step_limit = 50000000ul;
unsigned long native_call_steps;
unsigned long native_call_timeouts;
void (*native_call_timeout_hook)(void);
bool (*native_call_poison)(uint32_t site, uint32_t target, uint64_t *bits);
void (*native_call_stop)(uint32_t site, uint32_t target, uint16_t dont_care);

/* Interpret a call-out's callee in place, up to its return (ret, with S back
 * at s_after), interrupts held off and no time passing. */
static void call_in_place_to(Snes *s, uint32_t site, uint32_t target, uint32_t ret, uint32_t or_ret, uint16_t s_after)
{
    bool nmi = s->cpu.nmi_pending, irq = s->cpu.irq_line;
    s->cpu.nmi_pending = false;
    s->cpu.irq_line = false;
    while (!((cpu_pc24(&s->cpu) == ret || (or_ret && cpu_pc24(&s->cpu) == or_ret)) && s->cpu.s == s_after)) {
        if (native_call_steps >= native_call_step_limit) {
            native_call_timeouts++;
            if (native_call_timeout_hook)
                native_call_timeout_hook();
            else if (native_call_timeouts == 1)
                fprintf(stderr, "native_call: %06X called from %06X did not return\n", target, site);
            break;
        }
        const NativeRuntime *rt = active_runtime;
        uint32_t pc = cpu_pc24(&s->cpu);
        int idx = rt ? find_entry(rt, pc) : -1;
        while (idx >= 0 && idx < rt->count && rt->table[idx].addr == pc &&
               (rt->table[idx].idle || !entry_applies(&rt->table[idx], s)))
            idx++;
        if (idx >= 0 && idx < rt->count && rt->table[idx].addr == pc) {
            rt->table[idx].fn(s);
            if (rt->table[idx].generated)
                rc_timing = (RcTiming){ .limit = UINT32_MAX };
        } else if (s->interpreter) {
            s->interpreter(&s->cpu);
        } else {
            /* Without the interpreter (a player built without it runs hand ports
             * only inside the runtime, so this is a host's mistake): give up. */
            native_call_timeouts++;
            fprintf(stderr, "native_call: %06X called from %06X reached %06X, which only the interpreter runs\n",
                    target, site, pc);
            break;
        }
        native_call_steps++;
    }
    s->cpu.nmi_pending = nmi;
    s->cpu.irq_line = irq;
}

static void call_in_place(Snes *s, uint32_t site, uint32_t target, uint32_t ret, uint16_t s_after)
{
    call_in_place_to(s, site, target, ret, 0, s_after);
}

/* native_call_poison: the registers and flags `dont_care` names, each made to
 * differ from the caller's (a byte of A, X, Y, D or DB xored with a value that
 * isn't 0, a flag inverted). */
static void scramble(Cpu *c, uint16_t dont_care, uint64_t bits)
{
    uint8_t k[6];
    for (int i = 0; i < 6; i++) {
        k[i] = (uint8_t)(bits >> (8 * i));
        if (!k[i])
            k[i] = 0xA5;
    }
    bool index8 = (c->p & CPU_FLAG_X) != 0;
    if (dont_care & (DC_A | DC_AL))
        c->a ^= k[0];
    if (dont_care & (DC_A | DC_AH))
        c->a ^= (uint16_t)(k[1] << 8);
    if (dont_care & DC_X)
        c->x ^= (uint16_t)(index8 ? k[2] : k[2] | k[3] << 8);
    if (dont_care & DC_Y)
        c->y ^= (uint16_t)(index8 ? k[4] : k[4] | k[5] << 8);
    if (dont_care & DC_D)
        c->d ^= (uint16_t)(k[3] | k[0] << 8);
    if (dont_care & DC_DB)
        c->db ^= k[5];
    c->p ^= (uint8_t)((dont_care & DC_N ? CPU_FLAG_N : 0) | (dont_care & DC_V ? CPU_FLAG_V : 0) |
                      (dont_care & DC_Z ? CPU_FLAG_Z : 0) | (dont_care & DC_C ? CPU_FLAG_C : 0));
}

bool (*native_site_poison)(uint32_t site, uint32_t target, uint64_t *bits, uint16_t *set);

/* After a scrambled call: what the callee left alone is the caller's own
 * again, as shadow mode and the shipping mode have it. The callee ran twice
 * from the same machine, with the caller's values (clean) and scrambled. Left
 * alone, a register (a byte of A, a flag) ends with the caller's value in the
 * first run and the scrambled one in the second; written without being read,
 * with the same value in both, which can't be both, since the two differ. */
static uint16_t untouched(uint16_t out, uint16_t mine, uint16_t scrambled, uint16_t clean, uint16_t mask)
{
    if ((out & mask) == (scrambled & mask) && (clean & mask) == (mine & mask))
        return (uint16_t)((out & ~mask) | (mine & mask));
    return out;
}

static void keep_untouched_scrambled(Cpu *out, const Cpu *mine, const Cpu *scrambled, const Cpu *clean,
                                     uint16_t dont_care)
{
    if (dont_care & (DC_A | DC_AL))
        out->a = untouched(out->a, mine->a, scrambled->a, clean->a, 0x00FF);
    if (dont_care & (DC_A | DC_AH))
        out->a = untouched(out->a, mine->a, scrambled->a, clean->a, 0xFF00);
    if (dont_care & DC_X)
        out->x = untouched(out->x, mine->x, scrambled->x, clean->x, 0xFFFF);
    if (dont_care & DC_Y)
        out->y = untouched(out->y, mine->y, scrambled->y, clean->y, 0xFFFF);
    if (dont_care & DC_D)
        out->d = untouched(out->d, mine->d, scrambled->d, clean->d, 0xFFFF);
    if (dont_care & DC_DB)
        out->db = (uint8_t)untouched(out->db, mine->db, scrambled->db, clean->db, 0xFF);
    static const struct { uint16_t dc; uint8_t flag; } flags[] = {
        { DC_N, CPU_FLAG_N }, { DC_V, CPU_FLAG_V }, { DC_Z, CPU_FLAG_Z }, { DC_C, CPU_FLAG_C },
    };
    for (int i = 0; i < 4; i++)
        if (dont_care & flags[i].dc)
            out->p = (uint8_t)untouched(out->p, mine->p, scrambled->p, clean->p, flags[i].flag);
}

/* The clean run's machine, and whether a scrambled call is under way (a
 * call-out the callee's own ports make isn't scrambled: the port being fuzzed
 * is the one whose declarations are in question). */
static Snes *poison_trial;
static bool poisoning;

/* The call from `site`, its return `ret` pushed as the original pushes it (a
 * JSR's or a PER's address less one, a JSL's with its bank first). */
static void call_out_returning(Snes *s, uint32_t site, uint32_t target, uint32_t ret, bool long_call,
                               uint16_t dont_care, bool waits, NativeGoesOn yields)
{
    uint16_t s_after = s->cpu.s;
    if (long_call)
        push8_raw(s, ret >> 16);
    push16_raw(s, (uint16_t)((uint16_t)ret - 1));
    s->cpu.pb = target >> 16;
    s->cpu.pc = target & 0xFFFF;

    uint64_t poison_bits;
    uint16_t poison_set = dont_care;
    if (dont_care && native_site_poison && native_site_poison(site, target, &poison_bits, &poison_set))
        scramble(&s->cpu, (uint16_t)(poison_set & dont_care), poison_bits);

    Job *j = current_job;
    if (j && j->m == s) {
        j->state = JOB_CALLING;
        j->call_target = target;
        j->call_ret = ret;
        j->call_s_after = s_after;
        j->call_dont_care = dont_care;
        j->call_waits = waits;
        j->call_yields = yields != NULL;
        j->goes_on = yields;
        fiber_switch(fiber_main());
        return;
    }

    /* Not inside the runtime (tests, tools): interpret the callee right here,
     * unless the tool ends the run at this call. */
    if (native_call_stop)
        native_call_stop(site, target, dont_care);
    uint64_t bits;
    if (!dont_care || poisoning || !native_call_poison || !native_call_poison(site, target, &bits)) {
        call_in_place(s, site, target, ret, s_after);
        return;
    }
    if (!poison_trial && !(poison_trial = malloc(sizeof *poison_trial))) {
        call_in_place(s, site, target, ret, s_after);
        return;
    }
    /* A callee that runs past the budget abandons the port here (the tool's
     * timeout hook), so the flag is cleared again before every call. */
    poisoning = true;
    snes_clone(poison_trial, s);
    call_in_place(poison_trial, site, target, ret, s_after);
    Cpu clean = poison_trial->cpu, mine = s->cpu;
    scramble(&s->cpu, dont_care, bits);
    Cpu scrambled = s->cpu;
    call_in_place(s, site, target, ret, s_after);
    keep_untouched_scrambled(&s->cpu, &mine, &scrambled, &clean, dont_care);
    poisoning = false;
}

void native_poison_sibling_return(Snes *s, const NativeEntry *e) { poison_return(e, s, true); }

void native_poison_as_call(Snes *s, uint32_t site, uint32_t target, uint16_t dont_care)
{
    uint64_t bits;
    uint16_t set = dont_care;
    if (dont_care && native_site_poison && native_site_poison(site, target, &bits, &set))
        scramble(&s->cpu, (uint16_t)(set & dont_care), bits);
}

static void call_out(Snes *s, uint32_t site, uint32_t target, bool long_call, uint16_t dont_care, bool waits,
                     NativeGoesOn yields)
{
    uint32_t ret = (site & 0xFF0000) | (uint16_t)(site + (long_call ? 4 : 3));
    call_out_returning(s, site, target, ret, long_call, dont_care, waits, yields);
}

void native_call_dc(Snes *s, uint32_t site, uint32_t target, bool long_call, uint16_t dont_care)
{
    call_out(s, site, target, long_call, dont_care, false, NULL);
}

void native_yield_call_dc(Snes *s, uint32_t site, uint32_t target, bool long_call, uint16_t dont_care,
                          NativeGoesOn goes_on)
{
    call_out(s, site, target, long_call, dont_care, false, goes_on);
}

void native_jump_call_dc(Snes *s, uint32_t site, uint32_t target, uint32_t returns_to, uint16_t dont_care)
{
    call_out_returning(s, site, target, returns_to, false, dont_care, false, NULL);
}

void native_wait_call(Snes *s, uint32_t site, uint32_t target, bool long_call, uint16_t dont_care, int32_t wake)
{
    Job *j = current_job;
    if (j && j->m == s && active_runtime && active_runtime->mode == NATIVE_ON) {
        j->state = JOB_WAITING;
        j->call_target = target;
        j->wait_exit = 0;
        j->wait_given_up = 0;
        j->wait_polling = j->wait_pass_clocks = 0;
        j->wake = wake;
        fiber_switch(fiber_main());
        return;
    }
    call_out(s, site, target, long_call, dont_care, true, NULL);
}

static void wait_for_time(Snes *s, uint32_t clocks, int32_t wake)
{
    Job *j = current_job;
    if (!j || j->m != s || !active_runtime || active_runtime->mode != NATIVE_ON || !clocks)
        return;
    j->state = JOB_WAITING;
    j->call_target = 0;
    j->wait_exit = 0;
    j->wait_given_up = 0;
    j->wait_polling = j->wait_pass_clocks = 0;
    j->wake = wake;
    j->wake_clocks = clocks;
    fiber_switch(fiber_main());
}

void native_wait_time(Snes *s, uint32_t clocks) { wait_for_time(s, clocks, NATIVE_WAKE_TIME); }
void native_wait_own_time(Snes *s, uint32_t clocks) { wait_for_time(s, clocks, NATIVE_WAKE_OWN_TIME); }

bool native_waits_in_place(const Snes *s)
{
    const Job *j = current_job;
    return j && j->m == s && active_runtime && active_runtime->mode == NATIVE_ON;
}

/* A port's wait in a loop of its own, under the runtime: the machine stands
 * where the port is (ON), or the original runs the loop (SHADOW), and the port
 * is compared at its head with `dont_care` left out. */
static bool wait_in_the_loop(Job *j, uint32_t head, uint32_t exit, uint32_t given_up, int32_t wake, uint32_t passes,
                             uint32_t pass_clocks, uint16_t dont_care)
{
    j->state = JOB_WAITING;
    j->call_target = head;
    j->wait_exit = exit;
    j->wait_given_up = given_up;
    j->wait_polling = passes * pass_clocks;
    j->wait_pass_clocks = given_up ? pass_clocks : 0;
    j->wait_answered = true;
    j->wake = wake;
    j->call_dont_care = dont_care;
    j->call_waits = false;
    j->call_yields = false;
    fiber_switch(fiber_main());
    return j->wait_answered;
}

bool native_wait_loop_or_give_up(Snes *s, uint32_t head, uint32_t exit, uint32_t given_up, int32_t wake,
                                 uint32_t passes, uint32_t pass_clocks)
{
    Job *j = current_job;
    if (j && j->m == s && active_runtime)
        return wait_in_the_loop(j, head, exit, given_up, wake, passes, pass_clocks, NATIVE_WAIT_DONT_CARE);
    /* Outside the runtime: the original's loop, from its head until it leaves
     * (no interrupts come, so a loop that waits for one runs out of steps),
     * unless the tool ends the run at its head (ct_fuzz --stop-at-call). */
    uint8_t pb = s->cpu.pb;
    uint16_t at = s->cpu.pc;
    s->cpu.pb = (uint8_t)(head >> 16);
    s->cpu.pc = (uint16_t)head;
    if (native_call_stop)
        native_call_stop(head, head, NATIVE_WAIT_DONT_CARE);
    call_in_place_to(s, head, head, exit, given_up, s->cpu.s);
    bool answered = cpu_pc24(&s->cpu) != given_up || given_up == exit;
    s->cpu.pb = pb;
    s->cpu.pc = at;
    return answered;
}

void native_wait_loop(Snes *s, uint32_t head, uint32_t exit, int32_t wake)
{
    native_wait_loop_or_give_up(s, head, exit, 0, wake, 0, 0);
}

bool native_wait_loop_held(Snes *s, uint32_t head, uint32_t exit, int32_t wake, uint16_t dont_care)
{
    Job *j = current_job;
    if (j && j->m == s && active_runtime && active_runtime->mode == NATIVE_SHADOW) {
        (void)wait_in_the_loop(j, head, exit, 0, wake, 0, 0, (uint16_t)(dont_care | j->entry->dont_care));
        return true;
    }
    s->cpu.pb = (uint8_t)(head >> 16);
    s->cpu.pc = (uint16_t)head;
    native_dont_care_at_stop(s, head, dont_care);
    return false;
}

void native_call_poison_reset(void)
{
    poisoning = false;
}

void native_sync(Snes *s)
{
    Job *j = current_job;
    if (!j || j->m != s || !active_runtime || active_runtime->mode != NATIVE_ON || !s->bus.clocks)
        return;
    j->state = JOB_SYNCING;
    fiber_switch(fiber_main());
}

void native_await_latch(Snes *s)
{
    Job *j = current_job;
    if (!j || j->m != s || !active_runtime)
        return;
    if (active_runtime->mode == NATIVE_ON) {
        native_sync(s);
        return;
    }
    if (active_runtime->mode != NATIVE_SHADOW)
        return;
    j->state = JOB_LATCHING;
    fiber_switch(fiber_main());
}

void native_call(Snes *s, uint32_t site, uint32_t target, bool long_call)
{
    native_call_dc(s, site, target, long_call, 0);
}

static int time_as(Snes *s, uint32_t routine, bool sibling)
{
    Job *j = current_job;
    const NativeRuntime *rt = active_runtime;
    if (!j || j->m != s || !rt)
        return -1;
    int was = j->part;
    part_flush(j);
    int idx = find_entry(rt, routine & 0xFFFFFF);
    if (idx < 0 || rt->table[idx].generated) {
        j->bad_tags++;   /* nothing to estimate it with: it stays the port's own */
        j->bad_tag = routine;
        idx = -1;
    } else if (&rt->table[idx] == j->entry) {
        idx = -1;
    } else if (native_census_hooks.as_c) {
        native_census_hooks.as_c(native_census_hooks.ctx, j->entry, &rt->table[idx]);
    }
    if (j->depth < MAX_TIME_SCOPES)
        j->scopes[j->depth] = (TimeScope){ idx, sibling ? ++j->runs : 0 };
    j->depth++;
    j->part = idx;
    return was;
}

int native_time_as(Snes *s, uint32_t routine) { return time_as(s, routine, false); }
int native_time_as_sibling(Snes *s, uint32_t routine) { return time_as(s, routine, true); }

void native_time_end(Snes *s, int was)
{
    Job *j = current_job;
    if (!j || j->m != s || !active_runtime)
        return;
    part_flush(j);
    if (j->depth > 0)
        j->depth--;
    j->part = was;
}

void native_charge_as(Snes *s, uint32_t routine, uint32_t bus_clocks)
{
    Job *j = current_job;
    if (!j || j->m != s || !active_runtime)
        return;
    int was = native_time_as(s, routine);
    s->bus.clocks += bus_clocks;
    native_time_end(s, was);
}

uint32_t native_uncounted_begin(Snes *s)
{
    Job *j = current_job;
    if (!j || j->m != s || !active_runtime)
        return 0;
    part_flush(j);
    return s->bus.clocks;
}

void native_uncounted_end(Snes *s, uint32_t mark)
{
    Job *j = current_job;
    if (!j || j->m != s || !active_runtime || s->bus.clocks < mark)
        return;
    s->bus.clocks = mark;
    j->part_bus = mark;
}

void native_attach(NativeRuntime *rt, Snes *snes)
{
    if (rt->mode == NATIVE_OFF)
        return;
    snes->intercept = intercept;
    snes->intercept_ctx = rt;
    rt->attached = snes;
    for (int i = 0; i < MAX_ACTIVE; i++)
        rt->clone_since[i] = 0;   /* copies of another machine, if any */
    if (rt->mode == NATIVE_SHADOW && rt->generated && !rt->acc) {
        rt->cap_acc = RECORD_ACCESSES;
        rt->cap_bnd = RECORD_BOUNDARIES;
        rt->acc = malloc(sizeof *rt->acc * rt->cap_acc);
        rt->bnd = malloc(sizeof *rt->bnd * rt->cap_bnd);
        rt->replay_machine = calloc(1, sizeof *rt->replay_machine);
        rt->attached_bus = snes->cpu.bus;
        snes->cpu.bus.read = record_read;
        snes->cpu.bus.write = record_write;
    }
}

/* ---- reporting ---------------------------------------------------------------- */

/* Check what the record holds so far. */
static void replay_pending(const NativeRuntime *crt, bool final)
{
    NativeRuntime *rt = (NativeRuntime *)crt;
    if (rt->acc && rt->nbnd)
        replay_record(rt, final);
}

void native_report(const NativeRuntime *rt, FILE *out)
{
    static const char *modes[] = { "off", "shadow", "on" };
    replay_pending(rt, true);
    fprintf(out, "native routines (%s mode):\n", modes[rt->mode]);
    unsigned long gen_calls = 0, gen_matched = 0, gen_mismatched = 0, gen_interrupted = 0, gen_reached = 0;
    for (int i = 0; i < rt->count; i++) {
        const NativeEntry *e = &rt->table[i];
        const NativeStats *st = &rt->stats[i];
        if (e->generated) {
            gen_calls += st->calls;
            gen_matched += st->matched;
            gen_mismatched += st->mismatched;
            gen_interrupted += st->interrupted;
            gen_reached += st->calls > 0;
            continue;
        }
        fprintf(out, "  %06X %-40s calls %u", e->addr, e->name, st->calls);
        if (rt->mode == NATIVE_SHADOW) {
            fprintf(out, "  matched %u  mismatched %u  interrupted %u", st->matched, st->mismatched, st->interrupted);
            if (st->native_bus_clocks)
                fprintf(out, "  original/native-bus %.3f", (double)st->original_clocks / st->native_bus_clocks);
        }
        if (st->abandoned)
            fprintf(out, "  abandoned %u", st->abandoned);
        if (st->in_latch_wait)
            fprintf(out, "  in a latch wait %u (not compared)", st->in_latch_wait);

        if (st->skipped)
            fprintf(out, "  skipped %u", st->skipped);
        fprintf(out, "\n");
        if (rt->page_report && st->page_nd) {
            fprintf(out, "    pages:");
            for (unsigned k = 0; k < st->page_nd; k++)
                fprintf(out, " $%04X x%u", st->page_d[k], st->page_calls[k]);
            if (st->page_other)
                fprintf(out, " other x%u", st->page_other);
            fprintf(out, "\n");
        }
        if (rt->timing_report && st->t_stretches) {
            double orig = (double)st->t_original_clocks;
            /* The calibrated cost_milli is the one for the port's own clocks:
             * the original's time less DMA's and the attributed parts'
             * estimates, over the rest of its bus clocks. */
            double own_bus = (double)st->t_bus_clocks, own_dma = (double)st->t_dma_clocks, rest = orig;
            for (unsigned k = 0; k < st->t_nparts; k++) {
                const NativeTimePart *p = &st->t_parts[k];
                own_bus -= (double)p->bus;
                own_dma -= (double)p->dma;
                rest -= (double)p->dma + (double)(p->bus - p->dma) * rt->table[p->entry].cost_milli / 1000.0;
            }
            fprintf(out, "    timing: stretches %u  estimate exact %u  mean |error| %.1f  max |error| %u  of %.1f"
                         "  calibrated cost_milli %.0f\n",
                    st->t_stretches, st->t_estimate_exact, (double)st->t_estimate_error / st->t_stretches,
                    st->t_estimate_max_error, orig / st->t_stretches,
                    own_bus > own_dma ? 1000.0 * (rest - own_dma) / (own_bus - own_dma) : 0.0);
            /* Exact totals and the error histogram, for aggregating over
             * movies (tools/suite.py's timing check). */
            fprintf(out, "    timing: totals original %llu native-bus %llu dma %llu |error| %llu cost_milli %u"
                         "  error bits",
                    (unsigned long long)st->t_original_clocks, (unsigned long long)st->t_bus_clocks,
                    (unsigned long long)st->t_dma_clocks, (unsigned long long)st->t_estimate_error, e->cost_milli);
            for (unsigned k = 0; k < NATIVE_ERROR_BUCKETS; k++)
                fprintf(out, " %u", st->t_error_hist[k]);
            fprintf(out, "\n    timing: vblank stretches %u  max |error| %u  picture max |error| %u  vblank error bits",
                    st->t_vblank_stretches, st->t_vblank_max_error, st->t_picture_max_error);
            for (unsigned k = 0; k < NATIVE_ERROR_BUCKETS; k++)
                fprintf(out, " %u", st->t_vblank_error_hist[k]);
            fprintf(out, "\n");
            fprintf(out, "    timing: fit horizon %u (%.1f%% of time)  fit interrupts %u (%.1f%% of time)\n",
                    st->t_fit_horizon, orig ? 100.0 * st->t_fit_horizon_clocks / orig : 0.0, st->t_fit_interrupt,
                    orig ? 100.0 * st->t_fit_interrupt_clocks / orig : 0.0);
            fprintf(out, "    timing: unseen time values %s%u:", st->t_nextra > 8 ? ">" : "",
                    st->t_nextra > 8 ? 8 : st->t_nextra);
            for (unsigned k = 0; k < st->t_nextra && k < 8; k++)
                fprintf(out, " %u", st->t_extra[k]);
            fprintf(out, "\n");
            /* The clocks attributed to other routines (native_time_as), for the
             * timing gate's estimate and calibration; the rest are the port's. */
            for (unsigned k = 0; k < st->t_nparts; k++) {
                const NativeTimePart *p = &st->t_parts[k];
                const NativeEntry *pe = &rt->table[p->entry];
                fprintf(out, "    timing: part %06X %s native-bus %llu dma %llu cost_milli %u\n", pe->addr, pe->name,
                        (unsigned long long)p->bus, (unsigned long long)p->dma, pe->cost_milli);
            }
        }
        if (rt->timing_report && (st->t_bad_tags || st->t_part_overflows))
            fprintf(out, "    timing: attributions to no hand port %u (last %06X)  past the parts' room %u\n",
                    st->t_bad_tags, st->t_bad_tag, st->t_part_overflows);
        if (rt->timing_report && (st->t_exact || st->t_fallback || st->t_escapes))
            fprintf(out, "    timing: exact %u  fallback %u (untimed %u, timed I/O %u, no room %u)  register fixups %u"
                         "  state diffs %u  escapes %u\n",
                    st->t_exact, st->t_fallback, st->t_untimed, st->t_timed_io, st->t_no_fit, st->t_register_fixups,
                    st->t_state_diffs, st->t_escapes);
    }
    if (rt->generated) {
        fprintf(out, "  generated blocks: %d entries, %lu reached, calls %lu", rt->generated, gen_reached, gen_calls);
        if (rt->mode == NATIVE_SHADOW)
            fprintf(out, "  matched %lu  mismatched %lu  interrupted %lu  unfinished %lu", gen_matched, gen_mismatched,
                    gen_interrupted, rt->gen_unfinished);
        else
            fprintf(out, "  instructions %llu  stops %lu  resumed %lu  indexed %lu", (unsigned long long)generated_executed,
                    rt->gen_stops, rt->gen_resumes, rt->gen_indexed);
        fprintf(out, "\n");
    }
    if (rt->nactive)
        fprintf(out, "  %d routine(s) still in progress\n", rt->nactive);
    for (int i = 0; i < rt->nactive; i++)
        if (rt->active[i].failed_passes)
            fprintf(out, "  mismatch %06X %s: %s (still waiting for a pass that matches when the run ended)\n",
                    rt->active[i].entry->addr, rt->active[i].entry->name, rt->active[i].failed_why);
    for (int kind = 0; kind < 2; kind++) {
        for (int i = 0; i < rt->ndetails[kind]; i++)
            fprintf(out, "  %s\n", rt->details[kind][i]);
        if (rt->dropped[kind])
            fprintf(out, "  (%lu more %s not described; ct_dev --shadow-details N keeps up to %d)\n",
                    rt->dropped[kind], kind ? "notes" : "mismatches", MAX_DETAILS);
    }
}

const NativeStats *native_stats_named(const NativeRuntime *rt, const char *name)
{
    replay_pending(rt, false);
    for (int i = 0; i < rt->count; i++)
        if (rt->table[i].name && !strcmp(rt->table[i].name, name))
            return &rt->stats[i];
    return NULL;
}

const char *native_detail(const NativeRuntime *rt, int i)
{
    if (i < 0)
        return NULL;
    if (i < rt->ndetails[0])
        return rt->details[0][i];
    i -= rt->ndetails[0];
    return i < rt->ndetails[1] ? rt->details[1][i] : NULL;
}

int native_failed(const NativeRuntime *rt)
{
    replay_pending(rt, false);
    for (int i = 0; i < rt->count; i++)
        if (rt->stats[i].mismatched)
            return 1;
    for (int i = 0; i < rt->nactive; i++)
        if (rt->active[i].failed_passes)
            return 1;
    return 0;
}
