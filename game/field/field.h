/* Field mode: walking around maps. Interrupt handlers, frame sync, objects
 * and (to come) the event script engine. */
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

/* field/events.c */
void field_run_object_init_scripts(Snes *s);
void field_run_object_script(Snes *s);

/* field/objects.c */
void field_update_object_visibility(Snes *s);

#endif
