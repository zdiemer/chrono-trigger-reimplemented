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

/* The original leaves the direct page on the PPU registers and A with the $2100
 * it set it from and the last byte it wrote, 0, and hands over to the boot. */
void display_init_registers(Snes *s)
{
    vid_apply_screen(m_platform(s), PF_AT(0xFDC036), &power_on_screen);
    s->cpu.d = M_DP_PPU;
    s->cpu.a = M_DP_PPU;
    m_set_nz8(s, 0x00);
    m_stop_at(s, 0xC0000E, 0);
}
