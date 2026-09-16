/* Menu mode: the routines that drive the display.
 *
 * Two modes live in bank C2 and each has its own vblank handler. The mode at
 * $C2:800E (the menus proper, and the load screen every save movie starts in)
 * keeps its display state as a block of shadow bytes and copies it out in
 * vblank: twelve scroll bytes at $7E:0D04 through the work RAM port, sixteen
 * more at $7E:9680 with an MVN straight into $2123-$2132, then the sprite
 * table, the palette and the queued VRAM uploads. The mode at $C2:0000 (the
 * sprite objects of the item, equip and shop screens) keeps the same state on
 * direct page 0 and copies it byte by byte instead.
 *
 * The block copies are what P2 had to take apart: each byte is named here and
 * goes out through the call that stands for it, so the order the registers see
 * is the original's while the meaning is the service's.
 */
#include "menu/display.h"

#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"

/* The work RAM data port and its address register: memory access, not video
 * (docs/PLATFORM.md), so ports reach it directly. */
enum { WRAM_PORT = 0x2180, WRAM_ADDR = 0x2181, WRAM_ADDR_BANK = 0x2183 };

/* The setting every upload here uses unless it says otherwise: a word at a
 * time, stepping after the high byte. */
static const VidVramPort word_port = { .step = 1, .after_high = true };

/* The menus upload over DMA channel 7, the mode at $C2:0000 over channel 0. */
enum { MENU_DMA = 7, OBJECT_DMA = 0 };
enum { DMA_ONE_REGISTER = 0x00, DMA_WORDS = 0x01, DMA_FILL_WORDS = 0x09, DMA_FIXED_BYTE = 0x08 };

bool menu_display_bank_is_io(Snes *s) { return m_io_bank(s->cpu.db); }

static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }

/* PLP, narrowing X and Y when the pulled P makes them 8-bit. */
static void plp(Snes *s)
{
    s->cpu.p = m_pull8(s);
    if (s->cpu.p & CPU_FLAG_X) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
}

/* PEA #$xxyy : PLB : PLB, which leaves the data bank at the high byte. */
static void set_data_bank(Snes *s, uint16_t pea)
{
    m_push16(s, pea);
    for (int i = 0; i < 2; i++) {
        s->cpu.db = m_pull8(s);
        m_set_nz8(s, s->cpu.db);
    }
}

/* ---- the mode at $C2:800E ------------------------------------------------------ */

/* The blank-the-screen-and-stop-everything sequence three places in the mode
 * share: forced blank, every interrupt but the controller read off, no
 * transfers and no per-line effects. */
static void blank_and_stop(Snes *s, const uint32_t site[4])
{
    Platform *pf = m_platform(s);
    m_lda8(s, 0x80);
    vid_set_display(pf, PF_AT(site[0]), &(VidDisplay){ .blank = true });
    m_lda8(s, 0x01);
    tm_enable_interrupts(pf, PF_AT(site[1]), TM_INT_AUTO_JOYPAD);
    m_lda8(s, 0x00);
    vid_stop_uploads(pf, PF_AT(site[2]));
    vid_enable_line_effects(pf, PF_AT(site[3]), 0x00);
}

// $C2:800E
/* Entering the menus: native mode, the mode's data bank and the entry
 * parameters saved at $7E:0A00, then the screen blanked while it is set up.
 * Stops at the stack switch at $C2:8039, which the original carries on with. */
void menu_mode_start(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint32_t sites[4] = { 0xC28025, 0xC2802B, 0xC28031, 0xC28035 };
    m_set_flag(s, CPU_FLAG_I, true);
    m_set_flag(s, CPU_FLAG_C, false);   /* CLC : XCE, already in native mode */
    m_push16(s, c->d);
    m_push8(s, c->db);
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    set_data_bank(s, 0x7E7E);
    m_write8(s, m_abs(s, 0x0A00), (uint8_t)c->a);
    m_write16(s, m_abs(s, 0x0A01), c->x);
    blank_and_stop(s, sites);
    c->pc = 0x8039;
}

// $C2:82C1
/* Leaving the menus: the screen blanked, the caller's stack put back from
 * $7E:0920 and the registers restored. Returns with RTL. */
void menu_mode_return(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint32_t sites[4] = { 0xC282C3, 0xC282C9, 0xC282CF, 0xC282D3 };
    blank_and_stop(s, sites);
    m_xba(s);
    c->s = m_ldx(s, m_read16(s, m_abs(s, 0x0920)));
    plp(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_set_flag(s, CPU_FLAG_I, true);
    m_rtl(s);
}

// $C2:832F
/* The menus giving up and restarting the game: the same blank, then the boot
 * stack and a jump to $C0:0000. */
void menu_mode_restart(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint32_t sites[4] = { 0xC28331, 0xC28337, 0xC2833D, 0xC28341 };
    blank_and_stop(s, sites);
    c->s = m_ldx(s, 0x06FF);
    c->pb = 0xC0;
    c->pc = 0x0000;
}

// $C2:842A (the body of the NMI handler at $C2:840E, installed at the $0500
// stub from the table at $C2:8406)
/* The menus' vblank handler: acknowledges the interrupt, counts the frame at
 * $0D00, forces blank, and while $0D01 is clear runs the vblank uploads
 * ($C2:85D6) with the per-line effects off, restoring the enable shadow $0D13
 * afterwards. Then the input and timers ($C2:8473), the BG mode, and the
 * brightness from $0D10 (0 meaning forced blank). Stops at $C2:8466, where the
 * handler starts putting the registers back.
 *
 * The entry at $C2:840E saves the registers and moves the stack up to $1DFD,
 * which shadow validation reads as a routine that has returned, so the port
 * begins after the switch; the instructions it leaves out touch no hardware. */
void menu_nmi_work(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    /* The acknowledge is a CMP against $4210, whose bits 4-6 are open bus: the
     * interpreter's instruction fetch leaves the register's own address there
     * and native code doesn't, so the carry it sets is meaningless. Nothing
     * reads it before it is set again, and it is the routine's don't-care. */
    m_cmp8(s, (uint8_t)c->a, tm_ack_vblank(pf, PF_AT(0xC2842A)));
    uint8_t frame = (uint8_t)(m_read8(s, m_dp(s, 0x00)) + 1);
    m_write8(s, m_dp(s, 0x00), frame);
    m_set_nz8(s, frame);
    m_lda8(s, 0x80);
    vid_set_display(pf, PF_AT(0xC28431), &(VidDisplay){ .blank = true });
    if (!(m_lda8(s, m_read8(s, m_dp(s, 0x01))) & 0x80)) {
        vid_enable_line_effects(pf, PF_AT(0xC2843C), 0x00);
        m_jsr_dc(s, 0xC2843F, 0xC285D6, DC_C);
        if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
            return;
        uint8_t slots = m_lda8(s, m_read8(s, m_dp(s, 0x13)));
        vid_enable_line_effects(pf, PF_AT(0xC28446), m_lda8(s, (uint8_t)(slots & 0x7F)));
        m_write8(s, m_dp(s, 0x01), m_lda8(s, 0xFF));
    }
    m_jsr_dc(s, 0xC2844D, 0xC28473, DC_C);
    if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
        return;
    m_lda8(s, 0x09);
    vid_set_mode(pf, PF_AT(0xC28452), &(VidMode){ .mode = 1, .bg3_priority = true });
    uint8_t brightness = m_lda8(s, m_read8(s, m_dp(s, 0x10)));
    VidDisplay display = { .blank = true };
    if (brightness != 0)
        display = vid_display_decode(brightness);
    else
        m_lda8(s, 0x80);
    vid_set_display(pf, PF_AT(0xC2845B), &display);
    uint8_t wait = m_lda8(s, m_read8(s, m_abs(s, 0x0D76)));
    if (wait != 0) {
        wait--;
        m_write8(s, m_abs(s, 0x0D76), wait);
        m_set_nz8(s, wait);
    }
    c->pc = 0x8466;
}

/* $C2:8607-$C2:8623: STX $2102 : STX $4370 : STX $4372 : STX $4375 :
 * STA $4374 : STA $420B, with the sprite table at $7E:0700. */
static const uint8_t menu_oam_steps[] = {
    VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_CONTROL,     VID_STEP_DEST,
    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SIZE_LO,     VID_STEP_SIZE_HI,
    VID_STEP_SOURCE_BANK, VID_STEP_START,   VID_STEP_END,
};

// $C2:85D6
/* The menus' vblank work. The scroll shadows at $7E:0D04-$0D0F go out through
 * the work RAM port, two bytes to each of the six scroll registers; the sixteen
 * bytes at $7E:9680 are the window, screen and color math state, in register
 * order from $2123 to $2132; then the sprite table at $7E:0700, a block move
 * the frame asked for through bit 7 of $0D13, the palette, and the slot 0 table
 * for the frame ($7E:9E19 or $7E:9F19, whichever $7E:A01E names). Falls through
 * into the VRAM upload queue at $C2:8663, where this port stops.
 *
 * The original interleaves each read with the register write it feeds; the
 * calls below take the bytes of a register group together, which only moves
 * work RAM reads ahead of register writes that can't change them. */
void menu_vblank_upload(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);

    m_ldx(s, 0x0D04);
    m_write16(s, m_abs(s, WRAM_ADDR), 0x0D04);
    m_lda8(s, 0x00);
    m_write8(s, m_abs(s, WRAM_ADDR_BANK), 0x00);

    /* X walks the scroll registers themselves, as the original does; it is CPU
     * state, so the port keeps the values, named off the register block. */
    enum { FIRST_SCROLL = M_DP_PPU + 0x0D, PAST_SCROLL = M_DP_PPU + 0x13 };
    m_ldx(s, FIRST_SCROLL);
    do {
        uint8_t lo = m_lda8(s, m_read8(s, m_abs(s, WRAM_PORT)));
        uint8_t hi = m_lda8(s, m_read8(s, m_abs(s, WRAM_PORT)));
        uint16_t v = (uint16_t)(lo | hi << 8);
        int bg = (c->x - FIRST_SCROLL) / 2;
        bool vertical = ((c->x - FIRST_SCROLL) & 1) != 0;
        vid_set_scroll(pf, PF_AT(0xC285E7), (VidLayer)bg, v, v,
                       vertical ? VID_SCROLL_V : VID_SCROLL_H);
        m_inx(s);
        m_cmp16(s, c->x, PAST_SCROLL);
    } while (c->x < PAST_SCROLL);

    m_lda8(s, 0x00);
    m_xba(s);
    m_ldx(s, 0x9680);
    m_ldy(s, M_DP_PPU + 0x23);
    m_lda8(s, 0x0F);
    /* The MVN's sixteen bytes, each named by the register it lands in. */
    {
        uint8_t v[16];
        for (int i = 0; i < 16; i++)
            v[i] = m_read8(s, 0x7E9680 + i);
        static const uint32_t window_fields[9] = {
            VID_WINDOWS_BG1_BG2, VID_WINDOWS_BG3_BG4, VID_WINDOWS_OBJ_BACK,
            VID_WINDOWS_W1_LEFT, VID_WINDOWS_W1_RIGHT, VID_WINDOWS_W2_LEFT, VID_WINDOWS_W2_RIGHT,
            VID_WINDOWS_BG_LOGIC, VID_WINDOWS_OBJ_LOGIC,
        };
        VidWindows w = { 0 };
        uint32_t fields = 0;
        for (int i = 0; i < 9; i++) {
            vid_windows_decode(&w, window_fields[i], v[i]);
            fields |= window_fields[i];
        }
        vid_set_windows(pf, PF_AT(0xC28601), &w, fields);
        vid_set_screens(pf, PF_AT(0xC28601), v[9], v[10], VID_SCREEN_MAIN | VID_SCREEN_SUB);
        vid_windows_decode(&w, VID_WINDOWS_MAIN, v[11]);
        vid_windows_decode(&w, VID_WINDOWS_SUB, v[12]);
        vid_set_windows(pf, PF_AT(0xC28601), &w, VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
        VidColorMath math = { 0 };
        vid_color_math_decode(&math, VID_MATH_CONTROL, v[13]);
        vid_set_color_math(pf, PF_AT(0xC28601), &math, VID_MATH_CONTROL);
        vid_color_math_decode(&math, VID_MATH_LAYERS, v[14]);
        vid_set_color_math(pf, PF_AT(0xC28601), &math, VID_MATH_LAYERS);
        vid_set_fixed_color(pf, PF_AT(0xC28601), (uint8_t)(v[15] >> 5), (uint8_t)(v[15] & 0x1F));
        /* What the block move leaves behind; it sets no flags. */
        c->a = 0xFFFF;
        c->x = 0x9690;
        c->y = M_DP_PPU + 0x33;
        c->db = 0x00;
    }

    static const VidSnesXfer oam = { MENU_DMA, DMA_ONE_REGISTER, menu_oam_steps, NULL };
    m_ldx(s, 0x0000);
    m_ldx(s, 0x0400);
    m_ldx(s, 0x0700);
    m_ldx(s, 0x0220);
    m_lda8(s, 0x7E);
    vid_upload_oam(pf, PF_AT(0xC28607), PF_GAME(0x7E0700), &oam);
    m_lda8(s, 0x80);

    m_lda8(s, 0x80);
    m_trb8(s, m_dp(s, 0x13));
    if (!m_flag(s, CPU_FLAG_Z)) {
        /* The block move the frame asked for, through the MVN stub at $7E:9690. */
        m_push8(s, c->db);
        m_push8(s, c->p);
        set_data_bank(s, 0x7E7E);
        m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
        m_write16(s, m_abs(s, 0x9690), m_ldx(s, 0x7E54));
        m_write16(s, m_abs(s, 0x9692), m_ldx(s, 0x6B7E));
        m_ldx(s, m_read16(s, m_abs(s, 0x9694)));
        m_ldy(s, m_read16(s, m_abs(s, 0x9696)));
        m_lda16(s, m_read16(s, m_abs(s, 0x9698)));
        m_lda16(s, (uint16_t)(c->a - 1));
        m_jsl(s, 0xC2864B, 0x7E9690);
        if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != 0)
            return;
        plp(s);
        c->db = m_pull8(s);
        m_set_nz8(s, c->db);
    }

    m_jsr(s, 0xC28651, 0xC286A9);
    if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
        return;

    static const uint8_t table_steps[] = { VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_END };
    uint16_t table = m_ldx(s, 0x9E19);
    if (m_lda8(s, m_read8(s, 0x7EA01E)) != 0)
        table = m_ldx(s, 0x9F19);
    vid_set_line_table(pf, PF_AT(0xC28660), 0, table, table_steps);
    c->pc = 0x8663;
}

// $C2:8663
/* Run the queued VRAM uploads: $0D11 counts eight bytes per entry, and the
 * entries at $00:0C00 and up are sent from the last one down. An entry holds
 * the VRAM word address, the source address and bank, and a size, which the
 * transfer registers read as 64 KiB when it is 0. Leaves the queue empty. */
void menu_upload_vram_queue(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    /* STA $4370 before the loop; then STA $2116 : STA $4372 : STY $4374 :
     * STA $4375 : STX $420B in it. */
    static const uint8_t first_steps[] = {
        VID_STEP_CONTROL,   VID_STEP_DEST,      VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI,
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,   VID_STEP_START,
        VID_STEP_END,
    };
    m_push8(s, c->db);
    m_push16(s, c->d);
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    set_data_bank(s, 0x0000);

    if (m_ldx(s, m_read8(s, m_abs(s, 0x0D11))) != 0) {
        m_write8(s, m_abs(s, 0x0D11), 0x00);
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
        m_lda16(s, 0x1801);
        m_txa(s);
        c->a = m_adc16(s, c->a, 0x0BF8);
        m_ldx(s, 0x80);
        bool first = true;
        do {
            c->d = c->a;
            uint16_t vram = m_lda16(s, dp16(s, 0x00));
            uint16_t source = m_lda16(s, dp16(s, 0x02));
            uint8_t bank = (uint8_t)m_ldy(s, m_read8(s, m_dp(s, 0x04)));
            uint32_t size = m_lda16(s, dp16(s, 0x05));
            const VidSnesXfer x = { MENU_DMA, DMA_WORDS, first ? first_steps : steps, NULL };
            vid_upload_vram(pf, PF_AT(0xC28686), vram, PF_GAME((uint32_t)bank << 16 | source),
                            size ? size : 0x10000, &x);
            first = false;
            m_tdc(s);
            m_set_flag(s, CPU_FLAG_C, true);
            c->a = m_sbc16(s, c->a, 0x0008);
            m_cmp16(s, c->a, 0x0C00);
        } while (c->a >= 0x0C00);
    }
    plp(s);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}

// $C2:86A9
/* Send the working palette at $7E:9480 to CGRAM when any bit of $0D15 is set,
 * which the routines that change colors set and this one clears. */
void menu_upload_palette(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    /* STX $4370 : STX $4372 : STX $4375 : STA $4374 : STA $2121 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,   VID_STEP_DEST,      VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_SOURCE_BANK, VID_STEP_TARGET_LO,
        VID_STEP_START,     VID_STEP_END,
    };
    static const VidSnesXfer x = { MENU_DMA, DMA_ONE_REGISTER, steps, NULL };
    m_push8(s, c->db);
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M);
    set_data_bank(s, 0x0000);
    m_lda8(s, 0xFF);
    m_trb8(s, m_abs(s, 0x0D15));
    if (!m_flag(s, CPU_FLAG_Z)) {
        m_ldx(s, 0x2200);
        m_ldx(s, 0x9480);
        m_ldx(s, 0x0200);
        m_lda8(s, 0x7E);
        m_lda8(s, 0x00);
        vid_upload_palette(pf, PF_AT(0xC286BC), 0, PF_GAME(0x7E9480), 256, &x);
        m_lda8(s, 0x80);
    }
    plp(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}

// $C2:86DD
/* Build the per-line color math table the menus run on slot 0 and program the
 * slot for it: two registers per line ($2131 with the fixed color), read
 * directly out of bank $7E. There are two tables, $7E:9E19 and $7E:9F19,
 * swapped by $7E:A01D, so the frame that is showing keeps its own. The entries
 * come from the records at $7E:9D99: a zero count copies one color straight
 * through, a nonzero one ramps sixteen steps of the color list at $7E:9DE9,
 * which is refreshed from $C2:8771 first. */
void menu_build_math_line_table(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    /* LDA #$3101 : STA $004300 then LDA #$007E : STA $004304 */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL, VID_STEP_DEST, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_END,
    };
    m_push8(s, c->p);
    m_push8(s, c->db);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_ldx(s, 0x8771);
    m_ldy(s, 0x9DE9);
    m_lda16(s, 0x001F);
    m_mvn(s, 0x7E, 0xC2);
    m_lda16(s, 0x3101);
    const VidLineEffect e = { .target = VID_LINE_MATH_LAYERS, .table = PF_GAME(0x7E9E19),
                              .indirect = false, .indirect_bank = 0x7E };
    vid_set_line_effect(pf, PF_AT(0xC286ED), 0, &e, steps);
    m_lda16(s, 0x007E);

    m_ldy(s, 0x0000);
    m_lda16(s, 0x9E19);
    if (m_ldx(s, m_read16(s, m_abs(s, 0xA01D))) == 0) {
        m_ldy(s, 0x0100);
        m_lda16(s, 0x9F19);
    }
    m_write16(s, m_abs(s, 0xA01D), c->y);
    m_write16(s, m_abs(s, 0xA01B), 0x0000);

    m_ldx(s, 0x0000);
    for (;;) {
        m_sep(s, CPU_FLAG_M);
        uint8_t lines = m_lda8(s, m_read8(s, m_abs(s, (uint16_t)(0x9D99 + c->x))));
        if (lines & 0x80)
            break;
        m_rep(s, CPU_FLAG_M);
        if (lines == 0) {
            m_lda16(s, m_read16(s, m_abs(s, (uint16_t)(0x9D9A + c->x))));
            m_write16(s, m_abs(s, (uint16_t)(0x9E19 + c->y)), c->a);
            m_lda16(s, m_read16(s, m_abs(s, 0x9DF9)));
            m_write16(s, m_abs(s, (uint16_t)(0x9E1A + c->y)), c->a);
            for (int i = 0; i < 3; i++)
                m_iny(s);
        } else {
            m_lda16(s, m_read16(s, m_abs(s, (uint16_t)(0x9D9A + c->x))));
            m_lda16(s, (uint16_t)(c->a & 0x00FF));
            for (int i = 0; i < 4; i++)
                c->a = m_asl16(s, c->a);
            m_write16(s, m_abs(s, 0xA019), c->a);
            m_push16(s, c->x);
            m_ldx(s, 0x0000);
            do {
                m_rep(s, CPU_FLAG_M);
                m_lda16(s, m_read16(s, m_abs(s, 0xA01B)));
                m_set_flag(s, CPU_FLAG_C, false);
                c->a = m_adc16(s, c->a, m_read16(s, m_abs(s, 0xA019)));
                m_write16(s, m_abs(s, 0xA01B), c->a);
                m_sep(s, CPU_FLAG_M);
                m_xba(s);
                m_write8(s, m_abs(s, (uint16_t)(0x9E19 + c->y)), (uint8_t)c->a);
                m_write8(s, m_abs(s, 0xA01C), 0x00);
                m_rep(s, CPU_FLAG_M);
                m_lda16(s, m_read16(s, m_abs(s, (uint16_t)(0x9DE9 + c->x))));
                m_write16(s, m_abs(s, (uint16_t)(0x9E1A + c->y)), c->a);
                m_inx(s);
                m_inx(s);
                for (int i = 0; i < 3; i++)
                    m_iny(s);
                m_cmp16(s, c->x, 0x0020);
            } while (c->x < 0x0020);
            c->x = m_pull16(s);
        }
        m_inx(s);
        m_inx(s);
    }
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    plp(s);
    m_rts(s);
}

/* ---- the mode at $C2:0000 ------------------------------------------------------ */

// $C2:0043
/* The mode's video setup, written through a direct page on the register block:
 * no sprites, mode 1 with the BG3 priority bit, the four tilemaps at $6000,
 * $6800, $7800 and $7800 and character bases at $2000, $2000, $7000 and $7000,
 * every scroll and Mode 7 value at its identity, no windows, BG1-BG3 and the
 * sprites on the main screen with BG1, BG2 and the backdrop on the sub screen,
 * color math adding the sub screen with halving, and a black fixed color.
 * Stops at $C2:00F2, where the routine moves on to the CPU registers. */
void menu_object_init_video(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    m_push16(s, c->d);
    m_push16(s, M_DP_PPU);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);

    m_lda8(s, 0x60);
    VidObjConfig obj = vid_obj_config_decode(0x60);
    vid_set_obj_config(pf, PF_AT(0xC2004A), &obj);
    m_ldx(s, 0x0000);
    vid_set_oam_address(pf, PF_AT(0xC2004F), 0x0000, false);
    m_lda8(s, 0x09);
    vid_set_mode(pf, PF_AT(0xC20053), &(VidMode){ .mode = 1, .bg3_priority = true });
    m_lda8(s, 0x00);
    vid_set_mosaic(pf, PF_AT(0xC20057), 0x00, 1);
    static const struct { uint8_t byte; uint32_t site; } maps[4] = {
        { 0x61, 0xC2005B }, { 0x69, 0xC2005F }, { 0x79, 0xC20063 }, { 0x79, 0xC20067 },
    };
    for (int bg = 0; bg < 4; bg++) {
        VidBg b = { 0 };
        m_lda8(s, maps[bg].byte);
        vid_bg_map_decode(&b, maps[bg].byte);
        vid_set_bg_map(pf, PF_AT(maps[bg].site), (VidLayer)bg, &b);
    }
    m_lda8(s, 0x22);
    vid_set_bg_chars(pf, PF_AT(0xC2006B), VID_BG1, 0x2000, 0x2000);
    m_lda8(s, 0x77);
    vid_set_bg_chars(pf, PF_AT(0xC2006F), VID_BG3, 0x7000, 0x7000);
    m_lda8(s, 0x00);
    for (int bg = 0; bg < 4; bg++)
        vid_set_scroll(pf, PF_AT((uint32_t)(0xC20073 + 8 * bg)), (VidLayer)bg, 0, 0,
                       VID_SCROLL_H | VID_SCROLL_V);
    m_lda8(s, 0x80);
    vid_begin_vram_uploads(pf, PF_AT(0xC20095), VID_VRAM_WORD_PORT, PF_GAME(0), NULL);
    m_ldx(s, 0x0000);
    vid_set_vram_address(pf, PF_AT(0xC2009A), 0x0000);
    m_lda8(s, 0x00);
    vid_set_mode7(pf, PF_AT(0xC2009E), &(VidMode7){ 0 }, VID_M7_SELECT);
    VidMode7 m7 = { .a = 0x0100, .d = 0x0100 };
    vid_set_mode7(pf, PF_AT(0xC200A0), &m7, VID_M7_MATRIX);
    vid_set_mode7(pf, PF_AT(0xC200B8), &m7, VID_M7_CENTER);
    m_lda8(s, 0x00);
    vid_set_palette_address(pf, PF_AT(0xC200C0), 0x00);
    VidWindows w = { 0 };
    static const uint32_t window_fields[9] = {
        VID_WINDOWS_BG1_BG2, VID_WINDOWS_BG3_BG4, VID_WINDOWS_OBJ_BACK,
        VID_WINDOWS_W1_LEFT, VID_WINDOWS_W1_RIGHT, VID_WINDOWS_W2_LEFT, VID_WINDOWS_W2_RIGHT,
        VID_WINDOWS_BG_LOGIC, VID_WINDOWS_OBJ_LOGIC,
    };
    uint32_t fields = 0;
    for (int i = 0; i < 9; i++) {
        vid_windows_decode(&w, window_fields[i], 0x00);
        fields |= window_fields[i];
    }
    vid_set_windows(pf, PF_AT(0xC200C2), &w, fields);
    m_lda8(s, 0x17);
    m_lda8(s, 0x13);
    vid_set_screens(pf, PF_AT(0xC200D6), 0x17, 0x13, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    m_lda8(s, 0x00);
    vid_windows_decode(&w, VID_WINDOWS_MAIN, 0x00);
    vid_windows_decode(&w, VID_WINDOWS_SUB, 0x00);
    vid_set_windows(pf, PF_AT(0xC200DE), &w, VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
    VidColorMath math = { 0 };
    m_lda8(s, 0x02);
    vid_color_math_decode(&math, VID_MATH_CONTROL, 0x02);
    vid_set_color_math(pf, PF_AT(0xC200E4), &math, VID_MATH_CONTROL);
    m_lda8(s, 0x44);
    vid_color_math_decode(&math, VID_MATH_LAYERS, 0x44);
    vid_set_color_math(pf, PF_AT(0xC200E8), &math, VID_MATH_LAYERS);
    m_lda8(s, 0xE0);
    vid_set_fixed_color(pf, PF_AT(0xC200EC), VID_RED | VID_GREEN | VID_BLUE, 0);
    m_lda8(s, 0x00);
    vid_set_video_config(pf, PF_AT(0xC200F0), &(VidVideoConfig){ 0 });
    c->pc = 0x00F2;
}

/* The shadow block the mode at $C2:0000 keeps on direct page 0: $1E-$49, in the
 * order the vblank handler copies them out. */
static void menu_object_copy_shadows(Snes *s)
{
    Platform *pf = m_platform(s);
    uint8_t v[0x2C];
    for (int i = 0; i < 0x2C; i++)
        v[i] = m_read8(s, m_dp(s, (uint8_t)(0x1E + i)));
#define SH(cell) v[(cell) - 0x1E]
#define SH16(cell) ((uint16_t)(SH(cell) | SH((cell) + 1) << 8))

    m_lda8(s, SH(0x1E));
    VidMode mode = vid_mode_decode(SH(0x1E));
    vid_set_mode(pf, PF_AT(0xC201D0), &mode);
    m_lda8(s, SH(0x1F));
    vid_set_mosaic(pf, PF_AT(0xC201D5), (uint8_t)(SH(0x1F) & 0x0F), (uint8_t)((SH(0x1F) >> 4) + 1));

    /* $210D-$2112: the three layers' scroll, horizontal then vertical. */
    static const struct { uint8_t cell; bool vertical; uint32_t site; } scroll[6] = {
        { 0x20, false, 0xC201DA }, { 0x26, true, 0xC201E4 }, { 0x22, false, 0xC201EE },
        { 0x28, true, 0xC201F8 },  { 0x24, false, 0xC20202 }, { 0x2A, true, 0xC2020C },
    };
    for (int i = 0; i < 6; i++) {
        uint16_t val = (uint16_t)(SH(scroll[i].cell) | SH(scroll[i].cell + 1) << 8);
        m_lda8(s, SH(scroll[i].cell));
        m_lda8(s, SH(scroll[i].cell + 1));
        vid_set_scroll(pf, PF_AT(scroll[i].site), (VidLayer)(i / 2), val, val,
                       scroll[i].vertical ? VID_SCROLL_V : VID_SCROLL_H);
    }

    for (int i = 0; i < 8; i++)
        m_lda8(s, SH(0x2C + i));
    VidMode7 m7 = { .a = (int16_t)SH16(0x2C), .b = (int16_t)SH16(0x2E),
                    .c = (int16_t)SH16(0x30), .d = (int16_t)SH16(0x32) };
    vid_set_mode7(pf, PF_AT(0xC20216), &m7, VID_M7_MATRIX);
    for (int i = 0; i < 4; i++)
        m_lda8(s, SH(0x34 + i));
    m7.cx = (int16_t)SH16(0x34);
    m7.cy = (int16_t)SH16(0x36);
    vid_set_mode7(pf, PF_AT(0xC2023C), &m7, VID_M7_CENTER);

    /* $2123-$212B, $212C/$212D, $212E/$212F, $2130, $2131, $2132 three times. */
    static const uint32_t window_fields[9] = {
        VID_WINDOWS_BG1_BG2, VID_WINDOWS_BG3_BG4, VID_WINDOWS_OBJ_BACK,
        VID_WINDOWS_W1_LEFT, VID_WINDOWS_W1_RIGHT, VID_WINDOWS_W2_LEFT, VID_WINDOWS_W2_RIGHT,
        VID_WINDOWS_BG_LOGIC, VID_WINDOWS_OBJ_LOGIC,
    };
    VidWindows w = { 0 };
    uint32_t fields = 0;
    for (int i = 0; i < 9; i++) {
        m_lda8(s, SH(0x38 + i));
        vid_windows_decode(&w, window_fields[i], SH(0x38 + i));
        fields |= window_fields[i];
    }
    vid_set_windows(pf, PF_AT(0xC20250), &w, fields);
    m_lda8(s, SH(0x41));
    m_lda8(s, SH(0x42));
    vid_set_screens(pf, PF_AT(0xC2027D), SH(0x41), SH(0x42), VID_SCREEN_MAIN | VID_SCREEN_SUB);
    m_lda8(s, SH(0x43));
    m_lda8(s, SH(0x44));
    vid_windows_decode(&w, VID_WINDOWS_MAIN, SH(0x43));
    vid_windows_decode(&w, VID_WINDOWS_SUB, SH(0x44));
    vid_set_windows(pf, PF_AT(0xC20287), &w, VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
    VidColorMath math = { 0 };
    m_lda8(s, SH(0x45));
    vid_color_math_decode(&math, VID_MATH_CONTROL, SH(0x45));
    vid_set_color_math(pf, PF_AT(0xC20291), &math, VID_MATH_CONTROL);
    m_lda8(s, SH(0x46));
    vid_color_math_decode(&math, VID_MATH_LAYERS, SH(0x46));
    vid_set_color_math(pf, PF_AT(0xC20296), &math, VID_MATH_LAYERS);
    /* The fixed color one channel at a time: blue from $47, green from $49,
     * red from $48, each with its channel bit ORed in. */
    static const struct { uint8_t cell, bit; uint32_t site; } color[3] = {
        { 0x47, 0x80, 0xC2029B }, { 0x49, 0x40, 0xC202A2 }, { 0x48, 0x20, 0xC202A9 },
    };
    for (int i = 0; i < 3; i++) {
        uint8_t byte = (uint8_t)(SH(color[i].cell) | color[i].bit);
        m_lda8(s, SH(color[i].cell));
        m_lda8(s, byte);
        vid_set_fixed_color(pf, PF_AT(color[i].site), (uint8_t)(byte >> 5), (uint8_t)(byte & 0x1F));
    }
#undef SH16
#undef SH
}

// $C2:015E (installed at the $0501 NMI stub by $C2:0110)
/* The vblank handler of the sprite object mode. While bit 0 of $4C says the
 * main loop has finished a frame it sends the sprite table at $00:0700 and,
 * with bit 1, the palette at $00:0920, copies the whole shadow block to the
 * registers, runs the queued VRAM uploads and the C2 per-frame routine, sets
 * the brightness from $1C and $1D and the per-line enables from $4A, and then
 * runs the mode's object and sprite work. */
void menu_object_nmi(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    /* STX $2102 : STZ $4300 : STA $4301 : STX $4302 : STZ $4304 : STX $4305 :
     * STA $420B, and the same order for the palette with STA $2121 first. */
    static const uint8_t oam_steps[] = {
        VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI, VID_STEP_CONTROL, VID_STEP_DEST,
        VID_STEP_SOURCE_LO,   VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI,     VID_STEP_START,     VID_STEP_END,
    };
    static const uint8_t palette_steps[] = {
        VID_STEP_TARGET_LO,   VID_STEP_CONTROL,   VID_STEP_DEST,      VID_STEP_SOURCE_LO,
        VID_STEP_SOURCE_HI,   VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,
        VID_STEP_START,       VID_STEP_END,
    };
    static const VidSnesXfer oam = { OBJECT_DMA, DMA_ONE_REGISTER, oam_steps, NULL };
    static const VidSnesXfer palette = { OBJECT_DMA, DMA_ONE_REGISTER, palette_steps, NULL };

    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);
    m_push16(s, c->d);
    m_push8(s, c->db);
    m_lda16(s, 0x0000);
    c->d = 0x0000;
    m_sep(s, CPU_FLAG_M);
    m_push8(s, (uint8_t)c->a);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);

    m_lda8(s, tm_ack_vblank(pf, PF_AT(0xC2016D)));
    uint8_t counter = (uint8_t)(m_read8(s, m_dp(s, 0x4B)) + 1);
    m_write8(s, m_dp(s, 0x4B), counter);
    m_set_nz8(s, counter);
    uint8_t work = m_lda8(s, m_read8(s, m_dp(s, 0x4C)));
    m_bit_imm8(s, 0x01);
    if (work & 0x01) {
        m_lda8(s, 0x80);
        vid_set_display(pf, PF_AT(0xC2017D), &(VidDisplay){ .blank = true });
        m_tdc(s);
        vid_enable_line_effects(pf, PF_AT(0xC20181), 0x00);
        m_ldx(s, 0x0000);
        m_lda8(s, 0x04);
        m_ldx(s, 0x0700);
        m_ldx(s, 0x0220);
        m_lda8(s, 0x01);
        vid_upload_oam(pf, PF_AT(0xC20187), PF_GAME(0x000700), &oam);

        work = m_lda8(s, m_read8(s, m_dp(s, 0x4C)));
        m_bit_imm8(s, 0x02);
        if (work & 0x02) {
            m_tdc(s);
            m_lda8(s, 0x22);
            m_ldx(s, 0x0920);
            m_ldx(s, 0x0200);
            m_lda8(s, 0x01);
            vid_upload_palette(pf, PF_AT(0xC201AD), 0, PF_GAME(0x000920), 256, &palette);
        }
        m_write8(s, m_dp(s, 0x4C), 0x00);

        menu_object_copy_shadows(s);

        m_rep(s, CPU_FLAG_M);
        static const struct { uint16_t from, to; } copies[] = {
            { 0x8A21, 0x8A36 }, { 0x8A23, 0x8A39 }, { 0x8A25, 0x8A43 }, { 0x8A27, 0x8A46 },
            { 0x8A29, 0x8A50 }, { 0x8A2B, 0x8A53 }, { 0x8A2D, 0x8A5D }, { 0x8A2F, 0x8A60 },
            { 0x8A31, 0x8A6A }, { 0x8A33, 0x8A6D },
        };
        for (size_t i = 0; i < sizeof copies / sizeof copies[0]; i++)
            m_write16(s, 0x7E0000 | copies[i].to, m_lda16(s, m_read16(s, 0x7E0000 | copies[i].from)));
        m_lda16(s, m_read16(s, 0x7EA916));
        for (uint16_t a = 0xA919; a <= 0xA922; a = (uint16_t)(a + 3))
            m_write16(s, 0x7E0000 | a, c->a);
        m_sep(s, CPU_FLAG_M);

        m_jsr(s, 0xC20318, 0xC20405);
        if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
            return;
        m_jsl(s, 0xC2031B, 0xC28002);
        if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
            return;
        uint8_t brightness = m_lda8(s, m_read8(s, m_dp(s, 0x1C)));
        brightness = m_lda8(s, (uint8_t)(brightness | m_read8(s, m_dp(s, 0x1D))));
        VidDisplay display = vid_display_decode(brightness);
        vid_set_display(pf, PF_AT(0xC20323), &display);
        vid_enable_line_effects(pf, PF_AT(0xC20328), m_lda8(s, m_read8(s, m_dp(s, 0x4A))));
        m_jsr(s, 0xC2032B, 0xC20C4D);
        if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
            return;
        m_write8(s, m_dp(s, 0x4D), 0x00);
        static const uint32_t tail[3][2] = {
            { 0xC20330, 0xC2034D }, { 0xC20333, 0xC2051D }, { 0xC20336, 0xC20CEA },
        };
        for (int i = 0; i < 3; i++) {
            m_jsr(s, tail[i][0], tail[i][1]);
            if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
                return;
        }
        m_lda8(s, 0x01);
        m_tsb8(s, m_dp(s, 0x4C));
    }

    m_rep(s, CPU_FLAG_M);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    c->y = m_pull16(s);
    m_set_nz16(s, c->y);
    c->x = m_pull16(s);
    m_set_nz16(s, c->x);
    c->a = m_pull16(s);
    m_set_nz16(s, c->a);
    m_rti(s);
}

// $C2:0405
/* The sprite object mode's VRAM upload queue: while $5E is clear, the eight-byte
 * records from $60 up to $5F each name a source address and bank, a VRAM word
 * address, a size and the VRAM port setting to send them with, over channel 0.
 * Empties the queue and puts the record pointer back to $7E:F000. */
void menu_object_upload_vram_queue(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    /* STA $4302 : STA $2116 : STA $4305 : STA $4304 : STA $2115 : STA $4301 :
     * STA $4300 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_SOURCE_BANK, VID_STEP_VRAM_PORT,
        VID_STEP_DEST,      VID_STEP_CONTROL,   VID_STEP_START,       VID_STEP_END,
    };
    if (m_lda8(s, m_read8(s, m_dp(s, 0x5E))) != 0) {
        m_rts(s);
        return;
    }
    if (m_lda8(s, m_read8(s, m_dp(s, 0x5F))) == 0) {
        m_rts(s);
        return;
    }
    m_sep(s, CPU_FLAG_X);
    m_ldx(s, 0x00);
    uint8_t limit;
    do {
        m_rep(s, CPU_FLAG_M);
        uint16_t source = m_lda16(s, m_read16(s, m_dp(s, (uint8_t)(0x60 + c->x))));
        uint16_t vram = m_lda16(s, m_read16(s, m_dp(s, (uint8_t)(0x63 + c->x))));
        uint32_t size = m_lda16(s, m_read16(s, m_dp(s, (uint8_t)(0x65 + c->x))));
        m_sep(s, CPU_FLAG_M);
        uint8_t bank = m_lda8(s, m_read8(s, m_dp(s, (uint8_t)(0x62 + c->x))));
        VidVramPort port = vid_vram_port_decode(m_lda8(s, m_read8(s, m_dp(s, (uint8_t)(0x67 + c->x)))));
        m_lda8(s, 0x18);
        m_lda8(s, 0x01);
        const VidSnesXfer x = { OBJECT_DMA, DMA_WORDS, steps, &port };
        vid_upload_vram(pf, PF_AT(0xC20417), vram, PF_GAME((uint32_t)bank << 16 | source),
                        size ? size : 0x10000, &x);
        m_txa(s);
        m_set_flag(s, CPU_FLAG_C, false);
        m_set_a8(s, m_adc8(s, (uint8_t)c->a, 0x08));
        m_tax(s);
        limit = m_read8(s, m_dp(s, 0x5F));
        m_cmp8(s, (uint8_t)c->x, limit);
    } while ((uint8_t)c->x != limit);
    m_write8(s, m_dp(s, 0x5F), 0x00);
    m_rep(s, CPU_FLAG_X);
    m_write16(s, m_dp(s, 0xE0), m_ldx(s, 0xF000));
    m_write8(s, m_dp(s, 0xE2), m_lda8(s, 0x7E));
    m_rts(s);
}

bool menu_sprite_tiles_page_is_io(Snes *s) { return m_io_bank((uint8_t)s->cpu.d); }

// $C2:0ADC
/* Send one sprite's two tile columns to VRAM: the word address from $0C and
 * $10 (wrapping inside one 32-tile page), then $40 bytes from the pointer at
 * $E0 and $40 more from the next column, both with the port stepping a tile
 * column at a time. The data bank comes from the low byte of the direct page. */
void menu_object_upload_sprite_tiles(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    static const VidVramPort column_port = { .step = 32, .after_high = true };
    /* STX $2116 : STA $2115 : STA $4370 : STA $4371 : STX $4372 : STA $4374 :
     * STX $4375 : STA $420B */
    static const uint8_t first_steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_VRAM_PORT, VID_STEP_CONTROL,
        VID_STEP_DEST,      VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    /* STA $2116 : STA $4372 : STA $4374 : STA $2115 : STA $4370 : STA $4371 :
     * STX $4375 : STA $420B */
    static const uint8_t second_steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_VRAM_PORT, VID_STEP_CONTROL, VID_STEP_DEST,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    m_lda16(s, dp16(s, 0x0C));
    for (int i = 0; i < 3; i++)
        c->a = m_lsr16(s, c->a);
    m_cmp16(s, c->a, 0x0020);
    if (c->a >= 0x0020) {
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, 0x03E0);
    }
    m_set_flag(s, CPU_FLAG_C, false);
    uint16_t base = c->a = m_adc16(s, c->a, dp16(s, 0x10));
    m_write16(s, m_dp(s, 0x12), base);
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    m_push8(s, c->db);
    m_tdc(s);
    m_push8(s, (uint8_t)c->a);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);

    m_lda8(s, 0x81);
    m_lda8(s, 0x01);
    m_lda8(s, 0x18);
    uint16_t source = m_ldx(s, dp16(s, 0xE0));
    uint8_t bank = m_lda8(s, m_read8(s, m_dp(s, 0xE2)));
    m_ldx(s, 0x0040);
    const VidSnesXfer first = { MENU_DMA, DMA_WORDS, first_steps, &column_port };
    vid_upload_vram(pf, PF_AT(0xC20AF6), base, PF_GAME((uint32_t)bank << 16 | source), 0x40, &first);
    m_lda8(s, 0x80);

    m_rep(s, CPU_FLAG_M);
    m_lda16(s, dp16(s, 0x12));
    uint16_t next = m_lda16(s, (uint16_t)(c->a + 1));
    m_lda16(s, dp16(s, 0xE0));
    m_set_flag(s, CPU_FLAG_C, false);
    uint16_t source2 = c->a = m_adc16(s, c->a, 0x0040);
    uint16_t bank2 = m_lda16(s, dp16(s, 0xE2));
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, 0x81);
    m_lda8(s, 0x01);
    m_lda8(s, 0x18);
    m_ldx(s, 0x0040);
    const VidSnesXfer second = { MENU_DMA, DMA_WORDS, second_steps, &column_port };
    vid_upload_vram(pf, PF_AT(0xC20B22), next, PF_GAME((uint32_t)(bank2 & 0xFF) << 16 | source2), 0x40,
                    &second);
    m_lda8(s, 0x80);
    m_rep(s, CPU_FLAG_M);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}

/* ---- loading a screen's graphics ------------------------------------------------ */

// $C2:26A8
/* Clear all of VRAM with the two bytes at $C2:26D4, a whole 64 KiB from word 0. */
void menu_clear_vram(Snes *s)
{
    Platform *pf = m_platform(s);
    /* STX $2116 : STA $2115 : STA $4370 : STA $4371 : STX $4372 : STA $4374 :
     * STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_VRAM_PORT, VID_STEP_CONTROL,
        VID_STEP_DEST,      VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    static const VidSnesXfer x = { MENU_DMA, DMA_FILL_WORDS, steps, &word_port };
    m_ldx(s, 0x0000);
    m_lda8(s, 0x80);
    m_lda8(s, 0x09);
    m_lda8(s, 0x18);
    m_ldx(s, 0x26D4);
    m_lda8(s, 0xC2);
    m_ldx(s, 0x0000);
    vid_fill_vram(pf, PF_AT(0xC226AB), 0x0000, PF_GAME(0xC226D4), 0x10000, &x);
    m_lda8(s, 0x80);
    m_rts(s);
}

// $C2:2D70
/* The graphics loader's transfer: A bytes from $7F:X to VRAM word Y, a word at
 * a time. A size of 0 means a whole 64 KiB, as the registers read it. */
void menu_upload_vram(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    /* STX $4372 : STY $2116 : STA $4375 : STA $2115 : STX $4370 : STA $4374 :
     * STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_TARGET_LO, VID_STEP_TARGET_HI,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_VRAM_PORT, VID_STEP_CONTROL,
        VID_STEP_DEST,      VID_STEP_SOURCE_BANK, VID_STEP_START,   VID_STEP_END,
    };
    static const VidSnesXfer x = { MENU_DMA, DMA_WORDS, steps, &word_port };
    uint32_t size = c->a;
    vid_upload_vram(pf, PF_AT(0xC22D70), c->y, PF_GAME(0x7F0000 | c->x), size ? size : 0x10000, &x);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, 0x80);
    m_ldx(s, 0x1801);
    m_lda8(s, 0x7F);
    m_lda8(s, 0x80);
    m_rts(s);
}

// $C2:2D47
/* The palette the graphics loader decompressed to $7F:8C00, sent to CGRAM.
 * Reached by falling out of the loader's last block move; stops at the call to
 * $C2:2D91, which the original carries on with. */
void menu_upload_loaded_palette(Snes *s)
{
    Platform *pf = m_platform(s);
    /* STA $2121 : STX $4370 : STX $4372 : STA $4374 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_CONTROL,   VID_STEP_DEST,    VID_STEP_SOURCE_LO,
        VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,
        VID_STEP_START,     VID_STEP_END,
    };
    static const VidSnesXfer x = { MENU_DMA, DMA_ONE_REGISTER, steps, NULL };
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, 0x00);
    m_ldx(s, 0x2200);
    m_ldx(s, 0x8C00);
    m_lda8(s, 0x7F);
    m_ldx(s, 0x0200);
    vid_upload_palette(pf, PF_AT(0xC22D4B), 0, PF_GAME(0x7F8C00), 256, &x);
    m_lda8(s, 0x80);
    s->cpu.pc = 0x2D6A;
}

// $C2:940D
/* The video setup of the screens the graphics loader builds: forced blank, no
 * sprites, mode 1 with the BG3 priority bit, tilemaps at $B400, $C400 and
 * $D400 and character bases at 0 and $7000, everything else at its identity and
 * a black fixed color. Stops at $C2:94B8, where the routine moves on to the
 * CPU registers. */
void menu_init_video(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    m_push8(s, c->db);
    m_push16(s, c->d);
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_X);
    m_sep(s, CPU_FLAG_M);
    set_data_bank(s, 0x0000);
    m_push16(s, M_DP_PPU);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);

    m_lda8(s, 0x80);
    vid_set_display(pf, PF_AT(0xC2941F), &(VidDisplay){ .blank = true });
    m_lda8(s, 0x02);
    VidObjConfig obj = vid_obj_config_decode(0x02);
    vid_set_obj_config(pf, PF_AT(0xC29423), &obj);
    m_lda8(s, 0x09);
    vid_set_mode(pf, PF_AT(0xC29427), &(VidMode){ .mode = 1, .bg3_priority = true });
    static const struct { uint8_t byte; uint32_t site; } maps[4] = {
        { 0x5A, 0xC2942B }, { 0x62, 0xC2942F }, { 0x6A, 0xC29433 }, { 0x00, 0xC29437 },
    };
    for (int bg = 0; bg < 4; bg++) {
        VidBg b = { 0 };
        m_lda8(s, maps[bg].byte);
        vid_bg_map_decode(&b, maps[bg].byte);
        vid_set_bg_map(pf, PF_AT(maps[bg].site), (VidLayer)bg, &b);
    }
    m_lda8(s, 0x00);
    vid_set_bg_chars(pf, PF_AT(0xC2943B), VID_BG1, 0x0000, 0x0000);
    m_lda8(s, 0x07);
    vid_set_bg_chars(pf, PF_AT(0xC2943F), VID_BG3, 0x7000, 0x0000);
    m_lda8(s, 0x00);
    vid_set_mosaic(pf, PF_AT(0xC29443), 0x00, 1);
    for (int bg = 0; bg < 4; bg++)
        vid_set_scroll(pf, PF_AT((uint32_t)(0xC29445 + 8 * bg)), (VidLayer)bg, 0, 0,
                       VID_SCROLL_H | VID_SCROLL_V);
    m_lda8(s, 0x80);
    vid_begin_vram_uploads(pf, PF_AT(0xC29467), VID_VRAM_WORD_PORT, PF_GAME(0), NULL);
    m_ldx(s, 0x0000);
    vid_set_vram_address(pf, PF_AT(0xC2946C), 0x0000);
    m_lda8(s, 0x00);
    vid_set_mode7(pf, PF_AT(0xC29470), &(VidMode7){ 0 }, VID_M7_SELECT);
    VidMode7 m7 = { .a = 0x0100, .d = 0x0100 };
    vid_set_mode7(pf, PF_AT(0xC29472), &m7, VID_M7_MATRIX);
    vid_set_mode7(pf, PF_AT(0xC2948E), &m7, VID_M7_CENTER);
    m_lda8(s, 0x00);
    vid_set_palette_address(pf, PF_AT(0xC29492), 0x00);
    static const uint32_t window_fields[9] = {
        VID_WINDOWS_BG1_BG2, VID_WINDOWS_BG3_BG4, VID_WINDOWS_OBJ_BACK,
        VID_WINDOWS_W1_LEFT, VID_WINDOWS_W1_RIGHT, VID_WINDOWS_W2_LEFT, VID_WINDOWS_W2_RIGHT,
        VID_WINDOWS_BG_LOGIC, VID_WINDOWS_OBJ_LOGIC,
    };
    VidWindows w = { 0 };
    uint32_t fields = 0;
    for (int i = 0; i < 9; i++) {
        vid_windows_decode(&w, window_fields[i], 0x00);
        fields |= window_fields[i];
    }
    vid_set_windows(pf, PF_AT(0xC29494), &w, fields);
    vid_set_screens(pf, PF_AT(0xC294A6), 0x00, 0x00, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_windows_decode(&w, VID_WINDOWS_MAIN, 0x00);
    vid_windows_decode(&w, VID_WINDOWS_SUB, 0x00);
    vid_set_windows(pf, PF_AT(0xC294AA), &w, VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
    VidColorMath math = { 0 };
    vid_color_math_decode(&math, VID_MATH_CONTROL, 0x00);
    vid_set_color_math(pf, PF_AT(0xC294AE), &math, VID_MATH_CONTROL);
    vid_color_math_decode(&math, VID_MATH_LAYERS, 0x00);
    vid_set_color_math(pf, PF_AT(0xC294B0), &math, VID_MATH_LAYERS);
    vid_set_video_config(pf, PF_AT(0xC294B2), &(VidVideoConfig){ 0 });
    m_lda8(s, 0xE0);
    vid_set_fixed_color(pf, PF_AT(0xC294B6), VID_RED | VID_GREEN | VID_BLUE, 0);
    c->pc = 0x94B8;
}

// $C2:94D5
/* The rest of that setup: VRAM filled from word 0 with the two bytes at
 * $C2:9510, then three stretches of work RAM cleared with them. */
void menu_init_video_memory(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    /* STX $4300 : STX $4305 : STX $4302 : STA $4304 : STA $420B, with the VRAM
     * address and port already set by the first half of the routine. */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,   VID_STEP_DEST,      VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_START,
        VID_STEP_END,
    };
    static const VidSnesXfer x = { OBJECT_DMA, DMA_FILL_WORDS, steps, NULL };
    m_ldx(s, 0x1809);
    m_ldx(s, 0xFFFF);
    m_ldx(s, 0x9510);
    m_lda8(s, 0xC2);
    m_lda8(s, 0x01);
    vid_fill_vram(pf, PF_AT(0xC294D8), 0x0000, PF_GAME(0xC29510), 0xFFFF, &x);
    static const struct { uint16_t addr, size; uint32_t site; } clears[3] = {
        { 0x0C00, 0x1200, 0xC294F7 }, { 0x2E00, 0xD200, 0xC29500 }, { 0x0000, 0x0100, 0xC29509 },
    };
    for (int i = 0; i < 3; i++) {
        m_ldx(s, clears[i].addr);
        m_ldy(s, clears[i].size);
        m_jsr(s, clears[i].site, 0xC29512);
        if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != 0)
            return;
    }
    plp(s);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}

// $C2:9512
/* Clear Y bytes of work RAM from $7E:X with the two bytes at $C2:9510, over
 * DMA channel 0 into the work RAM port. The destination is memory, not video,
 * so the transfer goes through the SNES-only escape; the per-line effects are
 * turned off first, since the channel is shared with them. */
void menu_fill_memory(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    /* STY $4305 : STX $4300 : STX $4302 : STA $4304 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_CONTROL,     VID_STEP_DEST,
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_START,
        VID_STEP_END,
    };
    static const VidSnesXfer x = { OBJECT_DMA, DMA_FIXED_BYTE, steps, NULL };
    m_push8(s, c->db);
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M);
    set_data_bank(s, 0x0000);
    vid_enable_line_effects(pf, PF_AT(0xC2951B), 0x00);
    m_write16(s, m_abs(s, WRAM_ADDR), c->x);
    m_lda8(s, 0x00);
    m_write8(s, m_abs(s, WRAM_ADDR_BANK), 0x00);
    const VidSnesTransfer t = { .dest = 0x80, .source = 0xC29510, .size = c->y };
    m_ldx(s, 0x8008);
    m_ldx(s, 0x9510);
    m_lda8(s, 0xC2);
    m_lda8(s, 0x01);
    vid_snes_transfer(pf, PF_AT(0xC29526), &t, &x);
    plp(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}

// $C2:9542
/* Copy Y bytes from $FF:A into work RAM at $7E:X, over DMA channel 0 into the
 * work RAM port: memory again, so the SNES-only escape. */
void menu_copy_to_memory(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    /* STA $4302 : STY $4305 : STX $4300 : STA $4304 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SIZE_LO,     VID_STEP_SIZE_HI,
        VID_STEP_CONTROL,   VID_STEP_DEST,      VID_STEP_SOURCE_BANK, VID_STEP_START,
        VID_STEP_END,
    };
    static const VidSnesXfer x = { OBJECT_DMA, DMA_ONE_REGISTER, steps, NULL };
    m_push8(s, c->db);
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    set_data_bank(s, 0x0000);
    const VidSnesTransfer t = { .dest = 0x80, .source = 0xFF0000 | c->a, .size = c->y };
    m_write16(s, m_abs(s, WRAM_ADDR), c->x);
    m_ldx(s, 0x8000);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, 0x00);
    m_write8(s, m_abs(s, WRAM_ADDR_BANK), 0x00);
    m_lda8(s, 0xFF);
    m_lda8(s, 0x01);
    vid_snes_transfer(pf, PF_AT(0xC2954B), &t, &x);
    plp(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}

/* ---- window commands ------------------------------------------------------------ */

/* Command 12 of the window command interpreter ($C2:ED31) takes the channel and
 * its two control bytes from the command stream, so the target has to be read
 * back out of them; the guard leaves a channel that isn't one of the eight
 * bases, or a pair no target names, to the original, which writes at whatever
 * offsets the operand gives. */
bool menu_window_line_effect_is_known(Snes *s)
{
    uint16_t cell = (uint16_t)(s->cpu.d + 0x5B);
    uint16_t channel = (uint16_t)(snes_bus_peek(&s->bus, cell) |
                                  snes_bus_peek(&s->bus, (uint16_t)(cell + 1)) << 8);
    if ((channel & 0xFF8F) != M_DP_DMA)   /* the eight channels' own base addresses */
        return false;
    uint8_t control = snes_bus_peek(&s->bus, (uint16_t)(cell + 2));
    uint8_t dest = snes_bus_peek(&s->bus, (uint16_t)(cell + 3));
    VidLineEffect e = { 0 };
    return vid_line_effect_decode(control, dest, &e);
}

// $C2:F337
/* Program the per-line effect slot a window command names: the channel's base
 * address is in $5B and its transfer mode and destination register in $5D, and
 * its table lies at $7E:$9000 + 128 per slot, which the command's other
 * handlers fill. */
void menu_window_set_line_effect(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    static const uint8_t channel_steps[] = {
        VID_STEP_SOURCE_BANK, VID_STEP_INDIRECT_BANK, VID_STEP_CONTROL, VID_STEP_DEST, VID_STEP_END,
    };
    static const uint8_t table_steps[] = { VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_END };
    m_push8(s, c->db);
    m_push8(s, c->p);
    set_data_bank(s, 0x0000);
    uint16_t base = m_ldy(s, dp16(s, 0x5B));
    m_lda8(s, 0x7E);
    m_rep(s, CPU_FLAG_M);
    uint16_t channel = m_lda16(s, dp16(s, 0x5D));
    VidLineEffect e = { 0 };
    vid_line_effect_decode((uint8_t)channel, (uint8_t)(channel >> 8), &e);
    e.indirect_bank = 0x7E;

    m_lda16(s, dp16(s, 0x5B));
    m_lda16(s, (uint16_t)(c->a & 0x00F0));
    for (int i = 0; i < 3; i++)
        c->a = m_asl16(s, c->a);
    m_set_flag(s, CPU_FLAG_C, false);
    uint16_t table = c->a = m_adc16(s, c->a, 0x9000);
    e.table = PF_GAME(0x7E0000 | table);

    vid_set_line_effect(pf, PF_AT(0xC2F342), (uint8_t)((base >> 4) & 7), &e, channel_steps);
    m_write16(s, m_dp(s, 0x61), table);
    vid_set_line_table(pf, PF_AT(0xC2F35C), (uint8_t)((base >> 4) & 7), table, table_steps);

    plp(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}

// $FF:FC04
/* Build the indirect per-line table at $6E00 that slot 1 reads, and point the
 * slot at it: one entry of 96 lines, then sixteen groups of three lines whose
 * values come from the table at $FF:FC57. Returns with RTL. */
void menu_window_build_line_table(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint8_t table_steps[] = { VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_END };
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_lda16(s, 0x6E00);
    vid_set_line_table(m_platform(s), PF_AT(0xFFFC0A), 1, 0x6E00, table_steps);
    m_write16(s, m_abs(s, 0x6E00), m_ldx(s, 0x0060));
    m_write16(s, m_abs(s, 0x6E02), m_lda16(s, 0x00E0));
    m_write16(s, m_abs(s, 0x6E93), 0x0000);
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_write8(s, m_dp(s, 0x02), 0x00);
    m_write8(s, m_dp(s, 0x00), m_lda8(s, 0x02));
    m_ldy(s, 0x03);
    uint8_t group;
    do {
        m_lda8(s, 0x03);
        m_write8(s, m_dp(s, 0x01), 0x03);
        uint8_t left;
        do {
            m_write8(s, m_abs(s, (uint16_t)(0x6E00 + c->y)), m_lda8(s, 0x01));
            m_iny(s);
            m_write8(s, m_abs(s, (uint16_t)(0x6E00 + c->y)), m_lda8(s, m_read8(s, m_dp(s, 0x00))));
            m_iny(s);
            m_ldx(s, m_read8(s, m_dp(s, 0x02)));
            m_write8(s, m_abs(s, (uint16_t)(0x6E00 + c->y)),
                     m_lda8(s, m_read8(s, 0xFFFC57 + (uint8_t)c->x)));
            m_iny(s);
            left = (uint8_t)(m_read8(s, m_dp(s, 0x01)) - 1);
            m_write8(s, m_dp(s, 0x01), left);
            m_set_nz8(s, left);
        } while (left != 0);
        group = (uint8_t)(m_read8(s, m_dp(s, 0x02)) + 1);
        m_write8(s, m_dp(s, 0x02), group);
        m_set_nz8(s, group);
        m_lda8(s, m_read8(s, m_dp(s, 0x02)));
        m_cmp8(s, group, 0x08);
        if (group >= 0x08)
            m_write8(s, m_dp(s, 0x00), (uint8_t)m_ldx(s, 0x82));
        m_cmp8(s, group, 0x10);
    } while (group < 0x10);
    plp(s);
    m_rtl(s);
}
