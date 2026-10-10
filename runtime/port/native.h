/* Native routine registry for the development system.
 *
 * Modes:
 *   OFF     the interpreter runs everything.
 *   SHADOW  the interpreter runs everything; on each entry to a registered
 *           routine the machine is cloned, the native routine runs on the
 *           clone, and at each call it makes and when the original returns the
 *           two machines must match exactly (registers, WRAM, SRAM, VRAM,
 *           CGRAM, OAM, PPU and CPU I/O registers, APU ports). Timing isn't
 *           compared; it is measured (native_set_timing_report). See native_call.
 *           An entry the original reaches while it catches up with a native
 *           routine (inside the stretch that routine stood in for) is compared
 *           nested; if it is still running when the original gets to the point
 *           an enclosing routine waits for, it is set aside (abandoned) and the
 *           enclosing one compared.
 *   ON      registered routines run natively in place of the original.
 *
 * Several entries may share an address: hand-written ones are tried first,
 * then generated blocks (NativeEntry.generated).
 */
#ifndef CT_PORT_NATIVE_H
#define CT_PORT_NATIVE_H

#include <stdio.h>

#include "snes/system.h"

typedef void (*NativeFn)(Snes *s);

/* Registers and flags no caller reads after a routine returns, from
 * `python3 -m tools.disasm --calls ...` (disasm/out/liveness.json). Shadow
 * validation doesn't compare them. Bit order matches tools/disasm/liveness.py.
 * DC_A is the whole accumulator; DC_AH is its high byte alone and DC_AL its low
 * byte alone, which liveness tracks apart (code that works on an 8-bit A leaves
 * the high byte to whatever 16-bit arithmetic last put there; a caller's later
 * 16-bit load or TAX can read the high byte of an A whose low byte nothing
 * reads). DC_AL | DC_AH is the same claim as DC_A. */
enum {
    DC_A = 1 << 0, DC_X = 1 << 1, DC_Y = 1 << 2,
    DC_N = 1 << 3, DC_V = 1 << 4, DC_Z = 1 << 5, DC_C = 1 << 6,
    DC_DB = 1 << 7, DC_D = 1 << 8, DC_AH = 1 << 9, DC_AL = 1 << 10,
    DC_FLAGS = DC_N | DC_V | DC_Z | DC_C,
};

typedef struct NativeEntry {
    uint32_t addr;          /* 24-bit entry address */
    const char *name;
    NativeFn fn;
    uint8_t p_mask;         /* entry precondition on P: (p & p_mask) == p_value */
    uint8_t p_value;
    /* ON-mode time cost: the time the native routine's DMA transfers took, as
     * it is, plus the rest of its own bus-access clocks times cost_milli/1000.
     * Shadow mode's --timing-report gives the calibrated value to set it. */
    uint32_t cost_milli;
    uint16_t dont_care;     /* DC_* bits */
    /* Idle entries replace one pass of a busy-wait loop rather than a routine:
     * the function runs the loop body and either leaves PC at the loop head
     * (still waiting) or sets it to the loop's exit. In ON mode a waiting pass
     * skips ahead to the next point where $4212's blanking bits can change
     * (hblank end or start, or the line's end); SHADOW mode skips idle entries. */
    bool idle;
    /* For idle loops polling something other than $4212 (the APU ports): if
     * nonzero, a waiting pass takes this many master clocks, the loop's own
     * time per pass, since skipping ahead would change when it sees the other
     * processor's answer. */
    uint32_t idle_clocks;
    /* Optional extra precondition, for example for code the game places in
     * RAM: return false unless the bytes at the entry are the code the port was
     * written for (see m_code_matches). It runs on the real machine in every
     * mode, so it must only peek (snes_bus_peek): a bus read costs time and
     * sets open bus, which would make shadow runs differ from plain ones. */
    bool (*guard)(Snes *s);
    /* A block generated from the ROM (tools/recomp, docs/RECOMP.md): it runs to
     * completion on the machine without a fiber and never calls out, and its
     * time is exact. Hand-written entries at the same address take precedence.
     * cost_milli, dont_care and idle are unused; guard is set for blocks of RAM
     * code (M3), which run only while RAM holds the code they were generated from. */
    bool generated;
    /* Generated only: entered in emulation mode (the reset vector's first
     * instructions); p_mask and p_value are 0. Every other entry needs native mode. */
    bool emulation;
} NativeEntry;

/* Where generated code can continue at an instruction that isn't an entry
 * (docs/RECOMP.md, M3): for every instruction of every generated block, a block
 * whose function starts there, keyed by address | P's M and X bits << 24 |
 * E << 30 and sorted by key; guard as in NativeEntry. */
typedef struct RecompIndexEntry {
    uint32_t key;
    NativeFn fn;
    bool (*guard)(Snes *s);
    const char *name;
} RecompIndexEntry;

typedef enum { NATIVE_OFF, NATIVE_SHADOW, NATIVE_ON } NativeMode;

#define NATIVE_ERROR_BUCKETS 24
#define NATIVE_PAGE_SLOTS 16

typedef struct NativeStats {
    unsigned calls, matched, mismatched, interrupted, skipped;
    unsigned abandoned;         /* a callee unwound the routine's frame, so it never finished */
    /* SHADOW: time the original spent in the stretches native code replaces
     * (excluding call-outs), and the native routine's bus-access clocks for them. */
    uint64_t original_clocks;
    uint64_t native_bus_clocks;
    /* Timing measurement (docs/RECOMP.md, "Can a hand port be exact?"),
     * reported with native_set_timing_report. SHADOW, over every stretch no
     * interrupt fell in (a stretch is a call, or the part of one between
     * call-outs): how many; the original's CPU time and the port's bus clocks
     * summed (their ratio, less DMA's time, which is the same in both, is
     * the calibrated cost_milli); how many the
     * cost_milli estimate got exactly right; the estimate's total and largest
     * absolute error, in master clocks; and how many (and how much original
     * time) fit before the system's next horizon point (snes_horizon) or its
     * next interrupt or frame boundary (snes_interrupt_horizon). */
    unsigned t_stretches, t_estimate_exact, t_fit_horizon, t_fit_interrupt;
    uint64_t t_original_clocks, t_bus_clocks, t_dma_clocks;   /* t_dma_clocks: the part of both DMA took */
    uint64_t t_estimate_error, t_fit_horizon_clocks, t_fit_interrupt_clocks;
    uint32_t t_estimate_max_error;
    /* The estimate's absolute error per stretch, counted by bit length: bucket
     * 0 is exact, bucket k (1..NATIVE_ERROR_BUCKETS-1) holds errors of k bits
     * (2^(k-1) to 2^k - 1 master clocks), and the last bucket everything
     * longer. What the timing gate reads percentiles from (tools/suite.py). */
    unsigned t_error_hist[NATIVE_ERROR_BUCKETS];
    /* The same for the stretches that began on a vblank line, the NMI
     * handler's time for video work: how many, the largest error, and the
     * histogram. The rest began on a picture line; t_picture_max_error is
     * their largest error. */
    unsigned t_vblank_stretches;
    uint32_t t_vblank_max_error, t_picture_max_error;
    unsigned t_vblank_error_hist[NATIVE_ERROR_BUCKETS];
    /* The distinct values of the original's time minus the port's bus clocks
     * over uninterrupted stretches (up to 8; t_nextra 9 means more): the time
     * the port can't see, instruction fetches and internal cycles, which varies
     * with the path the original took. */
    uint32_t t_extra[8];
    unsigned t_nextra;
    /* ON with PORT_TIMING_ORACLE: calls run as the port with the exact time,
     * calls left to the generated block, and calls whose registers the port
     * left different from the original's (don't-care registers; fixed up) or
     * whose machine state differed otherwise. Fallbacks are counted again by why: the port called out or
     * synced (escapes; the prototype times whole calls only), no generated code
     * to time it, time-dependent I/O, no room before the horizon. */
    unsigned t_exact, t_fallback, t_register_fixups, t_state_diffs, t_escapes;
    unsigned t_untimed, t_timed_io, t_no_fit;
    /* SHADOW, over the same stretches: the port's bus clocks (and their DMA
     * part) it attributed to other routines with native_time_as, by routine;
     * the rest are its own. t_nparts of them, allocated as they appear. And
     * attributions that named no hand port (their clocks stay the port's own),
     * and those past the parts a stretch can hold (likewise). */
    struct NativeTimePart *t_parts;
    unsigned t_nparts;
    unsigned t_bad_tags, t_part_overflows;
    uint32_t t_bad_tag;            /* the latest address that named no hand port */
    /* With native_set_page_report: the direct page (D) the routine's calls
     * were made with, before its guard, as up to NATIVE_PAGE_SLOTS distinct
     * values and how many calls each (page_other: calls with any further D).
     * The evidence for re/ram.toml's page instances (docs/DISASSEMBLY.md,
     * "The accessors"). */
    uint16_t page_d[NATIVE_PAGE_SLOTS];
    unsigned page_calls[NATIVE_PAGE_SLOTS];
    unsigned page_nd, page_other;
} NativeStats;

/* A port's bus clocks attributed to the routine at table index `entry`. */
typedef struct NativeTimePart {
    int entry;
    uint64_t bus, dma;
} NativeTimePart;

/* How ON mode charges hand ports' time. ESTIMATE: their DMA time plus
 * cost_milli times the rest of their bus clocks, after they stop (the default). ORACLE: a prototype
 * that gives a port the original's exact time and keeps its call only where
 * that is exact: the port runs, then the generated code of the same routine
 * runs on a copy of the machine as it was, to where the port stopped, to
 * measure the time. If the whole call fits before the horizon, did no
 * time-dependent I/O and didn't call out, it is charged that time (and the
 * copy's end state is compared with the port's); otherwise the machine is put
 * back and the generated block runs instead. */
typedef enum { PORT_TIMING_ESTIMATE, PORT_TIMING_ORACLE } PortTiming;
/* The horizon ORACLE uses: snes_horizon, or with native_set_port_horizon(rt,
 * true) snes_interrupt_horizon, which lets a call run across refresh, HDMA and
 * line ends it doesn't interact with (it has no time-dependent I/O). */

typedef struct NativeRuntime NativeRuntime;

NativeRuntime *native_create(const NativeEntry *table, int count, NativeMode mode);
void native_destroy(NativeRuntime *rt);

/* ON mode: continue in generated code at any instruction of a generated block
 * (index sorted by key), not only at entries and where a block stopped. */
void native_set_index(NativeRuntime *rt, const RecompIndexEntry *index, int count);

void native_set_port_timing(NativeRuntime *rt, PortTiming timing);
void native_set_port_horizon(NativeRuntime *rt, bool interrupts_only);
/* Print the timing counters per routine in native_report. */
void native_set_timing_report(NativeRuntime *rt, bool on);
/* Count and print, per hand port, the direct pages its calls were made with. */
void native_set_page_report(NativeRuntime *rt, bool on);

/* What the runtime hands to code that isn't a hand port, for a tool that
 * measures it (ct_dev --fallbacks, dev/fallbacks.h): each call is made as it
 * happens, with the machine as it stands. Any member may be NULL. In SHADOW
 * mode the original runs everything, and the calls follow it: a port is
 * entered where the original reaches the entry, calls out where the original
 * reaches the call, and stops where the original reaches the stop, so between
 * entered and finished the original is running what the port stands for,
 * except inside its call-outs.
 *
 * refused: every hand-written entry registered at the machine's PC turned the
 * call away -- its precondition on P (`why` "widths"), the CPU's mode
 * ("emulation") or its guard ("guard") -- so the generated code or the
 * interpreter runs the original there. `entry` is the first of them. An idle
 * entry is a hand entry like any other.
 * skipped: a hand entry applied but the runtime didn't run it (the nesting of
 * suspended routines was at its limit), so the original runs.
 * called_out: the port running for `entry` called out to `target`; the machine
 * is at the callee's first instruction, its return address pushed.
 * stopped: the port finished without returning (it stopped at an instruction
 * of the original, m_stop_at or a PC it set); the machine is where it stopped,
 * and `entry_s` is S as it was when the port was entered.
 * entered: a hand port (not an idle entry) is about to run; the machine is at
 * its entry.
 * returned_from_interrupt: ON, the port finished with an RTI (m_rti).
 * idle: SHADOW, an idle entry applies here, and the original runs the pass
 * of the wait loop it stands for.
 * finished: SHADOW, the comparison of the port's call is over, however it
 * ended (it returned, stopped, was abandoned or interrupted). */
typedef struct NativeObserver {
    void *ctx;
    void (*refused)(void *ctx, Snes *s, const NativeEntry *entry, const char *why);
    void (*skipped)(void *ctx, Snes *s, const NativeEntry *entry);
    void (*called_out)(void *ctx, Snes *s, const NativeEntry *entry, uint32_t target);
    void (*stopped)(void *ctx, Snes *s, const NativeEntry *entry, uint16_t entry_s);
    void (*entered)(void *ctx, Snes *s, const NativeEntry *entry);
    void (*returned_from_interrupt)(void *ctx, Snes *s, const NativeEntry *entry);
    void (*idle)(void *ctx, Snes *s, const NativeEntry *entry);
    void (*finished)(void *ctx, const NativeEntry *entry);
} NativeObserver;
/* Observe ON mode's hand-offs (NULL: stop). The observer is copied. */
void native_set_observer(NativeRuntime *rt, const NativeObserver *observer);

/* Called at the start of every instruction a generated block executes, with
 * PC at it (PB:pc24), for a tool that counts what runs where (ct_dev
 * --fallbacks). NULL by default, which costs a test per instruction. */
extern void (*recomp_insn_hook)(Snes *s, uint32_t pc24);

/* Install on a system (sets snes->intercept). */
void native_attach(NativeRuntime *rt, Snes *snes);

/* How many mismatches native_report describes, and apart from them how many
 * notes (abandoned comparisons, timing): 16 each unless set, at most 256. */
void native_set_details(NativeRuntime *rt, int limit);

/* Per-routine results, and details of the first mismatches, then of the
 * first notes, to out. */
void native_report(const NativeRuntime *rt, FILE *out);

/* The comparison shadow mode uses: true, with the first difference described
 * in out, if the machines differ in anything CPU-side code can observe
 * (ignoring dont_care registers and flags, and state HDMA changes with time). */
bool native_compare(const Snes *original, const Snes *native, uint16_t dont_care, char *out, size_t n);

/* Stack bytes the original pushed and pulled again are dead: nothing reads
 * below S, and the next push overwrites them. Shadow mode doesn't compare the
 * ones between the lowest S the original reached since the last comparison
 * and its S now, so a port need not reproduce the frames of the subroutines it
 * inlines or the P bytes a callee saves and restores (docs/PORTING.md, Modes).
 * This copies them from the original into the native machine, for a tool that
 * compares the two itself (ct_fuzz). Bank 0's work RAM only. */
void native_copy_popped_stack(const Snes *original, Snes *native, uint16_t lowest_s);

/* For native routines that stop at an instruction of the original rather than
 * returning (setting PC): registers and flags that nothing reads at that point
 * before setting them, which SHADOW mode then doesn't compare there, on top of
 * the entry's dont_care. `at` is the instruction (24-bit), or the return
 * instruction (RTS, RTL, RTI) the routine carries out last; SHADOW mode
 * reports a routine that stops anywhere else as a mismatch, so the address
 * tools/dontcare_check.py checks the set at is the one it applies at. Outside
 * the runtime (ct_fuzz) the last values are kept in native_stop_dont_care_direct
 * and native_stop_at_direct, which the tool clears before each call. */
void native_dont_care_at_stop(Snes *s, uint32_t at, uint16_t dont_care);
/* The same, but the set is the stop's own: the entry's don't-care set, which
 * is the claim about its returns, isn't added there. For a port whose entry
 * leaves more dead after its return than is dead where it stops (an early exit
 * to code that reads a register its callers don't); tools/dontcare_check.py
 * checks the set alone at `at`. native_stop_own_direct says which was last
 * called outside the runtime. */
void native_own_dont_care_at_stop(Snes *s, uint32_t at, uint16_t dont_care);
extern uint16_t native_stop_dont_care_direct;
extern uint32_t native_stop_at_direct;
extern bool native_stop_own_direct;
/* What shadow mode (and ct_fuzz) leave out where a routine finishes: the
 * entry's set and the stop's, or the stop's alone if it was declared as its own. */
static inline uint16_t native_finish_dont_care(uint16_t entry, uint16_t stop, bool own)
{
    return own ? stop : (uint16_t)(entry | stop);
}
/* Whether the instruction at `at` is a return (RTS, RTL, RTI). */
bool native_is_return(const Snes *s, uint32_t at);
/* Called by m_rts, m_rtl and m_rti (machine.h): the routine finishes by
 * returning, where its entry's dont_care applies. A routine whose entry has a
 * dont_care and that finishes anywhere else (setting PC) must say where with
 * native_dont_care_at_stop, or SHADOW mode reports it: the set is checked
 * against liveness where it applies (tools/dontcare_check.py), which needs to
 * know where that is. Outside the runtime it sets native_returned_direct. */
void native_note_return(Snes *s);
/* The same for m_rti, which also tells the observer (NativeObserver) the
 * port returned from an interrupt. */
void native_note_rti(Snes *s);
extern bool native_returned_direct;

/* Nonzero if any shadow comparison failed. */
int native_failed(const NativeRuntime *rt);

/* For tests: the counters of the first entry registered as `name` (NULL if
 * none), and the i-th of the details native_report prints after them (the
 * first mismatches; NULL past the last). */
const NativeStats *native_stats_named(const NativeRuntime *rt, const char *name);
const char *native_detail(const NativeRuntime *rt, int i);

/* Generated blocks (docs/RECOMP.md). ON mode: a block runs until the system's
 * horizon (snes_horizon) and stops before the first instruction past it, with
 * I/O catching the machine up to its instruction first, so time and interrupts
 * are exactly the interpreter's; a block that stopped resumes where it stopped.
 * SHADOW mode: the interpreter's bus accesses and the machine at each
 * instruction boundary are recorded, and every generated block that applies at
 * a boundary is replayed against that record: same accesses in the same order,
 * same values written, same registers and time where the original reaches the
 * point the block stopped. */
/* Instructions executed in generated blocks so far (in every mode, replays
 * included). */
uint64_t native_generated_instructions(void);

/* The registered entry whose native code is running right now (the innermost
 * hand port, or an idle or generated entry), or NULL while the interpreter or
 * the system runs. For attributing hardware access (ct_dev --io-log). */
const NativeEntry *native_running_entry(void);

/* The first hand-written entry (idle entries included) registered at the
 * machine's PC whose precondition and guard hold, or NULL. For running
 * registered code synchronously (the APU-direct audio backend). */
const NativeEntry *native_hand_entry(Snes *s);

/* Number of registered routines entered and not yet left (suspended in
 * call-outs, or awaiting shadow comparison). Save states taken while this is
 * nonzero can't restore those routines. */
int native_suspended(const NativeRuntime *rt);

/* ON mode: master clocks hand ports have taken and the machine hasn't let pass
 * yet (a port's estimated time is charged after it stops, a scanline at a time;
 * see native_call). 0 in the other modes. A save state doesn't hold this either,
 * so a state taken while it is nonzero resumes with that time dropped: a tool
 * that wants its states to resume exactly saves only when both this and
 * native_suspended are 0. */
uint64_t native_owed(const NativeRuntime *rt);

/* For native routines: call the routine at `target` the way the JSR (3-byte)
 * or JSL (4-byte) instruction at `site` would, pushing the same return
 * address, and return once it has returned.
 *
 * Under the runtime, the native routine is suspended: the system loop runs the
 * callee as it would the original's (time passes, interrupts and frames happen,
 * a ported callee runs natively) and the routine resumes when the callee
 * returns to the call's return address with the stack as it was before the
 * call. SHADOW mode compares the machines when the original reaches the same
 * call (the first pass through it, with the same S, where they are the same:
 * a call inside a loop the port runs several passes of), ignoring `dont_care`
 * (DC_* bits: what the callee doesn't read), then again at each later call and
 * at return. If the callee unwinds past its return instead, the routine is
 * abandoned, as the original would be.
 *
 * Outside the runtime the callee runs synchronously (registered entries, else
 * the machine's interpreter), with interrupts held off and no time passing. */
void native_call(Snes *s, uint32_t site, uint32_t target, bool long_call);
void native_call_dc(Snes *s, uint32_t site, uint32_t target, bool long_call, uint16_t dont_care);

/* For native routines, before I/O whose timing another processor observes (the
 * APU ports): in ON mode, let the time the routine has taken so far pass on the
 * machine first. ON mode otherwise charges a routine's time after it stops, so
 * everything it wrote would appear at once, followed by a gap the original never
 * had; the SPC700 reading the ports in that gap sees a half-written message. No
 * effect in SHADOW mode or outside the runtime. */
void native_sync(Snes *s);

/* For native routines that read the beam's position (a budget of work by the
 * line the picture has reached): call it after latching the counters ($2137)
 * and before reading them. In SHADOW mode the routine runs on a copy of the
 * machine whose beam stands where the copy was last made, a few instructions
 * before the original latches, so a read near a line's end could see the
 * line before the original's: the routine waits here until the original
 * latches the counters itself, and reads what the original latched. ON mode
 * lets the routine's time so far pass first (native_sync). No effect outside
 * the runtime. */
void native_await_latch(Snes *s);

/* Time attribution, for a port that runs another routine's work as C rather
 * than calling out to it: a dispatcher that runs its handlers inline. A port's
 * time is estimated from its bus clocks times its entry's cost_milli (above);
 * a dispatcher's time follows which handlers ran, which one scalar can't fit.
 * So the port names the routine whose work it is doing:
 *
 *     int was = native_time_as(s, 0xFDE82C);   // $FD:E82C's work from here
 *     x = fade(s, slot, true);
 *     native_time_end(s, was);
 *
 * and the bus clocks in between are estimated with the cost_milli of the hand
 * port registered at that address, which shadow mode calibrates on that
 * routine's own calls like any other entry's (the original runs it as a
 * routine, so every call it makes is measured). The estimate is then the sum
 * over the routines the call actually ran. `routine` is a 24-bit address, the
 * routine's `// $BB:AAAA`: a tag, not a time; a port names no cycle counts.
 * Scopes nest (a handler's own handlers), and one naming the running port
 * itself is its own time. An address with no hand port registered counts as
 * the port's own time, and shadow mode's timing report names it (the timing
 * gate fails on it). Outside the runtime (ct_fuzz, tests) both do nothing. */
int native_time_as(Snes *s, uint32_t routine);
void native_time_end(Snes *s, int was);
/* Time charged rather than spent, for a port that does another way what the
 * original spends its time on (game/charge.h): `bus_clocks` more of the stretch's
 * bus clocks, as if the routine registered at `routine` had made them, so they
 * are estimated at its cost_milli (its own entry's when it is the running
 * port). And accesses left out of the estimate: the bus clocks between
 * native_uncounted_begin and native_uncounted_end don't count, for a plain fill
 * or copy whose time the port charges instead. Outside the runtime all three do
 * nothing. */
void native_charge_as(Snes *s, uint32_t routine, uint32_t bus_clocks);
uint32_t native_uncounted_begin(Snes *s);
void native_uncounted_end(Snes *s, uint32_t mark);

/* The same around one statement: NATIVE_TIME_AS(s, 0xCFEAC5, a = draw_parts(s)); */
#define NATIVE_TIME_AS(s, routine, statement)                 \
    do {                                                      \
        int native_time_was_ = native_time_as((s), (routine)); \
        statement;                                            \
        native_time_end((s), native_time_was_);               \
    } while (0)

/* Synchronous call-outs: the instructions callees may run in total
 * (native_call_steps counts them; a tool resets it) before a call gives up,
 * leaving the machine mid-callee; how many calls have given up; and a hook run
 * when one does, where a tool can abandon the native routine (a routine may
 * otherwise loop forever on what an unfinished callee left behind). */
extern unsigned long native_call_step_limit;
extern unsigned long native_call_steps;
extern unsigned long native_call_timeouts;
extern void (*native_call_timeout_hook)(void);

/* Synchronous call-outs, for ct_fuzz --poison-dc: when a call with a don't-care
 * set is made and this hook says so (it may pick calls by site and target), the
 * registers and flags the set names are scrambled, each to a value other than
 * the caller's, from the hook's random `bits`, so a callee that reads one of
 * them runs on something other than the original's value and the difference
 * shows. The callee runs twice from the same machine, first with the caller's
 * values on a copy: what it left alone (the caller's value after the first
 * run, the scrambled one after the second) is the caller's own again, as
 * shadow mode and the shipping mode keep it. Only the outermost call-outs are
 * scrambled, not the ones their callees' ports make. NULL by default; the
 * runtime's call-outs never use it. A tool that abandons a port mid-call-out
 * (its timeout hook) calls native_call_poison_reset before the next. */
extern bool (*native_call_poison)(uint32_t site, uint32_t target, uint64_t *bits);
void native_call_poison_reset(void);

/* For ct_dev --poison-site: when a hand port makes a call-out with a don't-care
 * set and this hook says so (it picks calls by site), the registers and flags
 * the set names (or as many of them as the hook leaves in `set`) are scrambled,
 * as native_call_poison does, before the original's code runs. In the shipping
 * mode nothing puts them back: a declaration that holds leaves the run as it
 * was, one that doesn't changes what the original's code does, which a trace
 * shows. NULL by default. */
extern bool (*native_site_poison)(uint32_t site, uint32_t target, uint64_t *bits, uint16_t *set);

/* The shipping mode's returns, for ct_dev --poison-return: where a hand port
 * registered at `entry` returns to the original's code and this hook says so,
 * the registers and flags its don't-care set names (or as many of them as the
 * hook leaves in `set`) are scrambled before the original's code goes on. A
 * declaration that holds leaves the run as it was; one that doesn't changes
 * what the original's code does, which a trace shows. NULL by default. */
extern bool (*native_return_poison)(uint32_t entry, uint64_t *bits, uint16_t *set);

/* Synchronous call-outs, for ct_fuzz --stop-at-call: called with the machine
 * at the callee's entry (the return address pushed); a tool that wants the
 * comparison to end there (a call that yields to another task, which never
 * returns on this stack) takes it and doesn't come back. NULL by default. */
extern void (*native_call_stop)(uint32_t site, uint32_t target, uint16_t dont_care);

#endif
