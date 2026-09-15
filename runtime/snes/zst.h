/* Importing ZSNES save states (.zst, .zs1-.zs9; ZSNES 1.51, "V143").
 *
 * The importer overwrites a powered-on machine with the state's memory, CPU,
 * PPU, CPU I/O and APU registers, rebuilding the PPU's decoded values through
 * its register writes, and leaves the machine at the start of vblank, where a
 * frame of ours ends: the state is taken just before line 225's NMI, so the
 * next frame starts with the NMI handler. What the state holds only in ZSNES's
 * own form (the S-DSP's voices, timer phases, HDMA progress, write latches) is
 * approximated; docs/RUNTIME.md, "ZSNES save states", lists how.
 *
 * Runs from an imported state are for coverage and exploring only: they can't
 * be replayed from power-on, and the approximations mean they don't match what
 * the original would have done, so they are never validation runs. */
#ifndef CT_SNES_ZST_H
#define CT_SNES_ZST_H

#include <stddef.h>
#include <stdint.h>

#include "system.h"

/* Import a state held in memory. On failure returns -1 and describes why in
 * err (if given), leaving the machine unchanged. */
int zst_import(Snes *snes, const uint8_t *data, size_t len, char *err, size_t err_len);

/* Same, reading the file at path. */
int zst_import_file(Snes *snes, const char *path, char *err, size_t err_len);

#endif
