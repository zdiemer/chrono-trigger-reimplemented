/* Where the menus proper meet the original's registers: the call the entry
 * wrappers and the bridges around call-outs make to the original's code, the
 * run of a sibling entry as the original's JSR reaches it, and the frame of
 * the original's routines a bridge stands in for. This, the entry wrappers at
 * the end of each file and the bridges they name, is the machine-aware code
 * of the menus proper; a build without the original's engine replaces it.
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

/* ---- Calling the original's code ------------------------------------------------------ */

/* Call the original's routine from the JSR (JSL) at `site`, declaring what it
 * doesn't read of the registers (checked by tools/dontcare_check.py, which
 * knows these two as it knows m_jsr_dc). True if the callee came back with
 * the widths, page and data bank it was called with; otherwise the port stops
 * at the call's return, and the original carries on from the callee's
 * machine. */
bool menu_call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care);
bool menu_call_long(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care);

/* The running task's wait for the next frame (or for frames), a call to the
 * task switch `routine` from the JSR at `site`: the next task goes on, and
 * this one from the call's return when its turn comes again, frames later.
 * The port waits parked across the other tasks' turns (native_yield_call_dc,
 * the tasks' own answer to whether it can still go on, menu_task_goes_on_from)
 * and goes on with them; true as menu_call's. Where the task never goes on
 * (stopped or started afresh, or the menus closed), the runtime drops the
 * port, as the original's routine never finishes either. */
bool menu_yield(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care);

/* The wait for the next frame most of the menus' work makes: the task
 * switch's ($C2:821E), which reads none of the registers, from the JSR at
 * `site`. */
enum { MENU_TASK_WAITS_A_FRAME = 0xC2821E, MENU_TASK_WAIT_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
bool menu_wait_a_frame_from(Snes *s, uint32_t site);

/* The same for a routine that comes back with widths of its own. */
bool menu_call_returning(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, bool a8, bool x8);

/* The same for a routine the original reaches by a jump through a pointer at
 * `site` it reads at run time (a JMP (abs)), with a return the code before
 * the jump pushed (a PER): the callee returns to `returns_to`, which is where
 * the port goes on, or stops. */
bool menu_call_jumped(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, uint32_t returns_to);

/* The machine a bridge expects back after a call made some other way (a
 * service's call-out, a sibling run as its JSR reaches it), noted before it. */
typedef struct {
    bool a8, x8;
    uint16_t page;
    uint8_t bank;
} MenuMachine;

MenuMachine menu_machine_now(const Snes *s);

/* The same test as menu_call's, after such a call returning to `returns_to`,
 * and the same stop. */
bool menu_back(Snes *s, uint32_t returns_to, MenuMachine m);

/* Run a sibling port's registered entry as the original's JSR at `site` calls
 * `routine`: the return address pushed as the call pushes it, so that the
 * entry's own call-outs meet the original's where it makes them, and its time
 * counted as the routine's own. True if it came back to the call's return with
 * the machine it was called with; otherwise it stopped where the original
 * carries on, or the port stops at the return. For a machine-shaped sibling:
 * one whose work is C on the game's state is called as C. */
bool menu_run(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *));

/* The same for a sibling the original reaches with a JSL. */
bool menu_run_long(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *));

/* The same for a sibling that comes back with widths that depend on what it
 * did (the equipment list's move leaves a 16-bit accumulator after a slide):
 * the widths it came back with stand. */
bool menu_run_any_widths(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *));

/* The same, the widths standing, for a sibling the original reaches by such a
 * jump (menu_call_jumped), with its return to `returns_to` pushed before it. */
bool menu_run_jumped(Snes *s, uint32_t returns_to, uint32_t routine, void (*entry)(Snes *));

/* Whether the runtime would run `entry`, a sibling's registered entry at
 * `routine`, for the original's call to it made now: its precondition and its
 * guard hold, and the run hasn't left it out. A bridge that runs the sibling
 * with menu_run asks this first, and where it wouldn't, the call is made (or
 * the bridge stops at it), so the original runs whatever the runtime would
 * have. Outside the runtime (ct_fuzz) it never would. */
bool menu_runs_as_port(Snes *s, uint32_t routine, void (*entry)(Snes *));

/* A sibling run as menu_run_any_widths runs it where the runtime would run it
 * (menu_runs_as_port), and otherwise the original's call to it from the JSR at
 * `site`, declaring what it doesn't read of the registers: either way the
 * widths it comes back with stand, and true as menu_back's. */
bool menu_run_or_call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *));

/* ---- The frame of the routines a bridge stands in for ------------------------------- */

/* Set the accumulator's and the index registers' widths as the original's
 * code has them at a call (index registers made 8-bit lose their high
 * bytes). */
void menu_set_widths(Snes *s, bool a8, bool x8);

/* The P a routine saves as it starts, and puts back as it ends, which puts
 * its caller's widths back. */
void menu_save_p(Snes *s);
void menu_restore_p(Snes *s);

/* The flags the original's code leaves, where they are saved on the stack
 * under a call-out (a routine's P, pushed as it starts) and compared there:
 * those of a comparison of `a` with `b`, of a test of `v`'s bits against the
 * accumulator's `a` (N and V from `v`), and of a load of `v`. The carry and the
 * overflow are left alone where the operation doesn't set them. Scaffolding
 * (ORIGINAL_EFFECT). */
void menu_leave_compare(Snes *s, uint8_t a, uint8_t b);
void menu_leave_bit_test(Snes *s, uint8_t a, uint8_t v);
void menu_leave_load(Snes *s, uint8_t v);

/* ... and A's high byte clear, as the save code's loads leave it. */
void menu_leave_a_high_byte_clear(Snes *s);

/* ---- The cursor kept on a list (menu/lists.c) ------------------------------------------ */

/* Its entry ($C2:EA53), and what the original leaves the code after it,
 * which branches on it (scaffolding). */
void menu_keep_the_cursor_on_the_list_entry(Snes *s);
void menu_leave_cursor_kept_on_the_list(Snes *s);
/* A screen's step keeping it as C, timed as the original's, and leaving what
 * the original leaves. */
void menu_step_keeps_the_cursor_on_the_list(Snes *s);

/* ---- The guards (registry .guard) ------------------------------------------------------ */

/* On the menus' page; and with the data bank on work RAM's as well. */
bool menu_on_page(Snes *s);
bool menu_on_page_work_ram(Snes *s);
/* ... or with a data bank that reaches low RAM, for a routine whose cells are
 * all there (bank $00's low RAM is work RAM's). */
bool menu_on_page_low_ram(Snes *s);

#endif
