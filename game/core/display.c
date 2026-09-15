/* Display and interrupt control shared by every game mode. */
#include "core/display.h"

#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"

/* $C0:0B4E
 * Blank the display and stop interrupts, DMA and HDMA: force blank on, NMI and
 * timer IRQs and auto-joypad off. Leaves the data bank at 0.
 * Entry: 8-bit A. Exit: A low byte 0, I set, N clear, Z set. */
void display_blank_and_stop_interrupts(Snes *s)
{
    Platform *pf = m_platform(s);
    m_set_flag(s, CPU_FLAG_I, true);

    /* The data bank is set by pushing 0 and pulling it, which leaves the 0 in
     * the byte just below the stack pointer. */
    m_push8(s, 0x00);
    s->cpu.db = m_pull8(s);

    vid_set_display(pf, PF_AT(0xC00B55), &(VidDisplay){ .blank = true });
    tm_enable_interrupts(pf, PF_AT(0xC00B5A), 0);
    vid_stop_uploads(pf, PF_AT(0xC00B5D));
    vid_enable_line_effects(pf, PF_AT(0xC00B60), 0);

    m_set_a8(s, 0x00);
    m_set_nz8(s, 0x00);
    m_rts(s);
}
