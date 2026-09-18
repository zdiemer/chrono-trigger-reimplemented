/* The native code a host runs the game with (docs/RECOMP.md): which registered
 * entries --native and --recomp select, and a runtime over them. Shared by
 * ct_play (frontend/player.c) and ct_drive (dev/ct_drive.c), so the two mean
 * the same by those options. ct_dev still builds its own table, for --exclude
 * (docs/TODO.md).
 */
#ifndef CT_FRONTEND_NATIVES_H
#define CT_FRONTEND_NATIVES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "port/native.h"
#include "snes/system.h"

typedef struct NativeTable {
    NativeEntry *entries;   /* ported routines, then generated blocks */
    int count;
    int ported, generated;
} NativeTable;

/* Choose the entries for `mode`:
 *   - the generated blocks if `recomp` and they were generated from this ROM
 *     (a warning on stderr if they were generated from another);
 *   - the ported routines, unless `recomp_only` in on mode with generated
 *     blocks: those alone keep the original's timing exactly, while ported
 *     routines charge an estimate. Shadow and off modes don't depend on timing,
 *     so they keep them, and without generated blocks the ported routines run.
 * Returns 0, or -1 out of memory. */
int natives_select(NativeTable *t, const uint8_t *rom, size_t rom_size, NativeMode mode, bool recomp,
                   bool recomp_only);
void natives_free(NativeTable *t);

/* A fresh runtime over t in `mode`, attached to snes (off mode attaches
 * nothing and clears any intercept), replacing `old` (NULL for none), which is
 * destroyed: routines it had suspended are dropped, and so are shadow
 * comparisons in progress. */
NativeRuntime *natives_restart(NativeRuntime *old, const NativeTable *t, NativeMode mode, Snes *snes);

#endif
