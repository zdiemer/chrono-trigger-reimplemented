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
} NativeStats;

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

/* Install on a system (sets snes->intercept). */
void native_attach(NativeRuntime *rt, Snes *snes);

/* Per-routine results, and details of the first mismatches, to out. */
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

/* Synchronous call-outs: the instructions callees may run in total
 * (native_call_steps counts them; a tool resets it) before a call gives up,
 * leaving the machine mid-callee; how many calls have given up; and a hook run
 * when one does, where a tool can abandon the native routine (a routine may
 * otherwise loop forever on what an unfinished callee left behind). */
extern unsigned long native_call_step_limit;
extern unsigned long native_call_steps;
extern unsigned long native_call_timeouts;
extern void (*native_call_timeout_hook)(void);

#endif
