/* Native routine registry for the development system.
 *
 * Modes:
 *   OFF     the interpreter runs everything.
 *   SHADOW  the interpreter runs everything; on each entry to a registered
 *           routine the machine is cloned, the native routine runs on the
 *           clone, and at each call it makes and when the original returns the
 *           two machines must match exactly (registers, WRAM, SRAM, VRAM,
 *           CGRAM, OAM, PPU and CPU I/O registers, APU ports). Timing isn't
 *           compared. See native_call.
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
 * validation doesn't compare them. Bit order matches tools/disasm/liveness.py. */
enum {
    DC_A = 1 << 0, DC_X = 1 << 1, DC_Y = 1 << 2,
    DC_N = 1 << 3, DC_V = 1 << 4, DC_Z = 1 << 5, DC_C = 1 << 6,
    DC_DB = 1 << 7, DC_D = 1 << 8,
};

typedef struct NativeEntry {
    uint32_t addr;          /* 24-bit entry address */
    const char *name;
    NativeFn fn;
    uint8_t p_mask;         /* entry precondition on P: (p & p_mask) == p_value */
    uint8_t p_value;
    /* ON-mode time cost: the native routine's own bus-access clocks times
     * cost_milli/1000. Shadow mode reports the original/native-bus ratio to set it. */
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

typedef struct NativeStats {
    unsigned calls, matched, mismatched, interrupted, skipped;
    unsigned abandoned;         /* a callee unwound the routine's frame, so it never finished */
    /* SHADOW: time the original spent in the stretches native code replaces
     * (excluding call-outs), and the native routine's bus-access clocks for them. */
    uint64_t original_clocks;
    uint64_t native_bus_clocks;
} NativeStats;

typedef struct NativeRuntime NativeRuntime;

NativeRuntime *native_create(const NativeEntry *table, int count, NativeMode mode);
void native_destroy(NativeRuntime *rt);

/* ON mode: continue in generated code at any instruction of a generated block
 * (index sorted by key), not only at entries and where a block stopped. */
void native_set_index(NativeRuntime *rt, const RecompIndexEntry *index, int count);

/* Install on a system (sets snes->intercept). */
void native_attach(NativeRuntime *rt, Snes *snes);

/* Per-routine results, and details of the first mismatches, to out. */
void native_report(const NativeRuntime *rt, FILE *out);

/* The comparison shadow mode uses: true, with the first difference described
 * in out, if the machines differ in anything CPU-side code can observe
 * (ignoring dont_care registers and flags, and state HDMA changes with time). */
bool native_compare(const Snes *original, const Snes *native, uint16_t dont_care, char *out, size_t n);

/* Nonzero if any shadow comparison failed. */
int native_failed(const NativeRuntime *rt);

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

/* Number of registered routines entered and not yet left (suspended in
 * call-outs, or awaiting shadow comparison). Save states taken while this is
 * nonzero can't restore those routines. */
int native_suspended(const NativeRuntime *rt);

/* For native routines: call the routine at `target` the way the JSR (3-byte)
 * or JSL (4-byte) instruction at `site` would, pushing the same return
 * address, and return once it has returned.
 *
 * Under the runtime, the native routine is suspended: the system loop runs the
 * callee as it would the original's (time passes, interrupts and frames happen,
 * a ported callee runs natively) and the routine resumes when the callee
 * returns to the call's return address with the stack as it was before the
 * call. SHADOW mode compares the machines when the original reaches the same
 * call, ignoring `dont_care` (DC_* bits: what the callee doesn't read), then
 * again at each later call and at return. If the callee unwinds past its return
 * instead, the routine is abandoned, as the original would be.
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
