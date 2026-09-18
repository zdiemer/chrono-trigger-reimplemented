/* The window driver's controls (docs/PLAYING.md, "Controls"): which keys and
 * gamepad inputs press which SNES buttons, and which drive the player (pause,
 * fast forward, quick save and load, fullscreen, quit). The defaults are the
 * keymap ct_play always had plus a gamepad layout; `--bind` and a key file
 * change them. Bindings are the driver's: the player only ever sees the
 * buttons they add up to. */
#ifndef CT_FRONTEND_BINDINGS_H
#define CT_FRONTEND_BINDINGS_H

#include <SDL.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    ACT_NONE,
    ACT_BUTTON,        /* a SNES button (Binding.button) */
    ACT_PAUSE,
    ACT_FAST_FORWARD,  /* held */
    ACT_QUICK_SAVE,
    ACT_QUICK_LOAD,
    ACT_FULLSCREEN,
    ACT_QUIT,
    ACT_COUNT
} Action;

/* Back to the defaults. */
void bindings_reset(void);

/* One binding, "INPUT = ACTION": a key by its SDL name ("X", "Right Shift",
 * "F5"), or "Pad " and a controller button ("Pad a", "Pad dpup",
 * "Pad start") or axis direction ("Pad leftx-", "Pad righttrigger"); and a
 * SNES button (A B X Y L R Start Select Up Down Left Right), Pause,
 * FastForward, QuickSave, QuickLoad, Fullscreen, Quit, or None to unbind.
 * It replaces whatever that input did. Returns 0, or -1 with why in err. */
int bindings_set(const char *spec, char *err, size_t err_len);

/* A key file: one binding per line, '#' starts a comment. Prints what it
 * can't read, naming `source`, and returns how many lines those were. */
int bindings_load(const char *text, size_t len, const char *source);

/* The controls as bound, for --help. */
void bindings_print(FILE *out);

/* A name for the first input bound to an action, for messages ("F7"), or
 * `fallback` if none is. */
const char *bindings_name(Action action, char *out, size_t n, const char *fallback);

/* The SNES buttons held on the keyboard and the open pads. */
uint16_t bindings_buttons(SDL_GameController *const *pads, int npads);

/* The action of a key pressed (ACT_NONE if unbound). */
Action bindings_key_action(SDL_Scancode key);

/* Whether any input bound to `action` is held, on the keyboard or a pad (for
 * fast forward, and for noticing pad presses of the others). */
bool bindings_held(Action action, SDL_GameController *const *pads, int npads, bool keyboard);

#endif
