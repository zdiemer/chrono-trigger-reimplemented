/* Ported routines and the table that registers them with the development
 * system (runtime/port/native.h). Every routine names its original address. */
#ifndef CT_GAME_NATIVES_H
#define CT_GAME_NATIVES_H

#include "port/native.h"

/* game/c0_system.c */
void c0_disable_display_and_interrupts(Snes *s);
void c0_install_field_nmi(Snes *s);
void c0_install_field_irq(Snes *s);
void c0_wait_frame_loop(Snes *s);

/* game/c2_system.c */
void c2_wait_frame_loop(Snes *s);
void c2_wait_ready_loop(Snes *s);

/* game/c0_nmi.c */
void c0_field_nmi(Snes *s);

/* game/c0_objects.c */
void c0_update_object_visibility(Snes *s);

/* game/ram_attract.c */
bool ram_attract_wait_loop_present(Snes *s);
void ram_attract_wait_loop(Snes *s);
bool ram_attract_vblank_wait_present(Snes *s);
void ram_attract_vblank_end_loop(Snes *s);
void ram_attract_vblank_start_loop(Snes *s);

/* game/c3_decompress.c */
void c3_decompress(Snes *s);

extern const NativeEntry game_natives[];
extern const int game_native_count;

#endif
