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
 * blank, sprites 16x16/32x32 with their tiles at VRAM $C000, mode 1 with BG3 in
 * front, every tilemap, character base and scroll cleared, the Mode 7 matrix set
 * to the identity, the windows cleared but for BG3's, both screens showing all
 * five layers, and a black fixed color. The reset code before this sets the CPU
 * registers and has no service; the port starts where the PPU block does and
 * ends at the jump into the boot, at $C0:000E.
 *
 * It runs once, at frame 0, which is why it matters out of proportion to its
 * size: the observer is built from the calls, so until this is one of them a
 * comparison from power-on starts out of step with the hardware. */
void display_init_registers(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    static const VidVramPort low_byte_port = { .step = 1, .after_high = false };

    m_rep(s, CPU_FLAG_M);
    c->d = m_lda16(s, M_DP_PPU);
    m_set_nz16(s, c->d);
    m_sep(s, CPU_FLAG_M);

    m_lda8(s, 0x80);
    vid_set_display(pf, PF_AT(0xFDC040), &(VidDisplay){ .blank = true });
    m_lda8(s, 0x63);
    VidObjConfig obj = vid_obj_config_decode(0x63);
    vid_set_obj_config(pf, PF_AT(0xFDC044), &obj);
    m_lda8(s, 0x00);
    vid_set_oam_address(pf, PF_AT(0xFDC048), 0x0000, false);
    m_lda8(s, 0x09);
    VidMode mode = vid_mode_decode(0x09);
    vid_set_mode(pf, PF_AT(0xFDC04E), &mode);
    m_lda8(s, 0x00);
    vid_set_mosaic(pf, PF_AT(0xFDC052), 0x00, 1);
    for (int bg = 0; bg < 4; bg++) {
        VidBg b = { 0 };
        vid_bg_map_decode(&b, 0x00);
        vid_set_bg_map(pf, PF_AT((uint32_t)(0xFDC054 + 2 * bg)), (VidLayer)bg, &b);
    }
    vid_set_bg_chars(pf, PF_AT(0xFDC05C), VID_BG1, 0x0000, 0x0000);
    vid_set_bg_chars(pf, PF_AT(0xFDC05E), VID_BG3, 0x0000, 0x0000);
    for (int bg = 0; bg < 4; bg++)
        vid_set_scroll(pf, PF_AT((uint32_t)(0xFDC060 + 8 * bg)), (VidLayer)bg, 0, 0,
                       VID_SCROLL_H | VID_SCROLL_V);
    vid_begin_vram_uploads(pf, PF_AT(0xFDC080), &low_byte_port, PF_GAME(0), NULL);
    vid_set_vram_address(pf, PF_AT(0xFDC082), 0x0000);
    vid_set_mode7(pf, PF_AT(0xFDC086), &(VidMode7){ 0 }, VID_M7_SELECT);
    /* The identity matrix, written as $00 $01 $00 $00 $00 $00 $00 $01 with the
     * accumulator stepped between the pairs. */
    m_lda8(s, 0x01);
    m_lda8(s, 0x00);
    m_lda8(s, 0x01);
    m_lda8(s, 0x00);
    vid_set_mode7(pf, PF_AT(0xFDC088), &(VidMode7){ .a = 0x0100, .d = 0x0100 }, VID_M7_MATRIX);
    vid_set_mode7(pf, PF_AT(0xFDC09D), &(VidMode7){ 0 }, VID_M7_CENTER);
    vid_set_palette_address(pf, PF_AT(0xFDC0A5), 0x00);

    VidWindows w = { 0 };
    vid_windows_decode(&w, VID_WINDOWS_BG1_BG2, 0x00);
    m_lda8(s, 0x03);
    vid_windows_decode(&w, VID_WINDOWS_BG3_BG4, 0x03);
    m_lda8(s, 0x00);
    static const uint32_t rest[] = {
        VID_WINDOWS_OBJ_BACK, VID_WINDOWS_W1_LEFT, VID_WINDOWS_W1_RIGHT, VID_WINDOWS_W2_LEFT,
        VID_WINDOWS_W2_RIGHT, VID_WINDOWS_BG_LOGIC, VID_WINDOWS_OBJ_LOGIC,
    };
    uint32_t fields = VID_WINDOWS_BG1_BG2 | VID_WINDOWS_BG3_BG4;
    for (size_t i = 0; i < sizeof rest / sizeof rest[0]; i++) {
        vid_windows_decode(&w, rest[i], 0x00);
        fields |= rest[i];
    }
    vid_set_windows(pf, PF_AT(0xFDC0A7), &w, fields);
    vid_windows_decode(&w, VID_WINDOWS_MAIN, 0x00);
    vid_windows_decode(&w, VID_WINDOWS_SUB, 0x00);
    vid_set_windows(pf, PF_AT(0xFDC0BD), &w, VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
    VidColorMath math = { 0 };
    vid_color_math_decode(&math, VID_MATH_CONTROL, 0x00);
    vid_color_math_decode(&math, VID_MATH_LAYERS, 0x00);
    vid_set_color_math(pf, PF_AT(0xFDC0C1), &math, VID_MATH_CONTROL | VID_MATH_LAYERS);
    m_lda8(s, 0x1F);
    vid_set_screens(pf, PF_AT(0xFDC0C7), 0x1F, 0x1F, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    m_lda8(s, 0xC0);
    vid_set_fixed_color(pf, PF_AT(0xFDC0CD), VID_GREEN | VID_BLUE, 0);
    m_lda8(s, 0x00);
    vid_set_video_config(pf, PF_AT(0xFDC0D1), &(VidVideoConfig){ 0 });

    c->pb = 0xC0;
    c->pc = 0x000E;
}
