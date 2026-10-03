/* Palette animation shared by every game mode, and the palette upload (core/palette.c). */
#ifndef CT_GAME_CORE_PALETTE_H
#define CT_GAME_CORE_PALETTE_H

#include <stdbool.h>

#include "snes/system.h"
#include "state.h"

/* Every palette animation stopped, as a scene does before it sets its own
 * colors (the scene program's call through $FD:FFEE). */
void palette_stop_animations(GameState *g);

/* A fade a mode starts in a slot of its own, rather than from a location's
 * set: `color_count` colors from `first_color`, lightened by a level that
 * moves `step` every `period` frames from `from_level` to `to_level` (a level
 * of 0 is the colors as loaded), on every channel. */
typedef struct {
    uint8_t first_color, color_count;
    uint8_t step, period;
    uint8_t from_level, to_level;
} PaletteFade;
void palette_start_fade_to_white(GameState *g, unsigned slot, PaletteFade fade);

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
/* Their guards. */
bool palette_animate_runs(Snes *s);
bool palette_frames_can_run(Snes *s);
bool palette_table_frames_can_run(Snes *s);
bool palette_rotation_can_run(Snes *s);
bool palette_fade_can_run(Snes *s);
bool palette_fade_loop_can_run(Snes *s);

void palette_upload(Snes *s);
bool palette_bank_is_io(Snes *s);

#endif
