/* The differential fuzzer's core (dev/ct_fuzz.c is its command line; see
 * there and docs/PORTING.md, "Fuzzing"): a registered routine against the
 * original on random machine states. Apart from the command line so that
 * tests can run it on made-up routines (tests/port/fuzz_test.c). */
#ifndef CT_DEV_FUZZ_H
#define CT_DEV_FUZZ_H

#include <stdio.h>

#include "port/native.h"
#include "snes/system.h"

#define FUZZ_MAX_AREAS 16

/* A range of work RAM the fuzzer sets: from the base state (keep) or random
 * bytes from lo to hi (fill). Absolute, lo_addr-hi_addr as 24-bit bus
 * addresses; or relative to a pointer the state holds: `count` bytes at the
 * 16-bit pointer in the work RAM cell `pointer` (or in X, Y or D), plus
 * `offset`, in `bank` (the pointer cell's bank by default). Relative ones are
 * applied last, in order, to the state as it is by then. */
typedef enum { FUZZ_AT_ADDRESS, FUZZ_AT_CELL, FUZZ_AT_X, FUZZ_AT_Y, FUZZ_AT_D } FuzzBase;

typedef struct FuzzArea {
    bool keep;
    FuzzBase base;
    uint32_t lo_addr, hi_addr;   /* FUZZ_AT_ADDRESS */
    uint32_t pointer;            /* FUZZ_AT_CELL: the cell's 24-bit address */
    int32_t offset;
    uint32_t count;
    int bank;                    /* -1: the cell's bank ($7E for a register) */
    uint8_t lo, hi;              /* fill */
} FuzzArea;

/* Parse "BBAAAA-BBAAAA" or "[BBAAAA]+OFF,COUNT" (also [X], [Y], [D], an
 * optional "BB:" bank before the bracket and -OFF), with "=lo-hi" after it for
 * a fill. Hex throughout. Returns false if it isn't one. */
bool fuzz_parse_area(const char *spec, bool keep, FuzzArea *out);

typedef struct FuzzOptions {
    unsigned long iterations;
    uint64_t seed;
    bool long_call, interrupt;
    unsigned range_lo[3], range_hi[3];   /* A, X, Y */
    unsigned range_step[3];              /* every step-th value from lo (0: every one) */
    FuzzArea areas[FUZZ_MAX_AREAS];
    int nareas;
    /* The work each side may do on a state, in bus accesses: the port's own
     * and every instruction the interpreter runs for it (its call-outs), and
     * the original's. See fuzz_run. */
    unsigned long budget;
    int details;                          /* mismatching states to describe in full */
    /* Scramble what each call-out's don't-care set names before its callee
     * runs (native_call_poison), from a generator of its own, so the states
     * are those of the same seed without it. */
    bool poison_dc;
    uint32_t poison_site;                 /* ...only at the call-outs from this site (0: all) */
} FuzzOptions;

#define FUZZ_DEFAULT_BUDGET 2000000ul
/* The original may do this many times the port's work, on top of the budget:
 * it fetches every instruction the port doesn't, and runs whole loops the
 * port runs as a few accesses (a count of 0 fading 65536 colors does 10 times
 * as many). */
#define FUZZ_ORIGINAL_FACTOR 16
/* A port that runs past its budget where the original finishes within this
 * share of it is a finding: a port does fewer accesses than the original (no
 * fetches), so a correct one would have finished. */
#define FUZZ_LOOP_SHARE 4

typedef struct FuzzResult {
    unsigned long matched, mismatched;
    unsigned long looped;          /* of mismatched: the port ran past its budget where the original finished */
    unsigned long guard;           /* skipped: the entry's guard refused the state */
    unsigned long callout;         /* skipped: a call-out ran past the budget */
    unsigned long both_long;       /* skipped: the port ran past its budget, and the original didn't finish soon either */
    unsigned long original_long;   /* skipped: the port returned, the original didn't within its budget */
    unsigned long scrambled;       /* --poison-dc: call-outs scrambled in the states compared */
} FuzzResult;

/* Fuzz entry e from `base` (a machine in native mode, with an interpreter).
 * Mismatches are described on `log`, the first opts->details in full. */
FuzzResult fuzz_run(const Snes *base, const NativeEntry *e, const FuzzOptions *opts, FILE *log);

void fuzz_default_options(FuzzOptions *o);

#endif
