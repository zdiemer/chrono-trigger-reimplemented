/* The differential fuzzer's core (dev/ct_fuzz.c is its command line; see
 * there and docs/PORTING.md, "Fuzzing"): a registered routine against the
 * original on random machine states. Apart from the command line so that
 * tests can run it on made-up routines (tests/port/fuzz_test.c). */
#ifndef CT_DEV_FUZZ_H
#define CT_DEV_FUZZ_H

#include <stdio.h>

#include "port/native.h"
#include "snes/system.h"

#define FUZZ_MAX_AREAS 48
#define FUZZ_MAX_VALUES 32
#define FUZZ_MAX_STOP_CALLS 12

/* A range of work RAM the fuzzer sets: from the base state (keep) or random
 * bytes from lo to hi (fill). Absolute, lo_addr-hi_addr as 24-bit bus
 * addresses; or relative to a pointer the state holds: `count` bytes at the
 * 16-bit pointer in the work RAM cell `pointer` (or in X, Y or D), plus
 * `offset`, in `bank` (the pointer cell's bank by default). Relative ones are
 * applied after the absolute ones, in order, to the state as it is by then.
 *
 * An area needn't be every byte of its range: with a `step` it is the
 * elements that start every step bytes from its first, for a field of each
 * record in a table. And a fill needn't be a byte range: it can pick each
 * element from a set of values (`nvalues` of them, each `width` bytes, low
 * byte first), so a word that must be one of a few real ones (a script's
 * place, a record's offset) is; or copy the bytes at `copy_from` (as the
 * state has them once every other area is applied: ties go last), so two
 * cells the game keeps equal are. */
typedef enum { FUZZ_AT_ADDRESS, FUZZ_AT_CELL, FUZZ_AT_X, FUZZ_AT_Y, FUZZ_AT_D } FuzzBase;

typedef struct FuzzArea {
    bool keep;
    FuzzBase base;
    uint32_t lo_addr, hi_addr;   /* FUZZ_AT_ADDRESS */
    uint32_t pointer;            /* FUZZ_AT_CELL: the cell's 24-bit address */
    int32_t offset;
    uint32_t count;
    int bank;                    /* -1: the cell's bank ($7E for a register) */
    uint8_t lo, hi;              /* fill: a byte from lo to hi */
    uint32_t step;               /* elements start every step bytes (0: every width bytes) */
    uint8_t width;               /* an element's bytes: 1, or a value set's width */
    int nvalues;                 /* fill: pick each element from values[] (0: from lo-hi) */
    uint32_t values[FUZZ_MAX_VALUES];
    bool copy;                   /* fill: the bytes at copy_from, in order */
    uint32_t copy_from;
} FuzzArea;

/* Parse "BBAAAA-BBAAAA" or "[BBAAAA]+OFF,COUNT" (also [X], [Y], [D], an
 * optional "BB:" bank before the bracket and -OFF), then an optional ":STEP",
 * then for a fill "=lo-hi" (a byte from lo to hi), "=V,V,..." (one of the
 * values, each as wide as the widest written: 2 hex digits a byte, 4 a word,
 * 6 a long) or "=@BBAAAA" (the bytes there). Hex throughout. Returns false if
 * it isn't one. */
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
    /* Call-outs to these routines end the comparison: the machines are compared
     * where the call is made, at the callee's entry, with the call's don't-care
     * set, and neither side goes on (a task's yield, which switches stacks). */
    uint32_t stop_calls[FUZZ_MAX_STOP_CALLS];
    int nstop_calls;
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
    unsigned long at_call;         /* of matched and mismatched: compared at a stop_calls call */
} FuzzResult;

/* Fuzz entry e from `base` (a machine in native mode, with an interpreter).
 * Mismatches are described on `log`, the first opts->details in full. */
FuzzResult fuzz_run(const Snes *base, const NativeEntry *e, const FuzzOptions *opts, FILE *log);

void fuzz_default_options(FuzzOptions *o);

#endif
