/* Battle: the NMI and IRQ handlers.
 *
 * The vblank handler starts the frame's display work: it blanks the screen,
 * runs the per-frame work for the battle mode and, unless the battle is
 * paused, $C0:0002 and the frame count, then takes the Start button's pause
 * and the debug frame advance. The line handler, at line 211,
 * blanks the screen for the rest of the frame and, on alternate frames,
 * applies the alternating-frame setting or runs the tile animation uploads.
 * Both save the registers on the stack, which stays live across the routines
 * they call out to, and both keep the hardware multiplier's or divider's
 * operands for the code they interrupted.
 *
 * Each handler's work is C; what it does to the original's registers is in
 * the entry functions, which save and restore them. The handlers run with the
 * battle page ($0000); the code they call leaves the data bank at $7E, which
 * they rely on, and a call that doesn't hands the rest back to the original. */
#include "battle/battle.h"

#include "battle/screen_data.h"
#include "platform/input.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "ram.h"

enum {
    SAVED_DIRECT_PAGE_BYTES = 43,
    BUTTON_START = 0x10,   /* in the high byte of the joypad word */
    BUTTON_R = 0x10,       /* in the low byte */
    BUTTON_L = 0x20,
    FLAG_NO_PAUSE = 0x20,  /* battle_setup_flags_copy */
    MODE_FIGHT = 1,        /* the battle mode Start pauses in */
};

/* ---- The machine at the handlers' edges -------------------------------------------- */

/* REP #$30 : PHA : PHX : PHY : PHB : PHD */
static void save_registers(Snes *s)
{
    Cpu *c = &s->cpu;
    c->p &= (uint8_t)~(CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);
    m_push8(s, c->db);
    m_push16(s, c->d);
}

/* REP #$30 : PLD : PLB : PLY : PLX : PLA : RTI */
static void restore_registers_and_return(Snes *s)
{
    Cpu *c = &s->cpu;
    c->p &= (uint8_t)~(CPU_FLAG_M | CPU_FLAG_X);
    c->d = m_pull16(s);
    c->db = m_pull8(s);
    c->y = m_pull16(s);
    c->x = m_pull16(s);
    c->a = m_pull16(s);
    m_rti(s);
}

static bool widths_m8_x16(const Snes *s) { return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M; }

/* After a call-out: false when the callee came back without 8-bit A, 16-bit
 * index registers and the data bank at $7E, which the rest relies on; the
 * machine is then at the return address, and the original carries on. */
static bool came_back(const Snes *s) { return widths_m8_x16(s) && s->cpu.db == 0x7E; }

/* The direct page's first 43 bytes, which the frame work uses as scratch,
 * saved around it with MVN. It leaves the block move's end pointers and a
 * count of $FFFF in the registers. */
static void move_direct_page(Snes *s, uint16_t from, uint16_t to)
{
    for (uint16_t i = 0; i < SAVED_DIRECT_PAGE_BYTES; i++)
        m_write8(s, 0x7E0000u + (uint16_t)(to + i), m_read8(s, 0x7E0000u + (uint16_t)(from + i)));
    s->cpu.x = (uint16_t)(from + SAVED_DIRECT_PAGE_BYTES);
    s->cpu.y = (uint16_t)(to + SAVED_DIRECT_PAGE_BYTES);
    s->cpu.a = 0xFFFF;
    s->cpu.db = 0x7E;
}

// $D1:FE71
/* Put the operands of the hardware divider (while the main code has it busy)
 * or multiplier back, redoing the operation this handler's own arithmetic
 * clobbered. */
static void restore_math_operands(Snes *s)
{
    if (m_read8(s, RAM_BATTLE_DIVIDER_BUSY)) {
        m_write8(s, 0x004204, m_read8(s, RAM_BATTLE_DIVIDER_OPERANDS));
        m_write8(s, 0x004205, m_read8(s, RAM_BATTLE_DIVIDER_OPERANDS + 1));
        m_write8(s, 0x004206, m_read8(s, RAM_BATTLE_DIVIDER_OPERANDS + 2));
    } else {
        m_write8(s, 0x004202, m_read8(s, RAM_BATTLE_MULTIPLIER_OPERANDS));
        m_write8(s, 0x004203, m_read8(s, RAM_BATTLE_MULTIPLIER_OPERANDS + 1));
    }
}

void battle_restore_math_operands(Snes *s)
{
    uint8_t last = m_read8(s, m_read8(s, RAM_BATTLE_DIVIDER_BUSY) ? RAM_BATTLE_DIVIDER_OPERANDS + 2
                                                                     : RAM_BATTLE_MULTIPLIER_OPERANDS + 1);
    restore_math_operands(s);
    m_set_a8(s, last);
    m_set_nz8(s, last);
    m_rtl(s);
}

/* ---- The vblank handler ------------------------------------------------------------- */

/* The frame count, and the timed battle message's countdown: when it runs out
 * the message closes (and one opened by id is forgotten, so it can show
 * again), and the IRQ applies the alternating-frame setting anew. */
static void count_frame(Snes *s)
{
    m_write16(s, RAM_BATTLE_FRAME_COUNT, (uint16_t)(m_read16(s, RAM_BATTLE_FRAME_COUNT) + 1));
    if (!m_read8(s, RAM_BATTLE_MESSAGE_TIMED))
        return;
    uint8_t left = (uint8_t)(m_read8(s, RAM_BATTLE_MESSAGE_FRAMES) - 1);
    m_write8(s, RAM_BATTLE_MESSAGE_FRAMES, left);
    if (left != 0)
        return;
    if (m_read8(s, RAM_BATTLE_MESSAGE_BY_ID))
        m_write8(s, RAM_BATTLE_MESSAGE_SHOWN, 0xFF);
    m_write8(s, RAM_BATTLE_MESSAGE_BY_ID, 0);
    m_write8(s, RAM_BATTLE_VRAM_COPY_MODE, 0);
    m_write8(s, RAM_BATTLE_MESSAGE_TIMED, 0);
    m_write8(s, RAM_BATTLE_ALTERNATE_APPLY, (uint8_t)(m_read8(s, RAM_BATTLE_ALTERNATE_APPLY) + 1));
}

/* Start pauses and unpauses the fight (once per press), unless pausing is
 * blocked or the battle is a demo. */
static void take_pause(Snes *s, uint8_t buttons_high)
{
    if (m_read8(s, RAM_BATTLE_MODE) == MODE_FIGHT) {
        if ((m_read8(s, RAM_BATTLE_SETUP_FLAGS_COPY) & FLAG_NO_PAUSE) || m_read8(s, RAM_BATTLE_PAUSE_BLOCKED)) {
            m_write8(s, RAM_BATTLE_PAUSED, 0);
        } else if (buttons_high & BUTTON_START) {
            if (!m_read8(s, RAM_BATTLE_PAUSE_HELD)) {
                m_write8(s, RAM_BATTLE_PAUSED, (uint8_t)((m_read8(s, RAM_BATTLE_PAUSED) + 1) & 0x01));
                m_write8(s, RAM_BATTLE_PAUSE_HELD, 1);
            }
            return;
        }
    }
    m_write8(s, RAM_BATTLE_PAUSE_HELD, 0);
}

/* The debug frame advance, where the ROM enables it: R ends the pause; while
 * paused by Start, the frame count's every fourth frame runs if L is held. */
static void take_frame_advance(Snes *s, uint8_t buttons_low_at_r, Platform *pf)
{
    if (!battle_frame_advance_enabled(s))
        return;
    if (buttons_low_at_r & BUTTON_R) {
        m_write8(s, RAM_BATTLE_PAUSED, 0);
        m_write8(s, RAM_BATTLE_PAUSE_HELD, 0);
        return;
    }
    if (!m_read8(s, RAM_BATTLE_PAUSE_HELD))
        return;
    uint8_t count = (uint8_t)(m_read8(s, RAM_BATTLE_FRAME_ADVANCE_COUNT) + 1);
    m_write8(s, RAM_BATTLE_FRAME_ADVANCE_COUNT, count);
    uint8_t paused = count & 0x03;
    if (paused == 0)
        paused = (uint8_t)(((in_read_low(pf, PF_AT(0xD1E86A), 0) & BUTTON_L) ^ BUTTON_L) >> 5);
    m_write8(s, RAM_BATTLE_PAUSED, paused);
}

// $D1:E77C
/* The battle's vblank handler (the JML at $0500 points here). Unless the last
 * one is still running, it saves the direct page's scratch around its work:
 * the line values' second copies, forced blank for the uploads, the frame
 * work for the battle mode ($CD:0C95) and, unless paused, $C0:0002 (through
 * $CD:0C89), the frame count and the message countdown; then pause and frame
 * advance, and it releases the battle's wait loops. */
void battle_nmi(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    save_registers(s);
    c->d = 0x0000;
    c->a = 0x0000;
    c->p |= CPU_FLAG_M;
    vid_enable_line_effects(pf, PF_AT(0xD1E78A), 0);
    tm_ack_vblank(pf, PF_AT(0xD1E78E));
    tm_enable_interrupts(pf, PF_AT(0xD1E794), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
    if (m_read8(s, RAM_BATTLE_INTERRUPT_BUSY)) {
        restore_registers_and_return(s);
        return;
    }
    m_write8(s, RAM_BATTLE_INTERRUPT_BUSY, (uint8_t)(m_read8(s, RAM_BATTLE_INTERRUPT_BUSY) + 1));

    move_direct_page(s, 0x0000, RAM_ABS(RAM_BATTLE_SAVED_DIRECT_PAGE));
    for (int i = 0; i < 4; i++) {
        static const uint8_t pairs[4] = { 0x0, 0x2, 0x8, 0xA };
        uint32_t from = RAM_BATTLE_SPLIT_LINE_VALUES + pairs[i];
        m_write16(s, from + 4, m_read16(s, from));
    }
    c->db = 0x00;
    vid_enable_line_effects(pf, PF_AT(0xD1E7CC), 0);
    tm_reset_beam_latch(pf, PF_AT(0xD1E7CF));
    vid_set_display(pf, PF_AT(0xD1E7D4), &(VidDisplay){ .blank = true });
    vid_begin_vram_uploads(pf, PF_AT(0xD1E7D7), VID_VRAM_WORD_PORT, PF_GAME(0), NULL);

    /* A is $80 (the display byte) for the frame work, which reads its high
     * byte and Y, the block move's end */
    c->a = 0x0080;
    c->p |= CPU_FLAG_M;
    m_jsl_dc(s, 0xD1E7DA, 0xCD0C95, DC_X | DC_N | DC_Z | DC_C);
    if (!came_back(s))
        return;
    if (!m_read8(s, RAM_BATTLE_PAUSED)) {
        m_jsl_dc(s, 0xD1E7E3, 0xCD0C89, DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C);
        if (!came_back(s))
            return;
        count_frame(s);
    }

    /* the joypad bytes are read where the original reads them */
    uint8_t high = 0;
    if (m_read8(s, RAM_BATTLE_MODE) == MODE_FIGHT
        && !(m_read8(s, RAM_BATTLE_SETUP_FLAGS_COPY) & FLAG_NO_PAUSE) && !m_read8(s, RAM_BATTLE_PAUSE_BLOCKED))
        high = in_read_high(pf, PF_AT(0xD1E81E), 0);
    take_pause(s, high);
    restore_math_operands(s);
    uint8_t low = battle_frame_advance_enabled(s) ? in_read_low(pf, PF_AT(0xD1E84B), 0) : 0;
    take_frame_advance(s, low, pf);

    move_direct_page(s, RAM_ABS(RAM_BATTLE_SAVED_DIRECT_PAGE), 0x0000);
    m_write8(s, RAM_BATTLE_INTERRUPT_BUSY, 0);
    m_write8(s, RAM_BATTLE_NMI_PENDING, 0);
    restore_registers_and_return(s);
}

/* ---- The line handler --------------------------------------------------------------- */

// $D1:FE4C
/* On the IRQ's even frames: when the alternating-frame request changed, or a
 * new application is asked for, apply it ($CE:EE6E, with the setting's base
 * plus the request and the VRAM copy mode) and clear the ask. */
void battle_irq_update_ca22(Snes *s)
{
    uint8_t request = m_read8(s, RAM_BATTLE_ALTERNATE_REQUEST);
    if (request != m_read8(s, RAM_BATTLE_ALTERNATE_LATCHED)) {
        m_write8(s, RAM_BATTLE_ALTERNATE_LATCHED, request);
    } else if (!m_read8(s, RAM_BATTLE_ALTERNATE_APPLY)) {
        /* what the original leaves: A the ask, the carry of the equal compare */
        m_set_a8(s, 0);
        m_set_nz8(s, 0);
        m_set_flag(s, CPU_FLAG_C, true);
        m_rts(s);
        return;
    }
    /* CLC : ADC twice; the callee takes the second's overflow, and the carry
     * comes back through it to the caller */
    uint8_t partial = (uint8_t)(m_read8(s, RAM_BATTLE_ALTERNATE_BASE) + m_read8(s, RAM_BATTLE_ALTERNATE_LATCHED));
    uint8_t mode = m_read8(s, RAM_BATTLE_VRAM_COPY_MODE);
    uint16_t sum = (uint16_t)(partial + mode);
    m_set_a8(s, (uint8_t)sum);
    m_set_flag(s, CPU_FLAG_C, sum > 0xFF);
    m_set_flag(s, CPU_FLAG_V, (~(partial ^ mode) & (partial ^ sum) & 0x80) != 0);
    m_jsl_dc(s, 0xD1FE69, 0xCEEE6E, DC_X | DC_Y | DC_N | DC_Z);
    if (!widths_m8_x16(s))
        return;
    m_write8(s, RAM_BATTLE_ALTERNATE_APPLY, 0);
    m_rts(s);
}

// $D1:FE97
/* The battle's line handler (the JML at $0504), up to its wait for hblank:
 * save the registers, mark the handler busy, acknowledge the IRQ, keep NMI
 * and auto-joypad on, and turn the line effects off. Continues at the wait,
 * $D1:FEB8, an idle entry. */
void battle_irq(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    save_registers(s);
    c->d = 0x0000;
    c->p |= CPU_FLAG_M;
    m_write8(s, RAM_BATTLE_INTERRUPT_BUSY, (uint8_t)(m_read8(s, RAM_BATTLE_INTERRUPT_BUSY) + 1));
    tm_ack_line_irq(pf, PF_AT(0xD1FEA8));
    tm_enable_interrupts(pf, PF_AT(0xD1FEAE), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
    c->db = 0x00;
    vid_enable_line_effects(pf, PF_AT(0xD1FEB5), 0);
    /* the wait loads A and tests it; the original has A 0 and Z set there */
    c->a = 0x0000;
    m_set_nz8(s, 0);
    m_stop_at(s, 0xD1FEB8, 0);
}

// $D1:FEBF
/* The rest of the line handler, from hblank: forced blank for the rest of
 * the frame, then on even frames the alternating-frame setting ($D1:FE4C,
 * data bank $7E) and on odd ones, unless paused, the tile animation uploads
 * ($FD:FFFD); the math operands back, and return. The line is the one the
 * battle set for the handler, which is when the write lands. */
void battle_irq_after_hblank(Snes *s)
{
    Cpu *c = &s->cpu;
    vid_raster_display(m_platform(s), PF_AT(0xD1FEC1), VID_AT_LINE_HANDLER, &(VidDisplay){ .blank = true });
    if (!(m_read8(s, RAM_BATTLE_FRAME_COUNT) & 0x01)) {
        c->db = 0x7E;
        c->a = (uint16_t)((c->a & 0xFF00) | 0x7E); /* 8-bit LDA: the high byte stays */
        m_jsr_dc(s, 0xD1FECE, 0xD1FE4C, DC_X | DC_Y | DC_N | DC_Z | DC_C);
        if (!widths_m8_x16(s))
            return;
    } else if (!m_read8(s, RAM_BATTLE_PAUSED)) {
        /* PHP : JSL $FD:FFFD : PLP -- the P byte is live under the call, with
         * the flags of the load that tested the pause */
        m_set_a8(s, 0);
        m_set_nz8(s, 0);
        m_push8(s, c->p);
        m_jsl_dc(s, 0xD1FEDA, 0xFDFFFD, DC_A | DC_X | DC_Y | DC_N | DC_Z);
        if (!widths_m8_x16(s))
            return;
        c->p = m_pull8(s);
        if (c->p & CPU_FLAG_X) {
            c->x &= 0xFF;
            c->y &= 0xFF;
        }
    }
    c->x = 0x0000;
    c->d = 0x0000;
    c->p |= CPU_FLAG_M;
    restore_math_operands(s);
    m_write8(s, RAM_BATTLE_INTERRUPT_BUSY, 0);
    restore_registers_and_return(s);
}

/* Guards. battle_setup_window_hdma and battle_upload_vram's callers set the
 * data bank to 0 for the registers, which the platform services address in
 * bank 0. */
bool battle_data_bank_is_io(Snes *s) { return m_io_bank(s->cpu.db); }

