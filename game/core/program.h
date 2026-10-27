/* The program's top: what runs from power-on, as functions rather than
 * addresses (core/program.c).
 *
 * The console starts the program in two places and interrupts it in two: the
 * boot, then the main thread, which never returns and waits for frames; and
 * once a frame the vblank handler and, where the mode wants the picture ended
 * early, the line handler. On the console the CPU reaches them through the
 * cartridge's vectors and the stubs in work RAM each mode points at its own
 * handlers (core/console.c, the entries that do it). A build with no machine
 * under it (ct_native) has its driver call them instead: the main thread as a
 * coroutine whose frame waits yield to the driver, the handlers as calls once
 * a frame (runtime/platform/native/host.h). */
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
 * start, which shows the title and then the game. It never returns. */
void program_main_thread(GameState *g, Platform *pf);

/* The frame's two interrupts, each sent to the handler the running mode
 * installed, or to none where the mode turned it away. */
void program_vblank(GameState *g, Platform *pf);
void program_line(GameState *g, Platform *pf);

/* Each game mode sends the two interrupts to its own handlers: the vblank's,
 * which sends the frame's work to the screen, and the line's, which ends the
 * picture where the mode wants it. `handler` is the handler's address. */
void program_install_vblank_handler(GameState *g, uint32_t handler);
void program_install_line_handler(GameState *g, uint32_t handler);

/* Where a stub sends its interrupt: to a handler, back to what it interrupted
 * (the mode turned it away), or nowhere yet -- before any mode has set it, it
 * holds whatever work RAM held at power-on. */
typedef enum { PROGRAM_STUB_UNSET, PROGRAM_STUB_RETURNS, PROGRAM_STUB_JUMPS } ProgramStubKind;
typedef struct {
    ProgramStubKind kind;
    uint32_t handler;   /* PROGRAM_STUB_JUMPS: the handler's address */
} ProgramStub;
ProgramStub program_vblank_stub(const GameState *g);
ProgramStub program_line_stub(const GameState *g);

/* Where the program's C ends: `routine` is the next thing the program does,
 * which only the machine runs yet (an entry the original's address reaches,
 * docs/PLAN.md, "The native program"). The state's backing supplies it: on
 * the machine the address is how the program goes on, so it is never called;
 * with no machine (runtime/port/state_host.c) the program stops there and the
 * driver says where. `what` names it for the report. */
void program_needs_the_machine(GameState *g, uint32_t routine, const char *what);
/* Whether it has: the program's C stops where it comes back to the main
 * thread. Never on the machine. */
bool program_went_no_further(const GameState *g);

/* Where a mode that ends sends the program next, which the main thread takes
 * when the mode returns to it: a scene of the scene engine's, the field
 * ending into it. The state's backing keeps it; on the machine the address
 * the mode jumps to is how the program goes on, and these are never called. */
void program_goes_on_to_a_scene(GameState *g, uint8_t scene);
bool program_next_scene(GameState *g, uint8_t *scene);
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
 * power-on (the Day of Lavos lost, the planet's scene over): the running
 * mode's loop ends at it (program_console_is_resetting) and the main thread
 * takes it (program_console_resets). */
void program_resets_the_console(GameState *g);
bool program_console_is_resetting(const GameState *g);
bool program_console_resets(GameState *g);

#endif
