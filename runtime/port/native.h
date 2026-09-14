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

typedef struct NativeEntry {
    uint32_t addr;          /* 24-bit entry address */
    const char *name;
    NativeFn fn;
    uint8_t p_mask;         /* entry precondition on P: (p & p_mask) == p_value */
    uint8_t p_value;
    uint32_t clocks;        /* master clocks charged in ON mode (typical cost of the original) */
} NativeEntry;

typedef enum { NATIVE_OFF, NATIVE_SHADOW, NATIVE_ON } NativeMode;

typedef struct NativeStats {
    unsigned calls, matched, mismatched, interrupted, skipped;
    uint64_t original_clocks;   /* summed over matched shadow calls */
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

#endif
