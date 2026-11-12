/* The screen and the interrupts, as every game mode stops them. */
#include "core/display.h"

#include "charge.h"
#include "platform/timing.h"
#include "platform/video.h"
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
