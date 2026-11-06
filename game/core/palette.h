/* Palette animation shared by every game mode, and the palette upload (core/palette.c). */
#ifndef CT_GAME_CORE_PALETTE_H
#define CT_GAME_CORE_PALETTE_H

#include <stdbool.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* Every palette animation stopped, as a scene does before it sets its own
 * colors (the scene program's call through $FD:FFEE). */
void palette_stop_animations(GameState *g);

/* The tint a fade or pulse moves the colors toward, by the channels it moves:
 * none (every channel, toward white or black), or a color, the channels that
 * lighten toward it or the others that darken away from it. */
typedef enum {
    PALETTE_TINT_NONE, PALETTE_TINT_RED, PALETTE_TINT_GREEN, PALETTE_TINT_YELLOW,
    PALETTE_TINT_BLUE, PALETTE_TINT_MAGENTA, PALETTE_TINT_CYAN,
} PaletteTint;

/* A fade a mode starts in a slot of its own, rather than from a location's
 * set: `color_count` colors from `first_color`, lightened by a level that
 * moves `step` every `period` frames from `from_level` to `to_level` (a level
 * of 0 is the colors as loaded), toward `tint` (none: every channel). */
typedef struct {
    uint8_t first_color, color_count;
    uint8_t step, period;
    uint8_t from_level, to_level;
    PaletteTint tint;
} PaletteFade;
void palette_start_fade_to_white(GameState *g, unsigned slot, PaletteFade fade);
/* The same, darkening the colors rather than lightening them. */
void palette_start_fade_to_black(GameState *g, unsigned slot, PaletteFade fade);

/* Whether a slot's animation is still playing (a fade, until it reaches its
 * level). */
bool palette_animation_playing(const GameState *g, unsigned slot);

/* Start the current location's palette animations (the field's loading). */
void palette_load_location_animations(GameState *g);

/* The palette animations' frame, every slot's (the field's and the scenes'
 * call through $FD:FFF7); it leaves A and Y at the end of the slots, which a
 * scene's task leaves for the code after it. */
void palette_animate_a_frame(GameState *g);
uint16_t palette_animations_leave(void);
/* The same as the program runs it: where the guard's state half doesn't take
 * the slots, the program goes no further (false). */
bool palette_animated(GameState *g);

/* The frame's work as a still-gated caller's machine path runs it, under the
 * original's call (core/machine_forms.c): the registers the original leaves,
 * and its return. Its contract: 8-bit A, 16-bit index registers, binary
 * arithmetic, a direct page whose low byte is 0, and slots the game makes. */
void palette_animate(Snes *s);
bool palette_animate_runs(Snes *s);

void palette_upload(Snes *s);
/* The working palette sent to CGRAM whole ($C0:712E), every mode's vblank
 * work. */
void palette_send_working(Platform *pf);
enum { PALETTE_UPLOAD_ENTRY = 0xC0712E };
bool palette_bank_is_io(Snes *s);

#endif
