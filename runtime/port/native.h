/* Native routine registry for the development system.
 *
 * Modes:
 *   OFF     the interpreter runs everything.
 *   SHADOW  the interpreter runs everything; on each entry to a registered
 *           routine the machine is cloned, the native routine runs on the
 *           clone, and when the original returns the two machines must match
 *           exactly (registers, WRAM, SRAM, VRAM, CGRAM, OAM, PPU and CPU I/O
 *           registers, APU ports). Timing isn't compared.
 *   ON      registered routines run natively in place of the original.
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
} NativeEntry;

typedef enum { NATIVE_OFF, NATIVE_SHADOW, NATIVE_ON } NativeMode;

typedef struct NativeStats {
    unsigned calls, matched, mismatched, interrupted, skipped;
    uint64_t original_clocks;   /* summed over matched shadow calls */
    uint64_t native_bus_clocks; /* bus-access clocks the native routine spent on those calls */
    double min_ratio, max_ratio; /* per call: original clocks / native bus clocks */
} NativeStats;

typedef struct NativeRuntime NativeRuntime;

NativeRuntime *native_create(const NativeEntry *table, int count, NativeMode mode);
void native_destroy(NativeRuntime *rt);

/* Install on a system (sets snes->intercept). */
void native_attach(NativeRuntime *rt, Snes *snes);

/* Per-routine results, and details of the first mismatches, to out. */
void native_report(const NativeRuntime *rt, FILE *out);

/* Nonzero if any shadow comparison failed. */
int native_failed(const NativeRuntime *rt);

/* For native routines: call the routine at `target` the way the JSR (3-byte)
 * or JSL (4-byte) instruction at `site` would, pushing the same return
 * address, and return once it has returned. A ported routine runs natively;
 * anything else runs on the interpreter (interrupts held off, no time passes). */
void native_call(Snes *s, uint32_t site, uint32_t target, bool long_call);

#endif
