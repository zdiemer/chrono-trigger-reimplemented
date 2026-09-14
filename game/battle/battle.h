/* Battle mode. */
#ifndef CT_GAME_BATTLE_H
#define CT_GAME_BATTLE_H

#include "snes/system.h"

/* battle/wait.c */
bool battle_frame_wait_is_idle(Snes *s);
void battle_wait_for_frame_loop(Snes *s);
void battle_wait_vblank_end_loop(Snes *s);
void battle_wait_hblank_loop(Snes *s);
void battle_wait_line_loop(Snes *s);

/* battle/nmi.c */
void battle_nmi(Snes *s);

/* battle/sprites.c */
void battle_draw_objects(Snes *s);
void battle_draw_shadow(Snes *s);
void battle_step_object_effect(Snes *s);
void battle_draw_sprite_parts(Snes *s);
void battle_copy_sprite_tiles(Snes *s);
void battle_draw_object_underlays(Snes *s);
void battle_draw_object_overlays(Snes *s);
void battle_step_prepared_sprites(Snes *s);

#endif
