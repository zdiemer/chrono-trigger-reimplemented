/* Ported routines and the table that registers them with the development
 * system (runtime/port/native.h). Every routine names its original address. */
#ifndef CT_GAME_NATIVES_H
#define CT_GAME_NATIVES_H

#include "port/native.h"

/* game/c0_system.c */
void c0_disable_display_and_interrupts(Snes *s);
void c0_install_field_nmi(Snes *s);
void c0_install_field_irq(Snes *s);

extern const NativeEntry game_natives[];
extern const int game_native_count;

#endif
