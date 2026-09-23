/* Field mode: synchronizing the main loop with the display. */
#include "field/field.h"

#include "platform/timing.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* $C0:EC71 (loop head inside the wait-for-next-frame routine at $C0:EC60)
 * The main loop waits here for the vblank handler to take the frame it
 * finished (field_frame_pending). Idle entry: one pass of the loop. */
void field_wait_for_frame_loop(Snes *s)
{
    uint8_t pending = field_frame_pending(state_of(s));
    m_set_a8(s, pending);
    m_set_nz8(s, pending);
    if (pending != 0)
        return;              /* still waiting: PC stays at the loop head */
    s->cpu.pc = 0xEC76;      /* the RTS after the loop */
    m_rts(s);
}

/* One pass of a wait for vblank before the field engine blanks the screen
 * and stops interrupts ($C0:0B4E) to load a location. The original spins on the vblank
 * interrupt's flag with that interrupt enabled, so it sees the flag only if
 * its read lands between the flag going up and the interrupt being taken;
 * otherwise the handler acknowledges the flag first ($C0:EA70) and the loop
 * misses it, and since the line interrupt before it sets the loop's phase the
 * same way every frame, misses it in every frame after. The original's fixed
 * timing wins that race; the shipping mode's estimated timing can lose it,
 * and did (the Lavos route stuck in map $DA01 when the line interrupt's exit
 * moved by 48 clocks). This waits for what the loop is for, vblank: the flag
 * as the original reads it, or the blanking status if the handler got there
 * first. Entered during vblank, after the handler, it leaves at once where the
 * original would spin until a later vblank's flag or for good: a timing
 * difference, and the load after it wants vblank. Idle entry; exits to `exit`. */
static void wait_for_vblank_pass(Snes *s, uint16_t at, uint16_t exit)
{
    Platform *pf = m_platform(s);
    uint8_t flag = m_lda8(s, tm_ack_vblank(pf, PF_AT(0xC00000 | at)));
    if ((flag & TM_VBLANK_PENDING) || (tm_video_status(pf, PF_AT(0xC00000 | at)) & TM_STATUS_VBLANK))
        s->cpu.pc = exit;
}

// $C0:3B71 (in event command $E1, change location now, after its port stops)
/* Waits on beam position: see wait_for_vblank_pass. */
void field_change_location_wait_for_vblank_loop(Snes *s) { wait_for_vblank_pass(s, 0x3B71, 0x3B76); }

// $C0:4181 (a handler in the table at $C0:4010: blank, then restart the field
// engine at $FD:C000 with a fresh stack)
/* Waits on beam position: see wait_for_vblank_pass. */
void field_restart_wait_for_vblank_loop(Snes *s) { wait_for_vblank_pass(s, 0x4181, 0x4186); }
