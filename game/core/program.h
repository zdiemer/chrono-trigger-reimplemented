/* Boot, mode hand-offs and frame callbacks. The driver resumes the main
 * coroutine and dispatches enabled frame events through these interfaces. */
#ifndef CT_GAME_CORE_PROGRAM_H
#define CT_GAME_CORE_PROGRAM_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "state.h"

/* The console's own registers and the picture's as the game starts: no
 * interrupts or transfers, and the picture blanked with every setting at its
 * start. */
void program_boot(GameState *g, Platform *pf);

/* The main thread, from power-on: the boot, then the field engine's first
 * start, which shows the title and then the game. It returns on a game-state failure. */
void program_main_thread(GameState *g, Platform *pf);

/* The frame's two interrupts, each sent to the handler the running mode
 * installed, or to none where the mode turned it away. */
void program_vblank(GameState *g, Platform *pf);
void program_line(GameState *g, Platform *pf);

/* Each mode installs ordinary C callbacks. The frame driver calls them after
 * the main thread yields; no address lookup or CPU interrupt frame exists. */
typedef void (*ProgramHandler)(GameState *g, Platform *pf);
typedef struct { ProgramHandler vblank, line; } ProgramHandlers;
/* Module storage supplied by the state owner, independent of mapped RAM. */
ProgramHandlers *program_handlers(GameState *g);
void program_install_vblank_handler(GameState *g, ProgramHandler handler);
void program_install_line_handler(GameState *g, ProgramHandler handler);
void program_ignore_interrupts(GameState *g);

/* Record the first invalid or unsupported state. The routine identifies the
 * diagnostic; it is never executed. The driver reports the failure. */
void program_fail(GameState *g, uint32_t routine, const char *what);
/* Whether an operation has reported a failure. */
bool program_went_no_further(const GameState *g);

/* A game-state invariant failed. */
void program_breaks_a_contract(GameState *g, uint32_t routine, const char *what);

/* A mode returns to the main loop with a scene request. Taking consumes it. */
void program_goes_on_to_a_scene(GameState *g, uint8_t scene);
bool program_next_scene(GameState *g, uint8_t *scene);
/* A driver that starts the game in a scene, as an event would start it from
 * the field (ct_native --scene): the scene's flags, and the song the field
 * would have had playing (0 for none), which a scene that keeps the field's
 * song waits on. The main thread takes it once, after the field's power-on. */
void program_starts_in_a_scene(GameState *g, uint8_t scene, uint8_t song);
bool program_first_scene(GameState *g, uint8_t *scene, uint8_t *song);
/* Or the field started again from its start, on a fresh stack, for the
 * place the party is (a script's change of place made at once): the field
 * takes it as its pass ends. */
void program_starts_the_field_again(GameState *g);
bool program_field_starts_again(GameState *g);
/* Or the game started afresh from the title, as at power-on, the field's
 * first start on a fresh stack (a game over): the main thread takes it as
 * the field returns. */
void program_starts_afresh_from_the_title(GameState *g);
bool program_starts_afresh(GameState *g);
/* Or the console reset, the boot and then the field's first start, as at
 * power-on (the Day of Lavos lost, the planet's scene over, a script's
 * event $FF $96): the running
 * mode's loop ends at it (program_console_is_resetting) and the main thread
 * takes it (program_console_resets). */
void program_resets_the_console(GameState *g);
bool program_console_is_resetting(const GameState *g);
bool program_console_resets(GameState *g);

#endif
