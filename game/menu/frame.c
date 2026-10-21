/* The menus: keeping the main loop in step with the display.
 *
 * Each of bank C2's two modes has a main loop that does a frame's work and
 * then waits for its vblank handler: the sprite-object mode's for the next
 * frame to be counted (game/menu/display.c, menu_object_nmi), the menus'
 * for the handler to have sent the uploads the frame queued
 * (menu_nmi_work). Each mode is in its wait at nearly every frame's end, so
 * the ports leave the waiting to the loops' idle entries here in the
 * shipping mode, which a saved machine holds (platform/timing.h,
 * tm_wait_for_handlers_held), and wait through them themselves elsewhere.
 * Between its waits the menus' loop runs a turn of each of their tasks
 * (menu/tasks.c), which a screen starts and stops. */
#include "menu/menu.h"

#include "core/input.h"
#include "menu/bridge.h"
#include "original_effects.h"
#include "platform/timing.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

// $C2:0346, $C2:046E (the wait's start, and the jump to it)
/* The sprite-object mode's wait for the next frame: until the vblank handler
 * has counted a frame past the one it sees now. The mode is in it at nearly
 * every frame's end, so a saved machine has to hold it: the shipping mode
 * leaves it to the loop's idle entry, below, which goes on as this does, and
 * the port stops at the loop (tm_wait_for_handlers_held). The loop keeps the
 * count it saw in A, comparing the count with it, and the comparison's flags
 * are dead after it (the entries' set). */
enum { FRAME_WAIT_LOOP = 0xC20348, FRAME_WAIT_RETURN = 0xC2034C, THE_LOOP_SETS_THE_FLAGS = DC_N | DC_Z | DC_C };

void menu_wait_for_frame(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t seen = menu_object_frame_counter(g);
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | seen);
    m_set_nz8(s, seen);
    while (menu_object_frame_counter(g) == seen)
        if (!tm_wait_for_handlers_held(m_platform(s), PF_LOOP_HELD(FRAME_WAIT_LOOP, FRAME_WAIT_RETURN, THE_LOOP_SETS_THE_FLAGS)))
            return;
    m_rts(s);
}

// $C2:0348 (the loop in the wait for the next frame at $C2:0346)
/* The sprite-object mode waits for the vblank handler to count a frame past
 * the one it saw (in A). Idle entry: one pass, which goes on to the routine's
 * return once the count has moved (the compare's flags are dead there). */
void menu_wait_for_frame_loop(Snes *s)
{
    uint8_t seen = (uint8_t)s->cpu.a;
    if (menu_object_frame_counter(state_of(s)) == seen) {
        m_stop_at(s, FRAME_WAIT_LOOP, 0);
        return;
    }
    s->cpu.pc = (uint16_t)FRAME_WAIT_RETURN;
    m_rts(s);
}

// $C2:81CA (in the menus' loop, $C2:81B6)
/* The menus wait for the vblank handler to have sent the frame's uploads.
 * Idle entry: one pass, which goes on at $C2:81CF once they are sent, where
 * nothing reads what the loop loaded. */
void menu_wait_ready_loop(Snes *s)
{
    m_stop_at(s, menu_vblank_done(state_of(s)) != 0 ? 0xC281CF : 0xC281CA, 0);
}

/* The first waits on the menus' page, the second reads its cell through a
 * data bank that reaches low RAM. */
bool menu_frame_wait_on_page(Snes *s) { return m_direct_page(s) == 0x0000; }
bool menu_ready_wait_reaches_low_ram(Snes *s) { return m_bank_reaches_low_ram(s); }

/* ---- Stopping the tasks ------------------------------------------------------------------------- */

/* For the original's code and shadow mode: it shifts the bits out of the
 * menus' first cell as it goes. */
static void leave_bits_shifted_out(GameState *g) { set_menu_scratch_tasks_left(g, 0); }

// $C2:8255
void menu_stop_tasks(GameState *g, uint8_t tasks)
{
    for (unsigned task = 0; task < MENU_TASKS; task++)
        if (tasks >> task & 1)
            set_menu_tasks_state(g, task, 0);
    ORIGINAL_EFFECT(leave_bits_shifted_out(g));
}

// $C2:8255
void menu_stop_tasks_entry(Snes *s)
{
    uint8_t tasks = (uint8_t)s->cpu.a;
    menu_stop_tasks(state_of(s), tasks);
    m_rts(s);
}

/* ---- The frame's input, from another bank ------------------------------------------------------- */

// $C2:8002
/* The long entry the field's and the battle's vblank handlers, and the
 * sprite-object mode's, take the frame's input through: the controller, the
 * button assignments and the play time clock (core/input.c), run as its call
 * at $C2:8006 reaches it. Where the controller isn't read yet, the input work
 * stops where the original goes on waiting for it, and the original returns
 * from there. */
enum { INPUT_CALL = 0xC28006, INPUT_WORK = 0xC284D2 };

void menu_take_the_frames_input(Snes *s)
{
    if (menu_run(s, INPUT_CALL, INPUT_WORK, input_update))
        m_rtl(s);
}
