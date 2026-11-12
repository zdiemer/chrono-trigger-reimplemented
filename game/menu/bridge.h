/* Menu state validation shared by native callers. */
#ifndef CT_GAME_MENU_BRIDGE_H
#define CT_GAME_MENU_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

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

/* The running task's wait for the next frame (or for frames), a call to the
 * task switch `routine` from the JSR at `site`: the next task goes on, and
 * this one from the call's return when its turn comes again, frames later.
 * Shipping calls the checked scheduler as C and parks after its switch;
 * shadow mode compares a native_yield_call_dc. The tasks' own answer to
 * whether a parked caller can still go on is menu_task_goes_on_from. The port
 * resumes with its task; true as menu_call's. Where the task never goes on
 * (stopped or started afresh, or the menus closed), the runtime drops the
 * port, as the original's routine never finishes either. */

/* The wait for the next frame most of the menus' work makes: the task
 * switch's ($C2:821E), which reads none of the registers, from the JSR at
 * `site`. */
enum { MENU_TASK_WAITS_A_FRAME = 0xC2821E };

/* The task switch's wait for frames ($C2:822B), which reads them in A's low
 * byte and swaps them into the high one (so A is read whole). */
enum { MENU_TASK_WAITS_FRAMES = 0xC2822B };



/* The same test as menu_call's, after such a call returning to `returns_to`,
 * and the same stop. */
/* A missing named form reports a contract failure at its constructed call. */

/* Run a sibling port's checked legacy adapter as the original's JSR at `site` calls
 * `routine`: the return address pushed as the call pushes it, so that the
 * entry's own call-outs meet the original's where it makes them, and its time
 * counted as the routine's own. True if it came back to the call's return with
 * the machine it was called with; otherwise it stopped where the original
 * carries on, or the port stops at the return. For a machine-shaped sibling:
 * one whose work is C on the game's state is called as C. */
/* Inline work keeps an uncharged, saveable frame; check before using its state. */

/* Replace a call-out with a checked C call, settling time at its call and
 * return. False means the call left its C chain; true means it returned to
 * the original return address. The caller checks its returned convention. */
/* Check an existing inline call after pushing its frame, retaining its
 * current time boundaries. Refusal stops at the callee's original entry. */

/* The same for a sibling the original reaches with a JSL. */

/* The same for a sibling that comes back with widths that depend on what it
 * did (the equipment list's move leaves a 16-bit accumulator after a slide):
 * the widths it came back with stand. */

/* The same, the widths standing, for a sibling the original reaches by such a
 * jump (menu_call_jumped), with its return to `returns_to` pushed before it. */

/* Whether the checked legacy adapter or retired service takes this machine state.
 * Calls also check after pushing their frame and setting the callee's PC;
 * the fuzzer supplies the same contracts outside the runtime. */

/* A checked sibling call accepting the widths it returns. A refusal leaves
 * the constructed call frame at the original entry and returns false. */

/* The original jumps to a sibling's routine, which never comes back: its
 * port run where the runtime would run it for the jump (menu_runs_as_port),
 * timed as its own; otherwise the port stops there, declaring what the
 * routine doesn't read, for the original to run it. */

/* Hand a former stop to a checked C continuation in shipping mode, keeping
 * the dispatch boundary. Shadow mode still compares at the original stop. */

/* ---- The frame of the routines a bridge stands in for ------------------------------- */

/* Set the accumulator's and the index registers' widths as the original's
 * code has them at a call (index registers made 8-bit lose their high
 * bytes). */

/* The P a routine saves as it starts, and puts back as it ends, which puts
 * its caller's widths back. */

/* Set the load flags preserved across a task switch or read by a checked
 * continuation. Carry and overflow remain unchanged. */

/* ---- The cursor kept on a list (menu/lists.c) ------------------------------------------ */

/* ---- The machine contracts ------------------------------------------------------ */

/* On the menus' page; and with the data bank on work RAM's as well. */
/* ... or with a data bank that reaches low RAM, for a routine whose cells are
 * all there (bank $00's low RAM is work RAM's). */

#endif
