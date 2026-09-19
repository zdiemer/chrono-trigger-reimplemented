/* Palette animation shared by every game mode, and the palette upload. */
#ifndef CT_GAME_CORE_PALETTE_H
#define CT_GAME_CORE_PALETTE_H

#include <stdbool.h>

#include "snes/system.h"

void palette_animate(Snes *s);
void palette_clear_animations(Snes *s);
void palette_load_animations(Snes *s);
/* The animation types' handlers ($FD:E437-$FD:E82C). */
void palette_animate_frames(Snes *s);
void palette_animate_frames_from_table(Snes *s);
void palette_rotate_down(Snes *s);
void palette_rotate_up(Snes *s);
void palette_fade_to_white(Snes *s);
void palette_restore_after_white(Snes *s);
void palette_restore_after_black(Snes *s);
void palette_fade_to_black(Snes *s);
void palette_pulse_white(Snes *s);
void palette_pulse_black(Snes *s);
/* The fades' channel modes ($FD:E64F-$FD:EA5C). */
void palette_darken_all(Snes *s);
void palette_darken_green_blue(Snes *s);
void palette_darken_red_blue(Snes *s);
void palette_darken_blue(Snes *s);
void palette_darken_red_green(Snes *s);
void palette_darken_green(Snes *s);
void palette_darken_red(Snes *s);
void palette_lighten_all(Snes *s);
void palette_lighten_red(Snes *s);
void palette_lighten_green(Snes *s);
void palette_lighten_red_green(Snes *s);
void palette_lighten_blue(Snes *s);
void palette_lighten_red_blue(Snes *s);
void palette_lighten_green_blue(Snes *s);
bool palette_animate_page_is_whole(Snes *s);
bool palette_handler_can_run(Snes *s);

void palette_upload(Snes *s);
bool palette_bank_is_io(Snes *s);

#endif
