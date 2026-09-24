/* The screen and the interrupts, as every game mode stops them and as the
 * console starts. */
#include "core/display.h"

#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"

// $C0:0B4E
/* Stop everything that shows or interrupts: the screen blank, the frame and
 * line interrupts and the controller's automatic read off, and every transfer
 * and per-line effect stopped. A mode does it before it changes what the
 * screen shows. */
static void blank_and_stop(Platform *pf)
{
    vid_set_display(pf, PF_AT(0xC00B55), &(VidDisplay){ .blank = true });
    tm_enable_interrupts(pf, PF_AT(0xC00B5A), 0);
    vid_stop_uploads(pf, PF_AT(0xC00B5D));
    vid_enable_line_effects(pf, PF_AT(0xC00B60), 0);
}

/* It masks the CPU's interrupts too and leaves the data bank 0; what it
 * leaves in A and the flags nobody reads (the entry's set). */
void display_blank_and_stop_interrupts(Snes *s)
{
    m_set_flag(s, CPU_FLAG_I, true);
    s->cpu.db = 0x00;
    blank_and_stop(m_platform(s));
    m_rts(s);
}

// $FD:C036
/* The register block at power-on, written through a direct page on it: forced
 * blank; the sprites 16x16 and 32x32 with their tiles at VRAM word $6000; mode
 * 1 with BG3 in front; every tilemap, character base and scroll at 0 and the
 * mosaic off; the Mode 7 plane unflipped with the identity matrix; no window
 * but window 1 on BG3, drawing outside it; no color math; all five layers on
 * both screens; and a black fixed color (its red left as it was). The reset
 * code before this sets the CPU registers and has no service; the port starts
 * where the PPU block does and ends at the jump into the boot, at $C0:000E.
 *
 * It runs once, at frame 0, which is why it matters out of proportion to its
 * size: the observer is built from the calls, so until this is one of them a
 * comparison from power-on starts out of step with the hardware. */
static const VidScreen power_on_screen = {
    .display = &(const VidDisplay){ .blank = true },
    .sprites = { .size_select = 3, .base = 0x6000 },
    .mode = { .mode = 1, .bg3_priority = true },
    .mosaic_size = 1,
    .bg = { { VID_TILEMAP(0x0000, 32, 32) }, { VID_TILEMAP(0x0000, 32, 32) }, { VID_TILEMAP(0x0000, 32, 32) },
            { VID_TILEMAP(0x0000, 32, 32) } },
    .mode7 = { .a = 0x0100, .d = 0x0100 },
    .windows = { .w1 = { [VID_BG3] = VID_WIN_OUT } },
    .main_layers = VID_ALL_LAYERS,
    .sub_layers = VID_ALL_LAYERS,
    /* VRAM written a byte at a time, the address stepping after the low one */
    .vram_port = { .step = 1, .after_high = false },
};

/* The port hands over to the boot where the original jumps to it; what it
 * leaves in the registers the boot sets again. */
void display_init_registers(Snes *s)
{
    vid_apply_screen(m_platform(s), PF_AT(0xFDC036), &power_on_screen);
    m_stop_at(s, 0xC0000E, DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C | DC_D);
}
