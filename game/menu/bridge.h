/* Checked C calls, held task switches and the remaining machine-facing
 * register conventions shared by the menus.
 *
 * The menus proper run on their page (D = $0000) with the data bank on work
 * RAM ($7E), reaching the tilemaps and tables above $2000 through it; unlike
 * the battle's and the field's code they change the accumulator's and the
 * index registers' widths from routine to routine, each saving its caller's
 * as it starts and putting them back as it ends. So a callee must hand back
 * the widths, the page and the data bank it was called with for a bridge to go
 * on after it. */
#ifndef CT_GAME_MENU_BRIDGE_H
#define CT_GAME_MENU_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include "port/machine.h"
#include "port/vars_ram.h"
#include "state.h"
#include "vars.h"

enum { MENU_PAGE = 0x0000, MENU_DATA_BANK = 0x7E };

/* A call's return is the instruction after it. */
enum { MENU_JSR_SIZE = 3, MENU_JSL_SIZE = 4 };

/* ---- Compatibility calls and held task switches --------------------------------------- */

/* Call the original's routine from the JSR (JSL) at `site`, declaring what it
 * doesn't read of the registers (checked by the calling convention, which
 * knows these two as it knows m_jsr_dc). True if the callee came back with
 * the widths, page and data bank it was called with; otherwise the port stops
 * at the call's return, and the original carries on from the callee's
 * machine. */
bool menu_call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care);

/* The running task's wait for the next frame (or for frames), a call to the
 * task switch `routine` from the JSR at `site`: the next task goes on, and
 * this one from the call's return when its turn comes again, frames later.
 * Shipping calls the checked scheduler as C and parks after its switch;
 * shadow mode compares a native_yield_call_dc. The tasks' own answer to
 * whether a parked caller can still go on is menu_task_goes_on_from. The port
 * resumes with its task; true as menu_call's. Where the task never goes on
 * (stopped or started afresh, or the menus closed), the runtime drops the
 * port, as the original's routine never finishes either. */
bool menu_yield(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care);

/* The wait for the next frame most of the menus' work makes: the task
 * switch's ($C2:821E), which reads none of the registers, from the JSR at
 * `site`. */
enum { MENU_TASK_WAITS_A_FRAME = 0xC2821E, MENU_TASK_WAIT_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
bool menu_wait_a_frame_from(Snes *s, uint32_t site);

/* The task switch's wait for frames ($C2:822B), which reads them in A's low
 * byte and swaps them into the high one (so A is read whole). */
enum { MENU_TASK_WAITS_FRAMES = 0xC2822B, MENU_TASK_WAIT_READS_THE_FRAMES = MENU_TASK_WAIT_READS_NOTHING & ~DC_A };

/* A task that is a loop -- a frame's work, then the wait from the JSR at
 * `site`, then round again from its start -- waits (menu/tasks.c): a frame,
 * or `frames`. `widths_its_own` where the loop sets its widths itself, as
 * the original's code has them at the wait. Where the runtime wouldn't run
 * the wait, the port stops at the JSR, declaring what the wait reads
 * (the calling convention reads it so, from the callers' sites). */
void menu_wait_in_the_task_loop(Snes *s, uint32_t site, bool widths_its_own);
void menu_wait_frames_in_the_task_loop(Snes *s, uint32_t site, uint8_t frames, bool widths_its_own);

/* The same for a routine that comes back with widths of its own. */

/* The same for a routine the original reaches by a jump through a pointer at
 * `site` it reads at run time (a JMP (abs)), with a return the code before
 * the jump pushed (a PER): the callee returns to `returns_to`, which is where
 * the port goes on, or stops. */

typedef struct {
    bool a8, x8;
    uint16_t page;
    uint8_t bank;
} MenuMachine;

MenuMachine menu_machine(const Snes *s);

/* The same test as menu_call's, after such a call returning to `returns_to`,
 * and the same stop. */
bool menu_back(Snes *s, uint32_t returns_to, MenuMachine m);
/* A missing named form reports a contract failure at its constructed call. */
void menu_refuse_call(Snes *s, uint32_t returns_to, uint32_t routine, bool far);

/* Run a sibling port's checked legacy adapter as the original's JSR at `site` calls
 * `routine`: the return address pushed as the call pushes it, so that the
 * entry's own call-outs meet the original's where it makes them, and its time
 * counted as the routine's own. True if it came back to the call's return with
 * the machine it was called with; otherwise it stopped where the original
 * carries on, or the port stops at the return. For a machine-shaped sibling:
 * one whose work is C on the game's state is called as C. */
/* Inline work keeps an uncharged, saveable frame; check before using its state. */
bool menu_begin_inline_call(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *));
void menu_end_inline_call(Snes *s, uint32_t site);

bool menu_run(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *));

/* Replace a call-out with a checked C call, settling time at its call and
 * return. False means the call left its C chain; true means it returned to
 * the original return address. The caller checks its returned convention. */
bool menu_run_called(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *));
bool menu_run_long_called(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *));
/* Check an existing inline call after pushing its frame, retaining its
 * current time boundaries. Refusal stops at the callee's original entry. */
bool menu_run_checked(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *));

/* The same for a sibling the original reaches with a JSL. */
bool menu_run_long(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *));

/* The same for a sibling that comes back with widths that depend on what it
 * did (the equipment list's move leaves a 16-bit accumulator after a slide):
 * the widths it came back with stand. */
bool menu_run_any_widths(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *));
bool menu_run_long_any_widths(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *));

/* The same, the widths standing, for a sibling the original reaches by such a
 * jump (menu_call_jumped), with its return to `returns_to` pushed before it. */
bool menu_run_jumped(Snes *s, uint32_t returns_to, uint32_t routine, void (*entry)(Snes *));

/* Whether the checked legacy adapter or retired service takes this machine state.
 * Calls also check after pushing their frame and setting the callee's PC;
 * the fuzzer supplies the same contracts outside the runtime. */
bool menu_runs_as_port(Snes *s, uint32_t routine, void (*entry)(Snes *));

/* A checked sibling call accepting the widths it returns. A refusal leaves
 * the constructed call frame at the original entry and returns false. */
bool menu_run_or_call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *));

/* The original jumps to a sibling's routine, which never comes back: its
 * port run where the runtime would run it for the jump (menu_runs_as_port),
 * timed as its own; otherwise the port stops there, declaring what the
 * routine doesn't read, for the original to run it. */
void menu_go_on_in(Snes *s, uint32_t routine, void (*entry)(Snes *), uint16_t dont_care);

/* Hand a former stop to a checked C continuation in shipping mode, keeping
 * the dispatch boundary. Shadow mode still compares at the original stop. */
void menu_continue(Snes *s, uint32_t routine, void (*entry)(Snes *), uint16_t dont_care);

/* ---- The frame of the routines a bridge stands in for ------------------------------- */

/* Set the accumulator's and the index registers' widths as the original's
 * code has them at a call (index registers made 8-bit lose their high
 * bytes). */
void menu_set_widths(Snes *s, bool a8, bool x8);

/* The P a routine saves as it starts, and puts back as it ends, which puts
 * its caller's widths back. */
void menu_save_p(Snes *s);
void menu_restore_p(Snes *s);

/* Set the load flags preserved across a task switch or read by a checked
 * continuation. Carry and overflow remain unchanged. */
void menu_leave_load(Snes *s, uint8_t v);

/* ---- The cursor kept on a list (menu/lists.c) ------------------------------------------ */

void menu_charge_cursor_kept_on_the_list(Snes *s);

/* ---- The machine contracts ------------------------------------------------------ */

/* On the menus' page; and with the data bank on work RAM's as well. */
bool menu_on_page(Snes *s);
bool menu_on_page_work_ram(Snes *s);
/* ... or with a data bank that reaches low RAM, for a routine whose cells are
 * all there (bank $00's low RAM is work RAM's). */
bool menu_on_page_low_ram(Snes *s);

#endif
