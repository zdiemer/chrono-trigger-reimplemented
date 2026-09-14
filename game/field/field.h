/* Field mode: walking around maps. Interrupt handlers, frame sync, objects.
 * The event script engine is in event/. */
#ifndef CT_GAME_FIELD_H
#define CT_GAME_FIELD_H

#include "snes/system.h"

/* field/interrupts.c */
void field_install_nmi(Snes *s);
void field_install_irq(Snes *s);

/* field/nmi.c */
void field_nmi(Snes *s);

/* field/frame.c */
void field_wait_for_frame_loop(Snes *s);

/* field/motion.c */
void field_direction_to_target(Snes *s);
void field_set_velocity(Snes *s);
void field_start_step(Snes *s);
void field_object_in_the_way(Snes *s);
void field_update_walk_animation(Snes *s);
void field_stand_animation(Snes *s);
void field_line_up_with_object(Snes *s);
void field_line_up_with_tile(Snes *s);

/* field/objects.c */
void field_update_object_visibility(Snes *s);

#endif
