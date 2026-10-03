/* The screen and the interrupts, as every game mode stops them. */
#include "core/display.h"

#include "charge.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"

// $C0:0B4E
/* Stop everything that shows or interrupts: the screen blank, the frame and
 * line interrupts and the controller's automatic read off, and every transfer
 * and per-line effect stopped. A mode does it before it changes what the
 * screen shows. */
void display_blank_and_stop(Platform *pf)
{
    vid_set_display(pf, PF_AT(0xC00B55), &(VidDisplay){ .blank = true });
    tm_enable_interrupts(pf, PF_AT(0xC00B5A), 0);
    vid_stop_uploads(pf, PF_AT(0xC00B5D));
    vid_enable_line_effects(pf, PF_AT(0xC00B60), 0);
}

/* It masks the CPU's interrupts too and leaves the data bank 0; what it
 * leaves in A and the flags nobody reads (the entry's set). */
static void go_dark(Snes *s)
{
    m_set_flag(s, CPU_FLAG_I, true);
    s->cpu.db = 0x00;
    display_blank_and_stop(m_platform(s));
}

void display_blank_and_stop_interrupts(Snes *s)
{
    go_dark(s);
    m_rts(s);
}

void display_go_dark(Snes *s) { TIMED_AS(state_of(s), DISPLAY_BLANK_AND_STOP_ENTRY, go_dark(s)); }
