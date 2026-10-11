/* The native code a host runs the game with (docs/RECOMP.md): which registered
 * entries --native, --generated, --exclude and --include select, and a runtime
 * over them. Shared by ct_play (frontend/player.c), ct_drive (dev/ct_drive.c)
 * and ct_dev (dev/ct_dev.c), so the three mean the same by those options.
 */
#ifndef CT_FRONTEND_NATIVES_H
#define CT_FRONTEND_NATIVES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "port/native.h"
#include "snes/system.h"

/* Where --generated only leaves the ported routines out. The option exists for
 * the original's exact timing, which ported routines lose (they charge an
 * estimate) and generated blocks keep; the tools differ in what else they
 * want from it:
 *   NATIVES_ONLY_TIMED   in on mode, when there are generated blocks: the only
 *                        case where leaving them out changes the timing. Shadow
 *                        and off modes run the original code whatever the table
 *                        holds, so they keep the ports (a shadow recording
 *                        still checks them), and without generated blocks the
 *                        ports run rather than nothing. ct_play and ct_drive.
 *   NATIVES_ONLY_ALWAYS  in every mode, generated blocks or not: the generated
 *                        code alone, or the interpreter alone. For validating
 *                        generated code by itself (shadow mode checks only the
 *                        generated blocks), and so that on mode stays exact
 *                        without them. ct_dev. */
typedef enum { NATIVES_ONLY_TIMED, NATIVES_ONLY_ALWAYS } NativesOnly;

typedef struct NativeChoice {
    NativeMode mode;
    bool generated;      /* the generated blocks, if they were generated from this ROM */
    bool generated_only; /* without the ported routines, where `only` says */
    NativesOnly only;
    /* Entries (ported routines, generated blocks and generated continuation
     * points, generated_BBAAAA_...) whose names start with one of these are left
     * out, for narrowing down which one causes a difference. */
    const char *const *exclude;
    int nexclude;
    /* If any: only the ported routines whose names start with one of these are
     * kept (generated blocks stay). */
    const char *const *include;
    int ninclude;
} NativeChoice;

typedef struct NativeTable {
    NativeEntry *entries;   /* ported routines, then generated blocks */
    int count;
    int ported, generated;
    GeneratedIndexEntry *index;   /* where generated code can continue (native_set_index) */
    int index_count;
} NativeTable;

/* Choose the entries for c (a warning on stderr if the generated blocks were
 * generated from another ROM: they aren't used). Returns 0, or -1 out of
 * memory. */
int natives_select(NativeTable *t, const uint8_t *rom, size_t rom_size, const NativeChoice *c);
void natives_free(NativeTable *t);

/* A fresh runtime over t in `mode`, attached to snes (off mode attaches
 * nothing and clears any intercept), replacing `old` (NULL for none), which is
 * destroyed: routines it had suspended are dropped, and so are shadow
 * comparisons in progress. */
NativeRuntime *natives_restart(NativeRuntime *old, const NativeTable *t, NativeMode mode, Snes *snes);

#endif
