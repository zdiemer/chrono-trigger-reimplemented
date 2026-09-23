/* Field mode: walking around maps. Interrupt handlers, frame sync, objects.
 * The event script engine is in event/. */
#ifndef CT_GAME_FIELD_H
#define CT_GAME_FIELD_H

#include <stdbool.h>

#include "snes/system.h"

/* field/interrupts.c */
void field_install_nmi(Snes *s);
void field_install_irq(Snes *s);

/* field/nmi.c */
void field_nmi(Snes *s);

/* field/frame.c */
void field_wait_for_frame_loop(Snes *s);
void field_change_location_wait_for_vblank_loop(Snes *s);
void field_restart_wait_for_vblank_loop(Snes *s);

/* field/motion.c */
void field_direction_to_target(Snes *s);
void field_set_velocity(Snes *s);
void field_start_step(Snes *s);
void field_object_in_the_way(Snes *s);
void field_line_up_with_object(Snes *s);
void field_line_up_with_tile(Snes *s);
void field_move_object(Snes *s);
void field_move_object_unbounded(Snes *s);
bool field_heads_current_object(Snes *s);
bool field_object_checks_its_way(Snes *s);
bool field_lines_up_with_an_object(Snes *s);

/* field/animation.c */
void field_update_walk_animation(Snes *s);
void field_stand_animation(Snes *s);
void field_step_object_animation(Snes *s);
void field_read_animation_frame(Snes *s);
bool field_object_delays_in_bank(Snes *s);
bool field_object_frames_in_bank(Snes *s);

/* field/object_view.c */
void field_update_object_visibility(Snes *s);
void field_update_object_in_window(Snes *s);

/* field/window_shape.c */
void field_rasterize_quad(Snes *s);
bool field_window_shape_table_in_place(Snes *s);

/* field/scroll.c */
void field_scroll_layers(Snes *s);

#endif
