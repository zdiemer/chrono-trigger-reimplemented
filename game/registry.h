/* The table registering ported routines with the development system
 * (runtime/port/native.h). */
#ifndef CT_GAME_REGISTRY_H
#define CT_GAME_REGISTRY_H

#include "port/native.h"

extern const NativeEntry game_natives[];
extern const int game_native_count;

/* The held waits' continuations (holds.h): entries the runtime runs where the
 * machine names their wait (NativeEntry.hold), kept apart from the registry's
 * table, whose entries the machine finds by address. */
extern const NativeEntry game_held_waits[];
extern const int game_held_wait_count;

/* The estimates kept for routines whose entries left the registry, whose
 * work ports run as C under their names (native_keep_estimates). */
extern const NativeEntry game_kept_estimates[];
extern const int game_kept_estimate_count;

/* Work RAM the comparisons leave out: the cells a subsystem whose gate
 * retired keeps for itself, which the original's code writes and its C
 * doesn't (native_leave_uncompared). */
extern const NativeRange game_uncompared_wram[];
extern const int game_uncompared_wram_count;

#endif
