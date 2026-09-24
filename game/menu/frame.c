/* The menus: keeping the main loop in step with the display.
 *
 * Each of bank C2's two modes has a main loop that does a frame's work and
 * then waits for its vblank handler: the sprite-object mode's for the next
 * frame to be counted (game/menu/display.c, menu_object_nmi), the menus'
 * for the handler to have sent the uploads the frame queued
 * (menu_nmi_work). Both waits are idle loops the runtime runs a pass at a
 * time. */
#include "menu/menu.h"

#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

// $C2:0348 (the loop in the wait for the next frame at $C2:0346)
/* The sprite-object mode waits for the vblank handler to count a frame past
 * the one it saw (in A). Idle entry: one pass, which goes on to the routine's
 * return once the count has moved (the compare's flags are dead there). */
void menu_wait_for_frame_loop(Snes *s)
{
    uint8_t seen = (uint8_t)s->cpu.a;
    if (menu_object_frame_counter(state_of(s)) == seen) {
        m_stop_at(s, 0xC20348, 0);
        return;
    }
    s->cpu.pc = 0x034C;   /* the RTS after the loop */
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
