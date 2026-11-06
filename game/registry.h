/* The table registering ported routines with the development system
 * (runtime/port/native.h). */
#ifndef CT_GAME_REGISTRY_H
#define CT_GAME_REGISTRY_H

#include "port/native.h"

extern const NativeEntry game_natives[];
extern const int game_native_count;

/* The retired routines' times (game/time_tags.c): entries marked time_only,
 * which the machine never enters, for the ports that name them. */
extern const NativeEntry game_time_tags[];
extern const int game_time_tag_count;

/* Work RAM the comparisons leave out: the cells a subsystem whose gate
 * retired keeps for itself, which the original's code writes and its C
 * doesn't (native_leave_uncompared). */
extern const NativeRange game_uncompared_wram[];
extern const int game_uncompared_wram_count;

#endif
